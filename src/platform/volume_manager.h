#pragma once

#include <memory>
#include <string>
#include <vector>

namespace datascythe {

class IVolumeManager {
public:
    virtual ~IVolumeManager() = default;

    /// Returns true when the path refers to the OS system volume.
    virtual bool is_system_volume(const std::string& path) const = 0;

    /// Best-effort dismount for a volume mount point like "E:\\".
    virtual bool dismount_volume(const std::string& mount_point, std::string& error_out) = 0;

    /// Lock and dismount all volumes on a physical drive index.
    virtual bool dismount_physical_drive(int physical_index, std::string& error_out) = 0;
};

std::unique_ptr<IVolumeManager> create_volume_manager();

}  // namespace datascythe