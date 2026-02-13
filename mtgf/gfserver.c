#define _POSIX_C_SOURCE 200112L

#include <sys/types.h>

#include <stddef.h>
// gfserver not standalone
#include "gfserver.h"
// gfserver.h not standalone
#include "gf-student-gflib.h"
#include "gfserver-student.h"

#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// name this type
typedef gfh_error_t (*HandlerFcn)(gfcontext_t**, char const*, void*);

struct gfserver_t {

    // configuration

    unsigned short portNumber; // which port to serve on?

    HandlerFcn handlerFcn;    // our handler
    void*      handlerFcnArg; // and its arg

    ContinueFcn continueFcn;    // our continue fcn
    void*       continueFcnArg; // and its arg

    size_t maxPending; // the maximum pending connections

    // some internal data

    Tokenizer*     tokenizer; // used to tokenize headers
    struct timeval timeout;   // time between calls to continueFcn

    // network data

    struct addrinfo* addrInfo;     // the addrinfo to destroy
    struct addrinfo* usedAddrInfo; // the one to accept on

    int    socketId;  // the socket we're listening on
    fd_set listenSet; // an fd_set containing just the listening socket.
};

//////////////////////////////////////////////////////////
// creation
//////////////////////////////////////////////////////////

// turn the provided number of microseconds into a timeval
static struct timeval make_timeval_(suseconds_t usec) {
    struct timeval tv;
    memset(&tv, 0, sizeof(tv));
    tv.tv_sec  = usec / 1000;
    tv.tv_usec = usec % 1000;
    return tv;
}

// create a server
gfserver_t* gfserver_create() {
    gfserver_t* out = (gfserver_t*)calloc(1, sizeof(gfserver_t));
    // user data
    out->portNumber = 61321;
    out->maxPending = 64;

    // internal data
    out->timeout   = make_timeval_(1000);
    out->tokenizer = tok_create();

    // network data
    out->socketId = -1;

    return out;
}

//////////////////////////////////////////////////////////
// setters
//////////////////////////////////////////////////////////

void gfserver_set_port(gfserver_t** const gfs, unsigned short const port) {
    (*gfs)->portNumber = port;
}

unsigned short gfserver_port(gfserver_t** gfs) {
    return (*gfs)->portNumber;
}

void gfserver_set_continue_fcn(gfserver_t** const gfs,
                               ContinueFcn        continueFcn,
                               void* const        continueArg) {
    (*gfs)->continueFcn    = continueFcn;
    (*gfs)->continueFcnArg = continueArg;
}

void gfserver_set_handlerarg(gfserver_t** gfs, void* arg) {
    (*gfs)->handlerFcnArg = arg;
}

void gfserver_set_handler(gfserver_t** gfs, HandlerFcn handlerFcn) {
    (*gfs)->handlerFcn = handlerFcn;
}

void gfserver_set_maxpending(gfserver_t** gfs, int maxPending) {
    (*gfs)->maxPending = maxPending;
}

//////////////////////////////////////////////////////////
// start listening
//////////////////////////////////////////////////////////

// return true if server is already in listening mode
bool listening_(gfserver_t* server) {
    assert(server->socketId == -1 || server->addrInfo != NULL);
    assert(server->socketId == -1 || server->usedAddrInfo != NULL);
    return server->socketId != -1;
}

// Wrapper around getaddrinfo.  Resolve addrinfo struct for serverName and port
// using getaddrinfo mainly to support IPv4 and IPv6 plus other functions are
// deprecated.
static struct addrinfo* resolve_address_info_(char const*          hostName,
                                              unsigned short const port) {
    char portNumStr[16];
    snprintf(portNumStr, sizeof(portNumStr), "%d", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));

    hints.ai_family   = AF_UNSPEC;   // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags    = AI_PASSIVE;  // use this host name

    struct addrinfo* newAddressInfo;
    if (getaddrinfo(hostName, portNumStr, &hints, &newAddressInfo)) {
        return NULL;
    }
    return newAddressInfo;
}

// avoid socket already in use errors when binding by marking socket reusable.
static void set_socket_reusable_(int const socketFd) {
    int const optionValue = 1;
    setsockopt(
        socketFd, SOL_SOCKET, SO_REUSEADDR, &optionValue, sizeof(optionValue));
}

// search through address info for a socket that will accept our connection.
// sets *usedAddrOut to the first address to which we can successfully bind and
// returns the socketId.  If no address can be bound to, return -1 and
// *usedAddrOut is unmodified.
static int create_and_bind_to_socket_(struct addrinfo*        ai,
                                      struct addrinfo** const usedAddr) {
    assert(ai);
    int socketFd = -1;
    for (; ai; ai = ai->ai_next) {
        if ((socketFd = socket(
                 ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == -1) {
            continue;
        }
        set_socket_reusable_(socketFd);
        if (bind(socketFd, ai->ai_addr, ai->ai_addrlen) == -1) {
            close(socketFd);
            continue;
        }
        *usedAddr = ai;
        return socketFd;
    }
    return -1;
}

// stop listening: close the socket, and free any address information
static void unlisten_(gfserver_t* const gfs) {
    if (gfs->socketId != -1) {
        close(gfs->socketId);
        gfs->socketId = -1;
    }
    if (gfs->addrInfo != NULL) {
        freeaddrinfo(gfs->addrInfo);
        gfs->addrInfo     = NULL;
        gfs->usedAddrInfo = NULL;
    }
}

// actually enter listening mode
static int listen_(gfserver_t* const gfs) {
    int status    = 0;
    gfs->addrInfo = resolve_address_info_("localhost", gfs->portNumber);
    if ((gfs->socketId = create_and_bind_to_socket_(
             gfs->addrInfo, &gfs->usedAddrInfo)) == -1) {
        status = -1;
        goto EXIT_POINT;
    }
    assert(gfs->usedAddrInfo);

    // start listening.
    if (listen(gfs->socketId, gfs->maxPending) == -1) {
        status = -1;
        goto EXIT_POINT;
    }

    // put our socket in the listening set, used later with select
    FD_SET(gfs->socketId, &gfs->listenSet);

EXIT_POINT:
    if (status != 0) {
        unlisten_(gfs);
    }
    return status;
}

int gfserver_listen(gfserver_t** const gfs) {
    return listen_(*gfs);
}

//////////////////////////////////////////////////////////
// serving
//////////////////////////////////////////////////////////

// if continueFcn is null, return true.  otherwise, return
// continueFcn(continueFcnArg)
static bool continue_(gfserver_t* const gfs) {
    return gfs->continueFcn == NULL || gfs->continueFcn(gfs->continueFcnArg);
}

// wait gfs->timeout for a connect.  returns false on time out or error. returns
// true otherwise.
static bool select_(gfserver_t* const gfs) {
    // both of these are written to by select, so make local copies
    fd_set         localFdSet   = gfs->listenSet;
    struct timeval localTimeout = gfs->timeout;
    // zero just means it timed out
    return select(gfs->socketId + 1, &localFdSet, NULL, NULL, &localTimeout) >
           0;
}

// cleanly shutdown our end.  indicate to the reader that there's nothing more
// coming and then flush our read buffer
static void shutdown_(int const      acceptedSocketId,
                      uint8_t* const buffer,
                      size_t const   bufferSize) {
    shutdown(acceptedSocketId, SHUT_WR);
    while (recv(acceptedSocketId, buffer, bufferSize, 0) > 0) {
    }
    close(acceptedSocketId);
}

// send the serialized version of Response to the client.  the buffer is
// provided just as a scratch to avoid each of these functions shoving a buffer
// on the stack.
static ssize_t send_response_(int const             acceptedSocketId,
                              Response const* const response,
                              uint8_t* const        buffer,
                              size_t const          bufferSize) {
    int const numWritten =
        snprintf_response((char*)buffer, bufferSize, response);
    assert(numWritten < bufferSize - 1);
    return sock_send_all(acceptedSocketId, buffer, (size_t)numWritten);
}

// send an error response to the client and shut down the socket.
static ssize_t send_error_and_shutdown_(int const            acceptedSocketId,
                                        ResponseStatus const error,
                                        uint8_t* const       buffer,
                                        size_t               bufferSize) {
    Response const response = {.status = error};
    ssize_t const  out =
        send_response_(acceptedSocketId, &response, buffer, bufferSize);
    shutdown_(acceptedSocketId, buffer, bufferSize);
    return out;
}

// create a context ready to pass to the handler.
// the created context will own the connection and handle closing it when the
// time comes.
static gfcontext_t* ctx_create_(int acceptedSocketId);

// once we're connected and know what file we're requesting, create a context
// and call the handler.
static gfh_error_t call_handler_(gfserver_t* const gfs,
                                 int const         acceptedSocketId,
                                 char const* const path) {
    // create a context
    gfcontext_t* ctx = ctx_create_(acceptedSocketId);
    // and call the handler
    return gfs->handlerFcn(&ctx, path, gfs->handlerFcnArg);
}

// do the main conneciton handling
// 1) reads the request header
// 2) if there's a problem, respond with invalid or error
// 3) if no problem, create a context and call the handler.
static int handle_connection_(gfserver_t* const gfs,
                              int const         acceptedSocketId) {
    ssize_t numRead      = 0;
    ssize_t numProcessed = 0;
    uint8_t buffer[1024];
    // reset tokenizer and read and tokenize as we go.
    tok_reset(gfs->tokenizer);
    // very important to check that the tokenizer isn't done before trying to
    // read.
    while (!tok_done(gfs->tokenizer) && !tok_invalid(gfs->tokenizer) &&
           (numRead = recv(acceptedSocketId, buffer, sizeof(buffer), 0)) > 0) {
        numProcessed = tok_process(gfs->tokenizer, (char*)buffer, numRead);
    }
    // something bad happened, tell the client so.
    if (numRead < 0 || !gfs->handlerFcn) {
        send_error_and_shutdown_(
            acceptedSocketId, ErrorResponse, buffer, sizeof(buffer));
        return -1;
    }
    // determine the request
    RequestGet request;
    if (numProcessed < numRead ||
        unpack_request_get(gfs->tokenizer, &request) != 0) {
        // malformed request, respond invalid and shutdown.
        send_error_and_shutdown_(
            acceptedSocketId, InvalidResponse, buffer, sizeof(buffer));
        return -1;
    }

    if (call_handler_(gfs, acceptedSocketId, request.path) != GF_OK) {
        // what am I supposed to do with this error?
    }
    return 0;
}

static void serve_(gfserver_t* const gfs) {
    // get into listening mode if we're not there already
    if (!listening_(gfs) && listen_(gfs) == -1) {
        return;
    }
    while (continue_(gfs)) {
        // wait for a connection
        if (!select_(gfs)) {
            continue;
        }

        // accept the connection
        int const acceptedSocket = accept(gfs->socketId,
                                          gfs->usedAddrInfo->ai_addr,
                                          &gfs->usedAddrInfo->ai_addrlen);
        // this would be rare
        if (acceptedSocket == -1) {
            continue;
        }

        // and handle it
        handle_connection_(gfs, acceptedSocket);
    }
}

void gfserver_serve(gfserver_t** gfs) {
    serve_(*gfs);
}

//////////////////////////////////////////////////////////
// destroy
//////////////////////////////////////////////////////////

void gfserver_destroy(gfserver_t** server) {
    unlisten_(*server);
    tok_destroy((*server)->tokenizer);
    free(*server);
    *server = NULL;
}

//////////////////////////////////////////////////////////
// context
//////////////////////////////////////////////////////////

struct gfcontext_t {
    // the socket we're talking to
    int acceptedSocketId;
    // if we get as far as the OK header, set these to know how much we're going
    // to send and how much we've sent thus far.  send requires that these be
    // set and will close things down once we send all of the data.
    ssize_t expectSent;
    size_t  sentSoFar;

    // scratch buffer
    uint8_t buffer[1024];
};

// given one of the macros in the header return the corresponding
// ResponseStatus.  See comments for ResponseStatus for why it exists.
ResponseStatus gfstatus_to_response_status_(gfstatus_t const in) {
    switch (in) {
    case GF_OK:
        return OkResponse;
    case GF_FILE_NOT_FOUND:
        return FileNotFoundResponse;
    case GF_ERROR:
        return ErrorResponse;
    case GF_INVALID:
        return InvalidResponse;
    }
    return UnknownResponse;
}

static gfcontext_t* ctx_create_(int const acceptedSocketId) {
    gfcontext_t* out      = (gfcontext_t*)calloc(1, sizeof(gfcontext_t));
    out->acceptedSocketId = acceptedSocketId;
    // initially the context doesn't know how much its gonna send.
    // the handler needs to call send_header first
    out->expectSent = -1;
    out->sentSoFar  = -1;
    return out;
}

static void ctx_destroy_(gfcontext_t* ctx) {
    free(ctx);
}

static gfcontext_t* ctx_send_header_(gfcontext_t* const ctx,
                                     gfstatus_t const   status,
                                     size_t const       fileLen,
                                     ssize_t* const     out) {
    ResponseStatus const rstatus = gfstatus_to_response_status_(status);
    assert(rstatus != UnknownResponse);
    if (rstatus != OkResponse) {
        // something wrong.  send an error and close things down.
        *out = send_error_and_shutdown_(
            ctx->acceptedSocketId, rstatus, ctx->buffer, sizeof(ctx->buffer));
        ctx_destroy_(ctx);
        return NULL;
    }
    Response const response = {.status = OkResponse, .size = fileLen};
    *out                    = send_response_(
        ctx->acceptedSocketId, &response, ctx->buffer, sizeof(ctx->buffer));
    // initialize what we're going to send and get ready
    ctx->expectSent = (ssize_t)fileLen;
    ctx->sentSoFar  = 0;
    return ctx;
}

// shudown the connection and destroy the context
static gfcontext_t* ctx_shutdown_and_destroy_(gfcontext_t* const ctx) {
    shutdown_(ctx->acceptedSocketId, ctx->buffer, sizeof(ctx->buffer));
    ctx_destroy_(ctx);
    return NULL;
}

static gfcontext_t* ctx_send_(gfcontext_t* const ctx,
                              void const* const  buffer,
                              size_t const       num,
                              ssize_t* const     numSent) {
    // should have gotten the header already and shouldn't be writing more than
    // we expect to.
    assert(ctx->expectSent >= 0);
    assert(ctx->sentSoFar + num <= (size_t)ctx->expectSent);

    // send the data.
    *numSent = sock_send_all(ctx->acceptedSocketId, buffer, num);
    if (*numSent == (ssize_t)num) {
        // successful send, update data sent
        ctx->sentSoFar += *numSent;
        if (ctx->sentSoFar == ctx->expectSent) {
            return ctx_shutdown_and_destroy_(ctx);
        }
    }
    return ctx;
}

static gfcontext_t* ctx_abort_(gfcontext_t* const ctx) {
    return ctx_shutdown_and_destroy_(ctx);
}

void gfs_abort(gfcontext_t** const ctx) {
    *ctx = ctx_abort_(*ctx);
}

ssize_t gfs_send(gfcontext_t** const ctx,
                 void const* const   data,
                 size_t const        len) {
    ssize_t out;
    *ctx = ctx_send_(*ctx, data, len, &out);
    return out;
}

ssize_t gfs_sendheader(gfcontext_t** const ctx,
                       gfstatus_t const    status,
                       size_t const        fileLen) {
    ssize_t out = 0;
    *ctx        = ctx_send_header_(*ctx, status, fileLen, &out);
    return out;
}
