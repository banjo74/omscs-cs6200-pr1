#include "compress_graph.hpp"

#include <boost/container_hash/hash.hpp>
#include <boost/hash2/hash_append.hpp>
#include <boost/hash2/sha2.hpp>

#include <algorithm>
#include <array>
#include <assert.h>
#include <functional>
#include <ranges>

namespace generator {
template <typename H, typename F>
void hash_append(H& h, F const& f, WordInfo const& wi) {
    using boost::hash2::hash_append;
    hash_append(h, f, wi.id);
}

template <typename H, typename F>
void hash_append(H& h, F const& f, GenericWord const& t) {}

template <typename H, typename F>
void hash_append(H& h, F const& f, Number const& t) {}

template <typename H, typename F>
void hash_append(H& h, F const& f, Token const& t) {
    using boost::hash2::hash_append;
    hash_append(h, f, t.index());
    std::visit(
        [&h, &f](auto const& t) {
            using boost::hash2::hash_append;
            hash_append(h, f, t);
        },
        t);
}

template <typename H, typename F>
void hash_append(H& h, F const& f, Action const& action) {
    using boost::hash2::hash_append;
    hash_append(h, f, action.toState);
    hash_append(h, f, action.resetRecording);
    if (action.token) {
        hash_append(h, f, *action.token);
    }
}

namespace {
using UsedCharacters = std::array<bool, num_characters>;

UsedCharacters used_characters(Graph const& g) {
    UsedCharacters out{}; // zero initialize
    std::ranges::for_each(g | std::views::join | std::views::keys,
                          [&out](auto const x) { out[x] = true; });
    return out;
}

// I don't have access to a good digest library in this environment otherwise,
// computing a digest would make this more efficient.
using ClassSignature = boost::hash2::sha2_256::result_type;

ClassSignature get_signature(Graph const& g, char const c) {
    boost::hash2::sha2_256 h;
    for (size_t i = 0; i < g.size(); ++i) {
        if (auto const iter = g[i].find(c); iter != g[i].end()) {
            using boost::hash2::hash_append;
            hash_append(h, boost::hash2::default_flavor{}, i);
            hash_append(h, boost::hash2::default_flavor{}, iter->second);
        }
    }
    return h.result();
}
} // namespace

CompressedGraph compress_graph(Graph g) {
    auto const isUsed = [usedCharacters = used_characters(g)](auto const& c) {
        return usedCharacters[c];
    };

    std::unordered_map<ClassSignature, size_t, boost::hash<ClassSignature>>
                    classes{{ClassSignature{}, 0}};
    CompressedGraph out;

    for (auto const c : all_characters() | std::views::filter(isUsed)) {
        auto const [iter, _] =
            classes.emplace(get_signature(g, c), classes.size());
        out.class_[c] = iter->second;
    }

    for (auto const c :
         all_characters() | std::views::filter(std::not_fn(isUsed))) {
        out.class_[c] = 0;
    }

    out.graph.reserve(g.size());
    for (auto const& m : g) {
        std::unordered_map<uint8_t, Action> cm;
        for (auto& [c, action] : m) {
            assert(out.class_[c] != 0);
            cm.emplace(out.class_[c], std::move(action));
        }
        out.graph.push_back(std::move(cm));
    }

    return out;
}
} // namespace generator
