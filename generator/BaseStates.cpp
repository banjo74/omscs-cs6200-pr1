#include "BaseStates.hpp"

#include <unordered_map>

namespace generator {
std::string state_string(size_t const s) {
    static std::unordered_map<BaseStates, std::string> const baseStates{
        {Start, "Start"},
        {InSpace, "InSpace"},
        {InDigits, "InDigits"},
        {InGenericWord, "InGenericWord"},
        {Finished, "Finished"},
        {Invalid, "Invalid"},
    };
    if (s < NumBaseStates) {
        return baseStates.at(BaseStates{s});
    }
    return std::to_string(s);
}
} // namespace generator
