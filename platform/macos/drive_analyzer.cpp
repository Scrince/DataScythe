#include "platform/drive_analyzer.h"

#if defined(DATASCYTHE_PLATFORM_MACOS)

#include <cstdio>
#include <memory>
#include <sstream>
#include <string>

namespace datascythe {

namespace {

void add(DriveAnalysis& analysis, const std::string& category, const std::string& name,
         const std::string& value) {
    analysis.fields.push_back({category, name, value.empty() ? "N/A" : value});
}

std::string bool_text(bool value) {
    return value ? "Yes" : "No";
}

std::string diskutil_info(const std::string& device_path) {
    std::string command = "/usr/sbin/diskutil info " + device_path + " 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return {};
    }
    char buffer[512];
    std::ostringstream out;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        out << buffer;
    }
    pclose(pipe);
    return out.str();
}

class MacDriveAnalyzer final : public IDriveAnalyzer {
public:
    DriveAnalysis analyze(const DriveInfo& drive) override {
        DriveAnalysis analysis;
        analysis.drive = drive;
        analysis.is_ssd = drive.type == DriveType::SSD;
        analysis.health_summary =
            "macOS diskutil information collected when available. Full SMART/NVMe health logs "
            "require platform-specific tooling outside this generic view.";

        add(analysis, "Identity", "Device path", drive.device_path);
        add(analysis, "Identity", "Physical index", std::to_string(drive.physical_index));
        add(analysis, "Identity", "Model", drive.model);
        add(analysis, "Identity", "Serial", drive.serial);
        add(analysis, "Capacity", "Size bytes", std::to_string(drive.size_bytes));
        add(analysis, "Classification", "SSD/non-rotational", bool_text(analysis.is_ssd));
        add(analysis, "Safety", "System drive", bool_text(drive.is_system_drive));
        add(analysis, "Safety", "Removable", bool_text(drive.is_removable));
        add(analysis, "Safety", "Read-only", bool_text(drive.is_read_only));

        const std::string info = diskutil_info(drive.device_path);
        if (info.empty()) {
            analysis.warnings.push_back("diskutil info was unavailable for " + drive.device_path);
        } else {
            add(analysis, "diskutil", "Raw info", info);
        }
        return analysis;
    }
};

}  

std::unique_ptr<IDriveAnalyzer> create_drive_analyzer() {
    return std::make_unique<MacDriveAnalyzer>();
}

}  

#endif
