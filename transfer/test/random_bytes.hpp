#ifndef transfer_test_random_bytes
#define transfer_test_random_bytes

#include "Bytes.hpp"

#include <algorithm>
#include <cstddef>
#include <random>
#include <vector>

namespace transfer::test {
template <typename Rng>
Bytes random_bytes(Rng& rng, size_t const s) {
    std::uniform_int_distribution<uint8_t> dist;
    Bytes                                  out(s);
    std::ranges::generate(out, [&dist, &rng] { return std::byte{dist(rng)}; });
    return out;
}
} // namespace transfer::test

#endif // include guard
