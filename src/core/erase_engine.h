#pragma once

#include "core/erase_config.h"
#include "core/erase_result.h"
#include "core/logger.h"
#include "platform/raw_device.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace datascythe {

/// Pure C++ erase engine. Platform specifics are injected via IRawDevice.
///
/// Security note: this engine overwrites every byte the OS exposes for the target.
/// Hardware-reserved regions (HPA/DCO, firmware, SSD controller remapped blocks,
/// wear-leveling pools) may remain outside user-space visibility.
class EraseEngine {
public:
    static constexpr std::size_t kBufferSize = 1024 * 1024;  // 1 MiB chunks
    static constexpr std::size_t kSectorSize = 512;

    explicit EraseEngine(std::unique_ptr<IRawDevice> device);

    void set_logger(Logger* logger) { logger_ = logger; }
    void request_cancel() { cancel_requested_.store(true); }

    EraseResult erase_target(const std::string& path, const EraseConfig& config,
                             ProgressCallback progress);

    /// Shred multiple files or the contents of a directory.
    EraseResult erase_paths(const std::vector<std::string>& paths, const EraseConfig& config,
                            ProgressCallback progress);

private:
    struct OperationContext {
        std::uint64_t overall_bytes_total = 0;
        std::uint64_t overall_bytes_done = 0;
        std::size_t file_index = 0;
        std::size_t file_count = 0;
        std::string current_target;
        std::chrono::steady_clock::time_point started{};
    };

    EraseResult erase_single_open_target(const std::string& path, const EraseConfig& config,
                                         std::uint64_t total_size, OperationContext& op,
                                         ProgressCallback progress);

    EraseResult run_quick_zero(const EraseConfig& config, std::uint64_t total_size,
                               OperationContext& op, ProgressCallback progress,
                               EraseResult& result);
    EraseResult run_passes(const EraseConfig& config, std::uint64_t total_size,
                           OperationContext& op, ProgressCallback progress, EraseResult& result);

    int final_verification_pattern(const EraseConfig& config) const;

    bool write_full_pass(int pattern_type, std::uint64_t total_size, std::size_t pass_index,
                         std::size_t total_passes, const std::string& pass_label,
                         const std::string& target_label, OperationContext& op,
                         ProgressCallback progress, EraseResult& result);

    bool verify_pass(int pattern_type, std::uint64_t total_size, OperationContext& op,
                     ProgressCallback progress, EraseResult& result);

    void report_progress(EraseProgress& prog, const OperationContext& op,
                         ProgressCallback& progress);

    std::uint64_t effective_size(IRawDevice& device, const EraseConfig& config,
                                 std::string& error_out) const;

    std::unique_ptr<IRawDevice> device_;
    Logger* logger_ = nullptr;
    std::atomic<bool> cancel_requested_{false};
};

}  // namespace datascythe