#include "core/verification.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace datascythe {

namespace {

constexpr std::size_t kSampleSize = 512;

bool verify_zero_samples_impl(IRawDevice& device, std::uint64_t total_size, EraseResult& result,
                              const std::function<bool()>& should_cancel) {
    if (total_size == 0) {
        result.verification_passed = true;
        return true;
    }

    std::vector<std::uint8_t> actual(kSampleSize);
    std::vector<std::uint64_t> offsets;
    offsets.push_back(0);
    if (total_size > kSampleSize) {
        offsets.push_back(total_size / 2);
        offsets.push_back(total_size - kSampleSize);
    }

    std::size_t samples_checked = 0;
    for (const std::uint64_t offset : offsets) {
        if (should_cancel && should_cancel()) {
            result.verification_passed = false;
            return false;
        }

        const std::size_t to_read =
            static_cast<std::size_t>(std::min<std::uint64_t>(kSampleSize, total_size - offset));

        std::string error;
        if (!device.read_at(offset, actual.data(), to_read, error)) {
            result.warnings.push_back("Verification read failed at offset " +
                                      std::to_string(offset) + ": " + error);
            result.verification_passed = false;
            return false;
        }

        for (std::size_t i = 0; i < to_read; ++i) {
            if (actual[i] != 0) {
                result.warnings.push_back("Non-zero byte at offset " +
                                          std::to_string(offset + i));
                result.verification_passed = false;
                return false;
            }
        }

        ++samples_checked;
    }

    result.verification_passed = true;
    result.verification_samples = samples_checked;
    return true;
}

}  // namespace

bool verify_zero_samples(IRawDevice& device, std::uint64_t total_size, EraseResult& result,
                         const std::function<bool()>& should_cancel) {
    return verify_zero_samples_impl(device, total_size, result, should_cancel);
}

bool verify_target_zeroed(const std::string& path, EraseResult& result) {
    auto device = create_raw_device();
    std::string error;
    if (!device->open(path, error)) {
        result.warnings.push_back("Verification open failed: " + error);
        result.verification_passed = false;
        return false;
    }

    const std::uint64_t size = device->size_bytes(error);
    if (size == 0) {
        device->close();
        result.warnings.push_back("Verification failed: unable to determine device size");
        result.verification_passed = false;
        return false;
    }

    const bool ok = verify_zero_samples_impl(*device, size, result, nullptr);
    device->close();
    return ok;
}

}  // namespace datascythe