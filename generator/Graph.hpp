#ifndef generator_Graph_hpp
#define generator_Graph_hpp

#include <boost/hash2/hash_append.hpp>

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace generator {
// WordInfo represents a single recognized word in the graph
// the Words structure below binds the actual text of the word
// to the information about the word.
struct WordInfo {
    std::string id;
    bool        operator==(WordInfo const&) const = default;
};

// Words binds the actual text of a recognized word to WordInfo
using Words = std::unordered_map<std::string, WordInfo>;

struct GenericWord {
    bool operator==(GenericWord const&) const = default;
};

struct Number {
    bool operator==(Number const&) const = default;
};

// one of the recognized tokens.  Note, Number and GenericWord are just tags.
using TokenBase = std::variant<GenericWord, Number, WordInfo>;

struct Token : TokenBase {
    using TokenBase::TokenBase;
};

// Action represents the action to take for each state X character combination
struct Action {
    // the state to transition to
    size_t toState;
    // for tokens that have variable content, reset recording content (the start
    // of one of these tokens)
    bool resetRecording;
    // if set, at the end of a token.
    std::optional<Token> token;
};

// a (di)graph.  Each node is represented by an element in the vector and
// represents a state of the FSM we're building.  Each node contains it's
// outbound edges as Action where Action::toState says which state the edge goes
// to.
using Graph = std::vector<std::unordered_map<char, Action>>;

} // namespace generator

#endif // include guard
