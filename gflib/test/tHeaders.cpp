#include "../gf-student.h"
#include "TokenizerPtr.hpp"
#include "terminator.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace gf::test;

TEST(RequestGet, RoundTrip) {
    RequestGet const requests[] = {
        {"/a/b/c/d"},
        {"/d/e/f/g"},
    };
    auto tok = create_tokenizer();
    for (auto const& request : requests) {
        char       buffer[1024];
        auto const n = snprintf_request_get(buffer, sizeof(buffer), &request);
        EXPECT_THAT(buffer,
                    testing::MatchesRegex(std::string{"GETFILE\\s+GET\\s+"} +
                                          request.path + terminator))
            << request.path;

        tok_reset(tok.get());
        process(tok, buffer, static_cast<size_t>(n));
        RequestGet out;
        EXPECT_EQ(unpack_request_get(tok.get(), &out), 0);
        EXPECT_STREQ(out.path, request.path);
    }
}

TEST(RequestGet, InvalidUnpack) {
    std::string const invalidRequests[] = {
        "not recognized",
        "GETFILE GET /a/b/c/d",                 // no terminator
        "GETFILE GET" + terminator,              // too few
        "OK GET /a/b/c/d" + terminator,          // not GETFILE
        "GETFILE OK /a/b/c/d" + terminator,      // not GET
        "GETFILE GET 123456" + terminator,       // not a path
        "GETFILE GET GET /a/b/c/d" + terminator, // too many
    };
    auto tok = create_tokenizer();
    for (auto const& s : invalidRequests) {
        tok_reset(tok.get());
        process(tok, s);
        RequestGet out;
        memset(&out, 0, sizeof(out));
        EXPECT_LT(unpack_request_get(tok.get(), &out), 0);
    }
}

TEST(Response, RoundTrip) {
    Response const responses[] = {
        {OkResponse, 123456},
        {ErrorResponse, 0},
        {FileNotFoundResponse, 0},
        {InvalidResponse, 0},
    };
    auto tok = create_tokenizer();
    for (auto const& response : responses) {
        char       buffer[1024];
        auto const n = snprintf_response(buffer, sizeof(buffer), &response);
        EXPECT_THAT(
            buffer,
            testing::MatchesRegex(std::string{"GETFILE\\s+((OK\\s+[0-9]+)|"
                                              "ERROR|FILE_NOT_FOUND|INVALID)"} +
                                  terminator));

        tok_reset(tok.get());
        process(tok, buffer, static_cast<size_t>(n));
        Response out;
        EXPECT_EQ(unpack_response(tok.get(), &out), 0) << buffer;
        EXPECT_EQ(out.status, response.status) << buffer;
        EXPECT_EQ(out.size, response.size) << buffer;
    }
}

TEST(Response, InvalidUnpack) {
    std::string const invalidResponses[] = {
        "not recognized",
        "GETFILE OK 123456",                  // no terminator
        "GETFILE INVALID",                    // no terminator
        "GETFILE" + terminator,                // too few
        "GET OK 123456" + terminator,          // invalid first
        "GET INVALID" + terminator,            // invalid first
        "GETFILE GET" + terminator,            // invalid second
        "GETFILE GET 123456" + terminator,     // invalid second
        "GETFILE GET" + terminator,            // invalid second
        "GETFILE INVALID 123456" + terminator, // size not expected
        "GETFILE OK /a/b/c/d" + terminator,    // size expected
        "GETFILE OK OK 123456" + terminator,   // too many
    };
    auto tok = create_tokenizer();
    for (auto const& s : invalidResponses) {
        tok_reset(tok.get());
        process(tok, s);
        Response out;
        memset(&out, 0, sizeof(out));
        EXPECT_LT(unpack_response(tok.get(), &out), 0);
    }
}
