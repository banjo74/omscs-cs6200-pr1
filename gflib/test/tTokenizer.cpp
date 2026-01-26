#include "../gf-student.h"
#include "TokenizerPtr.hpp"
#include "random_seed.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <random>

using namespace gf::test;

namespace {
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

std::string input_text(CppToken const& tok) {
    struct Visitor {
        std::string operator()(TokenId id) const {
            std::string const s = tok_str(id);
            std::string       out;
            for (auto const c : s) {
                auto const u = std::toupper(c);
                if (u == c && !out.empty()) {
                    out += '_';
                }
                out += u;
            }
            return out;
        }

        std::string operator()(size_t s) const {
            return std::to_string(s);
        }

        std::string operator()(std::string const& s) const {
            return s;
        }
    };

    return std::visit(Visitor{}, tok.data());
}

std::string const term{"\r\n\r\n"};
std::string const ignored{"123456"};

template <std::ranges::range R>
std::string input_text(R&& r) {
    std::string out;
    for (auto const& c : r) {
        if (!out.empty()) {
            out += " ";
        }
        out += input_text(c);
    }
    out += term;
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
}

template <typename Points>
void test_points(TokenizerPtr& tok, Points&& points) {
    std::ranges::for_each(points,
                          [&tok](auto const& p) { test_point(tok, p); });
}

} // namespace

TEST(Tokenizer, Basic) {
    auto tok = create_tokenizer();

    std::vector<CppToken> const baseTokens{
        SIMPLE_TOKEN_ID(DECL_TOKEN_ID) "/a/b/c/d/e/f/g",
        12,
        "/h/i/j/()/k/l/g123456",
        54321};

    std::vector<CppToken>                 bigInput;
    std::mt19937                          gen{gf::test::random_seed()};
    std::uniform_int_distribution<size_t> picker(0, baseTokens.size() - 1);
    for (size_t i = 0; i < 1024 * 1024; ++i) {
        bigInput.push_back(baseTokens[picker(gen)]);
    }

    Point const points[] = {
        {"GETFILE GET /a/path/to/a/file" + term,
         true,
         {GetfileToken, GetToken, "/a/path/to/a/file"}},
        {"GETFILE OK 1234567" + term, true, {GetfileToken, OkToken, 1234567}},
        {input_text(bigInput), true, bigInput},
        {"GETFILE OK 1234567 a/b/c", false, {GetfileToken, OkToken, 1234567}},
        {"GETFILE Ok 1234567", false, {GetfileToken}},
    };
    test_points(tok, points);
}
