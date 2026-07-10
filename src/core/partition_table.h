#pragma once

#include "core/erase_result.h"
#include "platform/raw_device.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace datascythe {


struct PartitionMetadataRegions {
    std::uint64_t front_size = 0;
    std::uint64_t back_offset = 0;
    std::uint64_t back_size = 0;
};


constexpr std::uint64_t kPartitionMetadataWipeBytes = 1024 * 1024;

PartitionMetadataRegions partition_metadata_regions(std::uint64_t device_size);


bool wipe_partition_metadata(IRawDevice& device, std::uint64_t device_size,
                             std::atomic<bool>& cancel_requested, std::string& error_out,
                             ProgressCallback progress = nullptr);

}  