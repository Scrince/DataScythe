#include "platform/secure_erase.h"
#include "platform/nvme_admin.h"

#if defined(DATASCYTHE_PLATFORM_LINUX)

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

#include <linux/nvme_ioctl.h>

// HDIO_DRIVE_CMD ioctl for ATA security erase.
#ifndef HDIO_DRIVE_CMD
#define HDIO_DRIVE_CMD 0x031f
#endif

namespace datascythe {

namespace {

enum DriveBusType { BusUnknown, BusAta, BusNvme };

bool is_nvme_device(const std::string& path) {
    return path.find("nvme") != std::string::npos;
}

bool open_read_write(const std::string& path, int& fd_out, std::string& error_out) {
    fd_out = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_out < 0) {
        error_out = std::string("open failed: ") + std::strerror(errno);
        return false;
    }
    return true;
}

DriveBusType detect_bus(const std::string& path) {
    if (is_nvme_device(path)) {
        return BusNvme;
    }
    if (path.find("/dev/sd") == 0 || path.find("/dev/hd") == 0) {
        return BusAta;
    }
    return BusUnknown;
}

bool ata_security_erase(int fd, std::string& error_out) {
    // HDIO_DRIVE_CMD buffer: 4-byte header + up to 512 bytes payload (see hdparm).
    unsigned char prepare[4 + 512]{};
    prepare[0] = 0xF3;  // ATA_OP_SECURITY_ERASE_PREPARE

    if (ioctl(fd, HDIO_DRIVE_CMD, prepare) < 0) {
        error_out = std::string("ATA SECURITY ERASE PREPARE failed: ") + std::strerror(errno);
        return false;
    }

    unsigned char erase[4 + 512]{};
    erase[0] = 0xF4;  // ATA_OP_SECURITY_ERASE_UNIT
    erase[1] = 0x11;  // enhanced erase (0x10 = normal)

    if (ioctl(fd, HDIO_DRIVE_CMD, erase) < 0) {
        error_out = std::string("ATA SECURITY ERASE UNIT failed: ") + std::strerror(errno);
        return false;
    }

    return true;
}

bool nvme_admin_passthrough(int fd, const nvme::PassthroughCommand& built,
                            std::vector<std::uint8_t>& data, std::string& error_out) {
    nvme_passthru_cmd cmd{};
    cmd.opcode = built.opcode;
    cmd.nsid = built.nsid;
    cmd.cdw10 = built.cdw10;
    cmd.cdw11 = built.cdw11;
    cmd.timeout_ms = built.timeout_ms;
    if (!data.empty()) {
        cmd.addr = reinterpret_cast<std::uint64_t>(data.data());
        cmd.data_len = static_cast<std::uint32_t>(data.size());
    }

    if (ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd) < 0) {
        error_out = std::string("NVMe admin ioctl failed: ") + std::strerror(errno);
        return false;
    }
    if (cmd.result != 0) {
        error_out = "NVMe admin returned result " + std::to_string(cmd.result);
        return false;
    }
    return true;
}

bool nvme_read_sanitize_status(int fd, nvme::SanitizeStatusLog& log_out, std::string& error_out) {
    std::vector<std::uint8_t> data(8, 0);
    auto built = nvme::make_sanitize_status_log_command(0, static_cast<std::uint32_t>(data.size()));
    built.cdw10 = nvme::kLogIdSanitizeStatus | ((static_cast<std::uint32_t>(data.size()) / 4 - 1) << 16);
    if (!nvme_admin_passthrough(fd, built, data, error_out)) {
        return false;
    }
    log_out.progress = static_cast<std::uint16_t>(data[0] | (data[1] << 8));
    log_out.status = data[2];
    return true;
}

bool nvme_sanitize_and_wait(int fd, nvme::SanitizeAction action, SecureEraseProgressCallback progress,
                            std::string& error_out) {
    nvme_passthru_cmd cmd{};
    const auto built = nvme::make_sanitize_command(action);
    cmd.opcode = built.opcode;
    cmd.nsid = built.nsid;
    cmd.cdw10 = built.cdw10;
    cmd.timeout_ms = built.timeout_ms;
    if (ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd) < 0) {
        error_out = std::string("NVMe sanitize ioctl failed: ") + std::strerror(errno);
        return false;
    }

    const auto poll = [&](nvme::SanitizeStatusLog& log, std::string& err) -> bool {
        return nvme_read_sanitize_status(fd, log, err);
    };
    return nvme::wait_for_sanitize_completion(poll, progress, std::chrono::minutes(60),
                                              error_out) == nvme::SanitizePollState::Complete;
}

bool nvme_sanitize(int fd, nvme::SanitizeAction action, std::string& error_out) {
    nvme_passthru_cmd cmd{};
    std::memset(&cmd, 0, sizeof(cmd));

    const auto built = nvme::make_sanitize_command(action);
    cmd.opcode = built.opcode;
    cmd.nsid = built.nsid;
    cmd.cdw10 = built.cdw10;
    cmd.timeout_ms = built.timeout_ms;

    if (ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd) < 0) {
        error_out = std::string("NVMe sanitize ioctl failed: ") + std::strerror(errno);
        return false;
    }

    if (cmd.result != 0) {
        error_out = "NVMe sanitize returned non-zero result " + std::to_string(cmd.result);
        return false;
    }

    return true;
}

class LinuxSecureErase final : public ISecureErase {
public:
    bool is_supported(const std::string& device_path, std::string& reason_out) override {
        const DriveBusType bus = detect_bus(device_path);
        switch (bus) {
            case BusNvme:
                reason_out = "NVMe Sanitize via NVME_IOCTL_ADMIN_CMD (requires root).";
                return true;
            case BusAta:
                reason_out = "ATA SECURITY ERASE via HDIO_DRIVE_CMD (requires root).";
                return true;
            default:
                reason_out = "Unknown or unsupported block device type: " + device_path;
                return false;
        }
    }

    EraseResult execute(const std::string& device_path,
                        SecureEraseProgressCallback progress) override {
        EraseResult result;
        int fd = -1;
        std::string error;

        if (!open_read_write(device_path, fd, error)) {
            result.message = error;
            result.error = EraseError::AccessDenied;
            return result;
        }

        const DriveBusType bus = detect_bus(device_path);
        bool ok = false;

        if (bus == BusNvme) {
            ok = nvme_sanitize_and_wait(fd, nvme::SanitizeAction::BlockErase, progress, error);
            if (!ok) {
                ok = nvme_sanitize_and_wait(fd, nvme::SanitizeAction::CryptoErase, progress,
                                            error);
            }
            if (ok) {
                result.success = true;
                result.message = "NVMe Sanitize completed on " + device_path;
            }
        } else if (bus == BusAta) {
            ok = ata_security_erase(fd, error);
            if (ok) {
                result.success = true;
                result.message = "ATA SECURITY ERASE issued to " + device_path;
            }
        } else {
            error = "Unsupported device for hardware secure erase.";
        }

        ::close(fd);

        if (!ok && !result.success) {
            result.message = error;
            result.error = EraseError::IoError;
            result.warnings.push_back("Run as root and ensure the device is not mounted.");
        }

        return result;
    }
};

}  // namespace

std::unique_ptr<ISecureErase> create_secure_erase() {
    return std::make_unique<LinuxSecureErase>();
}

}  // namespace datascythe

#endif