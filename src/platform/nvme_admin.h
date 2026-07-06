#pragma once

#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <thread>

namespace datascythe::nvme {

/// NVMe Admin command opcodes.
inline constexpr std::uint8_t kOpcodeGetLogPage = 0x02;
inline constexpr std::uint8_t kOpcodeSanitize = 0x84;

/// Sanitize Status log identifier.
inline constexpr std::uint8_t kLogIdSanitizeStatus = 0x81;

/// Sanitize action (CDW10 bits 2:0).
enum class SanitizeAction : std::uint32_t {
    ExitFailureMode = 1,
    BlockErase = 2,
    Overwrite = 3,
    CryptoErase = 4,
};

enum class SanitizePollState {
    InProgress,
    Complete,
    Failed,
    Timeout,
};

struct SanitizeStatusLog {
    std::uint16_t progress = 0;  // 0xFFFF = complete
    std::uint8_t status = 0;
};

#pragma pack(push, 1)

struct PassthroughCommand {
    std::uint8_t opcode = 0;
    std::uint8_t flags = 0;
    std::uint16_t rsvd1 = 0;
    std::uint32_t nsid = 0;
    std::uint32_t cdw2 = 0;
    std::uint32_t cdw3 = 0;
    std::uint64_t metadata = 0;
    std::uint64_t addr = 0;
    std::uint32_t metadata_len = 0;
    std::uint32_t data_len = 0;
    std::uint32_t cdw10 = 0;
    std::uint32_t cdw11 = 0;
    std::uint32_t cdw12 = 0;
    std::uint32_t cdw13 = 0;
    std::uint32_t cdw14 = 0;
    std::uint32_t cdw15 = 0;
    std::uint32_t timeout_ms = 0;
    std::uint32_t result = 0;
};

#pragma pack(pop)

inline std::uint32_t sanitize_cdw10(SanitizeAction action) {
    return static_cast<std::uint32_t>(action) & 0x7U;
}

inline PassthroughCommand make_sanitize_command(SanitizeAction action) {
    PassthroughCommand cmd{};
    std::memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = kOpcodeSanitize;
    cmd.cdw10 = sanitize_cdw10(action);
    cmd.timeout_ms = 120000;
    return cmd;
}

inline PassthroughCommand make_sanitize_status_log_command(std::uint64_t buffer_addr,
                                                           std::uint32_t buffer_len) {
    PassthroughCommand cmd{};
    std::memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = kOpcodeGetLogPage;
    cmd.nsid = 0xFFFFFFFFU;
    cmd.addr = buffer_addr;
    cmd.data_len = buffer_len;
    // LID = 0x81, NUMDL = (len/4)-1 in bits 31:16
    const std::uint32_t numd = (buffer_len / sizeof(std::uint32_t)) - 1U;
    cmd.cdw10 = kLogIdSanitizeStatus | (numd << 16);
    cmd.timeout_ms = 5000;
    return cmd;
}

inline int sanitize_progress_percent(std::uint16_t sprog) {
    if (sprog == 0xFFFF) {
        return 100;
    }
    // SPROG is 16-bit value where 0xFFFF indicates completion.
    return static_cast<int>((static_cast<std::uint32_t>(sprog) * 100U) / 0xFFFEU);
}

inline SanitizePollState interpret_status_log(const SanitizeStatusLog& log) {
    if (log.progress == 0xFFFF) {
        return SanitizePollState::Complete;
    }
    // SSTAT bit 0: sanitize completed; bit 1: global data erased
    if ((log.status & 0x06) != 0 && log.progress == 0) {
        return SanitizePollState::Failed;
    }
    return SanitizePollState::InProgress;
}

using SanitizePollCallback = std::function<bool(int percent, const std::string& status)>;

/// Generic polling loop; platform code calls poll_once repeatedly.
inline SanitizePollState wait_for_sanitize_completion(
    const std::function<bool(SanitizeStatusLog&, std::string&)>& poll_once,
    SanitizePollCallback progress, std::chrono::seconds timeout, std::string& error_out) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        SanitizeStatusLog log{};
        if (!poll_once(log, error_out)) {
            return SanitizePollState::Failed;
        }

        const auto state = interpret_status_log(log);
        const int percent = sanitize_progress_percent(log.progress);

        if (progress) {
            std::string status = "NVMe sanitize in progress";
            if (state == SanitizePollState::Complete) {
                status = "NVMe sanitize complete";
            }
            if (!progress(percent, status)) {
                error_out = "Cancelled while waiting for NVMe sanitize";
                return SanitizePollState::Failed;
            }
        }

        if (state == SanitizePollState::Complete) {
            return SanitizePollState::Complete;
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    error_out = "Timed out waiting for NVMe sanitize to complete";
    return SanitizePollState::Timeout;
}

}  // namespace datascythe::nvme