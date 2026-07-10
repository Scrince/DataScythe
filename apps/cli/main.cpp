#include "core/certificate.h"
#include "core/erase_config.h"
#include "core/erase_engine.h"
#include "core/logger.h"
#include "core/preflight.h"
#include "core/verification.h"
#include "core/version.h"
#include "platform/drive_enumerator.h"
#include "platform/drive_info.h"
#include "platform/raw_device.h"
#include "platform/secure_erase.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

void print_usage(const char* argv0) {
    std::fprintf(stderr,
                "DataScythe CLI - secure data erasure\n\n"
                "Usage:\n"
                "  %s --list-drives\n"
                "  %s [options] <target> [targets...]\n\n"
                "Options:\n"
                "  --mode MODE       full | quick | volume | files | folder | ssd-secure\n"
                "                    (default: full)\n"
                "  --passes N        Overwrite pass count (default: 3)\n"
                "  --no-random       Disable random shred passes\n"
                "  --no-zero         Skip final zero pass\n"
                "  --remove          Delete files after shredding\n"
                "  --no-remove       Keep files after shredding\n"
                "  --recursive       Recurse into subdirectories (folder mode)\n"
                "  --no-recursive    Do not recurse (folder mode)\n"
                "  --verify          Read back sample sectors after erase\n"
                "  --no-partition-wipe  Skip MBR/GPT metadata wipe on block devices\n"
                "  --no-ads          Skip NTFS alternate data streams (file/folder modes)\n"
                "  --certificate P   Export erasure certificate to path P on success\n"
                "  --yes             Skip interactive confirmation\n"
                "  --version         Show version information\n"
                "  --help            Show this help\n\n"
                "Examples:\n"
                "  %s --list-drives\n"
                "  %s --mode quick --yes \\\\.\\PhysicalDrive2\n"
                "  %s --mode files --remove file1.txt file2.doc\n"
                "  %s --mode folder --recursive C:\\temp\\shred_me\n"
                "  %s --mode ssd-secure --yes \\\\.\\PhysicalDrive1\n",
                argv0, argv0, argv0, argv0, argv0, argv0, argv0);
}

std::string drive_type_name(datascythe::DriveType type) {
    switch (type) {
        case datascythe::DriveType::HDD:
            return "HDD";
        case datascythe::DriveType::SSD:
            return "SSD";
        case datascythe::DriveType::Removable:
            return "Removable";
        case datascythe::DriveType::Virtual:
            return "Virtual";
        default:
            return "Unknown";
    }
}

void list_drives() {
    std::string error;
    auto enumerator = datascythe::create_drive_enumerator();
    if (!enumerator) {
        std::fprintf(stderr, "Drive enumeration unavailable on this platform.\n");
        std::exit(1);
    }

    const auto drives = enumerator->enumerate(error);
    if (!error.empty()) {
        std::fprintf(stderr, "Warning: %s\n", error.c_str());
    }

    for (const auto& drive : drives) {
        std::printf("Drive %d: %s\n", drive.physical_index, drive.device_path.c_str());
        std::printf("  Model:  %s\n", drive.model.c_str());
        std::printf("  Serial: %s\n", drive.serial.c_str());
        std::printf("  Size:   %llu bytes\n",
                    static_cast<unsigned long long>(drive.size_bytes));
        std::printf("  Type:   %s\n", drive_type_name(drive.type).c_str());
        std::printf("  System: %s\n", drive.is_system_drive ? "YES" : "no");
        if (!drive.volumes.empty()) {
            std::printf("  Volumes:");
            for (const auto& vol : drive.volumes) {
                std::printf(" %s", vol.mount_point.c_str());
            }
            std::printf("\n");
        }
        std::printf("\n");
    }
}

bool parse_mode(const std::string& text, datascythe::EraseMode& mode_out) {
    if (text == "full") {
        mode_out = datascythe::EraseMode::FullDeviceWipe;
        return true;
    }
    if (text == "quick") {
        mode_out = datascythe::EraseMode::QuickZeroFill;
        return true;
    }
    if (text == "volume") {
        mode_out = datascythe::EraseMode::ShredVolume;
        return true;
    }
    if (text == "files") {
        mode_out = datascythe::EraseMode::ShredFiles;
        return true;
    }
    if (text == "folder") {
        mode_out = datascythe::EraseMode::ShredDirectory;
        return true;
    }
    if (text == "ssd-secure") {
        mode_out = datascythe::EraseMode::SsdSecureErase;
        return true;
    }
    return false;
}

bool confirm_action(const std::string& summary) {
    std::fprintf(stderr, "WARNING: %s\n", summary.c_str());
    std::fprintf(stderr, "Type YES to confirm: ");
    std::string input;
    if (!std::getline(std::cin, input)) {
        return false;
    }
    return input == "YES";
}

bool parse_pass_count(const char* text, std::size_t& passes_out, std::string& error_out) {
    if (text[0] == '\0') {
        error_out = "Pass count must be a whole number.";
        return false;
    }
    for (const char* ch = text; *ch != '\0'; ++ch) {
        if (!std::isdigit(static_cast<unsigned char>(*ch))) {
            error_out = "Pass count must be a whole number: " + std::string(text);
            return false;
        }
    }
    try {
        std::size_t parsed_chars = 0;
        const unsigned long value = std::stoul(text, &parsed_chars, 10);
        if (parsed_chars != std::strlen(text)) {
            error_out = "Pass count must be a whole number: " + std::string(text);
            return false;
        }
        if (value > static_cast<unsigned long>(std::numeric_limits<std::size_t>::max())) {
            error_out = "Pass count is too large: " + std::string(text);
            return false;
        }
        passes_out = static_cast<std::size_t>(value);
        return true;
    } catch (const std::exception&) {
        error_out = "Pass count must be a whole number: " + std::string(text);
        return false;
    }
}

std::string resolve_volume_target(const std::string& device_path) {
    std::string error;
    auto enumerator = datascythe::create_drive_enumerator();
    if (!enumerator) {
        return device_path;
    }
    const auto drives = enumerator->enumerate(error);
    for (const auto& drive : drives) {
        if (drive.device_path == device_path && !drive.volumes.empty()) {
            return drive.volumes.front().mount_point;
        }
    }
    return device_path;
}

bool is_system_target(const std::string& path) {
    std::string error;
    auto enumerator = datascythe::create_drive_enumerator();
    if (!enumerator) {
        return false;
    }
    const auto drives = enumerator->enumerate(error);
    for (const auto& drive : drives) {
        if (drive.device_path == path && drive.is_system_drive) {
            return true;
        }
        for (const auto& vol : drive.volumes) {
            if (vol.mount_point == path && vol.is_system_volume) {
                return true;
            }
        }
    }
    return false;
}

}  

int main(int argc, char* argv[]) {
    datascythe::EraseConfig config;
    bool assume_yes = false;
    std::string certificate_path;
    std::vector<std::string> targets;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
        if (arg == "--version" || arg == "-V") {
            std::printf("%s %s\n", datascythe::kAppName, datascythe::kAppVersion);
            return 0;
        }
        if (arg == "--list-drives") {
            list_drives();
            return 0;
        }
        if (arg == "--yes") {
            assume_yes = true;
            continue;
        }
        if (arg == "--no-random") {
            config.use_random_passes = false;
            continue;
        }
        if (arg == "--no-zero") {
            config.final_zero_pass = false;
            continue;
        }
        if (arg == "--remove") {
            config.remove_after_shred = true;
            continue;
        }
        if (arg == "--no-remove") {
            config.remove_after_shred = false;
            continue;
        }
        if (arg == "--recursive") {
            config.recursive = true;
            continue;
        }
        if (arg == "--no-recursive") {
            config.recursive = false;
            continue;
        }
        if (arg == "--verify") {
            config.verify_after_erase = true;
            continue;
        }
        if (arg == "--no-partition-wipe") {
            config.wipe_partition_metadata = false;
            continue;
        }
        if (arg == "--no-ads") {
            config.shred_alternate_data_streams = false;
            continue;
        }
        if (arg == "--certificate" && i + 1 < argc) {
            certificate_path = argv[++i];
            continue;
        }
        if (arg == "--passes") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--passes requires a value.\n");
                return 1;
            }
            std::string parse_error;
            if (!parse_pass_count(argv[++i], config.pass_count, parse_error)) {
                std::fprintf(stderr, "%s\n", parse_error.c_str());
                return 1;
            }
            continue;
        }
        if (arg == "--mode" && i + 1 < argc) {
            if (!parse_mode(argv[++i], config.mode)) {
                std::fprintf(stderr, "Unknown mode: %s\n", argv[i]);
                return 1;
            }
            continue;
        }
        if (arg.rfind("--", 0) == 0) {
            std::fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            return 1;
        }
        targets.push_back(arg);
    }

    if (config.pass_count == 0 && !config.final_zero_pass &&
        config.mode != datascythe::EraseMode::QuickZeroFill &&
        config.mode != datascythe::EraseMode::SsdSecureErase) {
        std::fprintf(stderr, "Refusing to run with zero overwrite passes and no final zero pass.\n");
        return 1;
    }

    if (targets.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    for (const auto& target : targets) {
        if (is_system_target(target)) {
            std::fprintf(stderr, "Refusing to erase system target: %s\n", target.c_str());
            return 1;
        }
    }

    auto checker = datascythe::create_preflight_checker();
    if (checker) {
        for (const auto& target : targets) {
            const auto preflight = checker->check(target, config.mode);
            std::fprintf(stderr, "%s", datascythe::format_preflight_report(preflight).c_str());
            if (!preflight.ok()) {
                std::fprintf(stderr, "Pre-flight failed for %s\n", target.c_str());
                return 1;
            }
        }
    }

    if (!assume_yes) {
        std::string summary = "This will irreversibly destroy data on: ";
        for (std::size_t i = 0; i < targets.size(); ++i) {
            if (i > 0) {
                summary += ", ";
            }
            summary += targets[i];
        }
        if (!confirm_action(summary)) {
            std::fprintf(stderr, "Aborted.\n");
            return 1;
        }
    }

    datascythe::Logger logger;
    logger.info("CLI session started");

    auto progress = [](const datascythe::EraseProgress& p) -> bool {
        std::fprintf(stderr,
                     "\rPass %zu/%zu (%s) %.1f%% overall %.1f%% ETA %llds   ",
                     p.current_pass, p.total_passes, p.pass_label.c_str(), p.percent_complete,
                     p.overall_percent, static_cast<long long>(p.eta_seconds));
        std::fflush(stderr);
        return true;
    };

    if (config.mode == datascythe::EraseMode::SsdSecureErase) {
        if (targets.size() != 1) {
            std::fprintf(stderr, "ssd-secure mode accepts exactly one device path.\n");
            return 1;
        }

        auto secure = datascythe::create_secure_erase();
        if (!secure) {
            std::fprintf(stderr, "Hardware secure erase unavailable on this platform.\n");
            return 1;
        }

        std::string reason;
        if (!secure->is_supported(targets[0], reason)) {
            std::fprintf(stderr, "%s\n", reason.c_str());
            return 1;
        }

        std::fprintf(stderr, "%s\n", reason.c_str());
        auto secure_progress = [](int percent, const std::string& status) -> bool {
            std::fprintf(stderr, "\r%s %d%%   ", status.c_str(), percent);
            std::fflush(stderr);
            return true;
        };
        auto result = secure->execute(targets[0], secure_progress);
        std::fprintf(stderr, "\n");
        for (const auto& w : result.warnings) {
            std::fprintf(stderr, "WARN: %s\n", w.c_str());
        }
        if (!result.success) {
            std::fprintf(stderr, "ERROR: %s\n", result.message.c_str());
            return 1;
        }
        std::fprintf(stderr, "%s\n", result.message.c_str());
        if (config.verify_after_erase) {
            if (!datascythe::verify_target_zeroed(targets[0], result)) {
                std::fprintf(stderr, "ERROR: SSD secure erase verification failed\n");
                for (const auto& w : result.warnings) {
                    std::fprintf(stderr, "WARN: %s\n", w.c_str());
                }
                return 1;
            }
            std::fprintf(stderr, "Verification: PASSED (%zu samples)\n",
                         result.verification_samples);
        }
        if (!certificate_path.empty()) {
            const auto cert = datascythe::build_certificate(
                targets[0], config, result, logger.entries());
            std::string cert_error;
            if (!datascythe::export_certificate(cert, certificate_path, cert_error)) {
                std::fprintf(stderr, "Certificate export failed: %s\n", cert_error.c_str());
                return 1;
            }
            std::fprintf(stderr, "Certificate exported to %s\n", certificate_path.c_str());
        }
        return 0;
    }

    auto device = datascythe::create_raw_device();
    datascythe::EraseEngine engine(std::move(device));
    engine.set_logger(&logger);

    datascythe::EraseResult result;

    if (config.mode == datascythe::EraseMode::ShredFiles) {
        result = engine.erase_paths(targets, config, progress);
    } else if (config.mode == datascythe::EraseMode::ShredDirectory) {
        if (targets.size() != 1) {
            std::fprintf(stderr, "folder mode accepts exactly one directory path.\n");
            return 1;
        }
        config.mode = datascythe::EraseMode::ShredDirectory;
        result = engine.erase_target(targets[0], config, progress);
    } else {
        for (const auto& target : targets) {
            std::string resolved = target;
            if (config.mode == datascythe::EraseMode::ShredVolume) {
                resolved = resolve_volume_target(target);
            }
            result = engine.erase_target(resolved, config, progress);
            if (!result.success) {
                break;
            }
        }
    }

    std::fprintf(stderr, "\n");
    for (const auto& w : result.warnings) {
        std::fprintf(stderr, "WARN: %s\n", w.c_str());
    }

    if (!result.success) {
        std::fprintf(stderr, "ERROR: %s\n", result.message.c_str());
        return 1;
    }

    std::printf("%s\n", result.message.c_str());

    if (!certificate_path.empty()) {
        const std::string cert_target =
            targets.size() == 1 ? targets[0] : std::to_string(targets.size()) + " targets";
        const auto cert =
            datascythe::build_certificate(cert_target, config, result, logger.entries());
        std::string cert_error;
        if (!datascythe::export_certificate(cert, certificate_path, cert_error)) {
            std::fprintf(stderr, "Certificate export failed: %s\n", cert_error.c_str());
            return 1;
        }
        std::fprintf(stderr, "Certificate exported to %s\n", certificate_path.c_str());
    }

    if (config.verify_after_erase && result.success) {
        std::fprintf(stderr, "Verification: %s\n",
                     result.verification_passed ? "PASSED" : "FAILED");
    }

    return 0;
}
