#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace datascythe {

enum class DriveType {
    Unknown,
    HDD,
    SSD,
    Removable,
    Virtual,
};

struct VolumeInfo {
    std::string mount_point;
    std::string filesystem;
    std::uint64_t size_bytes = 0;
    bool is_system_volume = false;
};

struct DriveInfo {
    int physical_index = -1;
    std::string device_path;
    std::string model;
    std::string serial;
    std::string vendor;
    DriveType type = DriveType::Unknown;
    std::uint64_t size_bytes = 0;
    bool is_system_drive = false;
    bool is_removable = false;
    bool is_read_only = false;
    std::vector<VolumeInfo> volumes;
};

}  // namespace datascythe