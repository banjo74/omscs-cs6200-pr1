#ifndef generator_Graph_hpp
#define generator_Graph_hpp

#include <optional>
#include <set>
#include <stdexcept>
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

using Token = std::variant<GenericWord, Number, WordInfo>;

struct Action {
    size_t               toState;
    bool                 resetRecording;
    std::optional<Token> token;
};

using Graph = std::vector<std::unordered_map<char, Action>>;

enum BaseStates : size_t {
    Start,
    Invalid,
    Finished,
    InSpace,
    InDigits,
    InGenericWord,
    NumBaseStates,
};

/// Construct a graph based on the provided words, generic word starters, and
/// termination sequence.
///
/// Requirements:
/// Each element in the domain of words (the word text) must be non-empty and
/// consist of word characters, and may not start with a digit.
///
/// Each element of startsGenericWord must be a word character, my not be a
/// digit, and may not start any of the word text.
///
/// Teminator must be non-empty and may not contain word characters or the
/// space (32).
Graph build_graph(Words const&          words,
                  std::set<char> const& startsGenericWord,
                  std::string const&    terminator);

std::string state_string(size_t s);

struct BuildGraphInvalidArgument : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};

struct InvalidWord : BuildGraphInvalidArgument {
    InvalidWord(std::string);
};

struct InvalidStartsGenericWordCharacter : BuildGraphInvalidArgument {
    InvalidStartsGenericWordCharacter(char);
};

struct InvalidTerminator : BuildGraphInvalidArgument {
    InvalidTerminator(std::string);
};
} // namespace generator

#endif // include guard
