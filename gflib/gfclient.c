
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200112L

#include "gfclient-student.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <assert.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Modify this file to implement the interface specified in
// gfclient.h.

typedef void (*HeaderFcn)(void* buffer, size_t, void* arg);
typedef void (*WriteFcn)(void* buffer, size_t, void* arg);

struct gfcrequest_t {
    // user data
    char*          server;
    unsigned short port;
    char*          path;

    HeaderFcn headerFcn;
    void*     headerFcnArg;

    WriteFcn writeFcn;
    void*    writeFcnArg;

    // internal state
    // the final status after perform_
    gfstatus_t status;
    // the length of the file as told by the response from the server
    size_t fileLen;
    // the total number of bytes received, which may be less than fileLen
    size_t bytesReceived;
};

// optional function for cleaup processing.
void gfc_cleanup(gfcrequest_t** gfc) {
    free((*gfc)->server);
    free((*gfc)->path);
    free(*gfc);
    *gfc = NULL;
}

gfcrequest_t* gfc_create() {
    gfcrequest_t* out = (gfcrequest_t*)calloc(sizeof(gfcrequest_t), 1);

    out->server = strdup("localhost");
    out->port   = 12345;
    out->path   = strdup("/randomfile");

    return out;
}

void gfc_set_path(gfcrequest_t** gfc, char const* path) {
    free((*gfc)->path);
    (*gfc)->path = strdup(path);
}

void gfc_set_headerfunc(gfcrequest_t** gfc, HeaderFcn headerFcn) {
    (*gfc)->headerFcn = headerFcn;
}

void gfc_set_server(gfcrequest_t** gfc, char const* server) {
    free((*gfc)->server);
    (*gfc)->server = strdup(server);
}

void gfc_set_port(gfcrequest_t** gfc, unsigned short port) {
    (*gfc)->port = port;
}

void gfc_set_writearg(gfcrequest_t** gfc, void* writeFcnArg) {
    (*gfc)->writeFcnArg = writeFcnArg;
}

void gfc_set_headerarg(gfcrequest_t** gfc, void* headerFcnArg) {
    (*gfc)->headerFcnArg = headerFcnArg;
}

void gfc_set_writefunc(gfcrequest_t** gfc, WriteFcn writeFcn) {
    (*gfc)->writeFcn = writeFcn;
}

// Wrapper around getaddrinfo.  Resolve addrinfo struct for serverName and port
// using getaddrinfo mainly to support IPv4 and IPv6 plus other functions are
// deprecated.
static struct addrinfo* resolve_address_info_(char const* const    serverName,
                                              unsigned short const port) {
    char portNumStr[16];
    snprintf(portNumStr, sizeof(portNumStr), "%d", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));

    hints.ai_family   = AF_UNSPEC;    // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP
    hints.ai_flags    = AI_CANONNAME; // Canonicalize name of host, for giggles

    struct addrinfo* newAddressInfo;
    if (getaddrinfo(serverName, portNumStr, &hints, &newAddressInfo)) {
        return NULL;
    }
    return newAddressInfo;
}

// search through address info for a socket that will accept our connection.
// returns -1 on failure.  ec->addressInfo must be valid.
// addressInfo contains several options, e.g., IPv4 vs IPv6 or aliases.  use the
// first that succeeds.
static int create_and_connect_to_socket_(
    struct addrinfo const* const addrInfo) {
    int socketFid = -1;
    for (struct addrinfo const* ai = addrInfo; ai; ai = ai->ai_next) {
        if ((socketFid = socket(
                 ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == -1) {
            continue;
        }
        if (connect(socketFid, ai->ai_addr, ai->ai_addrlen) == -1) {
            close(socketFid);
            continue;
        }
        return socketFid;
    }
    return -1;
}

// send the request for path to the server on socketId.  Takes a scratch buffer
// to avoid a bunch of buffers on the stack.
static ssize_t send_request_(int const         socketId,
                             char const* const path,
                             uint8_t* const    buffer,
                             size_t const      bufferSize) {
    RequestGet const request = {.path = path};
    ssize_t const n = snprintf_request_get((char*)buffer, bufferSize, &request);
    return sock_send_all(socketId, buffer, n);
}

// the minimum value of a and b
static size_t min_(size_t const a, size_t const b) {
    return a > b ? b : a;
}

// read the response from the server on socketId.
// upon success:
//     * *response will be populated with the reponse information
//     * *tail will point to an offset in buffer after the last bytes of the
//       header.
//     * *tailSize will be the number of read bytes in the buffer, beyond *tail.
//     * gfc->headerFcn will be invoked with the full text of the header
//     * return 0
// upon failure:
//     * returns -1
//     * may modify output arguments but with invalid data
static int read_response_(gfcrequest_t*  gfc,
                          int const      socketId,
                          uint8_t* const buffer,
                          size_t const   bufferSize,
                          Response*      responseOut,
                          void** const   tailOut,
                          size_t* const  tailSizeOut) {
    Tokenizer* tok = tok_create();
    // this whole thing would be a lot cleaner if we didn't have headerFcn or it
    // could be written to incrementally.
    char   header[1024];
    size_t numWrittenToHeader = 0;
    int    status             = 0;
    while (!tok_done(tok) && !tok_invalid(tok)) {
        ssize_t const numRead = recv(socketId, buffer, bufferSize, 0);
        if (numRead == 0) {
            // socket closed, see what we've got, below
            break;
        }
        if (numRead < 0) {
            // read failure
            status = -1;
            goto EXIT_POINT;
        }
        ssize_t const numProcessed =
            tok_process(tok, (char*)buffer, (size_t)numRead);
        if (numProcessed > 0) {
            // update the tail and the tailSize, if we read again, these will be
            // updated.
            *tailOut     = buffer + numProcessed;
            *tailSizeOut = numRead - numProcessed;

            // copy data to header buffer to satisfy the header callback.
            // but don't overflow the header buffer.
            size_t const numToWriteToHeader =
                min_(sizeof(header) - numWrittenToHeader, numProcessed);
            memcpy(header + numWrittenToHeader, buffer, numToWriteToHeader);
            numWrittenToHeader += numToWriteToHeader;
        }
    }

    // okay, we've tokenized a header but we don't know if its the right tokens.
    // unpack it.
    status = unpack_response(tok, responseOut);
    if (status == 0 && gfc->headerFcn) {
        // we successfully unpacked so write to header
        gfc->headerFcn(header, numWrittenToHeader, gfc->headerFcnArg);
    }
EXIT_POINT:
    tok_destroy(tok);
    return status;
}

// send data to the write fcn provided by the client, since all data that we
// send to the client goes through here, also update bytesReceived after each
// successful write.
static void write_(gfcrequest_t* gfc, void* const buffer, size_t const n) {
    assert(gfc->bytesReceived + n <= gfc->fileLen);
    if (gfc->writeFcn) {
        gfc->writeFcn(buffer, n, gfc->writeFcnArg);
    }
    gfc->bytesReceived += n;
}

// keep reading data from the server on socketId until we've gotten gfc->fileLen
// bytes, the socket closes, or we get a read error.  write all received data to
// the client function.
static int finish_receiving_(gfcrequest_t* gfc,
                             int const     socketId,
                             void* const   buffer,
                             size_t const  n) {
    while (gfc->bytesReceived < gfc->fileLen) {
        ssize_t const numReceived = recv(
            socketId, buffer, min_(n, gfc->fileLen - gfc->bytesReceived), 0);
        if (numReceived < 0) {
            return -1;
        }
        if (numReceived == 0) {
            break;
        }
        write_(gfc, buffer, (size_t)numReceived);
    }
    return 0;
}

static int perform_(gfcrequest_t* gfc) {
    int status   = 0;
    int socketId = -1;

    // resolve the server and connect
    struct addrinfo* const addrInfo =
        resolve_address_info_(gfc->server, gfc->port);

    if (!addrInfo) {
        status = -1;
        goto EXIT_POINT;
    }
    if ((socketId = create_and_connect_to_socket_(addrInfo)) <= 0) {
        status = -1;
        goto EXIT_POINT;
    }

    // send the request
    uint8_t buffer[1024];
    if (send_request_(socketId, gfc->path, buffer, sizeof(buffer)) < 0) {
        status = -1;
        goto EXIT_POINT;
    }

    // get the response
    Response response;
    void*    tail     = NULL;
    size_t   tailSize = 0;
    status            = read_response_(
        gfc, socketId, buffer, sizeof(buffer), &response, &tail, &tailSize);
    if (status != 0) {
        gfc->status = GF_INVALID;
        goto EXIT_POINT;
    }

    switch (response.status) {
    case OkResponse: {
        gfc->status  = GF_OK;
        gfc->fileLen = response.size;
        // write the tail in the buffer that wasn't part of the header.  for
        // safety, if we happen to have more bytes in the tail than we expect,
        // just write the expected.  this would only happen if the server sent
        // us too much.
        write_(gfc, tail, min_(tailSize, gfc->fileLen));
        // now receive and write the rest
        status = finish_receiving_(gfc, socketId, buffer, sizeof(buffer));
        // update the *return* status to be -1 if we didn't get everything we
        // expected.
        status = gfc->fileLen != gfc->bytesReceived ? -1 : status;
        break;
    }

    // the other cases
    case InvalidResponse:
        gfc->status = GF_INVALID;
        status      = 0;
        break;
    case FileNotFoundResponse:
        gfc->status = GF_FILE_NOT_FOUND;
        status      = 0;
        break;
    case ErrorResponse:
        gfc->status = GF_ERROR;
        status      = 0;
        break;
    default:
        status = -1;
        break;
    };

EXIT_POINT:
    if (addrInfo != NULL) {
        freeaddrinfo(addrInfo);
    }
    if (socketId != -1) {
        close(socketId);
    }
    return status;
}

int gfc_perform(gfcrequest_t** gfc) {
    return perform_(*gfc);
}

size_t gfc_get_filelen(gfcrequest_t** gfc) {
    return (*gfc)->fileLen;
}

size_t gfc_get_bytesreceived(gfcrequest_t** gfc) {
    return (*gfc)->bytesReceived;
}

gfstatus_t gfc_get_status(gfcrequest_t** gfc) {
    return (*gfc)->status;
}

void gfc_global_init() {}

void gfc_global_cleanup() {}

char const* gfc_strstatus(gfstatus_t status) {
    char const* strstatus = "UNKNOWN";

    switch (status) {

    case GF_OK: {
        strstatus = "OK";
    } break;

    case GF_FILE_NOT_FOUND: {
        strstatus = "FILE_NOT_FOUND";
    } break;

    case GF_INVALID: {
        strstatus = "INVALID";
    } break;

    case GF_ERROR: {
        strstatus = "ERROR";
    } break;
    }

    return strstatus;
}
