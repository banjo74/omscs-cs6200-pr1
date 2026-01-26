#include "../characters.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>

using namespace generator;

namespace {
template <std::ranges::range R>
auto to_set(R&& r) {
    std::set<std::ranges::range_value_t<std::decay_t<R>>> out;
    std::ranges::copy(std::forward<R>(r), std::inserter(out, out.end()));
    return out;
}
} // namespace

TEST(charcters, all_characters) {
    EXPECT_EQ(to_set(all_characters()).size(), 256u);
}

TEST(characters, is_word_character) {
    for (auto const c : all_characters()) {
        EXPECT_EQ(is_word_character(c),
                  std::isprint(c) != 0 && std::isspace(c) == 0);
    }
}

TEST(characters, is_digit) {
    for (auto const c : all_characters()) {
        EXPECT_EQ(is_digit(c), std::isdigit(c) != 0);
    }
}

TEST(characters, word_characters) {
    auto const s = to_set(word_characters());
    for (auto const c : all_characters()) {
        EXPECT_EQ(is_word_character(c), s.contains(c));
    }
}

TEST(characters, digit_characters) {
    auto const s = to_set(digit_characters());
    for (auto const c : all_characters()) {
        EXPECT_EQ(is_digit(c), s.contains(c));
    }
}
