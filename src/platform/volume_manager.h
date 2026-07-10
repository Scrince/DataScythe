#pragma once

#include <memory>
#include <string>
#include <vector>

namespace datascythe {

class IVolumeManager {
public:
    virtual ~IVolumeManager() = default;

    
    virtual bool is_system_volume(const std::string& path) const = 0;

    
    virtual bool dismount_volume(const std::string& mount_point, std::string& error_out) = 0;

    
    virtual bool dismount_physical_drive(int physical_index, std::string& error_out) = 0;
};

std::unique_ptr<IVolumeManager> create_volume_manager();

}  