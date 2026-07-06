#pragma once

#include "core/erase_result.h"

#include <functional>
#include <memory>
#include <string>

namespace datascythe {

using SecureEraseProgressCallback = std::function<bool(int percent, const std::string& status)>;

/// Hardware-assisted secure erase (ATA SECURITY ERASE / NVMe Sanitize).
/// May succeed only when the device is not frozen and the OS grants privileged access.
class ISecureErase {
public:
    virtual ~ISecureErase() = default;

    virtual bool is_supported(const std::string& device_path, std::string& reason_out) = 0;
    virtual EraseResult execute(const std::string& device_path,
                                SecureEraseProgressCallback progress = nullptr) = 0;
};

std::unique_ptr<ISecureErase> create_secure_erase();

}  // namespace datascythe