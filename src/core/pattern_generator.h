#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace datascythe {



class PatternGenerator {
public:
    static constexpr std::size_t kSectorSize = 512;

    explicit PatternGenerator(std::uint64_t seed = std::random_device{}());

    
    
    void fill_buffer(int pattern_type, std::vector<std::uint8_t>& buffer, std::size_t size) const;

    
    std::string pass_label(int pattern_type, const std::vector<std::uint8_t>& sample) const;

    
    void fill_random(std::vector<std::uint8_t>& buffer, std::size_t size) const;

private:
    void fill_fixed_pattern(int type, std::uint8_t* data, std::size_t size) const;

    mutable std::mt19937_64 rng_;
};

}  