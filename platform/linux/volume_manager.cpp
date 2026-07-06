#include "platform/volume_manager.h"

#if defined(DATASCYTHE_PLATFORM_LINUX)

#include <fstream>
#include <memory>
#include <string>

namespace datascythe {

namespace {

class LinuxVolumeManager final : public IVolumeManager {
public:
    bool is_system_volume(const std::string& path) const override {
        std::ifstream mounts("/proc/mounts");
        std::string device, mount_point, fs_type;
        while (mounts >> device >> mount_point >> fs_type) {
            if (mount_point == "/" && path.find(device) != std::string::npos) {
                return true;
            }
            if (mount_point == path || mount_point + "/" == path) {
                return mount_point == "/";
            }
        }
        return path == "/" || path == "/root";
    }

    bool dismount_volume(const std::string& mount_point, std::string& error_out) override {
        (void)mount_point;
        error_out = "Use umount externally before wiping block devices";
        return false;
    }

    bool dismount_physical_drive(int physical_index, std::string& error_out) override {
        (void)physical_index;
        error_out = "Dismount volumes manually on Linux before full-device wipe";
        return false;
    }
};

}  // namespace

std::unique_ptr<IVolumeManager> create_volume_manager() {
    return std::make_unique<LinuxVolumeManager>();
}

}  // namespace datascythe

#endif