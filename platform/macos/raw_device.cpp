#include "platform/raw_device.h"

#if defined(DATASCYTHE_PLATFORM_MACOS)

#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef DKIOCGETBLOCKCOUNT
#include <sys/disk.h>
#endif

namespace datascythe {

namespace {

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
        (void)error_out;
        return 4096;
    }

    bool write_at(std::uint64_t offset, const void* data, std::size_t size,
                  std::string& error_out) override {
        if (lseek(fd_, static_cast<off_t>(offset), SEEK_SET) < 0) {
            error_out = "seek failed";
            return false;
        }
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        std::size_t done = 0;
        while (done < size) {
            const ssize_t n = ::write(fd_, bytes + done, size - done);
            if (n < 0) {
                error_out = "write failed";
                return false;
            }
            done += static_cast<std::size_t>(n);
        }
        return true;
    }

    bool read_at(std::uint64_t offset, void* data, std::size_t size,
                 std::string& error_out) override {
        if (lseek(fd_, static_cast<off_t>(offset), SEEK_SET) < 0) {
            error_out = "seek failed";
            return false;
        }
        auto* bytes = static_cast<std::uint8_t*>(data);
        std::size_t done = 0;
        while (done < size) {
            const ssize_t n = ::read(fd_, bytes + done, size - done);
            if (n < 0) {
                error_out = "read failed";
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
        (void)error_out;
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

}  // namespace

std::unique_ptr<IRawDevice> create_raw_device() {
    return std::make_unique<MacRawDevice>();
}

}  // namespace datascythe

#endif