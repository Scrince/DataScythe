#pragma once

#include <chrono>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace datascythe {

/// Thread-safe in-memory log with optional file export.
class Logger {
public:
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);

    void begin_session(const std::string& device_id, const std::string& mode_summary);
    void end_session(bool success);

    const std::vector<std::string>& entries() const { return entries_; }

    bool export_to_file(const std::string& path, std::string& error_out) const;

    void clear();

private:
    void append(const std::string& level, const std::string& message);

    mutable std::mutex mutex_;
    std::vector<std::string> entries_;
    std::chrono::system_clock::time_point session_start_{};
    std::string session_device_;
    std::string session_mode_;
};

}  // namespace datascythe