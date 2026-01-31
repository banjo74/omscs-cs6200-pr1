#ifndef generator_CompressedGraph_hpp
#define generator_CompressedGraph_hpp

#include "Graph.hpp"
#include "characters.hpp"

#include <array>
#include <cstdint>
#include <ranges>
#include <unordered_map>
#include <vector>

namespace generator {
using CharacterClass = std::array<uint8_t, num_characters>;

struct CompressedGraph {
    // a map from each character to it's character class
    CharacterClass class_;

    // like Graph except that the inner map is from character *class* to action.
    std::vector<std::unordered_map<uint8_t, Action>> graph;

    size_t numClasses() const;
};
} // namespace generator

#endif // include guard
