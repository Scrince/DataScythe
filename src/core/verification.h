#pragma once

#include "core/erase_result.h"
#include "platform/raw_device.h"

#include <cstdint>
#include <functional>
#include <string>

namespace datascythe {


bool verify_zero_samples(IRawDevice& device, std::uint64_t total_size, EraseResult& result,
                         const std::function<bool()>& should_cancel = nullptr);


bool verify_target_zeroed(const std::string& path, EraseResult& result);

}  