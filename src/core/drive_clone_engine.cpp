#include "core/drive_clone_engine.h"

#include "core/sha256.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <sstream>
#include <vector>

namespace datascythe {

namespace {

bool checked_multiply(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
    if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) {
        return false;
    }
    out = a * b;
    return true;
}

}  

DriveCloneEngine::DriveCloneEngine(std::unique_ptr<IRawDevice> source,
                                   std::unique_ptr<IRawDevice> target)
    : source_(std::move(source)), target_(std::move(target)) {}

void DriveCloneEngine::report_progress(const std::string& phase, std::size_t pass,
                                       std::size_t total_passes, std::uint64_t phase_done,
                                       std::uint64_t phase_total,
                                       std::uint64_t overall_done,
                                       std::uint64_t overall_total,
                                       ProgressCallback& progress) {
    if (!progress) {
        return;
    }

    EraseProgress prog;
    prog.current_pass = pass;
    prog.total_passes = total_passes;
    prog.pass_label = phase;
    prog.current_target = "drive clone";
    prog.bytes_written = phase_done;
    prog.total_bytes = phase_total;
    prog.percent_complete =
        phase_total == 0
            ? 100.0
            : static_cast<double>(phase_done) * 100.0 / static_cast<double>(phase_total);
    prog.overall_percent =
        overall_total == 0
            ? prog.percent_complete
            : static_cast<double>(overall_done) * 100.0 / static_cast<double>(overall_total);
    prog.eta_seconds = -1;

    if (!progress(prog)) {
        cancel_requested_.store(true);
    }
}

bool DriveCloneEngine::copy_bytes(std::uint64_t total_size, std::size_t total_passes,
                                  std::uint64_t overall_total, ProgressCallback progress,
                                  EraseResult& result) {
    std::vector<std::uint8_t> buffer(kBufferSize);
    std::uint64_t offset = 0;
    Sha256 source_hash;

    while (offset < total_size) {
        if (cancel_requested_.load()) {
            result.success = false;
            result.error = EraseError::Cancelled;
            result.message = "Drive clone cancelled by user";
            return false;
        }

        const auto chunk = static_cast<std::size_t>(
            std::min<std::uint64_t>(buffer.size(), total_size - offset));

        std::string error;
        if (!source_->read_at(offset, buffer.data(), chunk, error)) {
            result.success = false;
            result.error = EraseError::IoError;
            result.message = "Clone read failed at offset " + std::to_string(offset) + ": " + error;
            return false;
        }
        source_hash.update(buffer.data(), chunk);
        if (!target_->write_at(offset, buffer.data(), chunk, error)) {
            result.success = false;
            result.error = EraseError::IoError;
            result.message = "Clone write failed at offset " + std::to_string(offset) + ": " + error;
            return false;
        }

        offset += chunk;
        report_progress("clone", 1, total_passes, offset, total_size, offset, overall_total,
                        progress);
    }

    result.source_sha256 = source_hash.final_hex();

    std::string flush_error;
    if (!target_->flush(flush_error)) {
        result.success = false;
        result.error = EraseError::IoError;
        result.message = "Target flush failed after clone: " + flush_error;
        result.warnings.push_back(result.message);
        if (logger_) {
            logger_->warning(result.warnings.back());
        }
        return false;
    }

    return true;
}

bool DriveCloneEngine::verify_bytes(std::uint64_t total_size, ProgressCallback progress,
                                    EraseResult& result) {
    std::vector<std::uint8_t> source_buffer(kBufferSize);
    std::vector<std::uint8_t> target_buffer(kBufferSize);
    std::uint64_t offset = 0;
    std::uint64_t overall_total = 0;
    if (!checked_multiply(total_size, 2, overall_total)) {
        result.success = false;
        result.error = EraseError::InvalidTarget;
        result.message = "Clone size is too large to verify safely";
        return false;
    }
    Sha256 source_hash;
    Sha256 target_hash;

    while (offset < total_size) {
        if (cancel_requested_.load()) {
            result.success = false;
            result.error = EraseError::Cancelled;
            result.message = "Drive clone verification cancelled by user";
            return false;
        }

        const auto chunk = static_cast<std::size_t>(
            std::min<std::uint64_t>(source_buffer.size(), total_size - offset));

        std::string error;
        if (!source_->read_at(offset, source_buffer.data(), chunk, error)) {
            result.success = false;
            result.error = EraseError::IoError;
            result.message =
                "Verification source read failed at offset " + std::to_string(offset) + ": " + error;
            return false;
        }
        if (!target_->read_at(offset, target_buffer.data(), chunk, error)) {
            result.success = false;
            result.error = EraseError::IoError;
            result.message =
                "Verification target read failed at offset " + std::to_string(offset) + ": " + error;
            return false;
        }
        if (std::memcmp(source_buffer.data(), target_buffer.data(), chunk) != 0) {
            result.success = false;
            result.error = EraseError::IoError;
            result.verification_passed = false;
            result.message = "Verification mismatch at offset " + std::to_string(offset);
            return false;
        }
        source_hash.update(source_buffer.data(), chunk);
        target_hash.update(target_buffer.data(), chunk);

        offset += chunk;
        report_progress("verify", 2, 2, offset, total_size, total_size + offset,
                        overall_total, progress);
    }

    const std::string verified_source_hash = source_hash.final_hex();
    result.target_sha256 = target_hash.final_hex();
    if (!result.source_sha256.empty() && result.source_sha256 != verified_source_hash) {
        result.warnings.push_back(
            "Source SHA-256 changed between copy and verification; source may have been live.");
        if (logger_) {
            logger_->warning(result.warnings.back());
        }
    }
    result.verification_passed = true;
    result.verification_samples = static_cast<std::size_t>((total_size + kBufferSize - 1) / kBufferSize);
    return true;
}

EraseResult DriveCloneEngine::clone(const std::string& source_path,
                                    const std::string& target_path,
                                    const DriveCloneConfig& config,
                                    ProgressCallback progress) {
    cancel_requested_.store(false);
    EraseResult result;

    if (source_path == target_path) {
        result.success = false;
        result.error = EraseError::InvalidTarget;
        result.message = "Source and target drives are the same";
        return result;
    }

    std::string error;
    if (!source_->open(source_path, error)) {
        result.success = false;
        result.error = EraseError::AccessDenied;
        result.message = "Unable to open source: " + error;
        return result;
    }
    if (!target_->open(target_path, error)) {
        source_->close();
        result.success = false;
        result.error = EraseError::AccessDenied;
        result.message = "Unable to open target: " + error;
        return result;
    }

    const std::uint64_t source_size = source_->size_bytes(error);
    if (source_size == 0) {
        source_->close();
        target_->close();
        result.success = false;
        result.error = EraseError::InvalidTarget;
        result.message = error.empty() ? "Unable to determine source size" : error;
        return result;
    }

    const std::uint64_t target_size = target_->size_bytes(error);
    if (target_size < source_size) {
        source_->close();
        target_->close();
        result.success = false;
        result.error = EraseError::InvalidTarget;
        result.message = "Target is smaller than source";
        return result;
    }
    if (target_size > source_size) {
        result.warnings.push_back(
            "Target is larger than source; bytes beyond the source size are left unchanged.");
    }

    if (!target_->dismount_volumes(error)) {
        result.warnings.push_back("Target volume dismount warning: " + error);
    }

    if (logger_) {
        logger_->begin_session(source_path + " -> " + target_path,
                               config.verify_after_clone ? "Drive clone with verification"
                                                         : "Drive clone");
        logger_->info("Clone source bytes=" + std::to_string(source_size));
        logger_->info("Clone target bytes=" + std::to_string(target_size));
    }

    const std::size_t total_passes = config.verify_after_clone ? 2 : 1;
    std::uint64_t overall_total = 0;
    if (!checked_multiply(source_size, total_passes, overall_total)) {
        source_->close();
        target_->close();
        result.success = false;
        result.error = EraseError::InvalidTarget;
        result.message = "Clone size is too large to track safely";
        if (logger_) {
            logger_->error(result.message);
            logger_->end_session(false);
        }
        return result;
    }

    result.success = true;
    if (!copy_bytes(source_size, total_passes, overall_total, progress, result)) {
        source_->close();
        target_->close();
        if (logger_) {
            logger_->error(result.message);
            logger_->end_session(false);
        }
        return result;
    }

    if (config.verify_after_clone && !verify_bytes(source_size, progress, result)) {
        source_->close();
        target_->close();
        if (logger_) {
            logger_->error(result.message);
            logger_->end_session(false);
        }
        return result;
    }

    source_->close();
    target_->close();

    result.success = true;
    result.message = config.verify_after_clone
                         ? "Drive clone completed and verified byte-for-byte"
                         : "Drive clone completed";
    if (logger_) {
        logger_->info(result.message);
        logger_->end_session(true);
    }
    return result;
}

}  
