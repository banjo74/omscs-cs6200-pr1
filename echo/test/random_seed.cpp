
#include <boost/functional/hash.hpp>
#include <gtest/gtest.h>

#include <string>

namespace echo::test {
size_t random_seed() {
    auto const* const testInfo =
        testing::UnitTest::GetInstance()->current_test_info();
    size_t out = 0;
    boost::hash_combine(out, std::string_view{testInfo->test_suite_name()});
    boost::hash_combine(out, std::string_view{testInfo->name()});
    return out;
}
} // namespace echo::test
