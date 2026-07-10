#include "core/partition_table.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace datascythe {

PartitionMetadataRegions partition_metadata_regions(std::uint64_t device_size) {
    PartitionMetadataRegions regions;
    if (device_size == 0) {
        return regions;
    }

    regions.front_size = std::min(device_size, kPartitionMetadataWipeBytes);

    if (device_size > kPartitionMetadataWipeBytes * 2) {
        regions.back_size = kPartitionMetadataWipeBytes;
        regions.back_offset = device_size - regions.back_size;
    }

    return regions;
}

bool wipe_partition_metadata(IRawDevice& device, std::uint64_t device_size,
                             std::atomic<bool>& cancel_requested, std::string& error_out,
                             ProgressCallback progress) {
    const auto regions = partition_metadata_regions(device_size);
    if (regions.front_size == 0) {
        error_out = "Device size is zero";
        return false;
    }

    std::vector<std::uint8_t> zeros(64 * 1024, 0);
    const auto wipe_region = [&](std::uint64_t offset, std::uint64_t size,
                                 const char* label) -> bool {
        std::uint64_t written = 0;
        while (written < size) {
            if (cancel_requested.load()) {
                error_out = "Operation cancelled by user";
                return false;
            }

            const std::size_t chunk = static_cast<std::size_t>(
                std::min<std::uint64_t>(zeros.size(), size - written));

            if (!device.write_at(offset + written, zeros.data(), chunk, error_out)) {
                return false;
            }

            written += chunk;

            if (progress) {
                EraseProgress prog;
                prog.current_pass = 1;
                prog.total_passes = 1;
                prog.pass_label = label;
                prog.bytes_written = written;
                prog.total_bytes = size;
                prog.percent_complete =
                    static_cast<double>(written) * 100.0 / static_cast<double>(size);
                prog.overall_percent = prog.percent_complete;
                if (!progress(prog)) {
                    cancel_requested.store(true);
                    error_out = "Operation cancelled by user";
                    return false;
                }
            }
        }

        return true;
    };

    if (!wipe_region(0, regions.front_size, "partition metadata (start)")) {
        return false;
    }

    if (regions.back_size > 0) {
        if (!wipe_region(regions.back_offset, regions.back_size,
                         "partition metadata (backup)")) {
            return false;
        }
    }

    std::string flush_error;
    if (!device.flush(flush_error)) {
        error_out = flush_error;
        return false;
    }

    return true;
}

}  