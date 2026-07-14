#pragma once

#include "core/erase_result.h"
#include "core/logger.h"
#include "platform/raw_device.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace datascythe {

struct DriveCloneConfig {
    bool verify_after_clone = true;
    bool wipe_target_tail = false;
};

class DriveCloneEngine {
public:
    static constexpr std::size_t kBufferSize = 4 * 1024 * 1024;

    DriveCloneEngine(std::unique_ptr<IRawDevice> source, std::unique_ptr<IRawDevice> target);

    void set_logger(Logger* logger) { logger_ = logger; }
    void request_cancel() { cancel_requested_.store(true); }

    EraseResult clone(const std::string& source_path, const std::string& target_path,
                      const DriveCloneConfig& config, ProgressCallback progress);

private:
    bool copy_bytes(std::uint64_t total_size, std::size_t total_passes,
                    std::uint64_t overall_total, ProgressCallback progress, EraseResult& result);
    bool wipe_tail(std::uint64_t source_size, std::uint64_t target_size,
                   std::size_t pass, std::size_t total_passes, std::uint64_t overall_base,
                   std::uint64_t overall_total, ProgressCallback progress, EraseResult& result);
    bool verify_bytes(std::uint64_t total_size, ProgressCallback progress, EraseResult& result);
    void report_progress(const std::string& phase, std::size_t pass, std::size_t total_passes,
                         std::uint64_t phase_done, std::uint64_t phase_total,
                         std::uint64_t overall_done, std::uint64_t overall_total,
                         ProgressCallback& progress);

    std::unique_ptr<IRawDevice> source_;
    std::unique_ptr<IRawDevice> target_;
    Logger* logger_ = nullptr;
    std::atomic<bool> cancel_requested_{false};
};

}  
