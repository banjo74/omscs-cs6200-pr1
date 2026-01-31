#ifndef generator_BaseStates_hpp
#define generator_BaseStates_hpp

#include <cstddef>
#include <string>

namespace generator {
// These are states used by every Graph.
enum BaseStates : size_t {
    // always start here
    Start,
    // hit an invalid character
    Invalid,
    // hit the terminator
    Finished,
    // in whitespace
    InSpace,
    // in digits
    InDigits,
    // in a generic word
    InGenericWord,
    NumBaseStates,
};

// if s is a BaseState, return a string that looks like the BaseState.
// Otherwise, return s as a number.
std::string state_string(size_t s);

} // namespace generator
#endif // include guard
