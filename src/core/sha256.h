#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace datascythe {

class Sha256 {
public:
    void update(const void* data, std::size_t size);
    std::array<std::uint8_t, 32> final();
    std::string final_hex();

private:
    void transform(const std::uint8_t block[64]);

    std::array<std::uint8_t, 64> buffer_{};
    std::array<std::uint32_t, 8> state_{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::uint64_t bit_count_ = 0;
    std::size_t buffer_size_ = 0;
    bool finalized_ = false;
};

std::string sha256_hex(const void* data, std::size_t size);

}  
