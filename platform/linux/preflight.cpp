#include "core/preflight.h"
#include "core/path_collector.h"
#include "platform/drive_enumerator.h"

#if defined(DATASCYTHE_PLATFORM_LINUX)

#include <filesystem>
#include <memory>
#include <unistd.h>

namespace datascythe {

namespace {

bool needs_root(EraseMode mode) {
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

class LinuxPreflightChecker final : public IPreflightChecker {
public:
    PreflightResult check(const std::string& target, EraseMode mode) override {
        PreflightResult result;
        result.resolved_target = target;

        if (needs_root(mode) && geteuid() != 0) {
            result.issues.push_back(
                {PreflightSeverity::Error, "Root privileges are required for this operation."});
        }

        namespace fs = std::filesystem;
        std::error_code ec;

        if (mode == EraseMode::ShredFiles || mode == EraseMode::ShredDirectory) {
            if (!fs::exists(target, ec)) {
                result.issues.push_back(
                    {PreflightSeverity::Error, "Target path does not exist: " + target});
                return result;
            }
            std::string collect_error;
            auto files = PathCollector::collect_files(target, mode == EraseMode::ShredDirectory,
                                                      true, collect_error);
            if (files.empty()) {
                result.issues.push_back({PreflightSeverity::Error, "No files found to shred."});
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
            return result;
        }

        if (target.find("/dev/") == 0) {
            if (access(target.c_str(), R_OK | W_OK) != 0) {
                result.issues.push_back(
                    {PreflightSeverity::Error, "Cannot access device: " + target});
            }
            result.issues.push_back(
                {PreflightSeverity::Warning,
                 "Ensure all partitions on this device are unmounted (umount)."});
        }

        return result;
    }
};

}  

std::unique_ptr<IPreflightChecker> create_preflight_checker() {
    return std::make_unique<LinuxPreflightChecker>();
}

}  

#endif