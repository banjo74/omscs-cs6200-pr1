

#include "characters.hpp"

#include <cctype>

namespace generator {
bool is_word_character(char const c) {
    return std::isspace(c) == 0 && std::isprint(c) != 0;
}

bool is_digit(char const c) {
    return std::isdigit(c) != 0;
}

char space() {
    return ' ';
}
} // namespace generator
