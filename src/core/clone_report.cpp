#include "core/clone_report.h"

#include "core/certificate.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace datascythe {

namespace {

std::string timestamp_compact() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &time);
#else
    localtime_r(&time, &local_tm);
#endif
    std::ostringstream out;
    out << std::put_time(&local_tm, "%Y%m%d-%H%M%S");
    return out.str();
}

std::string timestamp_text() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &time);
#else
    localtime_r(&time, &local_tm);
#endif
    std::ostringstream out;
    out << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S UTC");
    return out.str();
}

void write_drive(std::ostream& out, const char* label, const DriveInfo& drive) {
    out << label << " path:       " << drive.device_path << '\n';
    out << label << " index:      " << drive.physical_index << '\n';
    out << label << " model:      " << (drive.model.empty() ? "N/A" : drive.model) << '\n';
    out << label << " vendor:     " << (drive.vendor.empty() ? "N/A" : drive.vendor) << '\n';
    out << label << " serial:     " << (drive.serial.empty() ? "N/A" : drive.serial) << '\n';
    out << label << " size bytes: " << drive.size_bytes << '\n';
    out << label << " system:     " << (drive.is_system_drive ? "yes" : "no") << '\n';
    out << label << " removable:  " << (drive.is_removable ? "yes" : "no") << '\n';
    out << label << " read-only:  " << (drive.is_read_only ? "yes" : "no") << '\n';
}

}  

std::string default_clone_report_path(const std::string& source, const std::string& target) {
    const std::string name = "datascythe-clone-report-" +
                             sanitize_target_for_filename(source) + "-to-" +
                             sanitize_target_for_filename(target) + "-" + timestamp_compact() +
                             ".txt";
#if defined(_WIN32)
    return default_certificate_directory() + "\\" + name;
#else
    return default_certificate_directory() + "/" + name;
#endif
}

bool export_clone_report(const CloneReport& report, const std::string& path,
                         std::string& error_out) {
    if (!ensure_parent_directory(path, error_out)) {
        return false;
    }

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) {
        error_out = "Unable to write clone report: " + path;
        return false;
    }

    out << "============================================================\n";
    out << "                  DATASCYTHE CLONE REPORT\n";
    out << "============================================================\n\n";
    out << "Generated:              " << timestamp_text() << '\n';
    out << "Status:                 " << (report.result.success ? "SUCCESS" : "FAILURE") << '\n';
    out << "Result:                 " << report.result.message << '\n';
    out << "Verification requested: " << (report.config.verify_after_clone ? "yes" : "no") << '\n';
    out << "Target tail wipe:       " << (report.config.wipe_target_tail ? "zero-fill" : "unchanged")
        << '\n';
    out << "Verification result:    "
        << (report.result.verification_passed ? "PASSED" : "FAILED") << '\n';
    out << "Source SHA-256:         "
        << (report.result.source_sha256.empty() ? "N/A" : report.result.source_sha256) << '\n';
    out << "Target SHA-256:         "
        << (report.result.target_sha256.empty() ? "N/A" : report.result.target_sha256) << '\n';

    out << "\nSource Drive\n";
    out << "------------\n";
    write_drive(out, "Source", report.source);

    out << "\nTarget Drive\n";
    out << "------------\n";
    write_drive(out, "Target", report.target);

    if (!report.result.warnings.empty()) {
        out << "\nWarnings:\n";
        for (const auto& warning : report.result.warnings) {
            out << "  - " << warning << '\n';
        }
    }

    out << "\nLimitations:\n";
    out << "  - Covers OS-addressable bytes from source offset 0 through source_size - 1.\n";
    out << "  - Target bytes beyond source_size are "
        << (report.config.wipe_target_tail ? "zero-filled when target is larger.\n"
                                           : "not modified when target is larger.\n");
    out << "  - A live source may change during acquisition unless externally write-blocked.\n";
    out << "  - Firmware-hidden/remapped regions are outside user-mode raw I/O visibility.\n";

    if (!report.log_excerpt.empty()) {
        out << "\nLog excerpt:\n";
        for (const auto& line : report.log_excerpt) {
            out << line << '\n';
        }
    }

    out << "\n============================================================\n";
    return true;
}

}  
