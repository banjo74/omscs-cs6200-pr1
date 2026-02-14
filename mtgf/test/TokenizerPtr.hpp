#ifndef gf_test_TokenizerPtr
#define gr_test_TokenizerPtr

#include "../gf-student-gflib.h"

#include <memory>
#include <stddef.h>
#include <string>

namespace gf::test {
struct TokenizerDeleter {
    void operator()(Tokenizer* t) const {
        tok_destroy(t);
    }
};

using TokenizerPtr = std::unique_ptr<Tokenizer, TokenizerDeleter>;

inline TokenizerPtr create_tokenizer() {
    return TokenizerPtr{tok_create()};
}

inline ssize_t process(TokenizerPtr&     tok,
                       char const* const buffer,
                       size_t const      n) {
    return tok_process(tok.get(), buffer, n);
}

inline ssize_t process(TokenizerPtr&      tok,
                       std::string const& str,
                       size_t const       n) {
    return process(tok, str.data(), n);
}

inline ssize_t process(TokenizerPtr& tok, std::string const& str) {
    return process(tok, str, str.size());
}
} // namespace gf::test

#endif // include guard
