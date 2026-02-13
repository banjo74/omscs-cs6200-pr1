// HEADER_START
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <sys/types.h>

#include <stddef.h>

/////////////////////////////////////////////////////////
// TransferClient
/////////////////////////////////////////////////////////

typedef struct TransferClientTag TransferClient;

typedef struct TransferSinkTag TransferSink;

typedef enum {
    TransferClientSuccess,
    TransferClientFailedToResolveHost,
    TransferClientFailedToOpen,
    TransferClientFailedToReceive,
    TransferClientFailedToConnect
} TransferClientStatus;

// Create a TransferClient and resolve the host name and port and return the
// client.
//
// Upon success:
// *status == TransferClientSuccess
// A transfer client ready for tc_receive
// The returned TransferClient must be destroyed with tc_destroy
TransferClient* tc_create(TransferClientStatus* status,
                          char const*           serverName,
                          unsigned short        port);

// connect to the host provied to tc_create and retrieve the data it provides.
// retrieved data is sent to the sink.  See TransferSink below.
TransferClientStatus tc_receive(TransferClient*, TransferSink* sink);

// destroy the provided client
void tc_destroy(TransferClient*);

/////////////////////////////////////////////////////////
// TransferSink
/////////////////////////////////////////////////////////

// see TransferSink below
typedef void* (*TransferSinkStartFcn)(void* sinkData);

// see TransferSink below
typedef ssize_t (*TransferSinkSendFcn)(void*       sinkData,
                                       void*       sessionData,
                                       void const* buffer,
                                       size_t      n);

// see TransferSink below
typedef void (*TransferSinkCancelFcn)(void* sinkData, void* sessionData);

// see TransferSink below
typedef int (*TransferSinkFinishFcn)(void* sinkData, void* sessionData);

/*!
 TransferSink is an abstraction of a data sink
 */
struct TransferSinkTag {
    // session = sink->startFcn(sink->sinkData);
    // starts a data read session, the returned value may be used
    // with sendFcn.  sink_start is a convenience call to this function.
    TransferSinkStartFcn startFcn;

    // nWritten = sink->sendFcn(sink, session, buffer, nBytes)
    // sends nBytes pointed to by buffer to the sink for this session.
    // upon success nWritten == nBytes
    //
    // sink_send is a convenience call to this function
    TransferSinkSendFcn sendFcn;

    // sink->cancelFcn(sink->sinkData, session)
    // cancels the current session leaving no side effects
    //
    // sink_cancel is a convenience call to this function
    TransferSinkCancelFcn cancelFcn;

    // sink->finishFcn(sink->sinkData, session)
    // successfully finishes the session.
    //
    // sink_finish is a convenience call to this function
    TransferSinkFinishFcn finishFcn;

    // client data
    void* sinkData;
};

// initialize an empty sink with the provided start, send, cancel, finish, and
// data.
void sink_initialize(TransferSink* emptyClient,
                     TransferSinkStartFcn,
                     TransferSinkSendFcn,
                     TransferSinkCancelFcn,
                     TransferSinkFinishFcn,
                     void* sinkData);

// see startFcn in TransferSink above
void* sink_start(TransferSink* sink);

// see sendFcn in TransferSink above
ssize_t sink_send(TransferSink* sink, void*, void const*, size_t);

// see cancelFcn in TransferSink above
void sink_cancel(TransferSink* sink, void*);

// see finishFcn in TransferSink above
int sink_finish(TransferSink* sink, void*);

/////////////////////////////////////////////////////////
// FileTransferSink
/////////////////////////////////////////////////////////

// a wrapper around TransferSink to for file writing
typedef struct FileTransferSinkTag FileTransferSink;

// create a FileTransferSink ready to write to fileName
// each sink session (call to start) will write to the provided file name.
// if the sink is canceled, the file will be unlinked, even
// if it existed before writing started.
FileTransferSink* fsink_create(char const* fileName);

// get the underlying sink
TransferSink* fsink_sink(FileTransferSink*);

// destroy the sink
void fsink_destroy(FileTransferSink*);

// HEADER_END

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFSIZE 512

#define USAGE                                             \
    "usage:\n"                                            \
    "  transferclient [options]\n"                        \
    "options:\n"                                          \
    "  -p                  Port (Default: 61321)\n"       \
    "  -s                  Server (Default: localhost)\n" \
    "  -h                  Show this help message\n"      \
    "  -o                  Output file (Default cs6200.txt)\n"

#ifndef TEST_MODE
/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {{"server", required_argument, NULL, 's'},
                                       {"output", required_argument, NULL, 'o'},
                                       {"help", no_argument, NULL, 'h'},
                                       {"port", required_argument, NULL, 'p'},
                                       {NULL, 0, NULL, 0}};

/* Main ========================================================= */
int main(int argc, char** argv) {
    int               option_char = 0;
    char*             hostname    = "localhost";
    unsigned short    portno      = 61321;
    char*             filename    = "cs6200.txt";
    FileTransferSink* fsink       = NULL;
    TransferClient*   tc          = NULL;

    setbuf(stdout, NULL);

    // Parse and set command line arguments
    while ((option_char = getopt_long(
                argc, argv, "s:p:o:hx", gLongOptions, NULL)) != -1) {
        switch (option_char) {
        case 's': // server
            hostname = optarg;
            break;
        case 'p': // listen-port
            portno = atoi(optarg);
            break;
        default:
            fprintf(stderr, "%s", USAGE);
            exit(1);
        case 'o': // filename
            filename = optarg;
            break;
        case 'h': // help
            fprintf(stdout, "%s", USAGE);
            exit(0);
            break;
        }
    }

    if (NULL == hostname) {
        fprintf(stderr, "%s @ %d: invalid host name\n", __FILE__, __LINE__);
        exit(1);
    }

    if (NULL == filename) {
        fprintf(stderr, "%s @ %d: invalid filename\n", __FILE__, __LINE__);
        exit(1);
    }

    if ((portno < 1025) || (portno > 65535)) {
        fprintf(stderr,
                "%s @ %d: invalid port number (%d)\n",
                __FILE__,
                __LINE__,
                portno);
        exit(1);
    }

    TransferClientStatus status = TransferClientSuccess;
    tc                          = tc_create(&status, hostname, portno);
    if (status != TransferClientSuccess) {
        goto EXIT_POINT;
    }

    fsink = fsink_create(filename);
    if ((status = tc_receive(tc, fsink_sink(fsink))) != TransferClientSuccess) {
        goto EXIT_POINT;
    }
EXIT_POINT:
    if (tc) {
        tc_destroy(tc);
    }
    if (fsink) {
        fsink_destroy(fsink);
    }
    return status != TransferClientSuccess;
}
#endif // TEST_MODE

/////////////////////////////////////////////////////////
// TransferClient
/////////////////////////////////////////////////////////

struct TransferClientTag {
    struct addrinfo* addrInfo;
};

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

TransferClient* tc_create(TransferClientStatus* const status,
                          char const* const           serverName,
                          unsigned short const        port) {
    assert(status);
    *status = TransferClientSuccess;

    TransferClient* const tc =
        (TransferClient*)calloc(1, sizeof(TransferClient));
    tc->addrInfo = resolve_address_info_(serverName, port);
    if (!tc->addrInfo) {
        *status = TransferClientFailedToResolveHost;
        free(tc);
        return NULL;
    }
    return tc;
}

// search through address info for a socket that will accept our connection.
// returns -1 on failure.  ec->addressInfo must be valid.
// addressInfo contains several options, e.g., IPv4 vs IPv6 or aliases.  use the
// first that succeeds.
static int create_and_connect_to_socket_(struct addrinfo* ai) {
    assert(ai);
    int socketFid = -1;
    for (; ai; ai = ai->ai_next) {
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

// start a session with sink.  keep reading data from socketId and send to the
// sink.  if a receive failure happens, cancel the sink session.  on success
// finish the sink session.
static TransferClientStatus receive_and_redirect_(int const     socketId,
                                                  TransferSink* sink) {
    char  buffer[1024];
    int   rc      = -1;
    void* session = sink_start(sink);
    if (!session) {
        return TransferClientFailedToOpen;
    }
    while ((rc = recv(socketId, buffer, sizeof(buffer), 0)) > 0) {
        sink_send(sink, session, buffer, (size_t)rc);
    }
    if (rc < 0) {
        sink_cancel(sink, session);
        return TransferClientFailedToReceive;
    }
    sink_finish(sink, session);
    return TransferClientSuccess;
}

TransferClientStatus tc_receive(TransferClient* const tc, TransferSink* sink) {
    int const socket = create_and_connect_to_socket_(tc->addrInfo);
    if (socket == -1) {
        return TransferClientFailedToConnect;
    }
    TransferClientStatus const status = receive_and_redirect_(socket, sink);
    close(socket);
    return status;
}

void tc_destroy(TransferClient* tc) {
    freeaddrinfo(tc->addrInfo);
    free(tc);
}

/////////////////////////////////////////////////////////
// TransferSink
/////////////////////////////////////////////////////////

void sink_initialize(TransferSink*         sink,
                     TransferSinkStartFcn  startFcn,
                     TransferSinkSendFcn   sendFcn,
                     TransferSinkCancelFcn cancelFcn,
                     TransferSinkFinishFcn finishFcn,
                     void*                 sinkData) {
    memset(sink, 0, sizeof(TransferSink));
    sink->startFcn  = startFcn;
    sink->sendFcn   = sendFcn;
    sink->cancelFcn = cancelFcn;
    sink->finishFcn = finishFcn;
    sink->sinkData  = sinkData;
}

void* sink_start(TransferSink* sink) {
    return sink->startFcn(sink->sinkData);
}

ssize_t sink_send(TransferSink* const sink,
                  void* const         session,
                  void const* const   buffer,
                  size_t const        n) {
    return sink->sendFcn(sink->sinkData, session, buffer, n);
}

void sink_cancel(TransferSink* sink, void* session) {
    sink->cancelFcn(sink->sinkData, session);
}

int sink_finish(TransferSink* sink, void* session) {
    return sink->finishFcn(sink->sinkData, session);
}

struct FileTransferSinkTag {
    TransferSink base;
    char*        fileName;
};

/////////////////////////////////////////////////////////
// FileTransferSink
/////////////////////////////////////////////////////////

// the start function for the file transfer sink.  Opens the file.  Use STDLIB
// file API instead of system.
static void* file_sink_start_(void* const sinkData) {
    FileTransferSink* const fsink = (FileTransferSink*)sinkData;
    return fopen(fsink->fileName, "w");
}

// basically fwrite
static ssize_t file_sink_send_(void* const       sinkData,
                               void* const       sessionData,
                               void const* const buffer,
                               size_t const      nBytes) {
    (void)sinkData;
    FILE* const fh = (FILE*)sessionData;
    return (ssize_t)fwrite(buffer, 1, nBytes, fh);
}

// close the file and then remove it.  ignore remove errors.
static void file_sink_cancel_(void* const sinkData, void* const sessionData) {
    FileTransferSink* const fsink = (FileTransferSink*)sinkData;
    FILE* const             fh    = (FILE*)sessionData;
    fclose(fh);
    remove(fsink->fileName);
}

// close the file
static int file_sink_finish_(void* const sinkData, void* const sessionData) {
    (void)sinkData;
    FILE* const fh = (FILE*)sessionData;
    fclose(fh);
    return 1;
}

FileTransferSink* fsink_create(char const* fileName) {
    FileTransferSink* out =
        (FileTransferSink*)calloc(1, sizeof(FileTransferSink));
    out->fileName = strdup(fileName);
    sink_initialize(&out->base,
                    file_sink_start_,
                    file_sink_send_,
                    file_sink_cancel_,
                    file_sink_finish_,
                    out);
    return out;
}

TransferSink* fsink_sink(FileTransferSink* fsink) {
    return &fsink->base;
}

void fsink_destroy(FileTransferSink* fsink) {
    free(fsink->fileName);
    free(fsink);
}
