// HEADER_START
#define _POSIX_C_SOURCE 200112L
#include <sys/types.h>

#include <stddef.h>

typedef struct TransferClientTag TransferClient;

typedef struct ReceiveClientTag ReceiveClient;

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
TransferClientStatus tc_receive(TransferClient*, ReceiveClient* receiveClient);

// destroy the provided client
void tc_destroy(TransferClient*);

typedef void* (*ReceiveClientStartFcn)(void* clientData);

typedef ssize_t (*ReceiveClientSendFcn)(void*       clientData,
                                        void*       sessionData,
                                        void const* buffer,
                                        size_t      n);

typedef void (*ReceiveClientCancelFcn)(void* clientData, void* sessionData);

typedef int (*ReceiveClientFinishFcn)(void* clientData, void* sessionData);

/*!
 */
struct ReceiveClientTag {
    ReceiveClientStartFcn  startFcn;
    ReceiveClientSendFcn   sendFcn;
    ReceiveClientCancelFcn cancelFcn;
    ReceiveClientFinishFcn finishFcn;
    void*                  clientData;
};

void rc_initialize(ReceiveClient* emptyClient,
                   ReceiveClientStartFcn,
                   ReceiveClientSendFcn,
                   ReceiveClientCancelFcn,
                   ReceiveClientFinishFcn,
                   void* clientData);

void* rc_start(ReceiveClient* client);

ssize_t rc_send(ReceiveClient* client, void*, void const*, size_t);

void rc_cancel(ReceiveClient* client, void*);

int rc_finish(ReceiveClient* client, void*);

typedef struct FileReceiveClientTag FileReceiveClient;

FileReceiveClient* frc_create(char const*);

ReceiveClient* frc_client(FileReceiveClient*);

void frc_destroy(FileReceiveClient*);

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
    int                option_char = 0;
    char*              hostname    = "localhost";
    unsigned short     portno      = 61321;
    char*              filename    = "cs6200.txt";
    FileReceiveClient* frc         = NULL;
    TransferClient*    tc          = NULL;

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

    frc = frc_create(filename);
    if ((status = tc_receive(tc, frc_client(frc))) != TransferClientSuccess) {
        goto EXIT_POINT;
    }
EXIT_POINT:
    if (tc) {
        tc_destroy(tc);
    }
    if (frc) {
        frc_destroy(frc);
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

static TransferClientStatus receive_and_redirect_(
    TransferClient* const tc,
    int const             socketId,
    ReceiveClient*        receiveClient) {
    char  buffer[1024];
    int   rc          = -1;
    void* receiveData = rc_start(receiveClient);
    while ((rc = recv(socketId, buffer, sizeof(buffer), 0)) > 0) {
        rc_send(receiveClient, receiveData, buffer, (size_t)rc);
    }
    if (rc < 0) {
        rc_cancel(receiveClient, receiveData);
        return TransferClientFailedToReceive;
    }
    rc_finish(receiveClient, receiveData);
    return TransferClientSuccess;
}

TransferClientStatus tc_receive(TransferClient* const tc,
                                ReceiveClient*        receiveClient) {
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

void rc_initialize(ReceiveClient*         rc,
                   ReceiveClientStartFcn  startFcn,
                   ReceiveClientSendFcn   sendFcn,
                   ReceiveClientCancelFcn cancelFcn,
                   ReceiveClientFinishFcn finishFcn,
                   void*                  clientData) {
    memset(rc, 0, sizeof(ReceiveClient));
    rc->startFcn   = startFcn;
    rc->sendFcn    = sendFcn;
    rc->cancelFcn  = cancelFcn;
    rc->finishFcn  = finishFcn;
    rc->clientData = clientData;
}

void* rc_start(ReceiveClient* rc) {
    return rc->startFcn(rc->clientData);
}

ssize_t rc_send(ReceiveClient* const rc,
                void* const          session,
                void const* const    buffer,
                size_t const         n) {
    return rc->sendFcn(rc->clientData, session, buffer, n);
}

void rc_cancel(ReceiveClient* rc, void* session) {
    rc->cancelFcn(rc->clientData, session);
}

int rc_finish(ReceiveClient* rc, void* session) {
    return rc->finishFcn(rc->clientData, session);
}

struct FileReceiveClientTag {
    ReceiveClient base;
    char*         fileName;
};

static void* file_receive_start_(void* const clientData) {
    FileReceiveClient* const frc = (FileReceiveClient*)clientData;
    return fopen(frc->fileName, "w");
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
    FileReceiveClient* const frc = (FileReceiveClient*)clientData;
    FILE* const              fh  = (FILE*)sessionData;
    fclose(fh);
    remove(frc->fileName);
}

static int file_receive_finish_(void* const clientData,
                                void* const sessionData) {
    (void)clientData;
    FILE* const fh = (FILE*)sessionData;
    fclose(fh);
    return 1;
}

FileReceiveClient* frc_create(char const* fileName) {
    FileReceiveClient* out =
        (FileReceiveClient*)calloc(1, sizeof(FileReceiveClient));
    rc_initialize(&out->base,
                  file_receive_start_,
                  file_receive_send_,
                  file_receive_cancel_,
                  file_receive_finish_,
                  out);
    return out;
}

ReceiveClient* frc_client(FileReceiveClient* frc) {
    return &frc->base;
}

void frc_destroy(FileReceiveClient* frc) {
    free(frc->fileName);
    free(frc);
}
