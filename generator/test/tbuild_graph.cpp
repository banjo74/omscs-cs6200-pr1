#include "../BaseStates.hpp"
#include "../Graph.hpp"
#include "../build_graph.hpp"
#include "../characters.hpp"
#include "Automaton.hpp"
#include "characters.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <assert.h>
#include <functional>

using namespace generator;
using namespace generator::test;

namespace generator {
Action const* action(Graph const& g, size_t const state, char const c) {
    auto const iter = g[state].find(c);
    if (iter != g[state].end()) {
        return &iter->second;
    }
    return nullptr;
}

size_t num_states(Graph const& g) {
    return g.size();
}
} // namespace generator

namespace {
[[maybe_unused]] void print_graph(std::ostream& stream, Graph const& g) {
    for (size_t i = 0; i < g.size(); ++i) {
        std::set<char> supportedChars;
        for (auto const& [c, action] : g[i]) {
            supportedChars.insert(c);
        }
        for (auto const c : supportedChars) {
            auto const& action = g[i].at(c);
            stream << "g[" << state_string(i) << "][" << to_readable(c) << "]={"
                   << state_string(action.toState) << "}" << std::endl;
        }
    }
}

} // namespace

TEST(Graph, JustDigits) {
    std::string const term{"\r\n\r\n"};
    auto const        g = build_graph({}, {}, term);
    Automaton<Graph>  a{g};
    EXPECT_TRUE(a.valid());

    EXPECT_EQ(g.size(), NumBaseStates + 3);
    check_invalidates(a, not_word_characters() | skip(term) | skip(space()));
    check_invalidates(a, word_characters() | skip_digits());

    SimplePoint const points[]{
        {"123456", {{size_t{123456}}}},
        {"  123456  54321 ", {{size_t{123456}, size_t{54321}}}},
        {"123456\r\n\r\n", {{size_t{123456}}}},
    };
    run_points(a, points);
}

TEST(Graph, GenericWord) {
    std::string const term{"\r\n\r\n"};
    auto const        g = build_graph({}, {'/'}, term);
    Automaton<Graph>  a{g};
    EXPECT_EQ(g.size(), NumBaseStates + 3);

    check_invalidates(a, not_word_characters() | skip(term) | skip(space()));
    check_invalidates(a, word_characters() | skip('/') | skip_digits());

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
    };
    run_points(a, points);
}

TEST(Graph, WithWords) {
    std::string const term{"\r\n\r\n"};
    auto const        g = build_graph({{"GETFILE", {"Getfile"}},
                                       {"GET", {"Get"}},
                                       {"OK", {"Ok"}},
                                       {"FILE_NOT_FOUND", {"FileNotFound"}},
                                       {"ERROR", {"Error"}},
                                       {"INVALID", {"Invalid"}}},
                                      {'/'},
                               term);
    Automaton<Graph>  a{g};
    EXPECT_EQ(g.size(), NumBaseStates + 35 + 3);

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
