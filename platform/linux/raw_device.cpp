#include "platform/raw_device.h"
#include "platform/volume_manager.h"

#if defined(DATASCYTHE_PLATFORM_LINUX)

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <spawn.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

#ifndef BLKGETSIZE64
#define BLKGETSIZE64 _IOR(0x12, 114, size_t)
#endif

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

class LinuxRawDevice final : public IRawDevice {
public:
    bool open(const std::string& path, std::string& error_out) override {
        close();
        fd_ = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (fd_ < 0) {
            error_out = std::string("open failed: ") + std::strerror(errno);
            return false;
        }

        struct stat st{};
        if (fstat(fd_, &st) != 0) {
            error_out = std::string("fstat failed: ") + std::strerror(errno);
            close();
            return false;
        }

        if (S_ISBLK(st.st_mode)) {
            target_type_ = RawTargetType::BlockDevice;
        } else if (S_ISREG(st.st_mode)) {
            target_type_ = RawTargetType::RegularFile;
        } else {
            target_type_ = RawTargetType::Unknown;
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
            std::uint64_t size = 0;
            if (ioctl(fd_, BLKGETSIZE64, &size) == 0) {
                return size;
            }
        }

        struct stat st{};
        if (fstat(fd_, &st) == 0) {
            return static_cast<std::uint64_t>(st.st_size);
        }

        error_out = std::strerror(errno);
        return 0;
    }

    std::uint64_t block_size(std::string& error_out) const override {
        (void)error_out;
        return target_type_ == RawTargetType::RegularFile ? 4096 : 512;
    }

    bool write_at(std::uint64_t offset, const void* data, std::size_t size,
                  std::string& error_out) override {
        if (lseek(fd_, static_cast<off_t>(offset), SEEK_SET) < 0) {
            error_out = std::strerror(errno);
            return false;
        }

        const auto* bytes = static_cast<const std::uint8_t*>(data);
        std::size_t written_total = 0;
        while (written_total < size) {
            const ssize_t written =
                ::write(fd_, bytes + written_total, size - written_total);
            if (written < 0) {
                error_out = std::strerror(errno);
                return false;
            }
            written_total += static_cast<std::size_t>(written);
        }
        return true;
    }

    bool read_at(std::uint64_t offset, void* data, std::size_t size,
                 std::string& error_out) override {
        if (lseek(fd_, static_cast<off_t>(offset), SEEK_SET) < 0) {
            error_out = std::strerror(errno);
            return false;
        }
        auto* bytes = static_cast<std::uint8_t*>(data);
        std::size_t read_total = 0;
        while (read_total < size) {
            const ssize_t n = ::read(fd_, bytes + read_total, size - read_total);
            if (n < 0) {
                error_out = std::strerror(errno);
                return false;
            }
            if (n == 0) {
                error_out = "Unexpected EOF during read";
                return false;
            }
            read_total += static_cast<std::size_t>(n);
        }
        return true;
    }

    bool flush(std::string& error_out) override {
        if (fsync(fd_) != 0) {
            error_out = std::strerror(errno);
            return false;
        }
        return true;
    }

    bool dismount_volumes(std::string& error_out) override {
        if (target_type_ != RawTargetType::BlockDevice && target_type_ != RawTargetType::Volume) {
            return true;
        }
        if (path_.empty()) {
            error_out = "No device path for dismount";
            return false;
        }

        // Unmount any mount points whose source device matches path_ (argv only).
        std::ifstream mounts("/proc/mounts");
        if (!mounts) {
            error_out = "Unable to read /proc/mounts for dismount";
            return false;
        }
        std::string device, mount_point, rest;
        std::vector<std::string> mount_points;
        while (mounts >> device >> mount_point) {
            std::getline(mounts, rest);
            if (device == path_ || device.find(path_) == 0 || path_.find(device) == 0) {
                mount_points.push_back(mount_point);
            }
        }
        if (mount_points.empty()) {
            // Nothing mounted — treat as success.
            return true;
        }
        for (const auto& mp : mount_points) {
            std::string err;
            if (!run_argv({"/bin/umount", "-f", mp}, err) &&
                !run_argv({"/usr/bin/umount", "-f", mp}, err)) {
                error_out = "umount failed for " + mp + ": " + err;
                return false;
            }
        }
        return true;
    }

    bool remove_target(std::string& error_out) override {
        if (target_type_ != RawTargetType::RegularFile) {
            error_out = "Not a regular file";
            return false;
        }
        const std::string path = path_;
        close();
        if (unlink(path.c_str()) != 0) {
            error_out = std::strerror(errno);
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
    return std::make_unique<LinuxRawDevice>();
}

}  

#endif