#include "platform/drive_enumerator.h"
#include "platform/volume_manager.h"

#if defined(DATASCYTHE_PLATFORM_LINUX)

#include <cstdint>
#include <dirent.h>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace datascythe {

namespace {

std::string read_sysfs_line(const std::string& path) {
    std::ifstream in(path);
    std::string value;
    if (in) {
        std::getline(in, value);
    }
    return value;
}

bool read_sysfs_u64(const std::string& path, std::uint64_t& out) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    in >> out;
    return true;
}

DriveType classify(const std::string& type_path) {
    const std::string removable = read_sysfs_line(type_path + "/removable");
    if (removable == "1") {
        return DriveType::Removable;
    }
    const std::string rotational = read_sysfs_line(type_path + "/queue/rotational");
    if (rotational == "0") {
        return DriveType::SSD;
    }
    if (rotational == "1") {
        return DriveType::HDD;
    }
    return DriveType::Unknown;
}

class LinuxDriveEnumerator final : public IDriveEnumerator {
public:
    std::vector<DriveInfo> enumerate(std::string& error_out) override {
        std::vector<DriveInfo> drives;
        DIR* dir = opendir("/sys/block");
        if (!dir) {
            error_out = "Unable to open /sys/block (root privileges may be required)";
            return drives;
        }

        auto volume_manager = create_volume_manager();

        while (dirent* entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (name == "." || name == "..") {
                continue;
            }

            DriveInfo info;
            const std::string sysfs = "/sys/block/" + name;
            info.physical_index = static_cast<int>(drives.size());
            info.device_path = "/dev/" + name;
            info.model = read_sysfs_line(sysfs + "/device/model");
            info.serial = read_sysfs_line(sysfs + "/device/serial");
            info.type = classify(sysfs);
            info.is_removable = info.type == DriveType::Removable;
            read_sysfs_u64(sysfs + "/size", info.size_bytes);
            info.size_bytes *= 512;

            drives.push_back(std::move(info));
        }
        closedir(dir);

        if (drives.empty()) {
            error_out = "No block devices found";
        }
        return drives;
    }
};

}  

std::unique_ptr<IDriveEnumerator> create_drive_enumerator() {
    return std::make_unique<LinuxDriveEnumerator>();
}

}  

#endif