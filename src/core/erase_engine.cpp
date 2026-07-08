#include "core/erase_engine.h"

#include "core/partition_table.h"
#include "core/pass_scheduler.h"
#include "core/path_collector.h"
#include "core/pattern_generator.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace datascythe {

EraseEngine::EraseEngine(std::unique_ptr<IRawDevice> device) : device_(std::move(device)) {}

void EraseEngine::report_progress(EraseProgress& prog, const OperationContext& op,
                                  ProgressCallback& progress) {
    if (op.overall_bytes_total > 0) {
        prog.overall_percent =
            static_cast<double>(op.overall_bytes_done) * 100.0 /
            static_cast<double>(op.overall_bytes_total);
    } else {
        prog.overall_percent = prog.percent_complete;
    }

    const auto elapsed = std::chrono::steady_clock::now() - op.started;
    const auto elapsed_sec =
        std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    if (op.overall_bytes_done > 0 && elapsed_sec > 0) {
        const double bytes_per_sec =
            static_cast<double>(op.overall_bytes_done) / static_cast<double>(elapsed_sec);
        const double remaining =
            static_cast<double>(op.overall_bytes_total - op.overall_bytes_done);
        prog.eta_seconds = static_cast<std::int64_t>(remaining / bytes_per_sec);
    } else {
        prog.eta_seconds = -1;
    }

    if (progress && !progress(prog)) {
        cancel_requested_.store(true);
    }
}

std::uint64_t EraseEngine::effective_size(IRawDevice& device, const EraseConfig& config,
                                          std::string& error_out) const {
    std::uint64_t size = device.size_bytes(error_out);
    if (size == 0) {
        return 0;
    }

    if (config.round_file_size_to_block && device.target_type() == RawTargetType::RegularFile) {
        const std::uint64_t block = device.block_size(error_out);
        if (block > 1) {
            size += block - 1 - (size - 1) % block;
        }
    }
    return size;
}

EraseResult EraseEngine::erase_paths(const std::vector<std::string>& paths,
                                     const EraseConfig& config, ProgressCallback progress) {
    cancel_requested_.store(false);
    EraseResult aggregate;
    aggregate.success = true;

    std::vector<std::string> all_files;
    for (const auto& path : paths) {
        std::string collect_error;
        auto files = PathCollector::collect_files(path, config.recursive,
                                                  config.shred_alternate_data_streams,
                                                  collect_error);
        if (!collect_error.empty()) {
            aggregate.success = false;
            aggregate.error = EraseError::InvalidTarget;
            aggregate.message = collect_error;
            return aggregate;
        }
        all_files.insert(all_files.end(), files.begin(), files.end());
    }

    if (all_files.empty()) {
        aggregate.success = false;
        aggregate.error = EraseError::InvalidTarget;
        aggregate.message = "No files found to shred";
        return aggregate;
    }

    OperationContext op;
    op.file_count = all_files.size();
    op.started = std::chrono::steady_clock::now();

    EraseConfig work_config = config;
    work_config.mode = EraseMode::ShredFiles;

    for (const auto& file : all_files) {
        std::string error;
        if (!device_->open(file, error)) {
            aggregate.warnings.push_back("Skipped (open failed): " + file + " - " + error);
            continue;
        }
        const std::uint64_t size = effective_size(*device_, work_config, error);
        device_->close();
        if (size == 0) {
            continue;
        }
        const std::size_t passes =
            work_config.pass_count + (work_config.final_zero_pass ? 1 : 0);
        op.overall_bytes_total += size * passes;
    }

    if (logger_) {
        logger_->begin_session("files:" + std::to_string(all_files.size()),
                               "ShredFiles passes=" + std::to_string(config.pass_count));
    }

    std::size_t failed_targets = 0;
    std::size_t shredded_targets = 0;

    for (const auto& file : all_files) {
        if (cancel_requested_.load()) {
            aggregate.success = false;
            aggregate.error = EraseError::Cancelled;
            aggregate.message = "Operation cancelled by user";
            break;
        }

        ++op.file_index;
        op.current_target = file;

        std::string error;
        if (!device_->open(file, error)) {
            aggregate.warnings.push_back("Skipped: " + file + " - " + error);
            ++failed_targets;
            continue;
        }

        const std::uint64_t size = effective_size(*device_, work_config, error);
        if (size == 0) {
            if (config.remove_after_shred) {
                if (!device_->remove_target(error)) {
                    aggregate.warnings.push_back("Failed to remove zero-size file " + file +
                                                 ": " + error);
                    ++failed_targets;
                } else if (logger_) {
                    logger_->info("Removed zero-size file " + file);
                }
            } else {
                aggregate.warnings.push_back("Skipped overwrite for zero-size file: " + file);
            }
            device_->close();
            ++shredded_targets;
            continue;
        }

        auto file_result =
            erase_single_open_target(file, work_config, size, op, progress);
        aggregate.warnings.insert(aggregate.warnings.end(), file_result.warnings.begin(),
                                  file_result.warnings.end());
        if (!file_result.success) {
            aggregate.success = false;
            aggregate.error = file_result.error;
            aggregate.message = file_result.message;
            device_->close();
            break;
        }

        if (config.remove_after_shred) {
            if (!device_->remove_target(error)) {
                aggregate.warnings.push_back("Failed to remove " + file + ": " + error);
                ++failed_targets;
            } else if (logger_) {
                logger_->info("Removed " + file);
            }
        }

        device_->close();
        ++shredded_targets;
    }

    if (aggregate.success && failed_targets > 0) {
        aggregate.success = false;
        aggregate.error = EraseError::IoError;
        aggregate.message = "Shred incomplete: " + std::to_string(failed_targets) +
                            " of " + std::to_string(all_files.size()) +
                            " file(s) failed or were skipped";
    } else if (aggregate.success) {
        aggregate.message = "Shredded " + std::to_string(shredded_targets) + " file(s)";
    }

    if (logger_) {
        logger_->end_session(aggregate.success);
    }
    return aggregate;
}

EraseResult EraseEngine::erase_target(const std::string& path, const EraseConfig& config,
                                      ProgressCallback progress) {
    cancel_requested_.store(false);
    EraseResult result;

    if (config.mode == EraseMode::ShredFiles || config.mode == EraseMode::ShredDirectory) {
        return erase_paths({path}, config, progress);
    }

    std::string error;
    if (!device_->open(path, error)) {
        result.success = false;
        result.error = EraseError::AccessDenied;
        result.message = error;
        if (logger_) {
            logger_->error(result.message);
        }
        return result;
    }

    if (config.mode == EraseMode::FullDeviceWipe || config.mode == EraseMode::QuickZeroFill) {
        if (!device_->dismount_volumes(error)) {
            result.warnings.push_back("Volume dismount warning: " + error);
            if (logger_) {
                logger_->warning(result.warnings.back());
            }
        }
    }

    const std::uint64_t total_size = effective_size(*device_, config, error);
    if (total_size == 0) {
        result.success = false;
        result.error = EraseError::InvalidTarget;
        result.message = error.empty() ? "Unable to determine target size" : error;
        device_->close();
        if (logger_) {
            logger_->error(result.message);
        }
        return result;
    }

    if (logger_) {
        std::ostringstream mode;
        mode << static_cast<int>(config.mode) << " passes=" << config.pass_count
             << " zero_final=" << (config.final_zero_pass ? "yes" : "no");
        logger_->begin_session(path, mode.str());
        logger_->info("Target size bytes=" + std::to_string(total_size));
    }

    result.warnings.push_back(
        "Best-effort overwrite of OS-addressable sectors only. SSD wear-leveling, HPA/DCO, "
        "and firmware regions may retain data.");

    OperationContext op;
    op.file_count = 1;
    op.file_index = 1;
    op.current_target = path;
    op.started = std::chrono::steady_clock::now();

    const bool block_device = device_->target_type() == RawTargetType::BlockDevice;
    const bool wipe_metadata =
        config.wipe_partition_metadata && block_device &&
        (config.mode == EraseMode::FullDeviceWipe || config.mode == EraseMode::QuickZeroFill);

    std::uint64_t metadata_bytes = 0;
    if (wipe_metadata) {
        const auto regions = partition_metadata_regions(total_size);
        metadata_bytes = regions.front_size + regions.back_size;
    }

    if (config.mode == EraseMode::QuickZeroFill) {
        op.overall_bytes_total = total_size + metadata_bytes;
    } else {
        op.overall_bytes_total =
            total_size * (config.pass_count + (config.final_zero_pass ? 1 : 0)) + metadata_bytes;
    }

    if (wipe_metadata) {
        if (logger_) {
            logger_->info("Wiping partition metadata regions");
        }
        if (!wipe_partition_metadata(*device_, total_size, cancel_requested_, error, progress)) {
            result.success = false;
            result.error = cancel_requested_.load() ? EraseError::Cancelled : EraseError::IoError;
            result.message = error.empty() ? "Partition metadata wipe failed" : error;
            device_->close();
            if (logger_) {
                logger_->error(result.message);
                logger_->end_session(false);
            }
            return result;
        }
        op.overall_bytes_done += metadata_bytes;
        result.warnings.push_back(
            "Partition metadata (MBR/GPT primary and backup) overwritten before device erase.");
    }

    auto op_result = erase_single_open_target(path, config, total_size, op, progress);

    if (config.remove_after_shred && device_->target_type() == RawTargetType::RegularFile &&
        op_result.success) {
        if (!device_->remove_target(error)) {
            op_result.warnings.push_back("Failed to remove file: " + error);
        }
    }

    device_->close();

    if (logger_) {
        logger_->end_session(op_result.success);
    }

    op_result.warnings.insert(op_result.warnings.begin(), result.warnings.begin(),
                              result.warnings.end());
    return op_result;
}

EraseResult EraseEngine::erase_single_open_target(const std::string& path,
                                                  const EraseConfig& config,
                                                  std::uint64_t total_size,
                                                  OperationContext& op,
                                                  ProgressCallback progress) {
    EraseResult result;
    result.success = true;
    op.current_target = path;

    if (config.mode == EraseMode::QuickZeroFill) {
        return run_quick_zero(config, total_size, op, progress, result);
    }

    return run_passes(config, total_size, op, progress, result);
}

int EraseEngine::final_verification_pattern(const EraseConfig& config) const {
    if (config.mode == EraseMode::QuickZeroFill) {
        return 0x000;
    }
    if (config.final_zero_pass) {
        return 0x000;
    }

    PassScheduler scheduler;
    std::vector<int> schedule =
        scheduler.build_schedule(config.pass_count, config.use_random_passes);
    if (schedule.empty()) {
        return 0x000;
    }
    return schedule.back();
}

EraseResult EraseEngine::run_quick_zero(const EraseConfig& config, std::uint64_t total_size,
                                        OperationContext& op, ProgressCallback progress,
                                        EraseResult& result) {
    result.success = true;
    if (!write_full_pass(0x000, total_size, 1, 1, "0x00", op.current_target, op, progress,
                         result)) {
        return result;
    }

    if (config.verify_after_erase) {
        if (!verify_pass(0x000, total_size, op, progress, result)) {
            result.success = false;
            result.error = EraseError::IoError;
            result.message = "Verification failed after quick zero-fill";
            return result;
        }
    }

    result.message = "Quick zero-fill completed";
    return result;
}

EraseResult EraseEngine::run_passes(const EraseConfig& config, std::uint64_t total_size,
                                    OperationContext& op, ProgressCallback progress,
                                    EraseResult& result) {
    result.success = true;

    PassScheduler scheduler;
    PatternGenerator patterns;

    std::vector<int> schedule =
        scheduler.build_schedule(config.pass_count, config.use_random_passes);

    for (const auto& custom : config.custom_patterns) {
        int encoded = static_cast<int>(custom.value & 0xfffU);
        if (custom.flip_first_bit_per_sector) {
            encoded |= 0x1000;
        }
        schedule.insert(schedule.begin(), encoded);
    }

    const std::size_t data_passes = schedule.size();
    const std::size_t total_passes = data_passes + (config.final_zero_pass ? 1 : 0);

    for (std::size_t i = 0; i < data_passes; ++i) {
        if (cancel_requested_.load()) {
            result.success = false;
            result.error = EraseError::Cancelled;
            result.message = "Operation cancelled by user";
            return result;
        }

        std::vector<std::uint8_t> sample(3);
        patterns.fill_buffer(schedule[i], sample, sample.size());
        const std::string label = patterns.pass_label(schedule[i], sample);

        if (!write_full_pass(schedule[i], total_size, i + 1, total_passes, label,
                             op.current_target, op, progress, result)) {
            return result;
        }
    }

    if (config.final_zero_pass) {
        if (cancel_requested_.load()) {
            result.success = false;
            result.error = EraseError::Cancelled;
            result.message = "Operation cancelled by user";
            return result;
        }
        if (!write_full_pass(0x000, total_size, total_passes, total_passes, "0x00 (final)",
                             op.current_target, op, progress, result)) {
            return result;
        }
    }

    if (config.verify_after_erase) {
        const int pattern = final_verification_pattern(config);
        if (pattern < 0) {
            result.warnings.push_back(
                "Verification skipped because the final pass used random data.");
            result.verification_passed = true;
        } else if (!verify_pass(pattern, total_size, op, progress, result)) {
            result.success = false;
            result.error = EraseError::IoError;
            result.message = "Verification failed after secure erase";
            return result;
        }
    }

    result.message = "Secure erase completed";
    if (logger_) {
        logger_->info(result.message);
    }
    return result;
}

bool EraseEngine::verify_pass(int pattern_type, std::uint64_t total_size, OperationContext& op,
                              ProgressCallback progress, EraseResult& result) {
    if (total_size == 0) {
        return true;
    }

    PatternGenerator patterns;
    constexpr std::size_t kSampleSize = 512;
    std::vector<std::uint8_t> expected(kSampleSize);
    std::vector<std::uint8_t> actual(kSampleSize);
    patterns.fill_buffer(pattern_type, expected, kSampleSize);

    std::vector<std::uint64_t> offsets;
    offsets.push_back(0);
    if (total_size > kSampleSize) {
        offsets.push_back(total_size / 2);
        offsets.push_back(total_size > kSampleSize ? total_size - kSampleSize : 0);
    }

    std::size_t samples_checked = 0;
    for (const std::uint64_t offset : offsets) {
        if (cancel_requested_.load()) {
            result.success = false;
            result.error = EraseError::Cancelled;
            result.message = "Operation cancelled by user";
            return false;
        }

        const std::size_t to_read =
            static_cast<std::size_t>(std::min<std::uint64_t>(kSampleSize, total_size - offset));

        std::string error;
        if (!device_->read_at(offset, actual.data(), to_read, error)) {
            result.warnings.push_back("Verification read failed at offset " +
                                      std::to_string(offset) + ": " + error);
            result.verification_passed = false;
            return false;
        }

        if (std::memcmp(actual.data(), expected.data(), to_read) != 0) {
            result.warnings.push_back("Verification mismatch at offset " + std::to_string(offset));
            result.verification_passed = false;
            return false;
        }

        ++samples_checked;

        if (progress) {
            EraseProgress prog;
            prog.current_pass = 1;
            prog.total_passes = 1;
            prog.pass_label = "verify";
            prog.current_target = op.current_target;
            prog.bytes_written = offset + to_read;
            prog.total_bytes = total_size;
            prog.percent_complete = 100.0;
            report_progress(prog, op, progress);
        }
    }

    result.verification_passed = true;
    result.verification_samples = samples_checked;
    if (logger_) {
        logger_->info("Verification passed (" + std::to_string(samples_checked) + " samples)");
    }
    return true;
}

bool EraseEngine::write_full_pass(int pattern_type, std::uint64_t total_size,
                                  std::size_t pass_index, std::size_t total_passes,
                                  const std::string& pass_label,
                                  const std::string& target_label, OperationContext& op,
                                  ProgressCallback progress, EraseResult& result) {
    PatternGenerator patterns;
    std::vector<std::uint8_t> buffer(EraseEngine::kBufferSize);
    patterns.fill_buffer(pattern_type, buffer, buffer.size());

    std::uint64_t offset = 0;
    const std::uint64_t pass_base = op.overall_bytes_done;

    while (offset < total_size) {
        if (cancel_requested_.load()) {
            result.success = false;
            result.error = EraseError::Cancelled;
            result.message = "Operation cancelled by user";
            return false;
        }

        const std::size_t chunk =
            static_cast<std::size_t>(std::min<std::uint64_t>(EraseEngine::kBufferSize,
                                                             total_size - offset));

        if (pattern_type < 0) {
            patterns.fill_random(buffer, chunk);
        }

        std::string error;
        if (!device_->write_at(offset, buffer.data(), chunk, error)) {
            result.success = false;
            result.error = EraseError::IoError;
            result.message = "Write failed at offset " + std::to_string(offset) + ": " + error;
            result.warnings.push_back(result.message);
            if (logger_) {
                logger_->warning(result.warnings.back());
            }
            return false;
        }

        offset += chunk;
        op.overall_bytes_done = pass_base + offset;

        if (progress) {
            EraseProgress prog;
            prog.current_pass = pass_index;
            prog.total_passes = total_passes;
            prog.pass_label = pass_label;
            prog.current_target = target_label;
            prog.bytes_written = offset;
            prog.total_bytes = total_size;
            prog.percent_complete =
                total_size == 0
                    ? 100.0
                    : (static_cast<double>(offset) * 100.0 / static_cast<double>(total_size));
            report_progress(prog, op, progress);
        }
    }

    std::string flush_error;
    if (!device_->flush(flush_error)) {
        result.success = false;
        result.error = EraseError::IoError;
        result.message = "Flush failed: " + flush_error;
        result.warnings.push_back(result.message);
        if (logger_) {
            logger_->warning(result.warnings.back());
        }
        return false;
    }

    return true;
}

}  // namespace datascythe
