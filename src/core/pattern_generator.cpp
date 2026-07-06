#include "core/pattern_generator.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace datascythe {

PatternGenerator::PatternGenerator(std::uint64_t seed) : rng_(seed) {}

void PatternGenerator::fill_fixed_pattern(int type, std::uint8_t* data, std::size_t size) const {
    if (size < 3) {
        return;
    }

    unsigned int bits = static_cast<unsigned int>(type) & 0xfffU;
    bits |= bits << 12;
    data[0] = static_cast<std::uint8_t>((bits >> 4) & 255U);
    data[1] = static_cast<std::uint8_t>((bits >> 8) & 255U);
    data[2] = static_cast<std::uint8_t>(bits & 255U);

    std::size_t i = 3;
    for (; i < size / 2; i *= 2) {
        std::memcpy(data + i, data, i);
    }
    if (i < size) {
        std::memcpy(data + i, data, size - i);
    }

    if (type & 0x1000) {
        for (std::size_t i = 0; i < size; i += kSectorSize) {
            data[i] ^= 0x80U;
        }
    }
}

void PatternGenerator::fill_buffer(int pattern_type, std::vector<std::uint8_t>& buffer,
                                   std::size_t size) const {
    buffer.resize(size);
    if (pattern_type < 0) {
        fill_random(buffer, size);
        return;
    }
    fill_fixed_pattern(pattern_type, buffer.data(), size);
}

void PatternGenerator::fill_random(std::vector<std::uint8_t>& buffer, std::size_t size) const {
    buffer.resize(size);
    std::uniform_int_distribution<int> dist(0, 255);
    for (std::size_t i = 0; i < size; ++i) {
        buffer[i] = static_cast<std::uint8_t>(dist(rng_));
    }
}

std::string PatternGenerator::pass_label(int pattern_type,
                                         const std::vector<std::uint8_t>& sample) const {
    if (pattern_type < 0) {
        return "random";
    }
    if (sample.size() >= 3) {
        std::ostringstream oss;
        oss.setf(std::ios::hex, std::ios::basefield);
        oss.width(2);
        oss.fill('0');
        oss << static_cast<int>(sample[0]);
        oss.width(2);
        oss << static_cast<int>(sample[1]);
        oss.width(2);
        oss << static_cast<int>(sample[2]);
        return oss.str();
    }
    return "fixed";
}

}  // namespace datascythe