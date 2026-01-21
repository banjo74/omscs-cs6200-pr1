
// HEADER_START
#include <stddef.h>

typedef struct EchoServerTag EchoServer;

enum EchoServerStatusTag {
    EchoServerSuccess,
    EchoServerFailedToBind,
    EchoServerFailedToListen,
    EchoSeverFailedToAccept,
    EchoServerFailedToReceive,
    EchoServerFailedToSend
};
typedef enum EchoServerStatusTag EchoServerStatus;

// Create an EchoServer listening on port.  Upon success, the server will be returned and
// will be in listening mode and should be accessible for connect () (call to ec_send_and_receive)
//
// Upon failure the returned value is NULL and statusOut is set to the failure status.
// statusOut must point to a valid address even on success.
EchoServer* es_create(EchoServerStatus* statusOut, unsigned short port, size_t maximumConnections);

unsigned short es_port(EchoServer const*);

// Start running the server.  While the server is in listen mode after calling ec_create,
// no connections will be accepted until es_run is executed.  es_run will continue accepting connections
// from clients until it encounters and error, or it receives a message es_quit_message().
//
// The server may be restarted after receiving a quit message.
EchoServerStatus es_run(EchoServer*);

// force the server to stop
void es_stop(EchoServer*);

// destroy the server.
void es_destroy(EchoServer*);
// HEADER_END

#define _POSIX_C_SOURCE 200112L

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFSIZE 1024

struct EchoServerTag {
    int              socketId;
    unsigned short   portNumber;
    struct addrinfo* addrInfo;     // the addrinfo to destroy
    struct addrinfo* usedAddrInfo; // the one to accept on
};

// Wrapper around getaddrinfo.  Resolve addrinfo struct for serverName and port
// using getaddrinfo mainly to support IPv4 and IPv6 plush other functions are deprecated.
static struct addrinfo* resolve_address_info_(char const* hostName, unsigned short const port) {
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

static void set_socket_reusable_(int const socketFd) {
    int const optionValue = 1;
    setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &optionValue, sizeof(optionValue));
}

// search through address info for a socket that will accept our connection.  returns -1 on failure.  ec->addressInfo must be valid.
static int create_and_bind_to_socket_(struct addrinfo* ai, struct addrinfo** const usedAddr) {
    assert(ai);
    int socketFd = -1;
    for (; ai; ai = ai->ai_next) {
        if ((socketFd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) ==
            -1) {
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

EchoServer* es_create(EchoServerStatus* const status, unsigned short const port, size_t const maximumConnections) {
    assert(status);

    *status = EchoServerSuccess;

    EchoServer* es = (EchoServer*)calloc(1, sizeof(EchoServer));
    es->portNumber = port;
    es->addrInfo   = resolve_address_info_(NULL, port);
    // for now just assert this is successful.  It's difficult to create a testing environment where this would fail.
    assert(es->addrInfo);
    if ((es->socketId = create_and_bind_to_socket_(es->addrInfo, &es->usedAddrInfo)) == -1) {
        *status = EchoServerFailedToBind;
        goto EXIT_POINT;
    }
    assert(es->usedAddrInfo);

    // es_create should leave it listening at exit so its ready to accept
    if ((listen(es->socketId, (int)maximumConnections)) == -1) {
        *status = EchoServerFailedToListen;
        goto EXIT_POINT;
    }

EXIT_POINT:
    if (*status != EchoServerSuccess) {
        es_destroy(es);
        return NULL;
    }
    return es;
}

unsigned short es_port(EchoServer const* es) {
    return es->portNumber;
}

static char const* shutdown_message_(void) {
    return "-->|SHUTDOWN_SERVER|<--";
}

// read data from socketId until its closed for reading.  buffer should point to a pointer which itself is unallocated.  numReceived should point to a size_t.
// upon success *buffer will point to a dynamically allocated buffer of data and *numReceived will be the amount of data read.
static EchoServerStatus read_from_socket_(int const socketId, char** const buffer, size_t* const numReceived) {
    char localBuffer[1024];
    int  rc      = 0;
    *numReceived = 0;
    *buffer      = NULL;
    while ((rc = recv(socketId, localBuffer, sizeof(localBuffer), 0)) > 0) {
        size_t const localSize = (size_t)rc;
        size_t const newSize   = *numReceived + localSize;
        *buffer                = (char*)realloc(*buffer, newSize);
        memcpy(*buffer + *numReceived, localBuffer, localSize);
        *numReceived = newSize;
        if (localSize < sizeof(localBuffer) || recv(socketId, NULL, 0, MSG_PEEK) <= 0) {
            break;
        }
    }
    if (rc < 0) {
        free(*buffer);
        *buffer = NULL;
        return EchoServerFailedToReceive;
    }
    return EchoServerSuccess;
}

// push size data through the socket
static EchoServerStatus push_through_socket_(int const         socketId,
                                             char const* const buffer,
                                             size_t const      size) {
    size_t numSent = 0;
    while (numSent < size) {
        int const rc = send(socketId, buffer + numSent, size - numSent, 0);
        if (rc < 0) {
            return EchoServerFailedToSend;
        }
        numSent += (size_t)rc;
    }
    return EchoServerSuccess;
}

EchoServerStatus es_run(EchoServer* es) {
    assert(es->socketId != -1);
    assert(es->usedAddrInfo);
    int          status;
    size_t const shutdownNessageSize = strlen(shutdown_message_());
    while (true) {
        int const acceptedSocket = accept(es->socketId, es->usedAddrInfo->ai_addr, &es->usedAddrInfo->ai_addrlen);
        if (acceptedSocket == -1) {
            return EchoSeverFailedToAccept;
        }
        char*  buffer      = NULL;
        size_t numReceived = 0;
        status             = read_from_socket_(acceptedSocket, &buffer, &numReceived);
        if (status != EchoServerSuccess) {
            close(acceptedSocket);
            return status;
        }
        // check if its our shutdown message.  only do the comparison if its exactly the right length.
        // this avoids an unnecessary comparison and keeps us from shutting down on, say, an empty message
        if (numReceived == shutdownNessageSize && strncmp(shutdown_message_(), buffer, shutdownNessageSize) == 0) {
            close(acceptedSocket);
            free(buffer);
            return EchoServerSuccess;
        }

        status = push_through_socket_(acceptedSocket, buffer, numReceived);
        close(acceptedSocket);
        free(buffer);
        if (status != EchoServerSuccess) {
            return status;
        }
    }
}

static int create_and_connect_to_socket_(struct addrinfo* ai) {
    int socketFid = -1;
    for (; ai; ai = ai->ai_next) {
        if ((socketFid = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) ==
            -1) {
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

void es_stop(EchoServer* es) {
    struct addrinfo* const addrinfo = resolve_address_info_("localhost", es_port(es));
    int const              socketId = create_and_connect_to_socket_(addrinfo);
    freeaddrinfo(addrinfo);
    if (socketId == -1) {
        exit(1);
        return;
    }
    send(socketId, shutdown_message_(), strlen(shutdown_message_()), 0);
    close(socketId);
}

void es_destroy(EchoServer* es) {
    if (es) {
        if (es->socketId != -1) {
            close(es->socketId);
        }
        if (es->addrInfo) {
            freeaddrinfo(es->addrInfo);
        }
        free(es);
    }
}

#define USAGE                                                          \
    "usage:\n"                                                         \
    "  echoserver [options]\n"                                         \
    "options:\n"                                                       \
    "  -p                  Port (Default: 14757)\n"                    \
    "  -m                  Maximum pending connections (default: 5)\n" \
    "  -h                  Show this help message\n"

#if !defined(TEST_MODE)
/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"maxnpending", required_argument, NULL, 'm'},
    {"port", required_argument, NULL, 'p'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}};

int main(int argc, char** argv) {
    int option_char;
    int portno      = 14757; /* port to listen on */
    int maxnpending = 5;

    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "p:m:hx", gLongOptions, NULL)) != -1) {
        switch (option_char) {
        case 'm': // server
            maxnpending = atoi(optarg);
            break;
        case 'h': // help
            fprintf(stdout, "%s ", USAGE);
            exit(0);
            break;
        case 'p': // listen-port
            portno = atoi(optarg);
            break;
        default:
            fprintf(stderr, "%s ", USAGE);
            exit(1);
        }
    }

    setbuf(stdout, NULL); // disable buffering

    if ((portno < 1025) || (portno > 65535)) {
        fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__, portno);
        exit(1);
    }
    if (maxnpending < 1) {
        fprintf(stderr, "%s @ %d: invalid pending count (%d)\n", __FILE__, __LINE__, maxnpending);
        exit(1);
    }

    EchoServerStatus status = EchoServerSuccess;
    EchoServer*      es     = es_create(&status, portno, maxnpending);
    if (!es) {
        goto EXIT_POINT;
    }
    status = es_run(es);

EXIT_POINT:
    if (es) {
        es_destroy(es);
    }
    if (status != EchoServerSuccess) {
        return 1;
    }
    return 0;
}
#endif
