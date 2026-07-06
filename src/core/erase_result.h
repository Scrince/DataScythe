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
    /// Seconds remaining for the entire operation (-1 if unknown).
    std::int64_t eta_seconds = -1;
    /// Overall progress across files and passes (0-100).
    double overall_percent = 0.0;
};

struct EraseResult {
    bool success = false;
    EraseError error = EraseError::None;
    std::string message;
    std::vector<std::string> warnings;
    bool verification_passed = true;
    std::size_t verification_samples = 0;
};

}  // namespace datascythe