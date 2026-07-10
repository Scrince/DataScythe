#pragma once

#include <cstddef>
#include <random>
#include <vector>

namespace datascythe {


class PassScheduler {
public:
    explicit PassScheduler(std::uint64_t seed = std::random_device{}());

    
    std::vector<int> build_schedule(std::size_t pass_count, bool include_random_passes) const;

private:
    mutable std::mt19937_64 rng_;
};

}  