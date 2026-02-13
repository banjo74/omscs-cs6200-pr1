// HEADER_START

#include <stddef.h>

typedef struct EchoClientTag EchoClient;

enum EchoClientStatusTag {
    EchoClientSuccess = 0,
    EchoClientFailedToResolveHost,
    EchoClientFailedToSend,
    EchoClientFailedToReceive,
    EchoClientFailedToConnectToSocket
};
typedef enum EchoClientStatusTag EchoClientStatus;

// Construct a new EchoClient.  Upon success, returns an EchoClient ready to
// send and receive. Upon failure, returns NULL and sets statusOut.  Can fail if
// hostNoame/port is unresolvable.  In this case *statusOut will be set to
// EchoClientFailedToResolveHost.
//
// Since the echo protocol stipulates that every send and receive is a new
// connection, socket conecctions are not created until ec_send_and_receive
// (once per call)
//
// statusOut must point to a valid address even on success.
EchoClient* ec_create(EchoClientStatus* statusOut,
                      char const*       hostName,
                      unsigned short    port);

// abstraction for where received messages go in ec_send_and_receive.
typedef void (*ReceiveFcn)(void*, char const*, size_t);

// send message to the server and wait for a response.  upon receiving a
// response send it to receiveFcn passing receiveData along with it.
EchoClientStatus ec_send_and_receive(EchoClient*,
                                     char const* message,
                                     ReceiveFcn  receiveFcn,
                                     void*       receiveData);

// destroy the EchoClient
void ec_destroy(EchoClient*);

// given a status return a descriptive error message
char const* ec_error_message(EchoClientStatus);

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

#define USAGE                                                            \
    "usage:\n"                                                           \
    "  echoclient [options]\n"                                           \
    "options:\n"                                                         \
    "  -p                  Port (Default: 14757)\n"                      \
    "  -s                  Server (Default: localhost)\n"                \
    "  -m                  Message to send to server (Default: \"Hello " \
    "Spring!!\")\n"                                                      \
    "  -h                  Show this help message\n"

#if !defined(TEST_MODE)
// implementation of ReceiveFcn that sends messages to stdout.  no client data
// needed for to_stdout_.
static void to_stdout_(void* const  unused,
                       char const*  message,
                       size_t const messageSize) {
    (void)unused;
    write(STDOUT_FILENO, message, messageSize);
}

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"message", required_argument, NULL, 'm'},
    {"port", required_argument, NULL, 'p'},
    {"server", required_argument, NULL, 's'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}};

/* Main ========================================================= */
int main(int argc, char** argv) {
    int            option_char = 0;
    unsigned short portno      = 14757;
    char*          hostname    = "localhost";
    char*          message     = "Hello Spring!!";

    // Parse and set command line arguments
    while ((option_char = getopt_long(
                argc, argv, "s:p:m:hx", gLongOptions, NULL)) != -1) {
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
        case 'm': // message
            message = optarg;
            break;
        case 'h': // help
            fprintf(stdout, "%s", USAGE);
            exit(0);
            break;
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

    if (NULL == message) {
        fprintf(stderr, "%s @ %d: invalid message\n", __FILE__, __LINE__);
        exit(1);
    }

    if (NULL == hostname) {
        fprintf(stderr, "%s @ %d: invalid host name\n", __FILE__, __LINE__);
        exit(1);
    }

    EchoClientStatus  status = EchoClientSuccess;
    EchoClient* const ec     = ec_create(&status, hostname, portno);
    if (!ec) {
        goto EXIT_POINT;
    }
    status = ec_send_and_receive(ec, message, to_stdout_, NULL);

EXIT_POINT:
    if (ec) {
        ec_destroy(ec);
    }

    if (status != EchoClientSuccess) {
        fprintf(stderr, "%s\n", ec_error_message(status));
        return 1;
    }

    return 0;
}
#endif

struct EchoClientTag {
    struct addrinfo* addressInfo;
};

// Wrapper around getaddrinfo.  Resolve addrinfo struct for serverName and port
// using getaddrinfo mainly to support IPv4 and IPv6 plush other functions are
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

EchoClient* ec_create(EchoClientStatus* const status,
                      char const* const       serverName,
                      unsigned short const    port) {
    assert(status);
    *status = EchoClientSuccess;

    EchoClient* const ec = (EchoClient*)calloc(1, sizeof(EchoClient));
    ec->addressInfo      = resolve_address_info_(serverName, port);
    if (!ec->addressInfo) {
        *status = EchoClientFailedToResolveHost;
        free(ec);
        return NULL;
    }
    return ec;
}

// search through address info for a socket that will accept our connection.
// returns -1 on failure.  ai must be valid.
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

// send all data through the socket.
static EchoClientStatus send_all_(int const         socketId,
                                  char const* const buffer,
                                  size_t const      size) {
    size_t numSent = 0;
    while (numSent < size) {
        int const rc = send(socketId, buffer + numSent, size - numSent, 0);
        if (rc < 0) {
            return EchoClientFailedToSend;
        }
        numSent += (size_t)rc;
    }
    return EchoClientSuccess;
}

// read data from the socket and send to receiveFcn.  we expect the server to
// close the socket when the message is done.  don't assume the message is the
// same size as the one we sent.  the server may be tuncating it.
static EchoClientStatus receive_and_redirect_(int const   socketId,
                                              ReceiveFcn  receiveFcn,
                                              void* const receiveData) {
    char buffer[1024]; // this buffer size doesn't need to match the buffer size
                       // in the server.
    ssize_t numReceived = 0;
    while ((numReceived = recv(socketId, buffer, sizeof(buffer), 0)) > 0) {
        receiveFcn(receiveData, buffer, (size_t)numReceived);
    }

    if (numReceived < 0) {
        return EchoClientFailedToReceive;
    }
    return EchoClientSuccess;
}

EchoClientStatus ec_send_and_receive(EchoClient* const ec,
                                     char const* const message,
                                     ReceiveFcn        receiveFcn,
                                     void* const       receiveData) {
    int const socketFd = create_and_connect_to_socket_(ec->addressInfo);
    int       status   = EchoClientSuccess;

    if (socketFd < 0) {
        status = EchoClientFailedToConnectToSocket;
        goto EXIT_POINT;
    }

    if ((status = send_all_(socketFd, message, strlen(message))) !=
        EchoClientSuccess) {
        goto EXIT_POINT;
    }

    status = receive_and_redirect_(socketFd, receiveFcn, receiveData);

EXIT_POINT:
    if (socketFd != -1) {
        close(socketFd);
    }
    return status;
}

void ec_destroy(EchoClient* const ec) {
    if (ec->addressInfo) {
        freeaddrinfo(ec->addressInfo);
    }
    free(ec);
}

char const* ec_error_message(EchoClientStatus const status) {
    static struct {
        EchoClientStatus status;
        char const*      message;
    } const messages[] = {
        {EchoClientFailedToConnectToSocket, "failed to connect to socket"},
        {EchoClientFailedToResolveHost, "failed to resolve host"},
        {EchoClientFailedToSend, "failed to send"},
        {EchoClientFailedToReceive, "failed to receive"}};

    for (size_t i = 0; i < sizeof(messages) / sizeof(messages[0]); ++i) {
        if (messages[i].status == status) {
            return messages[i].message;
        }
    }
    assert(false);
    return "";
}
