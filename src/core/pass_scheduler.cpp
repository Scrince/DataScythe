#include "core/pass_scheduler.h"

#include <algorithm>
#include <cstring>

namespace datascythe {

namespace {


const int kShredPatterns[] = {
    -2,
    2, 0x000, 0xFFF,
    2, 0x555, 0xAAA,
    -1,
    6, 0x249, 0x492, 0x6DB, 0x924, 0xB6D, 0xDB6,
    12, 0x111, 0x222, 0x333, 0x444, 0x666, 0x777,
        0x888, 0x999, 0xBBB, 0xCCC, 0xDDD, 0xEEE,
    -1,
    8, 0x1000, 0x1249, 0x1492, 0x16DB, 0x1924, 0x1B6D, 0x1DB6, 0x1FFF,
    14, 0x1111, 0x1222, 0x1333, 0x1444, 0x1555, 0x1666, 0x1777,
        0x1888, 0x1999, 0x1AAA, 0x1BBB, 0x1CCC, 0x1DDD, 0x1EEE,
    -1,
    0,
};

std::size_t random_choose(std::mt19937_64& rng, std::size_t upper_exclusive) {
    if (upper_exclusive <= 1) {
        return 0;
    }
    std::uniform_int_distribution<std::size_t> dist(0, upper_exclusive - 1);
    return dist(rng);
}

}  

PassScheduler::PassScheduler(std::uint64_t seed) : rng_(seed) {}

std::vector<int> PassScheduler::build_schedule(std::size_t pass_count,
                                               bool include_random_passes) const {
    if (pass_count == 0) {
        return {};
    }

    std::vector<int> fixed;
    fixed.reserve(pass_count);
    std::size_t random_needed = 0;
    std::size_t remaining = pass_count;

    const int* cursor = kShredPatterns;
    while (remaining > 0) {
        int descriptor = *cursor++;
        if (descriptor == 0) {
            cursor = kShredPatterns;
            continue;
        }

        if (descriptor < 0) {
            const std::size_t count = static_cast<std::size_t>(-descriptor);
            if (!include_random_passes) {
                continue;
            }
            if (count >= remaining) {
                random_needed += remaining;
                remaining = 0;
                break;
            }
            random_needed += count;
            remaining -= count;
            continue;
        }

        const std::size_t group_size = static_cast<std::size_t>(descriptor);
        const int* group = cursor;
        cursor += static_cast<std::ptrdiff_t>(group_size);

        if (group_size <= remaining) {
            fixed.insert(fixed.end(), group, group + static_cast<std::ptrdiff_t>(group_size));
            remaining -= group_size;
        } else if (remaining < 2 || 3 * remaining < group_size) {
            if (include_random_passes) {
                random_needed += remaining;
            }
            remaining = 0;
        } else {
            for (std::size_t i = 0; i < group_size && remaining > 0; ++i) {
                if (remaining == group_size || random_choose(rng_, group_size) < remaining) {
                    fixed.push_back(group[static_cast<std::ptrdiff_t>(i)]);
                    --remaining;
                }
            }
        }
    }

    const std::size_t total = pass_count;
    std::vector<int> schedule(total, 0);
    std::size_t fixed_count = fixed.size();
    std::size_t top = fixed_count;
    std::size_t random_slots = include_random_passes ? random_needed : 0;

    for (std::size_t i = 0; i < fixed_count; ++i) {
        schedule[i] = fixed[i];
    }

    if (random_slots == 0) {
        
        
        for (std::size_t i = fixed_count; i < total; ++i) {
            schedule[i] = fixed[i % fixed_count];
        }
        return schedule;
    }

    if (random_slots > total) {
        random_slots = total;
    }

    std::size_t random_remaining = random_slots;
    std::size_t accum = random_remaining > 0 ? random_remaining - 1 : 0;
    const std::size_t slope = random_remaining > 0 ? random_remaining - 1 : 0;

    for (std::size_t n = 0; n < total; ++n) {
        if (random_remaining > 0 && accum <= slope) {
            accum += total - 1;
            schedule[top++] = schedule[n];
            schedule[n] = -1;
            --random_remaining;
        } else if (top > n + 1) {
            const std::size_t swap_index = n + random_choose(rng_, top - n);
            std::swap(schedule[n], schedule[swap_index]);
        }
        if (random_remaining > 0) {
            accum -= slope;
        }
    }

    return schedule;
}

}  