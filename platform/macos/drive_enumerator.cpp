#include "platform/drive_enumerator.h"

#if defined(DATASCYTHE_PLATFORM_MACOS)

#include <dirent.h>
#include <memory>
#include <regex>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace datascythe {

namespace {

class MacDriveEnumerator final : public IDriveEnumerator {
public:
    std::vector<DriveInfo> enumerate(std::string& error_out) override {
        std::vector<DriveInfo> drives;
        DIR* dir = opendir("/dev");
        if (!dir) {
            error_out = "Unable to read /dev";
            return drives;
        }

        const std::regex whole_disk(R"(^disk\d+$)");
        int index = 0;

        while (dirent* entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (!std::regex_match(name, whole_disk)) {
                continue;
            }

            DriveInfo info;
            info.physical_index = index++;
            info.device_path = "/dev/" + name;
            info.model = name;
            info.type = DriveType::Unknown;
            drives.push_back(std::move(info));
        }
        closedir(dir);

        if (drives.empty()) {
            error_out = "No disk devices found in /dev";
        }
        return drives;
    }
};

}  

std::unique_ptr<IDriveEnumerator> create_drive_enumerator() {
    return std::make_unique<MacDriveEnumerator>();
}

}  

#endif