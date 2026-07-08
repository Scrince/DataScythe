#include "platform/drive_enumerator.h"
#include "platform/volume_manager.h"

#if defined(DATASCYTHE_PLATFORM_WINDOWS)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace datascythe {

namespace {

std::string wide_to_utf8(const wchar_t* wide) {
    if (!wide || wide[0] == L'\0') {
        return {};
    }
    const int required =
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(required - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), required, nullptr, nullptr);
    return out;
}

DriveType classify_bus(std::uint8_t bus_type, bool removable) {
    if (removable) {
        return DriveType::Removable;
    }
    switch (bus_type) {
        case BusTypeUsb:
            return DriveType::Removable;
        case BusTypeNvme:
        case BusTypeSd:
        case BusTypeMmc:
            return DriveType::SSD;
        default:
            return DriveType::Unknown;
    }
}

std::string descriptor_string(const std::vector<std::uint8_t>& buffer, DWORD offset) {
    if (offset == 0 || offset >= buffer.size()) {
        return {};
    }
    const auto begin = buffer.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto end = std::find(begin, buffer.end(), 0);
    return std::string(begin, end);
}

void refine_media_type(HANDLE handle, DriveInfo& info) {
    STORAGE_PROPERTY_QUERY query{};
    query.QueryType = PropertyStandardQuery;

    query.PropertyId = StorageDeviceSeekPenaltyProperty;
    DEVICE_SEEK_PENALTY_DESCRIPTOR seek{};
    DWORD returned = 0;
    if (DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &seek,
                        sizeof(seek), &returned, nullptr)) {
        info.type = seek.IncursSeekPenalty ? DriveType::HDD : DriveType::SSD;
        return;
    }

    query.PropertyId = StorageDeviceTrimProperty;
    DEVICE_TRIM_DESCRIPTOR trim{};
    if (DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &trim,
                        sizeof(trim), &returned, nullptr) &&
        trim.TrimEnabled) {
        info.type = DriveType::SSD;
    }
}

void query_read_only(HANDLE handle, DriveInfo& info) {
    DWORD returned = 0;
    if (DeviceIoControl(handle, IOCTL_DISK_IS_WRITABLE, nullptr, 0, nullptr, 0, &returned,
                        nullptr)) {
        info.is_read_only = false;
        return;
    }
    info.is_read_only = GetLastError() == ERROR_WRITE_PROTECT;
}

bool query_drive(int index, DriveInfo& info, int system_disk_number) {
    const std::string path = R"(\\.\PhysicalDrive)" + std::to_string(index);
    HANDLE handle =
        CreateFileA(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                  nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    info.physical_index = index;
    info.device_path = path;

    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;

    std::vector<std::uint8_t> buffer(sizeof(STORAGE_DEVICE_DESCRIPTOR) + 512);
    DWORD returned = 0;
    if (DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer.data(),
                        static_cast<DWORD>(buffer.size()), &returned, nullptr)) {
        const auto* desc = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
        if (desc->VendorIdOffset) {
            info.vendor = descriptor_string(buffer, desc->VendorIdOffset);
        }
        if (desc->ProductIdOffset) {
            info.model = descriptor_string(buffer, desc->ProductIdOffset);
        }
        if (desc->SerialNumberOffset) {
            info.serial = descriptor_string(buffer, desc->SerialNumberOffset);
        }
        info.is_removable = desc->RemovableMedia != 0;
        info.type = classify_bus(desc->BusType, info.is_removable);

        // Trim whitespace from identifiers.
        auto trim = [](std::string& s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
                s.erase(s.begin());
            }
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
                s.pop_back();
            }
        };
        trim(info.vendor);
        trim(info.model);
        trim(info.serial);
        if (!info.vendor.empty() && !info.model.empty()) {
            info.model = info.vendor + " " + info.model;
        }
    }

    GET_LENGTH_INFORMATION length_info{};
    if (DeviceIoControl(handle, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &length_info,
                        sizeof(length_info), &returned, nullptr)) {
        info.size_bytes = static_cast<std::uint64_t>(length_info.Length.QuadPart);
    }

    DISK_GEOMETRY geometry{};
    if (DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY, nullptr, 0, &geometry,
                        sizeof(geometry), &returned, nullptr)) {
        if (geometry.MediaType == RemovableMedia) {
            info.is_removable = true;
            info.type = DriveType::Removable;
        }
    }

    if (info.type != DriveType::Removable) {
        refine_media_type(handle, info);
    }
    query_read_only(handle, info);

    info.is_system_drive = (index == system_disk_number);
    CloseHandle(handle);
    return true;
}

void append_volumes(std::vector<DriveInfo>& drives) {
    auto volume_manager = create_volume_manager();

    char drive_letter = 'A';
    for (; drive_letter <= 'Z'; ++drive_letter) {
        const std::string root = std::string(1, drive_letter) + ":\\";
        const UINT type = GetDriveTypeA(root.c_str());
        if (type == DRIVE_NO_ROOT_DIR) {
            continue;
        }

        const std::string volume_path = R"(\\.\)" + std::string(1, drive_letter) + ":";
        HANDLE vol = CreateFileA(volume_path.c_str(), 0,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                                 nullptr);
        if (vol == INVALID_HANDLE_VALUE) {
            continue;
        }

        VOLUME_DISK_EXTENTS extents{};
        DWORD bytes = 0;
        if (!DeviceIoControl(vol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, &extents,
                             sizeof(extents), &bytes, nullptr)) {
            CloseHandle(vol);
            continue;
        }

        if (extents.NumberOfDiskExtents == 0) {
            CloseHandle(vol);
            continue;
        }

        const int disk_number = static_cast<int>(extents.Extents[0].DiskNumber);

        ULARGE_INTEGER free_bytes_available{}, total_bytes{}, total_free{};
        char filesystem[MAX_PATH + 1]{};
        GetVolumeInformationA(root.c_str(), nullptr, 0, nullptr, nullptr, nullptr, filesystem,
                                MAX_PATH);

        if (GetDiskFreeSpaceExA(root.c_str(), &free_bytes_available, &total_bytes, &total_free)) {
            VolumeInfo volume;
            volume.mount_point = root;
            volume.filesystem = filesystem;
            volume.size_bytes = total_bytes.QuadPart;
            volume.is_system_volume = volume_manager->is_system_volume(root);

            for (auto& drive : drives) {
                if (drive.physical_index == disk_number) {
                    drive.volumes.push_back(volume);
                    break;
                }
            }
        }

        CloseHandle(vol);
    }
}

int detect_system_physical_drive() {
    char windows_dir[MAX_PATH]{};
    if (GetWindowsDirectoryA(windows_dir, MAX_PATH) == 0) {
        return -1;
    }

    const std::string system_root = std::string(1, windows_dir[0]) + ":\\";
    const std::string volume_path = R"(\\.\)" + std::string(1, windows_dir[0]) + ":";
    HANDLE vol = CreateFileA(volume_path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, 0, nullptr);
    if (vol == INVALID_HANDLE_VALUE) {
        return -1;
    }

    VOLUME_DISK_EXTENTS extents{};
    DWORD bytes = 0;
    int disk = -1;
    if (DeviceIoControl(vol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, &extents,
                         sizeof(extents), &bytes, nullptr) &&
        extents.NumberOfDiskExtents > 0) {
        disk = static_cast<int>(extents.Extents[0].DiskNumber);
    }
    CloseHandle(vol);
    (void)system_root;
    return disk;
}

class WindowsDriveEnumerator final : public IDriveEnumerator {
public:
    std::vector<DriveInfo> enumerate(std::string& error_out) override {
        std::vector<DriveInfo> drives;
        const int system_disk = detect_system_physical_drive();

        for (int index = 0; index < 32; ++index) {
            DriveInfo info;
            if (query_drive(index, info, system_disk)) {
                drives.push_back(std::move(info));
            }
        }

        if (drives.empty()) {
            error_out = "No physical drives detected. Run as Administrator for full access.";
            return drives;
        }

        append_volumes(drives);
        std::sort(drives.begin(), drives.end(),
                  [](const DriveInfo& a, const DriveInfo& b) { return a.physical_index < b.physical_index; });
        return drives;
    }
};

}  // namespace

std::unique_ptr<IDriveEnumerator> create_drive_enumerator() {
    return std::make_unique<WindowsDriveEnumerator>();
}

}  // namespace datascythe

#endif
