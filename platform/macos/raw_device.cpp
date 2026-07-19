#include "platform/raw_device.h"

#if defined(DATASCYTHE_PLATFORM_MACOS)

#include <fcntl.h>
#include <memory>
#include <spawn.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#ifndef DKIOCGETBLOCKCOUNT
#include <sys/disk.h>
#endif

extern char** environ;

namespace datascythe {

namespace {

bool run_argv(const std::vector<std::string>& args, std::string& error_out) {
    if (args.empty()) {
        error_out = "empty command";
        return false;
    }
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& a : args) {
        argv.push_back(const_cast<char*>(a.c_str()));
    }
    argv.push_back(nullptr);

    pid_t pid = 0;
    const int spawn_rc = posix_spawn(&pid, args[0].c_str(), nullptr, nullptr, argv.data(), environ);
    if (spawn_rc != 0) {
        error_out = "posix_spawn failed";
        return false;
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        error_out = "waitpid failed";
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        error_out = "command failed: " + args[0];
        return false;
    }
    return true;
}

class MacRawDevice final : public IRawDevice {
public:
    bool open(const std::string& path, std::string& error_out) override {
        close();
        fd_ = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (fd_ < 0) {
            error_out = "open failed for " + path;
            return false;
        }

        struct stat st{};
        if (fstat(fd_, &st) != 0) {
            error_out = "fstat failed";
            close();
            return false;
        }

        if (S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode)) {
            target_type_ = RawTargetType::BlockDevice;
        } else if (S_ISREG(st.st_mode)) {
            target_type_ = RawTargetType::RegularFile;
        }

        path_ = path;
        return true;
    }

    void close() override {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        path_.clear();
        target_type_ = RawTargetType::Unknown;
    }

    bool is_open() const override { return fd_ >= 0; }
    RawTargetType target_type() const override { return target_type_; }

    std::uint64_t size_bytes(std::string& error_out) const override {
        if (!is_open()) {
            error_out = "Not open";
            return 0;
        }

        if (target_type_ == RawTargetType::BlockDevice) {
            std::uint64_t block_count = 0;
            std::uint32_t block_size = 0;
            if (ioctl(fd_, DKIOCGETBLOCKCOUNT, &block_count) == 0 &&
                ioctl(fd_, DKIOCGETBLOCKSIZE, &block_size) == 0) {
                return block_count * block_size;
            }
        }

        struct stat st{};
        if (fstat(fd_, &st) == 0) {
            return static_cast<std::uint64_t>(st.st_size);
        }
        error_out = "Unable to determine size";
        return 0;
    }

    std::uint64_t block_size(std::string& error_out) const override {
        if (!is_open()) {
            error_out = "Not open";
            return 0;
        }
        if (target_type_ == RawTargetType::BlockDevice) {
            std::uint32_t block_size = 0;
            if (ioctl(fd_, DKIOCGETBLOCKSIZE, &block_size) == 0 && block_size > 0) {
                return block_size;
            }
        }
        return 512;
    }

    bool write_at(std::uint64_t offset, const void* data, std::size_t size,
                  std::string& error_out) override {
        if (!is_open()) {
            error_out = "Not open";
            return false;
        }
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        std::size_t done = 0;
        while (done < size) {
            const ssize_t n = ::pwrite(fd_, bytes + done, size - done,
                                       static_cast<off_t>(offset + done));
            if (n < 0) {
                error_out = "pwrite failed";
                return false;
            }
            if (n == 0) {
                error_out = "short write";
                return false;
            }
            done += static_cast<std::size_t>(n);
        }
        return true;
    }

    bool read_at(std::uint64_t offset, void* data, std::size_t size,
                 std::string& error_out) override {
        if (!is_open()) {
            error_out = "Not open";
            return false;
        }
        auto* bytes = static_cast<std::uint8_t*>(data);
        std::size_t done = 0;
        while (done < size) {
            const ssize_t n = ::pread(fd_, bytes + done, size - done,
                                      static_cast<off_t>(offset + done));
            if (n < 0) {
                error_out = "pread failed";
                return false;
            }
            if (n == 0) {
                error_out = "unexpected EOF";
                return false;
            }
            done += static_cast<std::size_t>(n);
        }
        return true;
    }

    bool flush(std::string& error_out) override {
        if (fsync(fd_) != 0) {
            error_out = "fsync failed";
            return false;
        }
        return true;
    }

    bool dismount_volumes(std::string& error_out) override {
        if (target_type_ != RawTargetType::BlockDevice && target_type_ != RawTargetType::Volume) {
            // Regular files need no dismount.
            return true;
        }
        if (path_.empty()) {
            error_out = "No device path for dismount";
            return false;
        }
        // argv-only diskutil — never shell-concatenate paths.
        // Try unmountDisk first (whole disk); fall back to unmount for volumes.
        std::string err;
        if (run_argv({"/usr/sbin/diskutil", "unmountDisk", "force", path_}, err)) {
            return true;
        }
        if (run_argv({"/usr/sbin/diskutil", "unmount", "force", path_}, err)) {
            return true;
        }
        error_out = "diskutil unmount failed for " + path_ + ": " + err;
        return false;
    }

    bool remove_target(std::string& error_out) override {
        if (target_type_ != RawTargetType::RegularFile) {
            error_out = "Not a regular file";
            return false;
        }
        const std::string path = path_;
        close();
        if (unlink(path.c_str()) != 0) {
            error_out = "unlink failed";
            return false;
        }
        return true;
    }

private:
    int fd_ = -1;
    std::string path_;
    RawTargetType target_type_ = RawTargetType::Unknown;
};

}  

std::unique_ptr<IRawDevice> create_raw_device() {
    return std::make_unique<MacRawDevice>();
}

}  

#endif
