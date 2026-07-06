#include "core/preflight.h"
#include "core/path_collector.h"

#if defined(DATASCYTHE_PLATFORM_MACOS)

#include <filesystem>
#include <memory>
#include <unistd.h>

namespace datascythe {

namespace {

class MacPreflightChecker final : public IPreflightChecker {
public:
    PreflightResult check(const std::string& target, EraseMode mode) override {
        PreflightResult result;
        result.resolved_target = target;

        if (geteuid() != 0 && mode != EraseMode::ShredFiles && mode != EraseMode::ShredDirectory) {
            result.issues.push_back(
                {PreflightSeverity::Error, "Root privileges are required for this operation."});
        }

        if (mode == EraseMode::ShredFiles || mode == EraseMode::ShredDirectory) {
            namespace fs = std::filesystem;
            std::error_code ec;
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
                result.issues.push_back(
                    {PreflightSeverity::Info,
                     "Found " + std::to_string(files.size()) + " file(s) to shred."});
            }
            return result;
        }

        if (target.find("/dev/") == 0) {
            result.issues.push_back(
                {PreflightSeverity::Warning,
                 "Run 'diskutil unmountDisk' on this disk before erasing."});
        }

        return result;
    }
};

}  // namespace

std::unique_ptr<IPreflightChecker> create_preflight_checker() {
    return std::make_unique<MacPreflightChecker>();
}

}  // namespace datascythe

#endif