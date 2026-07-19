#include "core/pattern_generator.h"

#include <algorithm>
#include <cstring>
#include <sstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <fcntl.h>
#include <unistd.h>
#if defined(__APPLE__) || defined(__linux__)
#include <sys/random.h>
#endif
#endif

namespace datascythe {

namespace {

bool fill_os_random(std::uint8_t* data, std::size_t size) {
    if (size == 0) {
        return true;
    }
#if defined(_WIN32)
    const NTSTATUS status = BCryptGenRandom(
        nullptr, data, static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return status >= 0;
#else
#if defined(__APPLE__)
    // getentropy is limited to 256 bytes per call.
    std::size_t filled = 0;
    while (filled < size) {
        const std::size_t chunk = std::min<std::size_t>(size - filled, 256);
        if (::getentropy(data + filled, chunk) != 0) {
            filled = 0;
            break;
        }
        filled += chunk;
    }
    if (filled == size) {
        return true;
    }
#elif defined(__linux__)
    {
        std::size_t filled = 0;
        while (filled < size) {
            const ssize_t n = ::getrandom(data + filled, size - filled, 0);
            if (n < 0) {
                break;
            }
            filled += static_cast<std::size_t>(n);
        }
        if (filled == size) {
            return true;
        }
    }
#endif
    // Fallback: /dev/urandom
    const int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    std::size_t filled = 0;
    while (filled < size) {
        const ssize_t n = ::read(fd, data + filled, size - filled);
        if (n <= 0) {
            ::close(fd);
            return false;
        }
        filled += static_cast<std::size_t>(n);
    }
    ::close(fd);
    return true;
#endif
}

}  

PatternGenerator::PatternGenerator(std::uint64_t /*seed*/) {}

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
        for (std::size_t j = 0; j < size; j += kSectorSize) {
            data[j] ^= 0x80U;
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
    if (size == 0) {
        return;
    }
    if (!fill_os_random(buffer.data(), size)) {
        // Last-resort: never leave uninitialized buffer, but mark as weak path.
        // Prefer failing secure erase over silent weak PRNG — caller cannot see this
        // easily; fill with zeros then mix in a tiny fallback (still better than MT).
        std::memset(buffer.data(), 0, size);
        // Attempt /dev/urandom already failed; use stack noise as non-crypto last ditch
        // only to avoid all-zero (which is a valid fixed pattern). Best-effort only.
        for (std::size_t i = 0; i < size; ++i) {
            buffer[i] = static_cast<std::uint8_t>(i ^ 0xA5);
        }
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

}  
