#pragma once

#include <string>
#include <vector>

namespace datascythe {

/// Enumerates NTFS alternate data streams for a file (Windows). Empty on other platforms.
std::vector<std::string> enumerate_alternate_data_streams(const std::string& file_path);

}  // namespace datascythe