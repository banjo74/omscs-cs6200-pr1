#include "../Graph.hpp"
#include "../characters.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <assert.h>
#include <functional>

using namespace generator;

namespace {
auto not_word_characters() {
    return all_characters() |
           std::views::filter(std::not_fn(is_word_character));
}

class Automaton {
  public:
    using Token = std::variant<size_t, std::string, WordInfo>;

    Automaton(Graph g)
        : graph_{std::move(g)} {
        reset();
    }

    void reset() {
        state_ = 0;
        buffer_.clear();
    }

    bool valid() const {
        return state_ != Invalid;
    }

    bool finished() const {
        return state_ == Finished;
    }

    std::optional<Token> operator()(char c) {
        if (!valid()) {
            return {};
        }
        auto const iter = graph_[state_].find(c);
        if (iter == graph_[state_].end()) {
            state_ = Invalid;
            return {};
        }

        assert(iter->second.toState < graph_.size());
        state_ = iter->second.toState;

        if (iter->second.token) {
            return std::visit(TokenVisitor{buffer_}, *iter->second.token);
        }
        if (iter->second.resetRecording) {
            buffer_.clear();
        }
        buffer_ += c;

        return {};
    }

  private:
    struct TokenVisitor {
        std::string const& buffer_;

        Token operator()(GenericWord const&) const {
            return buffer_;
        }

        Token operator()(Number const&) const {
            return std::stoull(buffer_);
        }

        Token operator()(WordInfo const& info) const {
            return info;
        }
    };

    Graph       graph_;
    std::string buffer_;
    size_t      state_;
};

std::string to_readable(char c) {
    static std::unordered_map<char, std::string> const m{
        {'\n', "\\n"},
        {'\t', "\\t"},
        {'\r', "\\r"},
    };
    if (auto const iter = m.find(c); iter != m.end()) {
        return iter->second;
    }
    return {c};
}

std::string to_readable(std::string const& in) {
    std::string out;
    for (auto const c : in) {
        out += to_readable(c);
    }
    return out;
}

std::string readable_offset(std::string const& in, size_t const i) {
    assert(i < in.size());
    std::string out;
    for (size_t j = 0; j < i; ++j) {
        out += std::string(to_readable(in[j]).size(), ' ');
    }
    return out;
}

std::vector<Automaton::Token> tokenize(Automaton& a, std::string const& input) {
    a.reset();
    std::vector<Automaton::Token> out;
    for (size_t i = 0; i < input.size(); ++i) {
        auto const c = input[i];
        if (auto maybeToken = a(c)) {
            out.push_back(std::move(*maybeToken));
        }
        if (!a.valid()) {
            std::string const message = "invalid string:\n" +
                                        to_readable(input) + "\n" +
                                        readable_offset(input, i) + '^' + '\n' +
                                        readable_offset(input, i) + '|';
            throw std::runtime_error{message};
        }
    }
    if (!a.finished()) {
        if (auto maybeToken = a('\0')) {
            out.push_back(std::move(*maybeToken));
        }
    }
    return out;
}

struct SimplePoint {
    std::string                   input;
    std::vector<Automaton::Token> expected;
};

template <typename Points>
void run_points(Automaton& a, Points&& points) {
    for (auto const& point : points) {
        EXPECT_EQ(tokenize(a, point.input), point.expected) << point.input;
        EXPECT_TRUE(a.finished()) << point.input;
    }
}

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

template <typename Range>
void check_invalidates(Automaton& a, Range&& r) {
    a.reset();
    for (auto const x : r) {
        a(x);
        EXPECT_FALSE(a.valid()) << to_readable(x);
        a.reset();
    }
}

auto skip(std::string s) {
    return std::views::filter(
        [s](auto const c) { return s.find(c) == std::string::npos; });
}

auto skip(char const c) {
    return skip(std::string{c});
}

auto skip_digits() {
    return std::views::filter(std::not_fn(is_digit));
}
} // namespace

TEST(Graph, JustDigits) {
    std::string const term{"\r\n\r\n"};
    auto const        g = build_graph({}, {}, term);
    Automaton         a{g};
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
    Automaton         a{g};
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
    Automaton         a{g};
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
