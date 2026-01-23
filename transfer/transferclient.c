// HEADER_START
#define _POSIX_C_SOURCE 200112L
#include <sys/types.h>

#include <stddef.h>

typedef struct TransferClientTag TransferClient;

typedef struct TransferSinkTag TransferSink;

typedef enum {
    TransferClientSuccess,
    TransferClientFailedToResolveHost,
    TransferClientFailedToReceive,
    TransferClientFailedToConnect
} TransferClientStatus;

// Create a tc client and resolve the host name and port and return the client.
// Upon success:
// *statuc == TransferClientSuccess
// A transfer client ready for tc_receive
// The returned TransferClient must be destroyed with tc_destroy
TransferClient* tc_create(TransferClientStatus* status,
                          char const*           serverName,
                          unsigned short        port);

// connect to the host provied to tc_create and retrieve the data it provides.
// retrieved ata is sent to receiveFcn.
// userData is the first function
TransferClientStatus tc_receive(TransferClient*, TransferSink* receiveClient);

// destroy the provided client
void tc_destroy(TransferClient*);

typedef void* (*TransferSinkStartFcn)(void* clientData);

typedef ssize_t (*TransferSinkSendFcn)(void*       clientData,
                                       void*       sessionData,
                                       void const* buffer,
                                       size_t      n);

typedef void (*TransferSinkCancelFcn)(void* clientData, void* sessionData);

typedef int (*TransferSinkFinishFcn)(void* clientData, void* sessionData);

/*!
 */
struct TransferSinkTag {
    TransferSinkStartFcn  startFcn;
    TransferSinkSendFcn   sendFcn;
    TransferSinkCancelFcn cancelFcn;
    TransferSinkFinishFcn finishFcn;
    void*                 clientData;
};

void sink_initialize(TransferSink* emptyClient,
                     TransferSinkStartFcn,
                     TransferSinkSendFcn,
                     TransferSinkCancelFcn,
                     TransferSinkFinishFcn,
                     void* clientData);

void* sink_start(TransferSink* client);

ssize_t sink_send(TransferSink* client, void*, void const*, size_t);

void sink_cancel(TransferSink* client, void*);

int sink_finish(TransferSink* client, void*);

typedef struct FileTransferSinkTag FileTransferSink;

FileTransferSink* fsink_create(char const*);

TransferSink* fsink_client(FileTransferSink*);

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
    if ((status = tc_receive(tc, fsink_client(fsink))) !=
        TransferClientSuccess) {
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
// returns -1 on failure.  tc->addrInfo must be valid.
static int create_and_connect_to_socket_(TransferClient* const tc) {
    assert(tc->addrInfo);
    int socketFid = -1;
    for (struct addrinfo* ai = tc->addrInfo; ai; ai = ai->ai_next) {
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

static TransferClientStatus receive_and_redirect_(TransferClient* const tc,
                                                  int const     socketId,
                                                  TransferSink* receiveClient) {
    char  buffer[1024];
    int   rc          = -1;
    void* receiveData = sink_start(receiveClient);
    while ((rc = recv(socketId, buffer, sizeof(buffer), 0)) > 0) {
        sink_send(receiveClient, receiveData, buffer, (size_t)rc);
    }
    if (rc < 0) {
        sink_cancel(receiveClient, receiveData);
        return TransferClientFailedToReceive;
    }
    sink_finish(receiveClient, receiveData);
    return TransferClientSuccess;
}

TransferClientStatus tc_receive(TransferClient* const tc,
                                TransferSink*         receiveClient) {
    int const socket = create_and_connect_to_socket_(tc);
    if (socket == -1) {
        return TransferClientFailedToConnect;
    }
    TransferClientStatus const status =
        receive_and_redirect_(tc, socket, receiveClient);
    close(socket);
    return status;
}

void tc_destroy(TransferClient* tc) {
    freeaddrinfo(tc->addrInfo);
    free(tc);
}

void sink_initialize(TransferSink*         sink,
                     TransferSinkStartFcn  startFcn,
                     TransferSinkSendFcn   sendFcn,
                     TransferSinkCancelFcn cancelFcn,
                     TransferSinkFinishFcn finishFcn,
                     void*                 clientData) {
    memset(sink, 0, sizeof(TransferSink));
    sink->startFcn   = startFcn;
    sink->sendFcn    = sendFcn;
    sink->cancelFcn  = cancelFcn;
    sink->finishFcn  = finishFcn;
    sink->clientData = clientData;
}

void* sink_start(TransferSink* sink) {
    return sink->startFcn(sink->clientData);
}

ssize_t sink_send(TransferSink* const sink,
                  void* const         session,
                  void const* const   buffer,
                  size_t const        n) {
    return sink->sendFcn(sink->clientData, session, buffer, n);
}

void sink_cancel(TransferSink* sink, void* session) {
    sink->cancelFcn(sink->clientData, session);
}

int sink_finish(TransferSink* sink, void* session) {
    return sink->finishFcn(sink->clientData, session);
}

struct FileTransferSinkTag {
    TransferSink base;
    char*        fileName;
};

static void* file_receive_start_(void* const clientData) {
    FileTransferSink* const fsink = (FileTransferSink*)clientData;
    return fopen(fsink->fileName, "w");
}

static ssize_t file_receive_send_(void* const       clientData,
                                  void* const       sessionData,
                                  void const* const buffer,
                                  size_t const      nBytes) {
    (void)clientData;
    FILE* const fh = (FILE*)sessionData;
    return (ssize_t)fwrite(buffer, 1, nBytes, fh);
}

static void file_receive_cancel_(void* const clientData,
                                 void* const sessionData) {
    FileTransferSink* const fsink = (FileTransferSink*)clientData;
    FILE* const             fh    = (FILE*)sessionData;
    fclose(fh);
    remove(fsink->fileName);
}

static int file_receive_finish_(void* const clientData,
                                void* const sessionData) {
    (void)clientData;
    FILE* const fh = (FILE*)sessionData;
    fclose(fh);
    return 1;
}

FileTransferSink* fsink_create(char const* fileName) {
    FileTransferSink* out =
        (FileTransferSink*)calloc(1, sizeof(FileTransferSink));
    sink_initialize(&out->base,
                    file_receive_start_,
                    file_receive_send_,
                    file_receive_cancel_,
                    file_receive_finish_,
                    out);
    return out;
}

TransferSink* fsink_client(FileTransferSink* fsink) {
    return &fsink->base;
}

void fsink_destroy(FileTransferSink* fsink) {
    free(fsink->fileName);
    free(fsink);
}
