#include "core/preflight.h"
#include "core/path_collector.h"
#include "platform/drive_enumerator.h"
#include "platform/drive_info.h"

#if defined(DATASCYTHE_PLATFORM_WINDOWS)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <memory>
#include <string>

namespace datascythe {

namespace {

bool is_elevated() {
    BOOL elevated = FALSE;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
        elevated = elevation.TokenIsElevated;
    }
    CloseHandle(token);
    return elevated == TRUE;
}

const DriveInfo* find_drive_for_target(const std::vector<DriveInfo>& drives,
                                       const std::string& target) {
    for (const auto& drive : drives) {
        if (drive.device_path == target) {
            return &drive;
        }
        for (const auto& vol : drive.volumes) {
            if (vol.mount_point == target) {
                return &drive;
            }
        }
    }
    return nullptr;
}

bool needs_admin(EraseMode mode) {
    switch (mode) {
        case EraseMode::FullDeviceWipe:
        case EraseMode::QuickZeroFill:
        case EraseMode::ShredVolume:
        case EraseMode::SsdSecureErase:
            return true;
        default:
            return false;
    }
}

class WindowsPreflightChecker final : public IPreflightChecker {
public:
    PreflightResult check(const std::string& target, EraseMode mode) override {
        PreflightResult result;
        result.resolved_target = target;

        if (needs_admin(mode) && !is_elevated()) {
            result.issues.push_back(
                {PreflightSeverity::Error,
                 "Administrator privileges are required for this operation."});
        }

        std::string enum_error;
        auto enumerator = create_drive_enumerator();
        std::vector<DriveInfo> drives;
        if (enumerator) {
            drives = enumerator->enumerate(enum_error);
        }

        const DriveInfo* drive = find_drive_for_target(drives, target);
        if (drive) {
            if (drive->is_system_drive &&
                (mode == EraseMode::FullDeviceWipe || mode == EraseMode::QuickZeroFill ||
                 mode == EraseMode::SsdSecureErase)) {
                result.issues.push_back(
                    {PreflightSeverity::Error, "Refusing to erase the system drive."});
            }
            if (drive->is_read_only) {
                result.issues.push_back(
                    {PreflightSeverity::Error, "Target media is read-only."});
            }
            if (!drive->volumes.empty() &&
                (mode == EraseMode::FullDeviceWipe || mode == EraseMode::SsdSecureErase)) {
                result.issues.push_back(
                    {PreflightSeverity::Warning,
                     "Volumes are mounted on this drive. They will be dismounted automatically "
                     "(best effort)."});
            }
            result.estimated_bytes = drive->size_bytes;
        }

        namespace fs = std::filesystem;
        std::error_code ec;

        switch (mode) {
            case EraseMode::ShredFiles:
            case EraseMode::ShredDirectory: {
                if (!fs::exists(target, ec)) {
                    result.issues.push_back(
                        {PreflightSeverity::Error, "Target path does not exist: " + target});
                    break;
                }
                std::string collect_error;
                const bool recursive = mode == EraseMode::ShredDirectory;
                auto files = PathCollector::collect_files(target, recursive, true, collect_error);
                if (files.empty()) {
                    result.issues.push_back({PreflightSeverity::Error,
                                             collect_error.empty() ? "No files found to shred."
                                                                   : collect_error});
                } else {
                    std::uint64_t total = 0;
                    for (const auto& file : files) {
                        total += fs::file_size(file, ec);
                    }
                    result.estimated_bytes = total;
                    result.issues.push_back(
                        {PreflightSeverity::Info,
                         "Found " + std::to_string(files.size()) + " file(s) to shred."});
                }
                break;
            }
            case EraseMode::ShredVolume:
                if (drive && drive->volumes.empty()) {
                    result.issues.push_back(
                        {PreflightSeverity::Error, "No mounted volumes on selected drive."});
                }
                break;
            case EraseMode::FullDeviceWipe:
            case EraseMode::QuickZeroFill:
            case EraseMode::SsdSecureErase:
                if (!drive && target.find("PhysicalDrive") != std::string::npos) {
                    HANDLE h = CreateFileA((target.rfind(R"(\\.\)", 0) == 0 ? target
                                                                             : R"(\\.\)" + target)
                                             .c_str(),
                                         0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                         OPEN_EXISTING, 0, nullptr);
                    if (h == INVALID_HANDLE_VALUE) {
                        result.issues.push_back(
                            {PreflightSeverity::Error,
                             "Cannot open device. It may be in use or access is denied."});
                    } else {
                        CloseHandle(h);
                    }
                }
                break;
        }

        if (result.estimated_bytes > 0) {
            result.issues.push_back(
                {PreflightSeverity::Info,
                 "Estimated data size: " + std::to_string(result.estimated_bytes) + " bytes."});
        }

        return result;
    }
};

}  // namespace

std::unique_ptr<IPreflightChecker> create_preflight_checker() {
    return std::make_unique<WindowsPreflightChecker>();
}

}  // namespace datascythe

#endif