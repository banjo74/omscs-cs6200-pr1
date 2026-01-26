/*
 *  This file is for use by students to define anything they wish.  It is used
 * by both the gf server and client implementations
 */
#ifndef __GF_STUDENT_H__
#define __GF_STUDENT_H__

#include <sys/types.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////
// Tokenizer
/////////////////////////////////////////////////////////

typedef struct TokenizerTag Tokenizer;

#define SIMPLE_TOKEN_ID(_X) \
    _X(Getfile)             \
    _X(Get)                 \
    _X(Ok)                  \
    _X(FileNotFound)        \
    _X(Error)               \
    _X(Invalid)

#define COMPOUND_TOKEN_ID(_X) \
    _X(Size)                  \
    _X(Path)

#define TOKEN_ID(_X) SIMPLE_TOKEN_ID(_X) COMPOUND_TOKEN_ID(_X)

#define DECL_TOKEN_ID(_ID) _ID##Token,

typedef enum { UnknownToken = -1, TOKEN_ID(DECL_TOKEN_ID) } TokenId;

typedef struct TokenTag Token;

struct TokenTag {
    TokenId id;

    union {
        char const* path;
        size_t      size;
    } data;
};

Tokenizer* tok_create();

ssize_t tok_process(Tokenizer*, char const* buffer, size_t n);

size_t tok_num_tokens(Tokenizer const*);

Token tok_token(Tokenizer const*, size_t n);

void tok_reset(Tokenizer*);

void tok_destroy(Tokenizer*);

char const* tok_str(TokenId);

#ifdef __cplusplus
}
#endif
#endif // __GF_STUDENT_H__
