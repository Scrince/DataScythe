#pragma once

#include "core/erase_config.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace datascythe {

enum class PreflightSeverity {
    Info,
    Warning,
    Error,
};

struct PreflightIssue {
    PreflightSeverity severity = PreflightSeverity::Info;
    std::string message;
};

struct PreflightResult {
    bool ok() const {
        for (const auto& issue : issues) {
            if (issue.severity == PreflightSeverity::Error) {
                return false;
            }
        }
        return true;
    }

    std::vector<PreflightIssue> issues;
    std::uint64_t estimated_bytes = 0;
    std::string resolved_target;
};

class IPreflightChecker {
public:
    virtual ~IPreflightChecker() = default;
    virtual PreflightResult check(const std::string& target, EraseMode mode) = 0;
};

std::unique_ptr<IPreflightChecker> create_preflight_checker();

/// Format preflight output for display.
std::string format_preflight_report(const PreflightResult& result);

}  // namespace datascythe