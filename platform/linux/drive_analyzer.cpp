#include "platform/drive_analyzer.h"

#if defined(DATASCYTHE_PLATFORM_LINUX)

#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace datascythe {

namespace {

std::string read_line(const std::string& path) {
    std::ifstream in(path);
    std::string value;
    if (in) {
        std::getline(in, value);
    }
    return value;
}

void add(DriveAnalysis& analysis, const std::string& category, const std::string& name,
         const std::string& value) {
    analysis.fields.push_back({category, name, value.empty() ? "N/A" : value});
}

std::string bool_text(bool value) {
    return value ? "Yes" : "No";
}

std::string basename(const std::string& path) {
    const auto pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

class LinuxDriveAnalyzer final : public IDriveAnalyzer {
public:
    DriveAnalysis analyze(const DriveInfo& drive) override {
        DriveAnalysis analysis;
        analysis.drive = drive;
        analysis.is_ssd = drive.type == DriveType::SSD;
        analysis.health_summary =
            "Linux sysfs properties collected. Full SMART/NVMe health logs require vendor tooling "
            "or smartctl/nvme-cli outside this generic view.";

        add(analysis, "Identity", "Device path", drive.device_path);
        add(analysis, "Identity", "Physical index", std::to_string(drive.physical_index));
        add(analysis, "Identity", "Model", drive.model);
        add(analysis, "Identity", "Serial", drive.serial);
        add(analysis, "Capacity", "Size bytes", std::to_string(drive.size_bytes));
        add(analysis, "Classification", "SSD/non-rotational", bool_text(analysis.is_ssd));
        add(analysis, "Safety", "System drive", bool_text(drive.is_system_drive));
        add(analysis, "Safety", "Removable", bool_text(drive.is_removable));
        add(analysis, "Safety", "Read-only", bool_text(drive.is_read_only));

        const std::string sysfs = "/sys/block/" + basename(drive.device_path);
        add(analysis, "Sysfs", "Rotational", read_line(sysfs + "/queue/rotational"));
        add(analysis, "Sysfs", "Removable", read_line(sysfs + "/removable"));
        add(analysis, "Sysfs", "Read-only", read_line(sysfs + "/ro"));
        add(analysis, "Sysfs", "Logical block size",
            read_line(sysfs + "/queue/logical_block_size"));
        add(analysis, "Sysfs", "Physical block size",
            read_line(sysfs + "/queue/physical_block_size"));
        add(analysis, "Sysfs", "Minimum IO size", read_line(sysfs + "/queue/minimum_io_size"));
        add(analysis, "Sysfs", "Optimal IO size", read_line(sysfs + "/queue/optimal_io_size"));
        add(analysis, "Sysfs", "Discard max bytes",
            read_line(sysfs + "/queue/discard_max_bytes"));
        add(analysis, "Sysfs", "Discard granularity",
            read_line(sysfs + "/queue/discard_granularity"));
        add(analysis, "Sysfs", "Scheduler", read_line(sysfs + "/queue/scheduler"));
        add(analysis, "Sysfs", "Write cache", read_line(sysfs + "/queue/write_cache"));
        add(analysis, "Sysfs", "Firmware revision", read_line(sysfs + "/device/rev"));
        add(analysis, "Sysfs", "Vendor", read_line(sysfs + "/device/vendor"));

        return analysis;
    }
};

}  

std::unique_ptr<IDriveAnalyzer> create_drive_analyzer() {
    return std::make_unique<LinuxDriveAnalyzer>();
}

}  

#endif
