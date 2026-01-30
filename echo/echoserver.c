
// HEADER_START
#include <stdbool.h>
#include <stddef.h>

typedef struct EchoServerTag EchoServer;

enum EchoServerStatusTag {
    EchoServerSuccess,
    EchoServerFailedToBind,
    EchoServerFailedToListen,
    EchoServerFailedToSelect,
    EchoServerFailedToAccept,
    EchoServerFailedToRead,
    EchoServerFailedToSend,
};
typedef enum EchoServerStatusTag EchoServerStatus;

// Create an EchoServer listening on port.  Upon success, the server will be
// returned and will be in listening mode and should be accessible for connect
// () (call to ec_send_and_receive)
//
// with each connection, the server will read at most maximumMessageLength
// characters and send them back to the client.
//
// Upon failure the returned value is NULL and statusOut is set to the failure
// status. statusOut must point to a valid address even on success.
EchoServer* es_create(EchoServerStatus* statusOut,
                      size_t            maximumMessageLength,
                      unsigned short    port,
                      size_t            maximumConnections);

// get the port
unsigned short es_port(EchoServer const*);

// function used with es_run, below, to decide whether to continue running
// or exit.
typedef bool (*ContinueFcn)(void*);

// function used with es_run, below, to log error conditions
typedef void (*LogFcn)(void*, EchoServerStatus);

// Start running the server.  The server is in listen mode after calling
// ec_create but no connections will be accepted until es_run is executed.
// es_run will continue accepting connections from clients or until
// continueFcn(continueFcnData) returns false.  If continueFcn is null,
// continueFcnData is ignored and runs forever.
//
// If server enounters an error while running (send error, receive error, etc.)
// logFcn(logFcnData, status) is called.  If logFcn is null, there is no
// logging.
//
// The server may be restarted (rerunning es_run) on the same port after
// continue function stops it.
EchoServerStatus es_run(EchoServer*,
                        ContinueFcn continueFcn,
                        void*       continueFcnData,
                        LogFcn      logFcn,
                        void*       logFcnData);

// destroy the server.
void es_destroy(EchoServer*);

// given a status return a descriptive error message
char const* es_error_message(EchoServerStatus);
// HEADER_END

#define _POSIX_C_SOURCE 200112L

#include <netinet/in.h>
#include <sys/select.h>
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

#define USAGE                                                          \
    "usage:\n"                                                         \
    "  echoserver [options]\n"                                         \
    "options:\n"                                                       \
    "  -p                  Port (Default: 14757)\n"                    \
    "  -m                  Maximum pending connections (default: 5)\n" \
    "  -h                  Show this help message\n"

#if !defined(TEST_MODE)
/* OPTIONS DESCRIPTOR ======================================================
 */
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
    while ((option_char =
                getopt_long(argc, argv, "p:m:hx", gLongOptions, NULL)) != -1) {
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
        fprintf(stderr,
                "%s @ %d: invalid port number (%d)\n",
                __FILE__,
                __LINE__,
                portno);
        exit(1);
    }
    if (maxnpending < 1) {
        fprintf(stderr,
                "%s @ %d: invalid pending count (%d)\n",
                __FILE__,
                __LINE__,
                maxnpending);
        exit(1);
    }

    EchoServerStatus status = EchoServerSuccess;
    EchoServer*      es     = es_create(&status, BUFSIZE, portno, maxnpending);
    if (!es) {
        goto EXIT_POINT;
    }
    status = es_run(es, NULL, NULL, NULL, NULL);

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

struct EchoServerTag {
    int              socketId;
    unsigned short   portNumber;
    struct addrinfo* addrInfo;     // the addrinfo to destroy
    struct addrinfo* usedAddrInfo; // the one to accept on

    size_t maxMessageLength; // the maximum message length read per connection

    struct timeval
        timeout; // the duration to wait between checks of the continue function
    fd_set listenSet; // an fd_set containing just the listening socket.
};

// Wrapper around getaddrinfo.  Resolve addrinfo struct for serverName and port
// using getaddrinfo mainly to support IPv4 and IPv6 plus the other POSIX
// address resolution functions are deprecated.  Note, if hostName is NULL,
// resolves the localhost.
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
                                      struct addrinfo** const usedAddrOut) {
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
        *usedAddrOut = ai;
        return socketFd;
    }
    return -1;
}

// helper to make a timeval given a total number of microseconds
static struct timeval make_timeval_(suseconds_t usec) {
    struct timeval tv;
    memset(&tv, 0, sizeof(tv));
    tv.tv_sec  = usec / 1000;
    tv.tv_usec = usec % 1000;
    return tv;
}

EchoServer* es_create(EchoServerStatus* const status,
                      size_t                  maxMessageLength,
                      unsigned short const    port,
                      size_t const            maximumConnections) {
    assert(status);

    *status = EchoServerSuccess;

    EchoServer* es       = (EchoServer*)calloc(1, sizeof(EchoServer));
    es->portNumber       = port;
    es->maxMessageLength = maxMessageLength;
    es->addrInfo         = resolve_address_info_(NULL, port);
    // for now just assert this is successful.  It's difficult to create a
    // testing environment where this would fail.
    assert(es->addrInfo);
    if ((es->socketId = create_and_bind_to_socket_(es->addrInfo,
                                                   &es->usedAddrInfo)) == -1) {
        *status = EchoServerFailedToBind;
        goto EXIT_POINT;
    }
    assert(es->usedAddrInfo);

    // es_create should leave it listening at exit so its ready to accept
    if ((listen(es->socketId, (int)maximumConnections)) == -1) {
        *status = EchoServerFailedToListen;
        goto EXIT_POINT;
    }

    FD_SET(es->socketId, &es->listenSet);
    es->timeout = make_timeval_(1000);

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

static void log_(LogFcn                 logFcn,
                 void* const            logFcnData,
                 EchoServerStatus const status) {
    if (logFcn) {
        logFcn(logFcnData, status);
    }
}

// send all data through socketId until send fails or all data is sent
static ssize_t send_all_(int const         socketId,
                         char const* const buffer,
                         size_t const      size) {
    ssize_t numSent = 0;
    while (numSent < size) {
        ssize_t const rc = send(socketId, buffer + numSent, size - numSent, 0);
        if (rc <= 0) {
            return -1;
        }
        numSent += rc;
    }
    return numSent;
}

// receive a block of data from socketId and then send it back to socketId
// Assume we just need one call to recv.  See:
// https://piazza.com/class/mka6wqnuwdsr6/post/289
// https://piazza.com/class/mka6wqnuwdsr6/post/122
static void receive_and_send_all_(int const    acceptedSocket,
                                  size_t const maxMessageLength,
                                  LogFcn       logFcn,
                                  void* const  logFcnData) {
    char          buffer[maxMessageLength]; // C99
    ssize_t const read = recv(acceptedSocket, buffer, maxMessageLength, 0);
    if (read < 0) {
        log_(logFcn, logFcnData, EchoServerFailedToRead);
    }
    if (send_all_(acceptedSocket, buffer, (size_t)read) != read) {
        log_(logFcn, logFcnData, EchoServerFailedToRead);
    }
}

// if fcn is null, return true.  otherwise, return fcn(data)
static bool continue_(ContinueFcn fcn, void* data) {
    return fcn == NULL || fcn(data);
}

// wait es->timeout for a connect.  returns true for pending connection, false
// for timeout or failure.
static bool select_(EchoServer* es, LogFcn logFcn, void* logFcnData) {
    fd_set         localFdSet   = es->listenSet;
    struct timeval localTimeout = es->timeout;
    int            result =
        select(es->socketId + 1, &localFdSet, NULL, NULL, &localTimeout);
    if (result < 0) {
        log_(logFcn, logFcnData, EchoServerFailedToSelect);
    }
    // zero just means it timed out
    return result > 0;
}

// cleanly shutdown the send.  indicate to the reader that there's nothing more
// coming and then flush our read buffer
static void shutdown_(int const socketId) {
    shutdown(socketId, SHUT_WR);
    char buffer[1024];
    while (recv(socketId, buffer, sizeof(buffer), 0) > 0) {
    }
    close(socketId);
}

EchoServerStatus es_run(EchoServer* es,
                        ContinueFcn continueFcn,
                        void*       continueFcnData,
                        LogFcn      logFcn,
                        void*       logFcnData) {
    assert(es->socketId != -1);
    assert(es->usedAddrInfo);
    while (continue_(continueFcn, continueFcnData)) {
        if (!select_(es, logFcn, logFcnData)) {
            continue;
        }
        int const acceptedSocket = accept(es->socketId,
                                          es->usedAddrInfo->ai_addr,
                                          &es->usedAddrInfo->ai_addrlen);
        if (acceptedSocket == -1) {
            log_(logFcn, logFcnData, EchoServerFailedToAccept);
            continue;
        }
        receive_and_send_all_(
            acceptedSocket, es->maxMessageLength, logFcn, logFcnData);
        shutdown_(acceptedSocket);
    }
    return EchoServerSuccess;
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

char const* es_error_message(EchoServerStatus const status) {
    static struct {
        EchoServerStatus status;
        char const*      message;
    } const messages[] = {
        {EchoServerFailedToBind, "failed to bind"},
        {EchoServerFailedToListen, "failed to listen"},
        {EchoServerFailedToSelect, "failed to select"},
        {EchoServerFailedToAccept, "failed to accept"},
        {EchoServerFailedToRead, "failed to read"},
        {EchoServerFailedToSend, "failed to send"},
    };

    for (size_t i = 0; i < sizeof(messages) / sizeof(messages[0]); ++i) {
        if (messages[i].status == status) {
            return messages[i].message;
        }
    }
    assert(false);
    return "";
}
