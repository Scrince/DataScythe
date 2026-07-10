#include "core/preflight.h"

#include <sstream>

namespace datascythe {

std::string format_preflight_report(const PreflightResult& result) {
    std::ostringstream oss;
    for (const auto& issue : result.issues) {
        switch (issue.severity) {
            case PreflightSeverity::Error:
                oss << "[ERROR] ";
                break;
            case PreflightSeverity::Warning:
                oss << "[WARN] ";
                break;
            case PreflightSeverity::Info:
                oss << "[INFO] ";
                break;
        }
        oss << issue.message << '\n';
    }
    return oss.str();
}

}  