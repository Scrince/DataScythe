#pragma once

#include "core/erase_result.h"
#include "platform/raw_device.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace datascythe {

/// Byte ranges covering MBR/GPT primary and backup metadata on a block device.
struct PartitionMetadataRegions {
    std::uint64_t front_size = 0;
    std::uint64_t back_offset = 0;
    std::uint64_t back_size = 0;
};

/// First megabyte (MBR + GPT primary) and last megabyte (backup GPT) when large enough.
constexpr std::uint64_t kPartitionMetadataWipeBytes = 1024 * 1024;

PartitionMetadataRegions partition_metadata_regions(std::uint64_t device_size);

/// Overwrite partition metadata regions with zeros before a full-device wipe.
bool wipe_partition_metadata(IRawDevice& device, std::uint64_t device_size,
                             std::atomic<bool>& cancel_requested, std::string& error_out,
                             ProgressCallback progress = nullptr);

}  // namespace datascythe