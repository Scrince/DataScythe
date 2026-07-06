#include "platform/secure_erase.h"
#include "platform/nvme_admin.h"

#if defined(DATASCYTHE_PLATFORM_WINDOWS)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace datascythe {

namespace {

#ifndef IOCTL_ATA_PASS_THROUGH
#define IOCTL_ATA_PASS_THROUGH 0x0004D02C
#endif

#ifndef IOCTL_STORAGE_PROTOCOL_COMMAND
#define IOCTL_STORAGE_PROTOCOL_COMMAND 0x002D1400
#endif

#ifndef ATA_FLAGS_DRDY_REQUIRED
#define ATA_FLAGS_DRDY_REQUIRED 0x10
#endif

#ifndef ATA_TASK_FILE_COMMAND
#define ATA_TASK_FILE_COMMAND 6
#define ATA_TASK_FILE_FEATURES 0
#define ATA_TASK_FILE_SECTOR_COUNT 2
#endif

#ifndef ProtocolTypeNvme
#define ProtocolTypeNvme 3
#endif

struct AtaPassThroughEx {
    std::uint16_t Length;
    std::uint16_t Reserved;
    std::uint32_t AtaFlags;
    std::uint8_t PathId;
    std::uint8_t TargetId;
    std::uint8_t Lun;
    std::uint8_t Reserved2;
    std::uint32_t DataTransferLength;
    std::uint32_t TimeOutValue;
    std::uint32_t Reserved3;
    std::uintptr_t DataBufferOffset;
    std::uint8_t PreviousTaskFile[8];
    std::uint8_t CurrentTaskFile[8];
};

#pragma pack(push, 1)
struct StorageProtocolCommand {
    std::uint32_t Version;
    std::uint32_t Length;
    std::uint32_t ProtocolType;
    std::uint32_t Flags;
    std::uint32_t ReturnStatus;
    std::uint32_t ErrorCode;
    std::uint32_t CommandLength;
    std::uint32_t ErrorInfoLength;
    std::uint32_t DataToDeviceTransferLength;
    std::uint32_t DataFromDeviceTransferLength;
    std::uint32_t TimeOutValue;
    std::uint32_t ErrorInfoOffset;
    std::uint32_t DataToDeviceBufferOffset;
    std::uint32_t DataFromDeviceBufferOffset;
    std::uint32_t CommandSpecific;
    std::uint32_t Reserved0;
    std::uint32_t FixedProtocolReturnData;
    std::uint32_t SenseInfoLength;
    std::uint32_t BufferLength;
};

struct NvmeCommand {
    std::uint32_t CDW0;
    std::uint32_t NSID;
    std::uint32_t Reserved[2];
    std::uint64_t MPTR;
    std::uint64_t PRP1;
    std::uint64_t PRP2;
    std::uint32_t CDW10;
    std::uint32_t CDW11;
    std::uint32_t CDW12;
    std::uint32_t CDW13;
    std::uint32_t CDW14;
    std::uint32_t CDW15;
};
#pragma pack(pop)

enum DriveBusType { BusUnknown, BusAta, BusNvme };

bool open_drive(const std::string& path, HANDLE& handle_out, std::string& error_out) {
    std::string normalized = path;
    if (normalized.rfind(R"(\\.\)", 0) != 0) {
        normalized = R"(\\.\)" + normalized;
    }

    handle_out = CreateFileA(normalized.c_str(), GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                             nullptr);
    if (handle_out == INVALID_HANDLE_VALUE) {
        error_out = "Unable to open drive for secure erase (error " +
                    std::to_string(GetLastError()) + ")";
        return false;
    }
    return true;
}

DriveBusType detect_bus_type(HANDLE handle) {
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;

    std::vector<std::uint8_t> buffer(sizeof(STORAGE_DEVICE_DESCRIPTOR) + 256);
    DWORD returned = 0;
    if (!DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                         buffer.data(), static_cast<DWORD>(buffer.size()), &returned, nullptr)) {
        return BusUnknown;
    }

    const auto* desc = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
    if (desc->BusType == BusTypeNvme) {
        return BusNvme;
    }
    if (desc->BusType == BusTypeAta || desc->BusType == BusTypeSata || desc->BusType == BusTypeScsi) {
        return BusAta;
    }
    return BusUnknown;
}

bool ata_security_erase(HANDLE handle, std::string& error_out) {
    auto send_command = [&](std::uint8_t command, std::uint16_t value) -> bool {
        AtaPassThroughEx apt{};
        std::memset(&apt, 0, sizeof(apt));
        apt.Length = static_cast<std::uint16_t>(sizeof(apt));
        apt.AtaFlags = ATA_FLAGS_DRDY_REQUIRED;
        apt.DataTransferLength = 0;
        apt.TimeOutValue = 120;
        apt.CurrentTaskFile[ATA_TASK_FILE_SECTOR_COUNT] = 1;
        apt.CurrentTaskFile[ATA_TASK_FILE_COMMAND] = command;
        apt.CurrentTaskFile[ATA_TASK_FILE_FEATURES] = static_cast<std::uint8_t>(value & 0xFF);
        apt.CurrentTaskFile[ATA_TASK_FILE_SECTOR_COUNT + 1] =
            static_cast<std::uint8_t>((value >> 8) & 0xFF);

        DWORD bytes = 0;
        if (!DeviceIoControl(handle, IOCTL_ATA_PASS_THROUGH, &apt, sizeof(apt), &apt, sizeof(apt),
                             &bytes, nullptr)) {
            error_out = "ATA pass-through failed (error " + std::to_string(GetLastError()) + ")";
            return false;
        }
        return true;
    };

    if (!send_command(0xF3, 0x0022)) {
        return false;
    }
    return send_command(0xF4, 0x0022);
}

bool nvme_send_admin(HANDLE handle, const nvme::PassthroughCommand& pt, std::size_t data_from_length,
                     std::size_t data_from_offset, std::vector<std::uint8_t>& buffer,
                     std::string& error_out) {
    constexpr std::size_t kTailReserve = 512;
    const std::size_t kBufferSize =
        sizeof(StorageProtocolCommand) + sizeof(NvmeCommand) + kTailReserve;
    buffer.assign(kBufferSize, 0);
    auto* spc = reinterpret_cast<StorageProtocolCommand*>(buffer.data());
    auto* nvme = reinterpret_cast<NvmeCommand*>(buffer.data() + sizeof(StorageProtocolCommand));

    spc->Version = 1;
    spc->Length = static_cast<std::uint32_t>(sizeof(StorageProtocolCommand));
    spc->ProtocolType = ProtocolTypeNvme;
    spc->CommandLength = sizeof(NvmeCommand);
    spc->TimeOutValue = std::max(1U, pt.timeout_ms / 1000U);
    spc->DataToDeviceTransferLength = 0;
    spc->DataFromDeviceTransferLength = static_cast<std::uint32_t>(data_from_length);
    spc->DataFromDeviceBufferOffset = static_cast<std::uint32_t>(data_from_offset);
    spc->BufferLength = static_cast<std::uint32_t>(kBufferSize);

    nvme->CDW0 = pt.opcode;
    nvme->NSID = pt.nsid;
    nvme->CDW10 = pt.cdw10;
    nvme->CDW11 = pt.cdw11;

    DWORD bytes = 0;
    if (!DeviceIoControl(handle, IOCTL_STORAGE_PROTOCOL_COMMAND, buffer.data(),
                         static_cast<DWORD>(buffer.size()), buffer.data(),
                         static_cast<DWORD>(buffer.size()), &bytes, nullptr)) {
        error_out = "NVMe admin IOCTL failed (error " + std::to_string(GetLastError()) + ")";
        return false;
    }

    if (spc->ReturnStatus != 0) {
        error_out = "NVMe admin returned status " + std::to_string(spc->ReturnStatus) + " error " +
                    std::to_string(spc->ErrorCode);
        return false;
    }

    return true;
}

bool nvme_read_sanitize_status(HANDLE handle, nvme::SanitizeStatusLog& log_out,
                               std::string& error_out) {
    constexpr std::size_t kLogSize = 8;
    constexpr std::size_t kDataOffset = sizeof(StorageProtocolCommand) + sizeof(NvmeCommand);

    std::vector<std::uint8_t> buffer;
    auto built = nvme::make_sanitize_status_log_command(0, kLogSize);
    built.cdw10 = nvme::kLogIdSanitizeStatus | ((kLogSize / 4 - 1) << 16);

    if (!nvme_send_admin(handle, built, kLogSize, kDataOffset, buffer, error_out)) {
        return false;
    }

    const auto* raw = buffer.data() + kDataOffset;
    log_out.progress = static_cast<std::uint16_t>(raw[0] | (raw[1] << 8));
    log_out.status = raw[2];
    return true;
}

bool nvme_sanitize(HANDLE handle, nvme::SanitizeAction action, std::string& error_out) {
    const auto pt = nvme::make_sanitize_command(action);
    std::vector<std::uint8_t> buffer;
    return nvme_send_admin(handle, pt, 0, 0, buffer, error_out);
}

bool nvme_sanitize_and_wait(HANDLE handle, nvme::SanitizeAction action,
                            SecureEraseProgressCallback progress, std::string& error_out) {
    if (!nvme_sanitize(handle, action, error_out)) {
        return false;
    }

    const auto poll = [&](nvme::SanitizeStatusLog& log, std::string& err) -> bool {
        return nvme_read_sanitize_status(handle, log, err);
    };

    const auto state = nvme::wait_for_sanitize_completion(
        poll, progress, std::chrono::minutes(60), error_out);

    return state == nvme::SanitizePollState::Complete;
}

class WindowsSecureErase final : public ISecureErase {
public:
    bool is_supported(const std::string& device_path, std::string& reason_out) override {
        HANDLE handle = INVALID_HANDLE_VALUE;
        if (!open_drive(device_path, handle, reason_out)) {
            return false;
        }

        const DriveBusType bus = detect_bus_type(handle);
        CloseHandle(handle);

        switch (bus) {
            case BusNvme:
                reason_out = "NVMe Sanitize (block erase) via STORAGE_PROTOCOL_COMMAND.";
                return true;
            case BusAta:
                reason_out = "ATA SECURITY ERASE available (drive must not be frozen/locked).";
                return true;
            default:
                reason_out = "Hardware secure erase not available for this bus type.";
                return false;
        }
    }

    EraseResult execute(const std::string& device_path,
                        SecureEraseProgressCallback progress) override {
        EraseResult result;
        HANDLE handle = INVALID_HANDLE_VALUE;
        std::string error;

        if (!open_drive(device_path, handle, error)) {
            result.message = error;
            result.error = EraseError::AccessDenied;
            return result;
        }

        const DriveBusType bus = detect_bus_type(handle);
        bool ok = false;

        if (bus == BusNvme) {
            ok = nvme_sanitize_and_wait(handle, nvme::SanitizeAction::BlockErase, progress, error);
            if (!ok) {
                ok = nvme_sanitize_and_wait(handle, nvme::SanitizeAction::CryptoErase, progress,
                                            error);
            }
            if (ok) {
                result.success = true;
                result.message = "NVMe Sanitize completed on " + device_path;
            }
        } else if (bus == BusAta) {
            ok = ata_security_erase(handle, error);
            if (ok) {
                result.success = true;
                result.message = "ATA SECURITY ERASE command issued to " + device_path;
                result.warnings.push_back(
                    "Some drives require a power cycle after ATA secure erase.");
            }
        } else {
            error = "Unsupported drive bus type for hardware secure erase.";
        }

        CloseHandle(handle);

        if (!ok && !result.success) {
            result.message = error;
            result.error = EraseError::IoError;
            result.warnings.push_back(
                "Ensure the drive is not frozen, is disconnected from RAID, and is running as "
                "Administrator.");
        }

        return result;
    }
};

}  // namespace

std::unique_ptr<ISecureErase> create_secure_erase() {
    return std::make_unique<WindowsSecureErase>();
}

}  // namespace datascythe

#endif