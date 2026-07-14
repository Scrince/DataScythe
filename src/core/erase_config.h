#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace datascythe {


enum class EraseMode {
    
    ShredVolume,
    
    FullDeviceWipe,
    
    QuickZeroFill,
    
    ShredFiles,
    
    ShredDirectory,
    
    SsdSecureErase,
};

enum class VerificationMode {
    Sparse,
    Full,
    Percent,
};


struct FixedPattern {
    std::uint32_t value = 0;
    bool flip_first_bit_per_sector = false;
};


struct EraseConfig {
    EraseMode mode = EraseMode::FullDeviceWipe;

    
    std::size_t pass_count = 3;

    
    bool use_random_passes = true;

    
    bool final_zero_pass = true;

    
    std::vector<FixedPattern> custom_patterns;

    
    bool round_file_size_to_block = true;

    
    bool remove_after_shred = true;

    
    bool recursive = true;

    
    bool verify_after_erase = false;

    VerificationMode verification_mode = VerificationMode::Sparse;

    double verification_percent = 10.0;

    
    bool shred_alternate_data_streams = true;

    
    bool wipe_partition_metadata = true;
};

}  
