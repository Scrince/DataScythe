#include "platform/volume_manager.h"

#if defined(DATASCYTHE_PLATFORM_WINDOWS)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cctype>
#include <memory>
#include <string>

namespace datascythe {

namespace {

class WindowsVolumeManager final : public IVolumeManager {
public:
    bool is_system_volume(const std::string& path) const override {
        char windows_dir[MAX_PATH]{};
        if (GetWindowsDirectoryA(windows_dir, MAX_PATH) == 0) {
            return false;
        }

        if (path.size() >= 2 && path[1] == ':') {
            const char drive_letter = static_cast<char>(std::toupper(path[0]));
            const char system_letter = static_cast<char>(std::toupper(windows_dir[0]));
            return drive_letter == system_letter;
        }
        return false;
    }

    bool dismount_volume(const std::string& mount_point, std::string& error_out) override {
        std::string volume_path = mount_point;
        if (volume_path.back() != '\\') {
            volume_path.push_back('\\');
        }

        if (volume_path.size() >= 2 && volume_path[1] == ':') {
            volume_path = R"(\\.\)" + volume_path.substr(0, 2);
        }

        HANDLE handle = CreateFileA(volume_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                                    nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            error_out = "Unable to open volume " + mount_point;
            return false;
        }

        DWORD bytes_returned = 0;
        const BOOL ok =
            DeviceIoControl(handle, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &bytes_returned,
                            nullptr);
        CloseHandle(handle);

        if (!ok) {
            error_out = "FSCTL_DISMOUNT_VOLUME failed for " + mount_point + " (Win32 error " +
                        std::to_string(GetLastError()) + ")";
            return false;
        }
        return true;
    }

    bool dismount_physical_drive(int physical_index, std::string& error_out) override {
        bool any_failed = false;
        char drive_letter = 'A';
        for (; drive_letter <= 'Z'; ++drive_letter) {
            const std::string root = std::string(1, drive_letter) + ":\\";
            const UINT type = GetDriveTypeA(root.c_str());
            if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) {
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
            if (DeviceIoControl(vol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, &extents,
                                sizeof(extents), &bytes, nullptr)) {
                if (extents.NumberOfDiskExtents > 0 &&
                    static_cast<int>(extents.Extents[0].DiskNumber) == physical_index) {
                    std::string local_error;
                    if (!dismount_volume(root, local_error)) {
                        any_failed = true;
                        error_out += local_error + "; ";
                    }
                }
            }
            CloseHandle(vol);
        }
        return !any_failed;
    }
};

}  

std::unique_ptr<IVolumeManager> create_volume_manager() {
    return std::make_unique<WindowsVolumeManager>();
}

}  

#endif