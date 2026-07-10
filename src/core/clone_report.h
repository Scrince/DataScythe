#pragma once

#include "core/drive_clone_engine.h"
#include "core/erase_result.h"
#include "platform/drive_info.h"

#include <string>
#include <vector>

namespace datascythe {

struct CloneReport {
    DriveInfo source;
    DriveInfo target;
    DriveCloneConfig config;
    EraseResult result;
    std::vector<std::string> log_excerpt;
};

std::string default_clone_report_path(const std::string& source, const std::string& target);
bool export_clone_report(const CloneReport& report, const std::string& path,
                         std::string& error_out);

}  
