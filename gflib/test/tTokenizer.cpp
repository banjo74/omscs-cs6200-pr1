#include "../gf-student.h"
#include "TokenizerPtr.hpp"
#include "random_seed.hpp"
#include "terminator.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <random>

using namespace gf::test;

namespace {
inline std::string to_readable(char c) {
    static std::unordered_map<char, std::string> const m{
        {'\n', "\\n"},
        {'\t', "\\t"},
        {'\r', "\\r"},
        {127, "DEL"},
    };
    if (auto const iter = m.find(c); iter != m.end()) {
        return iter->second;
    }
    if (std::isprint(c) != 0) {
        return {c};
    }
    char buffer[12];
    std::snprintf(
        buffer, sizeof(buffer), "0x%.2x", static_cast<unsigned int>(c));
    return buffer;
}

inline std::string to_readable(std::string const& in) {
    std::string out;
    for (auto const c : in) {
        out += to_readable(c);
    }
    return out;
}

class CppToken {
  public:
    using Data = std::variant<TokenId, std::string, size_t>;

    CppToken(TokenId id = UnknownToken)
        : data_{id} {}

    CppToken(std::string path)
        : data_{std::move(path)} {}

    CppToken(char const* path)
        : CppToken{std::string{path}} {}

    CppToken(size_t size)
        : data_{size} {}

    CppToken(Token const& tok) {
        if (tok.id == PathToken) {
            data_ = tok.data.path;
        } else if (tok.id == SizeToken) {
            data_ = tok.data.size;
        } else {
            data_ = tok.id;
        }
    }

    TokenId id() const {
        return std::visit(GetId{}, data_);
    }

    Data const& data() const {
        return data_;
    }

    bool operator==(CppToken const& o) const = default;

  private:
    struct GetId {
        TokenId operator()(TokenId x) const {
            return x;
        }

        TokenId operator()(std::string const&) const {
            return PathToken;
        }

        TokenId operator()(size_t const&) const {
            return SizeToken;
        }
    };

    Data data_;
};

std::ostream& operator<<(std::ostream& stream, CppToken const& tok) {
    struct Visitor {
        std::ostream& stream;

        void operator()(TokenId id) const {
            stream << tok_str(id);
        }

        void operator()(size_t s) const {
            stream << tok_str(SizeToken) << ", " << s;
        }

        void operator()(std::string const& s) const {
            stream << tok_str(PathToken) << ", " << s;
        }
    };

    stream << '{';
    std::visit(Visitor{stream}, tok.data());
    stream << '}';
    return stream;
}

Token to_token(CppToken const& tok) {
    struct Visitor {
        Token operator()(TokenId id) const {
            Token out;
            out.id = id;
            return out;
        }

        Token operator()(size_t s) const {
            Token out;
            out.id        = SizeToken;
            out.data.size = s;
            return out;
        }

        Token operator()(std::string const& s) const {
            Token out;
            out.id        = PathToken;
            out.data.path = s.c_str();
            return out;
        }
    };

    return std::visit(Visitor{}, tok.data());
}

std::string const ignored{"123456"};

std::string input_text(std::vector<CppToken> const& r) {
    std::vector<Token> tokens;
    tokens.reserve(r.size());
    for (auto const& t : r) {
        tokens.emplace_back(to_token(t));
    }
    size_t const needed =
        snprintf_tokens(NULL, 0, tokens.data(), tokens.size());

    std::string out(needed, ' ');
    snprintf_tokens(out.data(), out.size() + 1, tokens.data(), tokens.size());
    return out;
}

std::vector<CppToken> get_tokens(TokenizerPtr const& tok) {
    std::vector<CppToken> out;
    for (size_t i = 0; i < tok_num_tokens(tok.get()); ++i) {
        out.emplace_back(tok_token(tok.get(), i));
    }
    return out;
}

struct Point {
    std::string           input;
    bool                  success;
    std::vector<CppToken> expected;
};

void test_point(TokenizerPtr& tok, Point const& point) {
    tok_reset(tok.get());
    std::string const fullInput = point.input + ignored;
    {
        auto const numProcessed = process(tok, fullInput);
        EXPECT_EQ(tok_done(tok.get()), point.success) << point.input;
        if (point.success) {
            EXPECT_EQ(numProcessed, static_cast<ssize_t>(point.input.size()))
                << point.input;
        }
        EXPECT_EQ(point.expected, get_tokens(tok)) << point.input;
    }

    tok_reset(tok.get());
    for (size_t i = 0; i < fullInput.size(); i += 3) {
        process(tok,
                fullInput.data() + i,
                std::min(size_t{3}, fullInput.size() - i));
    }
    EXPECT_EQ(tok_done(tok.get()), point.success) << point.input;
    EXPECT_EQ(point.expected, get_tokens(tok)) << point.input;

    {
        std::string const roundTrip = input_text(get_tokens(tok));
        tok_reset(tok.get());
        EXPECT_EQ(static_cast<ssize_t>(roundTrip.size()),
                  process(tok, roundTrip.data(), roundTrip.size()));
        EXPECT_EQ(point.expected, get_tokens(tok))
            << point.input << ":round trip";
    }
}

template <typename Points>
void test_points(TokenizerPtr& tok, Points&& points) {
    std::ranges::for_each(points,
                          [&tok](auto const& p) { test_point(tok, p); });
}

class RandomToken {
  public:
    RandomToken()
        : validChars_{"abcdefghijklmnopqrstuvwxyz0123456789/_()"}
        , idDist_{0, NumTokens - 1}
        , charDist_{0, validChars_.size() - 1}
        , strSizeDist_{3, 204} {}

    template <typename Gen>
    CppToken operator()(Gen& gen) {
        auto const id = static_cast<TokenId>(idDist_(gen));
        switch (id) {
        case SizeToken:
            return CppToken{sizeDist_(gen)};
        case PathToken:
            return CppToken{randomString(gen)};
        default:
            return CppToken{id};
        }
    }

  private:
    std::string                           validChars_;
    std::uniform_int_distribution<int>    idDist_;
    std::uniform_int_distribution<size_t> sizeDist_;
    std::uniform_int_distribution<size_t> charDist_;
    std::uniform_int_distribution<size_t> strSizeDist_;

    template <typename Gen>
    std::string randomString(Gen& gen) {
        std::string  out{'/'};
        size_t const n = strSizeDist_(gen);
        for (size_t i = 0; i < n; ++i) {
            out += validChars_[charDist_(gen)];
        }
        return out;
    }
};

} // namespace

TEST(Tokenizer, Basic) {
    auto tok = create_tokenizer();

    std::vector<CppToken> bigInput;
    std::mt19937          gen{gf::test::random_seed()};
    RandomToken           randomToken;
    while (bigInput.size() < 1024) {
        bigInput.push_back(randomToken(gen));
    }

    Point const points[] = {
        {"GETFILE GET /a/path/to/a/file" + terminator,
         true,
         {GetfileToken, GetToken, "/a/path/to/a/file"}},
        {"GETFILE OK 1234567" + terminator,
         true,
         {GetfileToken, OkToken, 1234567}},
        {input_text(bigInput), true, bigInput},
        {"GETFILE OK 1234567 a/b/c", false, {GetfileToken, OkToken, 1234567}},
        {"GETFILE Ok 1234567", false, {GetfileToken}},
    };
    test_points(tok, points);
}
