#include "core/certificate.h"

#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace datascythe {

namespace {

std::string timestamp_now() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &time);
#else
    localtime_r(&time, &local_tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S UTC");
    return oss.str();
}

}  // namespace

std::string sanitize_target_for_filename(const std::string& target) {
    std::string sanitized;
    sanitized.reserve(target.size());
    for (char ch : target) {
        if (ch == '\\' || ch == '/' || ch == ':' || ch == '*' || ch == '?' || ch == '"' ||
            ch == '<' || ch == '>' || ch == '|') {
            sanitized.push_back('_');
        } else {
            sanitized.push_back(ch);
        }
    }
    if (sanitized.empty()) {
        sanitized = "target";
    }
    return sanitized;
}

std::string default_certificate_directory() {
#if defined(_WIN32)
    if (const char* profile = std::getenv("USERPROFILE")) {
        return std::string(profile) + "\\Documents\\DataScythe\\Certificates";
    }
    return "Certificates";
#else
    if (const char* home = std::getenv("HOME")) {
        return std::string(home) + "/Documents/DataScythe/Certificates";
    }
    return "Certificates";
#endif
}

std::string default_certificate_path(const std::string& target) {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &time);
#else
    localtime_r(&time, &local_tm);
#endif

    std::ostringstream filename;
    filename << "datascythe-certificate-" << sanitize_target_for_filename(target) << '-'
             << std::put_time(&local_tm, "%Y%m%d-%H%M%S") << ".txt";

#if defined(_WIN32)
    return default_certificate_directory() + "\\" + filename.str();
#else
    return default_certificate_directory() + "/" + filename.str();
#endif
}

bool ensure_parent_directory(const std::string& path, std::string& error_out) {
    const auto slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return true;
    }

    const std::string dir = path.substr(0, slash);
    if (dir.empty()) {
        return true;
    }

    std::string built;
    built.reserve(dir.size());
    for (std::size_t i = 0; i < dir.size(); ++i) {
        const char ch = dir[i];
        built.push_back(ch);
        const bool is_sep = ch == '/' || ch == '\\';
        if (is_sep && built.size() > 1) {
#if defined(_WIN32)
            if (_mkdir(built.c_str()) != 0 && errno != EEXIST) {
                error_out = "Unable to create directory: " + built;
                return false;
            }
#else
            if (mkdir(built.c_str(), 0755) != 0 && errno != EEXIST) {
                error_out = "Unable to create directory: " + built;
                return false;
            }
#endif
        }
    }

#if defined(_WIN32)
    if (_mkdir(dir.c_str()) != 0 && errno != EEXIST) {
#else
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
#endif
        error_out = "Unable to create directory: " + dir;
        return false;
    }
    return true;
}

std::string mode_to_string(EraseMode mode) {
    switch (mode) {
        case EraseMode::FullDeviceWipe:
            return "Full-device wipe";
        case EraseMode::QuickZeroFill:
            return "Quick zero-fill";
        case EraseMode::ShredVolume:
            return "Shred volume";
        case EraseMode::ShredFiles:
            return "Shred files";
        case EraseMode::ShredDirectory:
            return "Shred directory";
        case EraseMode::SsdSecureErase:
            return "SSD hardware secure erase";
    }
    return "Unknown";
}

ErasureCertificate build_certificate(const std::string& target, const EraseConfig& config,
                                     const EraseResult& result,
                                     const std::vector<std::string>& log_entries) {
    ErasureCertificate cert;
    cert.target = target;
    cert.mode_name = mode_to_string(config.mode);
    cert.pass_count = config.pass_count;
    cert.random_passes = config.use_random_passes;
    cert.final_zero_pass = config.final_zero_pass;
    cert.verification_enabled = config.verify_after_erase;
    cert.verification_passed = result.verification_passed;
    cert.partition_metadata_wipe = config.wipe_partition_metadata;
    cert.completed_at = timestamp_now();
    cert.success = result.success;
    cert.result_message = result.message;
    cert.warnings = result.warnings;

    const std::size_t max_lines = 50;
    const std::size_t start =
        log_entries.size() > max_lines ? log_entries.size() - max_lines : 0;
    for (std::size_t i = start; i < log_entries.size(); ++i) {
        cert.log_excerpt.push_back(log_entries[i]);
    }
    if (!cert.log_excerpt.empty()) {
        cert.started_at = cert.log_excerpt.front().substr(0, 19);
    } else {
        cert.started_at = cert.completed_at;
    }

    return cert;
}

bool export_certificate(const ErasureCertificate& cert, const std::string& path,
                        std::string& error_out) {
    if (!ensure_parent_directory(path, error_out)) {
        return false;
    }

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) {
        error_out = "Unable to write certificate: " + path;
        return false;
    }

    out << "============================================================\n";
    out << "              DATASCYTHE ERASURE CERTIFICATE\n";
    out << "============================================================\n\n";
    out << "Target:              " << cert.target << '\n';
    out << "Mode:                " << cert.mode_name << '\n';
    out << "Passes:              " << cert.pass_count << '\n';
    out << "Random passes:       " << (cert.random_passes ? "yes" : "no") << '\n';
    out << "Final zero pass:     " << (cert.final_zero_pass ? "yes" : "no") << '\n';
    out << "Partition metadata:  " << (cert.partition_metadata_wipe ? "wiped" : "skipped")
        << '\n';
    out << "Verification:        " << (cert.verification_enabled ? "enabled" : "disabled")
        << '\n';
    if (cert.verification_enabled) {
        out << "Verification result: " << (cert.verification_passed ? "PASSED" : "FAILED")
            << '\n';
    }
    out << "Started:             " << cert.started_at << '\n';
    out << "Completed:           " << cert.completed_at << '\n';
    out << "Status:              " << (cert.success ? "SUCCESS" : "FAILURE") << '\n';
    out << "Result:              " << cert.result_message << '\n';

    if (!cert.warnings.empty()) {
        out << "\nWarnings:\n";
        for (const auto& warning : cert.warnings) {
            out << "  - " << warning << '\n';
        }
    }

    out << "\nLimitations:\n";
    out << "  - Covers OS-addressable regions only.\n";
    out << "  - SSD wear-leveling, HPA/DCO, and firmware areas may retain data.\n";
    out << "  - Backups and remote copies are outside scope.\n";

    if (!cert.log_excerpt.empty()) {
        out << "\nLog excerpt:\n";
        for (const auto& line : cert.log_excerpt) {
            out << line << '\n';
        }
    }

    out << "\n============================================================\n";
    return true;
}

}  // namespace datascythe