#include "core/logger.h"

#include <iomanip>
#include <sstream>

namespace datascythe {

namespace {

std::string timestamp_now() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &time);
#else
    localtime_r(&time, &local_tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

}  // namespace

void Logger::info(const std::string& message) { append("INFO", message); }
void Logger::warning(const std::string& message) { append("WARN", message); }
void Logger::error(const std::string& message) { append("ERROR", message); }

void Logger::begin_session(const std::string& device_id, const std::string& mode_summary) {
    std::lock_guard<std::mutex> lock(mutex_);
    session_start_ = std::chrono::system_clock::now();
    session_device_ = device_id;
    session_mode_ = mode_summary;
    append("INFO", "Session started for " + device_id + " mode=" + mode_summary);
}

void Logger::end_session(bool success) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto end = std::chrono::system_clock::now();
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(end - session_start_).count();
    append(success ? "INFO" : "ERROR",
           std::string("Session ended status=") + (success ? "success" : "failure") +
               " duration_s=" + std::to_string(seconds));
}

bool Logger::export_to_file(const std::string& path, std::string& error_out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) {
        error_out = "Failed to open log file for writing: " + path;
        return false;
    }
    for (const auto& line : entries_) {
        out << line << '\n';
    }
    return true;
}

void Logger::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
}

void Logger::append(const std::string& level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.push_back("[" + timestamp_now() + "] [" + level + "] " + message);
}

}  // namespace datascythe