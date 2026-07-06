#include "core/path_collector.h"

#include "platform/ads_enumerator.h"

#include <filesystem>
#include <system_error>

namespace datascythe {

namespace fs = std::filesystem;

void append_with_streams(const std::string& file_path, bool include_ads,
                         std::vector<std::string>& files) {
    files.push_back(file_path);
    if (!include_ads) {
        return;
    }
    for (const auto& stream_path : enumerate_alternate_data_streams(file_path)) {
        files.push_back(stream_path);
    }
}

std::vector<std::string> PathCollector::collect_files(const std::string& path, bool recursive,
                                                      bool include_alternate_data_streams,
                                                      std::string& error_out) {
    std::vector<std::string> files;
    std::error_code ec;
    const fs::path root(path);

    if (!fs::exists(root, ec)) {
        error_out = "Path does not exist: " + path;
        return files;
    }

    if (fs::is_regular_file(root, ec)) {
        append_with_streams(fs::absolute(root, ec).string(), include_alternate_data_streams, files);
        return files;
    }

    if (!fs::is_directory(root, ec)) {
        error_out = "Path is not a file or directory: " + path;
        return files;
    }

    const auto options = recursive ? fs::directory_options::skip_permission_denied
                                   : fs::directory_options::none;

    if (recursive) {
        for (const auto& entry : fs::recursive_directory_iterator(root, options, ec)) {
            if (ec) {
                error_out = ec.message();
                break;
            }
            if (entry.is_regular_file(ec)) {
                append_with_streams(entry.path().string(), include_alternate_data_streams, files);
            }
        }
    } else {
        for (const auto& entry : fs::directory_iterator(root, options, ec)) {
            if (ec) {
                error_out = ec.message();
                break;
            }
            if (entry.is_regular_file(ec)) {
                append_with_streams(entry.path().string(), include_alternate_data_streams, files);
            }
        }
    }

    return files;
}

}  // namespace datascythe