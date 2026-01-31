#include "CompressedGraph.hpp"

#include <algorithm>

namespace generator {
size_t CompressedGraph::numClasses() const {
    return std::ranges::max(class_) + 1;
}
} // namespace generator
