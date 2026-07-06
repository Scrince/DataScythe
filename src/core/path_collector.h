#pragma once

#include <string>
#include <vector>

namespace datascythe {

/// Collects file paths for shredding. Skips symlinks by default.
class PathCollector {
public:
    static std::vector<std::string> collect_files(const std::string& path, bool recursive,
                                                  bool include_alternate_data_streams,
                                                  std::string& error_out);
};

}  // namespace datascythe