#pragma once

#include "core/erase_config.h"
#include "core/erase_result.h"

#include <string>
#include <vector>

namespace datascythe {

struct ErasureCertificate {
    std::string target;
    std::string mode_name;
    std::size_t pass_count = 0;
    bool random_passes = false;
    bool final_zero_pass = false;
    bool verification_enabled = false;
    bool verification_passed = false;
    bool partition_metadata_wipe = false;
    std::string started_at;
    std::string completed_at;
    bool success = false;
    std::string result_message;
    std::string content_sha256;
    std::vector<std::string> warnings;
    std::vector<std::string> log_excerpt;
};

ErasureCertificate build_certificate(const std::string& target, const EraseConfig& config,
                                     const EraseResult& result,
                                     const std::vector<std::string>& log_entries);

bool export_certificate(const ErasureCertificate& cert, const std::string& path,
                        std::string& error_out);

std::string mode_to_string(EraseMode mode);


std::string sanitize_target_for_filename(const std::string& target);


std::string default_certificate_directory();


std::string default_certificate_path(const std::string& target);

bool ensure_parent_directory(const std::string& path, std::string& error_out);

}  
