#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace datascythe {

/// How the erase engine should operate on a target.
enum class EraseMode {
    /// Shred-style overwrite of a file or mounted volume path.
    ShredVolume,
    /// Overwrite every OS-addressable byte on a physical device.
    FullDeviceWipe,
    /// Single zero pass over the entire device (fast sanitization).
    QuickZeroFill,
    /// Shred one or more user-selected files.
    ShredFiles,
    /// Shred all files in a directory (optionally recursive).
    ShredDirectory,
    /// Hardware-assisted SSD secure erase (ATA / NVMe).
    SsdSecureErase,
};

/// Fixed overwrite pattern for a single pass.
struct FixedPattern {
    std::uint32_t value = 0;
    bool flip_first_bit_per_sector = false;
};

/// User-configurable erase parameters.
struct EraseConfig {
    EraseMode mode = EraseMode::FullDeviceWipe;

    /// Number of overwrite passes (ignored for QuickZeroFill).
    std::size_t pass_count = 3;

    /// Mix random passes into the schedule (GNU shred-style).
    bool use_random_passes = true;

    /// Append a final all-zero pass to obscure shredding activity.
    bool final_zero_pass = true;

    /// Optional user-defined fixed patterns applied before the shred schedule.
    std::vector<FixedPattern> custom_patterns;

    /// When true, round regular-file sizes up to the filesystem block size.
    bool round_file_size_to_block = true;

    /// Delete files after shredding (GNU shred --remove semantics).
    bool remove_after_shred = true;

    /// When shredding a directory, include subdirectories recursively.
    bool recursive = true;

    /// Read back sample sectors after the final pass to confirm overwrite.
    bool verify_after_erase = false;

    /// Shred NTFS alternate data streams alongside files (Windows).
    bool shred_alternate_data_streams = true;

    /// Overwrite MBR/GPT metadata at the start and backup GPT at the end of block devices.
    bool wipe_partition_metadata = true;
};

}  // namespace datascythe