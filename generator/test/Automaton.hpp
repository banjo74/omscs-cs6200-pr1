#ifndef generator_test_Automaton_hpp
#define generator_test_Automaton_hpp

#include "../Graph.hpp"
#include "characters.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <variant>

namespace generator::test {
using AutomatonToken = std::variant<size_t, std::string, WordInfo>;

template <typename G>
class Automaton {
  public:
    using Token = AutomatonToken;

    Automaton(G g)
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
        auto const maybeAction = action(graph_, state_, c);
        if (!maybeAction) {
            state_ = Invalid;
            return {};
        }

        assert(maybeAction->toState < num_states(graph_));
        state_ = maybeAction->toState;

        if (maybeAction->token) {
            return std::visit(TokenVisitor{buffer_}, *maybeAction->token);
        }
        if (maybeAction->resetRecording) {
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

    G           graph_;
    std::string buffer_;
    size_t      state_;
};

template <typename G>
std::vector<AutomatonToken> tokenize(Automaton<G>&      a,
                                     std::string const& input) {
    a.reset();
    std::vector<AutomatonToken> out;
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

template <typename G, typename Range>
void check_invalidates(Automaton<G>& a, Range&& r) {
    a.reset();
    for (auto const x : r) {
        a(x);
        EXPECT_FALSE(a.valid()) << to_readable(x);
        a.reset();
    }
}

struct SimplePoint {
    std::string                 input;
    std::vector<AutomatonToken> expected;
};

template <typename G, typename Points>
void run_points(Automaton<G>& a, Points&& points) {
    for (auto const& point : points) {
        EXPECT_EQ(tokenize(a, point.input), point.expected) << point.input;
        EXPECT_TRUE(a.finished()) << point.input;
    }
}
} // namespace generator::test

#endif // include guard
