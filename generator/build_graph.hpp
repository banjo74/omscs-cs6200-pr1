#ifndef generator_build_graph_hpp
#define generator_build_graph_hpp

#include "Graph.hpp"

#include <set>
#include <stdexcept>

namespace generator {
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
