#include <stddef.h>

namespace echo::test {
// at least on my system, this is as about as big as we can expect to recv in a
// single shot
constexpr size_t const max_buffer = 1 << 10;
} // namespace echo::test
