#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace datascythe {

enum class EraseError {
    None = 0,
    AccessDenied,
    DeviceBusy,
    InvalidTarget,
    IoError,
    Cancelled,
    PrivilegeRequired,
    ReadOnlyMedia,
    Unknown,
};

struct EraseProgress {
    std::size_t current_pass = 0;
    std::size_t total_passes = 0;
    std::string pass_label;
    std::string current_target;
    std::uint64_t bytes_written = 0;
    std::uint64_t total_bytes = 0;
    double percent_complete = 0.0;
    
    std::int64_t eta_seconds = -1;
    
    double overall_percent = 0.0;
};

struct EraseResult {
    bool success = false;
    EraseError error = EraseError::None;
    std::string message;
    std::vector<std::string> warnings;
    bool verification_passed = true;
    std::size_t verification_samples = 0;
    std::uint64_t verification_bytes = 0;
    std::string source_sha256;
    std::string target_sha256;
};

}  
