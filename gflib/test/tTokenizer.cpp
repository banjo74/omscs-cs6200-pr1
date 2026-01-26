#include "../gf-student.h"
#include "random_seed.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <random>

namespace {
class CppToken {
  public:
    using Data      = std::variant<std::string, size_t>;
    using MaybeData = std::optional<Data>;

    CppToken(TokenId id)
        : id_{id} {
        assert(id_ != PathToken);
        assert(id_ != SizeToken);
        assert(!data_);
    };

    CppToken(TokenId id, std::string path)
        : id_{id}
        , data_{std::move(path)} {
        assert(id_ == PathToken);
    }

    CppToken(TokenId id, size_t size)
        : id_{id}
        , data_{size} {
        assert(id_ == SizeToken);
    }

    CppToken(Token const& tok)
        : id_{tok.id} {
        if (id_ == PathToken) {
            data_ = tok.data.path;
        }
        if (id_ == SizeToken) {
            data_ = tok.data.size;
        }
    }

    CppToken(CppToken const& o)
        : id_{o.id_} {
        if (id_ == PathToken || id_ == SizeToken) {
            data_ = o.data_;
        } else {
            data_.reset();
        }
    }

    TokenId id() const {
        return id_;
    }

    MaybeData const& data() const {
        return data_;
    }

    bool operator==(CppToken const& o) const = default;

  private:
    TokenId   id_;
    MaybeData data_;
};

std::ostream& operator<<(std::ostream& stream, CppToken const& tok) {
    stream << '{' << tok_str(tok.id());
    if (tok.data()) {
        stream << ", ";
        std::visit([&stream](auto const& x) { stream << x; }, *tok.data());
    }
    stream << '}';
    return stream;
}

std::string input_text(CppToken const& t) {
    if (t.id() == SizeToken) {
        return std::to_string(std::get<size_t>(*t.data()));
    }
    if (t.id() == PathToken) {
        return std::get<std::string>(*t.data());
    }
    std::string const s = tok_str(t.id());
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

std::string const term{"\r\n\r\n"};
std::string const ignored{"123456"};

template <std::ranges::range R>
std::string input_text(R&& r) {
    std::string out;
    for (auto const& c : r) {
        out += " " + input_text(c);
    }
    return out + term;
}

struct TokenizerDeleter {
    void operator()(Tokenizer* t) const {
        tok_destroy(t);
    }
};

using TokenizerPtr = std::unique_ptr<Tokenizer, TokenizerDeleter>;

TokenizerPtr create_tokenizer() {
    return TokenizerPtr{tok_create()};
}

ssize_t process(TokenizerPtr& tok, char const* const buffer, size_t const n) {
    return tok_process(tok.get(), buffer, n);
}

ssize_t process(TokenizerPtr& tok, std::string const& str, size_t const n) {
    return process(tok, str.data(), n);
}

ssize_t process(TokenizerPtr& tok, std::string const& str) {
    return process(tok, str, str.size());
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
    EXPECT_GT(process(tok, point.input), 0) << point.input;
    EXPECT_EQ(process(tok, ignored) == 0, point.success) << point.input;
    EXPECT_EQ(point.expected, get_tokens(tok)) << point.input;

    tok_reset(tok.get());
    for (size_t i = 0; i < point.input.size(); i += 3) {
        process(tok,
                point.input.data() + i,
                std::min(size_t{3}, point.input.size() - i));
    }
    EXPECT_EQ(process(tok, ignored) == 0, point.success) << point.input;
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
        SIMPLE_TOKEN_ID(DECL_TOKEN_ID){PathToken, "/a/b/c/d/e/f/g"},
        {SizeToken, 12},
        {PathToken, "/h/i/j/()/k/l/g123456"},
        {SizeToken, 54321}};

    std::vector<CppToken>                 bigInput;
    std::mt19937                          gen{gf::test::random_seed()};
    std::uniform_int_distribution<size_t> picker(0, baseTokens.size());
    for (size_t i = 0; i < 1025 * 1023; ++i) {
        bigInput.push_back(baseTokens[picker(gen)]);
    }

    Point const points[] = {
        {"GETFILE GET /a/path/to/a/file" + term + ignored,
         true,
         {{GetfileToken}, {GetToken}, {PathToken, "/a/path/to/a/file"}}},
        {"GETFILE OK 1234567" + term + ignored,
         true,
         {{GetfileToken}, {OkToken}, {SizeToken, 1234567}}},
        {input_text(bigInput) + ignored, true, bigInput}};
    test_points(tok, points);
}
