#ifndef generator_characters_hpp
#define generator_characters_hpp

#include <limits>
#include <ranges>

namespace generator {
/// Return true if the provided character is a valid word character
/// A valid word character is a non-witespace printable character
/// (in the current locale)
bool is_word_character(char);

/// just a wrapper around std::isdigit but returns a bool instead of an int
bool is_digit(char);

inline auto all_characters() {
    return std::views::iota(
               static_cast<int>(std::numeric_limits<char>::min()),
               static_cast<int>(std::numeric_limits<char>::max()) + 1) |
           std::views::transform(
               [](auto const x) { return static_cast<char>(x); });
}

inline auto word_characters() {
    return all_characters() | std::views::filter(is_word_character);
}

inline auto digit_characters() {
    return std::views::iota('0', '9' + 1);
}

char space();
} // namespace generator

#endif // include guard
