/*
 *  This file is for use by students to define anything they wish.  It is used
 * by both the gf server and client implementations
 */
#ifndef __GF_STUDENT_H__
#define __GF_STUDENT_H__

#include <sys/types.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
// make these functions available to C++ tests.
extern "C" {
#endif

//////////////////////////////////////
// Tokenizer
//////////////////////////////////////

/*!
 The Tokenizer does the basic work of tokenizing the GETFILE protocol
 header.  It's more general than looking for just one instance of the protocol
 and instead just tokenizes a string into one of a set of keywords (simple
 tokens) and tokens with value: the size, and a path (starting with a /).

 The tokenizer helps abstract the state of processing the header so that
 it can be processed in incremental pieces should either side receive these
 pieces broken across several calls to recv.

 From the external perspective the tokenizer has 3 important states:
 1) Actively tokenizing
 2) Done (found the terminating sequence of characters)
 3) Invalid (found an unrecognized sequence of characters)

 Once Done or Invalid, the tokenizer will read no more tokens until
 it is reset.  tok_done and tok_invalid provide an indication of the 2'nd and
 3'rd states described above.  !tok_done && !tok_invalid means it's in the 1st
 state above.
 */
typedef struct TokenizerTag Tokenizer;

/*!
 Tokens recognized by the tokenizer.  Those in SIMPLE_TOKEN_ID correspond to
 keywords, GETFILE, GET, etc.

 Those in COMPOUND_TOKEN_ID have corresponding data.  See the Token structure
 below.
 */

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

// A Token.  All tokens have an id and the compound tokens have some data,
// stored in data.
struct TokenTag {
    // the id
    TokenId id;

    union {
        // points to a null-terminated string that is a path.  paths must start
        // with / the memory for this string is owned by the tokenizer and can
        // be invalidated if the tokenizer processes more tokens or is reset.
        char const* path;

        // the value of a size read in as a decimal value.
        size_t size;
    } data;
};

// allocate and reset a tokenizer ready to process text
Tokenizer* tok_create();

// Do the processing starting with buffer and processing up to n characters.
// Possible return values (with a non-zero n) are:
//
// Greater than zero: the number of characters processed.  This value will be
// <= n.  If the tokenizer has found the terminating sequence then tok_done
// will return true and the returned value from this function will be the
// offset into buffer that is the first character after the terminating
// sequence.
//
// 0 the tokenizer has already encountered the terminating sequence and accepts
// no more input (assuming n > 0).
//
// -1 the tokenizer encountered an invalid sequence of characters.  tok_invalid
// will return true in this case.
ssize_t tok_process(Tokenizer*, char const* buffer, size_t n);

// returns true if the tokenizer has enountered the terminating sequence of
// characters.
bool tok_done(Tokenizer const*);

// returns true if the tokenizer has encountered an invalid sequence of
// characters
bool tok_invalid(Tokenizer const*);

// return the number of tokens enountered by the tokenizer so far.
// more specifically, the number of tokens *completed* by the tokenizer so far.
// can be called at any time, even if invalid (will return the number tokens
// encountered before entering the invalid state).
size_t tok_num_tokens(Tokenizer const*);

// return the n'th token.  must be n < tok_num_tokens.
// If the token is a PathToken, the path pointed to by the token
// can by invalidated by calls to tok_reset, tok_process, or tok_destroy.
Token tok_token(Tokenizer const*, size_t n);

// reset the tokenizer to start processing a new sequence of characters
// will invalidate all previously accessed tokens.
void tok_reset(Tokenizer*);

// destroy the token
void tok_destroy(Tokenizer*);

// for debugging purposes, return a string representation of the provided token
// id.  this is not the same as how these tokens appear in header text.
char const* tok_str(TokenId);

// the string of characters that represents the end of a header
char const* tok_terminator();

/////////////////////////////////////////////////////////
// Headers
/////////////////////////////////////////////////////////

typedef struct RequestGetTag RequestGet;

// a request to the server
struct RequestGetTag {
    char const* path;
};

// print the request into the provided buffer.  printed in the protocol format
// and includes the terminating sequence of characters.
// returns like snprintf.
int snprintf_request_get(char* buffer, size_t n, RequestGet const*);

// if the Tokenizer is done and contains the expected tokens, then populate the
// provided RequestGet and return 0.  Otherwise, return -1.
// The Tokenizer must have 3 tokens: GetfileToken, GetToken, and PathToken.
int unpack_request_get(Tokenizer const* tok, RequestGet*);

typedef struct ResponseTag Response;

// Basically a duplicate of the enum in gfclient.h but need a seperate one
// because the #define's in gfserver.h will conflict with the enumerators in
// gfclient.h.
typedef enum {
    UnknownResponse = -1,
    OkResponse,
    FileNotFoundResponse,
    ErrorResponse,
    InvalidResponse,
} ResponseStatus;

// response from the server to the client.
struct ResponseTag {
    // The status of the response
    ResponseStatus status;
    // If status is OkResponse, then this is the size of the file to follow.
    size_t size;
};

// print the Response into the provided buffer.  printed in the protocol format
// and includes the terminating sequence of characters.
// returns like snprintf.
int snprintf_response(char* buffer, size_t n, Response const*);

// if the Tokenizer is done and contains the expected tokens, then populate the
// provided Response and return 0.  Otherwise, return -1.
// The Tokenizer must have 2 or 3 tokens:
//
// GetfileToken, OkToken, SizeToken
// GetfileToken, FileNotFoundToken
// GetfileToken, ErrorToken
// GetfileToken, InvalidToken
int unpack_response(Tokenizer const* tok, Response*);

/////////////////////////////////////////////////////////
// Socket Helpers
/////////////////////////////////////////////////////////

// send all data in buffer to socketId.  On success, returns the amount of data
// sent (which should be n).  On failure, returns -1.
ssize_t sock_send_all(int const            socketId,
                      uint8_t const* const buffer,
                      size_t               n);

#ifdef __cplusplus
}
#endif
#endif // __GF_STUDENT_H__
