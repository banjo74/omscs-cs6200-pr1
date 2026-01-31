#include "../BaseStates.hpp"
#include "../CompressedGraph.hpp"
#include "../build_graph.hpp"
#include "../characters.hpp"
#include "../compress_graph.hpp"
#include "Automaton.hpp"
#include "characters.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <assert.h>
#include <functional>

using namespace generator;
using namespace generator::test;

namespace generator {
Action const* action(CompressedGraph const& g,
                     size_t const           state,
                     char const             c) {
    auto const iter = g.graph[state].find(g.class_[c]);
    if (iter != g.graph[state].end()) {
        return &iter->second;
    }
    return nullptr;
}

size_t num_states(CompressedGraph const& g) {
    return g.graph.size();
}
} // namespace generator

namespace {
[[maybe_unused]] void print_graph(std::ostream&          stream,
                                  CompressedGraph const& g) {
    for (size_t i = 0; i < g.class_.size(); ++i) {
        stream << to_readable(static_cast<char>(i)) << "->"
               << static_cast<int>(g.class_[i]) << std::endl;
    }
    for (size_t i = 0; i < g.graph.size(); ++i) {
        std::set<uint8_t> supportedChars;
        for (auto const& [c, action] : g.graph[i]) {
            supportedChars.insert(c);
        }
        for (auto const c : supportedChars) {
            auto const& action = g.graph[i].at(c);
            stream << "g[" << state_string(i) << "][" << static_cast<int>(c)
                   << "]={" << state_string(action.toState) << "}" << std::endl;
        }
    }
}
} // namespace

TEST(CompressedGraph, WithWords) {
    std::string const term{"\r\n\r\n"};
    auto const        g =
        compress_graph(build_graph({{"GETFILE", {"Getfile"}},
                                    {"GET", {"Get"}},
                                    {"OK", {"Ok"}},
                                    {"FILE_NOT_FOUND", {"FileNotFound"}},
                                    {"ERROR", {"Error"}},
                                    {"INVALID", {"Invalid"}}},
                                   {'/'},
                                   term));

    Automaton<CompressedGraph> a{g};

    std::string const firstLetters{"GOFEI"};
    check_invalidates(a, not_word_characters() | skip(term) | skip(space()));
    check_invalidates(
        a, word_characters() | skip('/') | skip(firstLetters) | skip_digits());

    SimplePoint const points[]{
        {"123456", {{size_t{123456}}}},
        {"  123456  54321 ", {{size_t{123456}, size_t{54321}}}},
        {"123456\r\n\r\n", {{size_t{123456}}}},
        {"/a/b/c/d/e 123456\r\n\r\n",
         {{std::string{"/a/b/c/d/e"}, size_t{123456}}}},
        {"/a/b/c/d/e123456\r\n\r\n", {{std::string{"/a/b/c/d/e123456"}}}},
        {"/a/b/c/d/e123456 /qrs 12345 \r\n\r\n",
         {{std::string{"/a/b/c/d/e123456"},
           std::string{"/qrs"},
           size_t{12345}}}},
        {"GET /a/b/c/d/e123456 /qrs 12345 \r\n\r\n",
         {{WordInfo{"Get"},
           std::string{"/a/b/c/d/e123456"},
           std::string{"/qrs"},
           size_t{12345}}}},
        {"GETFILE GET /a/b/c/d/e123456 /qrs 12345 \r\n\r\n",
         {{WordInfo{"Getfile"},
           WordInfo{"Get"},
           std::string{"/a/b/c/d/e123456"},
           std::string{"/qrs"},
           size_t{12345}}}},
        {"GETFILE /a/b/c/d/e123456 GET /qrs INVALID 12345 \r\n\r\n",
         {{WordInfo{"Getfile"},
           std::string{"/a/b/c/d/e123456"},
           WordInfo{"Get"},
           std::string{"/qrs"},
           WordInfo{"Invalid"},
           size_t{12345}}}},
    };
    run_points(a, points);
}
