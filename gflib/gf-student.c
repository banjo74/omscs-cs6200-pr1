/*
 *  This file is for use by students to define anything they wish.  It is used
 * by both the gf server and client implementations
 */

#include "gf-student.h"

#include <sys/socket.h>

#include <assert.h>
#include <memory.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/////////////////////////////////////////////////////////
// Tokenizer
/////////////////////////////////////////////////////////

struct TokenizerTag {
    uint8_t state;

    // a vector of tokens.  they are added with push_token_ below.
    // there's a capacity, greater then numTokens, that is the total number of
    // tokens we can hold before realloc.  push_token_ will handle the realloc.
    // The token capacity is not reset with tok_reset.
    Token* tokens;
    size_t tokenCapacity;
    size_t numTokens;

    // buffer to store generic strings.  each generic string is written to the
    // the characters are written as they are processed.  when the end of the
    // generic string is processed, a null terminator is added, and
    // startOfCurrent moves to the next character in the buffer.
    // push_char_ adds characters to this buffer and handles realloc's if
    // needed.
    char*  buffer;
    size_t bufferCapacity;
    size_t startOfCurrent;
    size_t bufferCursor;

    // the running value of decimal numbers as they are processed.
    size_t numberValue;
};

struct ActionTag {
    // state to transition to
    uint8_t toState        : 7;
    // should we reset recording the current string or number with this
    // transition
    uint8_t resetRecording : 1;
    // token finished with this transition.  Set to UnknownToken when there is
    // no token.
    int8_t token;
};
typedef struct ActionTag Action;

Tokenizer* tok_create() {
    Tokenizer* out      = (Tokenizer*)calloc(sizeof(Tokenizer), 1);
    out->tokenCapacity  = 12;
    out->tokens         = (Token*)malloc(sizeof(Token) * out->tokenCapacity);
    out->bufferCapacity = 64;
    out->buffer         = (char*)malloc(out->bufferCapacity);
    tok_reset(out);
    return out;
}

void push_char_(Tokenizer* const tok, char const c) {
    if (tok->bufferCursor == tok->bufferCapacity) {
        // at capacity, realloc and update tokens to point into new buffer
        char const* const originalBufferPtr = tok->buffer;

        tok->bufferCapacity *= 2;
        tok->buffer = (char*)realloc(tok->buffer, tok->bufferCapacity);
        if (tok->buffer != originalBufferPtr) {
            for (size_t i = 0; i < tok->numTokens; ++i) {
                if (tok->tokens[i].id == PathToken) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuse-after-free"
                    // GCC gets a little nanny here.  we're just using the
                    // previous address to update the pointers in tokens, if
                    // any.
                    size_t const originalOffset =
                        tok->tokens[i].data.path - originalBufferPtr;
#pragma GCC diagnostic pop
                    tok->tokens[i].data.path = tok->buffer + originalOffset;
                }
            }
        }
    }
    tok->buffer[tok->bufferCursor++] = c;
}

void push_token_(Tokenizer* const tok, Token const token) {
    if (tok->numTokens == tok->tokenCapacity) {
        // at capacity, realloc
        tok->tokenCapacity *= 2;
        tok->tokens =
            (Token*)realloc(tok->tokens, sizeof(Token) * tok->tokenCapacity);
    }
    tok->tokens[tok->numTokens++] = token;
}

/*!
 The table generator identifies 3 standard states:
 0: start
 1: done
 2: invalid
 */

bool tok_done(Tokenizer const* tok) {
    return tok->state == 2;
}

bool tok_invalid(Tokenizer const* tok) {
    return tok->state == 1;
}

// get_action_ accesses the table, below.  Use the forward declaration to keep
// the table at the bottom of the file.
static Action const* get_action_(uint8_t state, char c);

ssize_t tok_process(Tokenizer* const  tok,
                    char const* const buffer,
                    size_t const      n) {
    size_t i = 0;
    for (; i < n && !tok_done(tok) && !tok_invalid(tok); ++i) {
        Action const* const action = get_action_(tok->state, buffer[i]);
        tok->state                 = action->toState;
        switch (action->token) {
        case UnknownToken:
            break;
        case GetfileToken:
        case GetToken:
        case OkToken:
        case FileNotFoundToken:
        case ErrorToken:
        case InvalidToken: {
            Token const token = {.id = action->token};
            push_token_(tok, token);
            break;
        }
        case SizeToken: {
            Token const token = {.id        = SizeToken,
                                 .data.size = tok->numberValue};
            push_token_(tok, token);
            break;
        }
        case PathToken: {
            // terminate the written path
            push_char_(tok, '\0');
            // create and push the token
            Token const token = {
                .id        = PathToken,
                .data.path = tok->buffer + tok->startOfCurrent};
            push_token_(tok, token);
            // update the start of current to be the next character in the
            // buffer.
            tok->startOfCurrent = tok->bufferCursor;
            break;
        }
        }
        if (action->resetRecording) {
            // actions tell us to reset recording.  this is either the beginning
            // of a generic word (path) or a number (size).
            tok->bufferCursor = tok->startOfCurrent;
            tok->numberValue  = 0;
        }
        // just push the character and update the number regardless of what
        // state we're in.  we could add more data to the table to tell us when
        // this is necessary but it doesn't cost us much to do it all of the
        // time and the text of the table is big enough already.  note that most
        // transitions reset recording so we not actually accumulating a lot of
        // characters.
        push_char_(tok, buffer[i]);
        tok->numberValue *= 10;
        tok->numberValue += buffer[i] - '0';
    }
    if (tok_invalid(tok)) {
        return -1;
    }
    return i;
}

size_t tok_num_tokens(Tokenizer const* const tok) {
    return tok->numTokens;
}

Token tok_token(Tokenizer const* const tok, size_t const i) {
    assert(i < tok_num_tokens(tok));
    return tok->tokens[i];
}

void tok_reset(Tokenizer* const tok) {
    tok->state          = 0;
    tok->numTokens      = 0;
    tok->startOfCurrent = 0;
    tok->bufferCursor   = 0;
    tok->numberValue    = 0;
}

void tok_destroy(Tokenizer* const tok) {
    free(tok->tokens);
    free(tok->buffer);
    free(tok);
}

char const* tok_str(TokenId const id) {
#define TOKEN_ID_STR(_ID, _TEXT) #_ID,
    static char const* const names[] = {TOKEN_ID(TOKEN_ID_STR)};
#undef TOKEN_ID_STR
    return names[id];
}

char const* tok_terminator() {
    return "\r\n\r\n";
}

char const* token_text_(TokenId const id) {
#define TOKEN_TEXT(_ID, _TEXT) _TEXT,
    static char const* const text[] = {TOKEN_ID(TOKEN_TEXT)};
#undef TOKEN_TEXT
    return text[id];
}

// sprintf a single token with a string prefix
static int snprintf_token_(char* const  buffer,
                           size_t const bufferSize,
                           char const*  prefix,
                           Token const* token) {
    switch (token->id) {
    case SizeToken:
        return snprintf(buffer, bufferSize, "%s%zu", prefix, token->data.size);
    case PathToken:
        return snprintf(buffer, bufferSize, "%s%s", prefix, token->data.path);
    default:
        return snprintf(
            buffer, bufferSize, "%s%s", prefix, token_text_(token->id));
    }
}

// compute the remaining buffer size, if written > available, return 0.
// otherwise, return available - written.
static size_t rem_(size_t const written, size_t const available) {
    return written > available ? 0 : available - written;
}

int snprintf_tokens(char* const  buffer,
                    size_t const bufferSize,
                    Token const* tokens,
                    size_t       numTokens) {
    size_t      out    = 0;
    char const* prefix = "";
    for (size_t i = 0; i < numTokens; ++i) {
        out += snprintf_token_(
            buffer + out, rem_(out, bufferSize), prefix, tokens + i);
        prefix = " ";
    }
    out +=
        snprintf(buffer + out, rem_(out, bufferSize), "%s", tok_terminator());
    return out;
}

/////////////////////////////////////////////////////////
// Headers
/////////////////////////////////////////////////////////

int snprintf_request_get(char* const       buffer,
                         size_t const      n,
                         RequestGet const* request) {
    Token const tokens[] = {{.id = GetfileToken},
                            {.id = GetToken},
                            {.id = PathToken, .data.path = request->path}};
    return snprintf_tokens(
        buffer, n, tokens, sizeof(tokens) / sizeof(tokens[0]));
}

int unpack_request_get(Tokenizer const* const tok, RequestGet* const request) {
    if (
        // must be done
        !tok_done(tok) ||
        // must have 3 tokens
        tok_num_tokens(tok) != 3 ||
        // GETFILE
        tok_token(tok, 0).id != GetfileToken ||
        // GET
        tok_token(tok, 1).id != GetToken ||
        // PATH
        tok_token(tok, 2).id != PathToken) {
        return -1;
    }
    request->path = tok_token(tok, 2).data.path;
    return 0;
}

static TokenId status_to_token_(ResponseStatus const s) {
#define OUTPUT_CASE(Id) \
    case Id##Response:  \
        return Id##Token
    switch (s) {
        OUTPUT_CASE(Ok);
        OUTPUT_CASE(FileNotFound);
        OUTPUT_CASE(Error);
        OUTPUT_CASE(Invalid);
    default:
        return UnknownToken;
    }
#undef OUTPUT_CASE
}

int snprintf_response(char* const     buffer,
                      size_t const    n,
                      Response const* response) {
    Token tokens[] = {{.id = GetfileToken},
                      {.id = status_to_token_(response->status)},
                      {.id = SizeToken, .data.size = response->size}};
    return snprintf_tokens(
        buffer, n, tokens, response->status == OkResponse ? 3 : 2);
}

/// map a TokenId to a status.  return UnknownResponse if the TokenId
/// doesn't map to any status.
static ResponseStatus token_to_status_(TokenId const id) {
#define OUTPUT_CASE(Id) \
    case Id##Token:     \
        return Id##Response
    switch (id) {
        OUTPUT_CASE(Ok);
        OUTPUT_CASE(FileNotFound);
        OUTPUT_CASE(Error);
        OUTPUT_CASE(Invalid);
    default:
        return UnknownResponse;
    }
#undef OUTPUT_CASE
}

int unpack_response(Tokenizer const* const tok, Response* const request) {
    if (
        // must be done
        !tok_done(tok) ||
        // must have at lest 2 tokens
        tok_num_tokens(tok) < 2 ||
        // GETFILE
        tok_token(tok, 0).id != GetfileToken) {
        return -1;
    }
    ResponseStatus const status = token_to_status_(tok_token(tok, 1).id);
    if (status == UnknownResponse) {
        return -1;
    }

    if (status == OkResponse) {
        if (tok_num_tokens(tok) != 3 || tok_token(tok, 2).id != SizeToken) {
            return -1;
        }
        request->size = tok_token(tok, 2).data.size;
    } else {
        if (tok_num_tokens(tok) != 2) {
            return -1;
        }
        request->size = 0;
    }
    request->status = status;
    return 0;
}

/////////////////////////////////////////////////////////
// Socket Helpers
/////////////////////////////////////////////////////////

ssize_t sock_send_all(int const            socketId,
                      uint8_t const* const buffer,
                      size_t               n) {
    ssize_t numSent = 0;
    while (numSent < n) {
        ssize_t const localSent =
            send(socketId, buffer + numSent, n - numSent, 0);
        if (localSent <= 0) {
            return -1;
        }
        numSent += localSent;
    }
    return numSent;
}

Action const* get_action_(uint8_t const state, char const c) {
    // Table generated by generator in:
    // https://github.com/banjo74/omscs-cs6200-pr1.git
    // character_class maps a character to its class
    // action_table maps state X character class to action
    static uint8_t const character_class[128] = {
        1,  0, 0,  0,  0, 0, 0,  0, 0,  0,  2,  0, 0,  3,  0,  0, 0,  0, 0,
        0,  0, 0,  0,  0, 0, 0,  0, 0,  0,  0,  0, 0,  4,  5,  5, 5,  5, 5,
        5,  5, 5,  5,  5, 5, 5,  5, 5,  6,  7,  7, 7,  7,  7,  7, 7,  7, 7,
        7,  5, 5,  5,  5, 5, 5,  5, 8,  5,  5,  9, 10, 11, 12, 5, 13, 5, 14,
        15, 5, 16, 17, 5, 5, 18, 5, 19, 20, 21, 5, 5,  5,  5,  5, 5,  5, 5,
        22, 5, 5,  5,  5, 5, 5,  5, 5,  5,  5,  5, 5,  5,  5,  5, 5,  5, 5,
        5,  5, 5,  5,  5, 5, 5,  5, 5,  5,  5,  5, 5,  0};
    static Action const action_table[44][23] = {
        {{1, 0, UnknownToken},  {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {3, 1, UnknownToken},  {1, 0, UnknownToken},
         {5, 1, UnknownToken},  {4, 1, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {13, 1, UnknownToken}, {18, 1, UnknownToken},
         {34, 1, UnknownToken}, {6, 1, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken},  {32, 1, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken},  {2, 1, UnknownToken},  {1, 0, UnknownToken},
         {41, 1, UnknownToken}, {3, 1, UnknownToken},  {1, 0, UnknownToken},
         {5, 1, UnknownToken},  {4, 1, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {13, 1, UnknownToken}, {18, 1, UnknownToken},
         {34, 1, UnknownToken}, {6, 1, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken},  {32, 1, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {2, 0, SizeToken},    {1, 0, UnknownToken},
         {41, 0, SizeToken},   {3, 0, SizeToken},    {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {4, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {2, 0, PathToken},    {1, 0, UnknownToken},
         {41, 0, PathToken},   {3, 0, PathToken},    {5, 0, UnknownToken},
         {5, 0, UnknownToken}, {5, 0, UnknownToken}, {5, 0, UnknownToken},
         {5, 0, UnknownToken}, {5, 0, UnknownToken}, {5, 0, UnknownToken},
         {5, 0, UnknownToken}, {5, 0, UnknownToken}, {5, 0, UnknownToken},
         {5, 0, UnknownToken}, {5, 0, UnknownToken}, {5, 0, UnknownToken},
         {5, 0, UnknownToken}, {5, 0, UnknownToken}, {5, 0, UnknownToken},
         {5, 0, UnknownToken}, {5, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {7, 1, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {8, 1, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {9, 1, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {10, 1, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {11, 1, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {12, 1, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}},
        {{1, 0, UnknownToken},  {2, 1, InvalidToken}, {1, 0, UnknownToken},
         {41, 1, InvalidToken}, {3, 1, InvalidToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}},
        {{1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {14, 1, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}},
        {{1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {15, 1, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {16, 1, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {17, 1, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {2, 1, ErrorToken},   {1, 0, UnknownToken},
         {41, 1, ErrorToken},  {3, 1, ErrorToken},   {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {19, 1, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {20, 1, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {21, 1, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {22, 1, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {23, 1, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {24, 1, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {25, 1, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {26, 1, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {27, 1, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {28, 1, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {29, 1, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {30, 1, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {31, 1, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}},
        {{1, 0, UnknownToken},      {2, 1, FileNotFoundToken},
         {1, 0, UnknownToken},      {41, 1, FileNotFoundToken},
         {3, 1, FileNotFoundToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},      {1, 0, UnknownToken},
         {1, 0, UnknownToken},      {1, 0, UnknownToken},
         {1, 0, UnknownToken},      {1, 0, UnknownToken},
         {1, 0, UnknownToken},      {1, 0, UnknownToken},
         {1, 0, UnknownToken},      {1, 0, UnknownToken},
         {1, 0, UnknownToken},      {1, 0, UnknownToken},
         {1, 0, UnknownToken},      {1, 0, UnknownToken},
         {1, 0, UnknownToken},      {1, 0, UnknownToken},
         {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {33, 1, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {2, 1, OkToken},      {1, 0, UnknownToken},
         {41, 1, OkToken},     {3, 1, OkToken},      {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {35, 1, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {36, 1, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {2, 1, GetToken},     {1, 0, UnknownToken},
         {41, 1, GetToken},    {3, 1, GetToken},     {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {37, 1, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {38, 1, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {39, 1, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {40, 1, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken},  {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken},  {2, 1, GetfileToken}, {1, 0, UnknownToken},
         {41, 1, GetfileToken}, {3, 1, GetfileToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken}, {42, 1, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}},
        {{1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {43, 1, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken},  {1, 0, UnknownToken}},
        {{1, 0, UnknownToken}, {1, 0, UnknownToken}, {2, 1, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}, {1, 0, UnknownToken},
         {1, 0, UnknownToken}, {1, 0, UnknownToken}}};
    if (c >= sizeof(character_class) / sizeof(character_class[0])) {
        return NULL;
    }
    return &action_table[state][character_class[(size_t)c]];
}
