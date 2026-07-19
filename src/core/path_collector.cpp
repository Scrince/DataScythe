#include "core/path_collector.h"

#include "platform/ads_enumerator.h"

#include <filesystem>
#include <system_error>

namespace datascythe {

namespace fs = std::filesystem;

namespace {

bool path_under_root(const fs::path& root_real, const fs::path& candidate, std::error_code& ec) {
    const fs::path cand_real = fs::weakly_canonical(candidate, ec);
    if (ec) {
        return false;
    }
    auto root_it = root_real.begin();
    auto cand_it = cand_real.begin();
    for (; root_it != root_real.end() && cand_it != cand_real.end(); ++root_it, ++cand_it) {
        if (*root_it != *cand_it) {
            return false;
        }
    }
    return root_it == root_real.end();
}

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

bool is_safe_regular_file(const fs::directory_entry& entry, const fs::path& root_real,
                          std::error_code& ec) {
    // Refuse symlinks so shred never follows links outside the requested tree.
    if (entry.is_symlink(ec)) {
        return false;
    }
    if (!entry.is_regular_file(ec)) {
        return false;
    }
    return path_under_root(root_real, entry.path(), ec);
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

    // Top-level path must not be a symlink when collecting a single file.
    if (fs::is_symlink(root, ec)) {
        error_out = "Refusing to shred through a symbolic link: " + path;
        return files;
    }

    const fs::path root_real = fs::weakly_canonical(root, ec);
    if (ec) {
        error_out = "Unable to resolve path: " + path + " (" + ec.message() + ")";
        return files;
    }

    if (fs::is_regular_file(root, ec)) {
        append_with_streams(root_real.string(), include_alternate_data_streams, files);
        return files;
    }

    if (!fs::is_directory(root, ec)) {
        error_out = "Path is not a file or directory: " + path;
        return files;
    }

    const auto options = recursive ? fs::directory_options::skip_permission_denied
                                   : fs::directory_options::none;
    // Never follow directory symlinks into foreign trees.
    const auto follow = fs::directory_options::none;

    if (recursive) {
        for (const auto& entry :
             fs::recursive_directory_iterator(root, options | follow, ec)) {
            if (ec) {
                error_out = ec.message();
                break;
            }
            std::error_code entry_ec;
            if (is_safe_regular_file(entry, root_real, entry_ec)) {
                append_with_streams(entry.path().string(), include_alternate_data_streams, files);
            }
        }
    } else {
        for (const auto& entry : fs::directory_iterator(root, options | follow, ec)) {
            if (ec) {
                error_out = ec.message();
                break;
            }
            std::error_code entry_ec;
            if (is_safe_regular_file(entry, root_real, entry_ec)) {
                append_with_streams(entry.path().string(), include_alternate_data_streams, files);
            }
        }
    }

    return files;
}

}  
