
#ifndef echo_test_random_string_hpp
#define echo_test_random_string_hpp

#include <assert.h>
#include <random>
#include <string>

namespace echo::test {
// produces a random string of length size using the provided random stream rng.
// the string consists of hexidecimal digits generated from the random stream.
template <typename Rng>
std::string random_string(Rng& rng, size_t const size) {
    std::uniform_int_distribution<unsigned long int> dist;
    std::string                                      out;
    int const numChars = 2 * sizeof(unsigned long int);
    while (out.size() < size) {
        char         strBuffer[numChars + 1];
        size_t const n = std::snprintf(
            strBuffer, sizeof(strBuffer), "%0*lx", numChars, dist(rng));
        assert(n == numChars);
        out.append(strBuffer, n);
    }
    out.resize(size);
    return out;
}
} // namespace echo::test

#endif // include guard
