#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace datascythe {


class PatternGenerator {
public:
    static constexpr std::size_t kSectorSize = 512;

    /// Seed is unused for random fills (OS CSPRNG). Kept for API compatibility.
    explicit PatternGenerator(std::uint64_t seed = 0);

    void fill_buffer(int pattern_type, std::vector<std::uint8_t>& buffer, std::size_t size) const;

    std::string pass_label(int pattern_type, const std::vector<std::uint8_t>& sample) const;

    /// Fill buffer from OS CSPRNG (/dev/urandom, getentropy, or BCryptGenRandom).
    void fill_random(std::vector<std::uint8_t>& buffer, std::size_t size) const;

private:
    void fill_fixed_pattern(int type, std::uint8_t* data, std::size_t size) const;
};

}  
