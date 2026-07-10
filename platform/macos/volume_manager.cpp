#include "platform/volume_manager.h"

#if defined(DATASCYTHE_PLATFORM_MACOS)

#include <memory>
#include <string>

namespace datascythe {

namespace {

class MacVolumeManager final : public IVolumeManager {
public:
    bool is_system_volume(const std::string& path) const override {
        return path == "/" || path.find("/System/Volumes/Data") != std::string::npos;
    }

    bool dismount_volume(const std::string& mount_point, std::string& error_out) override {
        (void)mount_point;
        error_out = "Dismount volumes with diskutil before wiping";
        return false;
    }

    bool dismount_physical_drive(int physical_index, std::string& error_out) override {
        (void)physical_index;
        error_out = "Dismount volumes with diskutil before wiping";
        return false;
    }
};

}  

std::unique_ptr<IVolumeManager> create_volume_manager() {
    return std::make_unique<MacVolumeManager>();
}

}  

#endif