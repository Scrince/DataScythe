#pragma once

#include "platform/drive_info.h"

#include <memory>
#include <string>
#include <vector>

namespace datascythe {

struct DriveAnalysisField {
    std::string category;
    std::string name;
    std::string value;
};

struct DriveAnalysis {
    DriveInfo drive;
    bool is_ssd = false;
    std::string health_summary;
    std::vector<DriveAnalysisField> fields;
    std::vector<std::string> warnings;
};

class IDriveAnalyzer {
public:
    virtual ~IDriveAnalyzer() = default;
    virtual DriveAnalysis analyze(const DriveInfo& drive) = 0;
};

std::unique_ptr<IDriveAnalyzer> create_drive_analyzer();

}  
