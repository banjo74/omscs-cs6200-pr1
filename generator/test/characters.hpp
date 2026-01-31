#ifndef test_characters_hpp
#define test_characters_hpp

#include "../characters.hpp"

#include <functional>
#include <ranges>
#include <string>

namespace generator::test {
inline auto not_word_characters() {
    return all_characters() |
           std::views::filter(std::not_fn(is_word_character));
}

inline auto skip(std::string s) {
    return std::views::filter(
        [s](auto const c) { return s.find(c) == std::string::npos; });
}

inline auto skip(char const c) {
    return skip(std::string{c});
}

inline auto skip_digits() {
    return std::views::filter(std::not_fn(is_digit));
}

inline std::string to_readable(char c) {
    static std::unordered_map<char, std::string> const m{
        {'\n', "\\n"},
        {'\t', "\\t"},
        {'\r', "\\r"},
        {127, "DEL"},
    };
    if (auto const iter = m.find(c); iter != m.end()) {
        return iter->second;
    }
    if (std::isprint(c) != 0) {
        return {c};
    }
    char buffer[12];
    std::snprintf(
        buffer, sizeof(buffer), "0x%.2x", static_cast<unsigned int>(c));
    return buffer;
}

inline std::string to_readable(std::string const& in) {
    std::string out;
    for (auto const c : in) {
        out += to_readable(c);
    }
    return out;
}

inline std::string readable_offset(std::string const& in, size_t const i) {
    assert(i < in.size());
    std::string out;
    for (size_t j = 0; j < i; ++j) {
        out += std::string(to_readable(in[j]).size(), ' ');
    }
    return out;
}

} // namespace generator::test

#endif // include guard
