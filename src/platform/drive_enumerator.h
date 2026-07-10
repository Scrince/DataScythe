#pragma once

#include "platform/drive_info.h"

#include <memory>
#include <string>
#include <vector>

namespace datascythe {

class IDriveEnumerator {
public:
    virtual ~IDriveEnumerator() = default;
    virtual std::vector<DriveInfo> enumerate(std::string& error_out) = 0;
};

std::unique_ptr<IDriveEnumerator> create_drive_enumerator();

}  