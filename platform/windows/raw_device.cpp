#include "platform/raw_device.h"
#include "platform/volume_manager.h"

#if defined(DATASCYTHE_PLATFORM_WINDOWS)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>
#include <string>

namespace datascythe {

namespace {

bool is_physical_path(const std::string& path) {
    return path.find("PhysicalDrive") != std::string::npos ||
           path.find("physicaldrive") != std::string::npos;
}

bool is_volume_path(const std::string& path) {
    if (path.size() >= 4 && path.rfind(R"(\\.\)", 0) == 0 && path.back() == ':') {
        return true;
    }
    return path.size() >= 2 && path[1] == ':' &&
           (path.size() == 2 || path[2] == '\\');
}

bool parse_physical_drive_index(const std::string& path, int& index_out) {
    const auto pos = path.find("PhysicalDrive");
    if (pos == std::string::npos) {
        return false;
    }

    const std::string suffix = path.substr(pos + 13);
    if (suffix.empty() ||
        !std::all_of(suffix.begin(), suffix.end(), [](unsigned char ch) {
            return std::isdigit(ch) != 0;
        })) {
        return false;
    }

    unsigned long value = 0;
    for (const char ch : suffix) {
        value = value * 10 + static_cast<unsigned long>(ch - '0');
        if (value > static_cast<unsigned long>(std::numeric_limits<int>::max())) {
            return false;
        }
    }

    index_out = static_cast<int>(value);
    return true;
}

class WindowsRawDevice final : public IRawDevice {
public:
    bool open(const std::string& path, std::string& error_out) override {
        close();

        std::string normalized = path;
        if (is_physical_path(normalized) && normalized.rfind(R"(\\.\)", 0) != 0) {
            normalized = R"(\\.\)" + normalized;
        }

        target_type_ = RawTargetType::Unknown;
        if (is_physical_path(normalized)) {
            target_type_ = RawTargetType::BlockDevice;
        } else if (is_volume_path(normalized)) {
            target_type_ = RawTargetType::Volume;
        } else {
            target_type_ = RawTargetType::RegularFile;
        }

        DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
        if (target_type_ == RawTargetType::RegularFile) {
            share |= FILE_SHARE_DELETE;
        }

        handle_ = CreateFileA(normalized.c_str(), GENERIC_READ | GENERIC_WRITE, share, nullptr,
                              OPEN_EXISTING, FILE_FLAG_WRITE_THROUGH, nullptr);

        if (handle_ == INVALID_HANDLE_VALUE) {
            const DWORD err = GetLastError();
            if (err == ERROR_ACCESS_DENIED) {
                error_out =
                    "Access denied opening " + normalized +
                    ". Administrator privileges may be required for raw device access.";
            } else if (err == ERROR_SHARING_VIOLATION) {
                error_out = "Target is in use. Close programs using " + normalized + ".";
            } else {
                error_out = "Failed to open " + normalized + " (Win32 error " +
                            std::to_string(err) + ")";
            }
            handle_ = INVALID_HANDLE_VALUE;
            return false;
        }

        path_ = normalized;
        return true;
    }

    void close() override {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
        path_.clear();
        target_type_ = RawTargetType::Unknown;
    }

    bool is_open() const override { return handle_ != INVALID_HANDLE_VALUE; }

    RawTargetType target_type() const override { return target_type_; }

    std::uint64_t size_bytes(std::string& error_out) const override {
        if (!is_open()) {
            error_out = "Device is not open";
            return 0;
        }

        if (target_type_ == RawTargetType::BlockDevice) {
            GET_LENGTH_INFORMATION length_info{};
            DWORD bytes_returned = 0;
            if (DeviceIoControl(handle_, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &length_info,
                                sizeof(length_info), &bytes_returned, nullptr)) {
                return static_cast<std::uint64_t>(length_info.Length.QuadPart);
            }
        }

        LARGE_INTEGER file_size{};
        if (GetFileSizeEx(handle_, &file_size)) {
            return static_cast<std::uint64_t>(file_size.QuadPart);
        }

        error_out = "Unable to query target size (Win32 error " + std::to_string(GetLastError()) +
                    ")";
        return 0;
    }

    std::uint64_t block_size(std::string& error_out) const override {
        if (!is_open()) {
            error_out = "Device is not open";
            return 512;
        }

        if (target_type_ == RawTargetType::BlockDevice) {
            DISK_GEOMETRY geometry{};
            DWORD bytes = 0;
            if (DeviceIoControl(handle_, IOCTL_DISK_GET_DRIVE_GEOMETRY, nullptr, 0, &geometry,
                                sizeof(geometry), &bytes, nullptr)) {
                return static_cast<std::uint64_t>(geometry.BytesPerSector);
            }
        }

        DWORD sectors_per_cluster = 0;
        DWORD bytes_per_sector = 0;
        DWORD free_clusters = 0;
        DWORD total_clusters = 0;
        if (target_type_ == RawTargetType::RegularFile && path_.size() >= 2) {
            std::string root = path_.substr(0, 2) + "\\";
            if (GetDiskFreeSpaceA(root.c_str(), &sectors_per_cluster, &bytes_per_sector,
                                  &free_clusters, &total_clusters)) {
                return static_cast<std::uint64_t>(sectors_per_cluster) *
                       static_cast<std::uint64_t>(bytes_per_sector);
            }
        }

        return 4096;
    }

    bool write_at(std::uint64_t offset, const void* data, std::size_t size,
                  std::string& error_out) override {
        if (!is_open()) {
            error_out = "Device is not open";
            return false;
        }

        LARGE_INTEGER pos{};
        pos.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(handle_, pos, nullptr, FILE_BEGIN)) {
            error_out = "Seek failed at offset " + std::to_string(offset);
            return false;
        }

        const auto* bytes = static_cast<const std::uint8_t*>(data);
        std::size_t written_total = 0;
        while (written_total < size) {
            DWORD chunk = static_cast<DWORD>(
                std::min<std::size_t>(size - written_total, static_cast<std::size_t>(MAXDWORD)));
            DWORD written = 0;
            if (!WriteFile(handle_, bytes + written_total, chunk, &written, nullptr)) {
                error_out = "WriteFile failed at offset " +
                            std::to_string(offset + written_total) + " (Win32 error " +
                            std::to_string(GetLastError()) + ")";
                return false;
            }
            if (written == 0) {
                error_out = "WriteFile returned zero bytes at offset " +
                            std::to_string(offset + written_total);
                return false;
            }
            written_total += written;
        }
        return true;
    }

    bool read_at(std::uint64_t offset, void* data, std::size_t size,
                 std::string& error_out) override {
        if (!is_open()) {
            error_out = "Device is not open";
            return false;
        }

        LARGE_INTEGER pos{};
        pos.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(handle_, pos, nullptr, FILE_BEGIN)) {
            error_out = "Seek failed at offset " + std::to_string(offset);
            return false;
        }

        auto* bytes = static_cast<std::uint8_t*>(data);
        std::size_t read_total = 0;
        while (read_total < size) {
            DWORD chunk = static_cast<DWORD>(
                std::min<std::size_t>(size - read_total, static_cast<std::size_t>(MAXDWORD)));
            DWORD read = 0;
            if (!ReadFile(handle_, bytes + read_total, chunk, &read, nullptr)) {
                error_out = "ReadFile failed at offset " + std::to_string(offset + read_total);
                return false;
            }
            if (read == 0) {
                error_out = "ReadFile returned zero bytes at offset " + std::to_string(offset);
                return false;
            }
            read_total += read;
        }
        return true;
    }

    bool flush(std::string& error_out) override {
        if (!is_open()) {
            error_out = "Device is not open";
            return false;
        }
        if (!FlushFileBuffers(handle_)) {
            error_out = "FlushFileBuffers failed (Win32 error " + std::to_string(GetLastError()) +
                        ")";
            return false;
        }
        return true;
    }

    bool dismount_volumes(std::string& error_out) override {
        if (target_type_ != RawTargetType::BlockDevice) {
            return true;
        }

        int index = -1;
        if (!parse_physical_drive_index(path_, index)) {
            error_out = "Invalid physical drive path: " + path_;
            return false;
        }
        auto volume_manager = create_volume_manager();
        return volume_manager->dismount_physical_drive(index, error_out);
    }

    bool remove_target(std::string& error_out) override {
        if (!is_open()) {
            error_out = "Target is not open";
            return false;
        }
        if (target_type_ != RawTargetType::RegularFile) {
            error_out = "Remove is only supported for regular files";
            return false;
        }

        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;

        if (SetFileAttributesA(path_.c_str(), FILE_ATTRIBUTE_NORMAL) == 0) {
            
        }

        if (DeleteFileA(path_.c_str()) == 0) {
            error_out = "DeleteFile failed (Win32 error " + std::to_string(GetLastError()) + ")";
            return false;
        }
        path_.clear();
        target_type_ = RawTargetType::Unknown;
        return true;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::string path_;
    RawTargetType target_type_ = RawTargetType::Unknown;
};

}  

std::unique_ptr<IRawDevice> create_raw_device() {
    return std::make_unique<WindowsRawDevice>();
}

}  

#endif
