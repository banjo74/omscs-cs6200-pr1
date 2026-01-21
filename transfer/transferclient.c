// HEADER_START
typedef struct TransferClientTag TransferClient;

typedef enum {
    TransferClientSuccess,
    TransferClientFailedToResolveHost,
    TransferClientFailedToReceive,
    TransferClientFailedToConnect
} TransferClientStatus;

TransferClient* tc_create(TransferClientStatus*, char const* hostName, unsigned short port);

typedef void (*ReceiveFcn)(void const* toWrite, size_t numToWrite, void* userData);

TransferClientStatus tc_receive(TransferClient*, ReceiveFcn receiveFcn, void* userData);

void tc_destroy(TransferClient*);

// HEADER_END
#define _POSIX_C_SOURCE 200112L

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFSIZE 512

struct TransferClientTag {
    struct addrinfo* addrInfo;
};

// Wrapper around getaddrinfo.  Resolve addrinfo struct for serverName and port
// using getaddrinfo mainly to support IPv4 and IPv6 plush other functions are deprecated.
static struct addrinfo* resolve_address_info_(char const* const serverName, unsigned short const port) {
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

TransferClient* tc_create(TransferClientStatus* const status, char const* const serverName, unsigned short const port) {
    assert(status);
    *status = TransferClientSuccess;

    TransferClient* const tc = (TransferClient*)calloc(1, sizeof(TransferClient));
    tc->addrInfo             = resolve_address_info_(serverName, port);
    if (!tc->addrInfo) {
        *status = TransferClientFailedToResolveHost;
        free(tc);
        return NULL;
    }
    return tc;
}

// search through address info for a socket that will accept our connection.  returns -1 on failure.  tc->addrInfo must be valid.
static int create_and_connect_to_socket_(TransferClient* const tc) {
    assert(tc->addrInfo);
    int socketFid = -1;
    for (struct addrinfo* ai = tc->addrInfo; ai; ai = ai->ai_next) {
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

static TransferClientStatus receive_and_redirect_(TransferClient* const tc, int const socketId, ReceiveFcn receiveFcn, void* const receiveData) {
    char buffer[1024];
    int  rc = -1;
    while ((rc = recv(socketId, buffer, sizeof(buffer), 0)) > 0) {
        receiveFcn(receiveData, buffer, (size_t)rc);
    }
    if (rc < 0) {
        return TransferClientFailedToReceive;
    }
    return TransferClientSuccess;
}

TransferClientStatus tc_receive(TransferClient* const tc, ReceiveFcn receiveFcn, void* const receiveData) {
    int const socket = create_and_connect_to_socket_(tc);
    if (socket == -1) {
        return TransferClientFailedToConnect;
    }
    TransferClientStatus const status = receive_and_redirect_(tc, socket, receiveFcn, receiveData);
    close(socket);
    return status;
}

#define USAGE                                             \
    "usage:\n"                                            \
    "  transferclient [options]\n"                        \
    "options:\n"                                          \
    "  -p                  Port (Default: 61321)\n"       \
    "  -s                  Server (Default: localhost)\n" \
    "  -h                  Show this help message\n"      \
    "  -o                  Output file (Default cs6200.txt)\n"

#ifndef TEST_MODE
static void write_to_file_(void* const fh, void const* const data, size_t const n) {
    fwrite(data, n, 1, (FILE*)fh);
}
/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"server", required_argument, NULL, 's'},
    {"output", required_argument, NULL, 'o'},
    {"help", no_argument, NULL, 'h'},
    {"port", required_argument, NULL, 'p'},
    {NULL, 0, NULL, 0}};

/* Main ========================================================= */
int main(int argc, char** argv) {
    int            option_char = 0;
    char*          hostname    = "localhost";
    unsigned short portno      = 61321;
    char*          filename    = "cs6200.txt";

    setbuf(stdout, NULL);

    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "s:p:o:hx", gLongOptions, NULL)) != -1) {
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
        fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__, portno);
        exit(1);
    }

    FILE*                fh     = NULL;
    TransferClientStatus status = TransferClientSuccess;
    TransferClient*      tc     = tc_create(&status, hostname, portno);
    if (status != TransferClientSuccess) {
        goto EXIT_POINT;
    }
    fh = fopen(filename, "w");
    if ((status = tc_receive(tc, write_to_file_, fh)) != TransferClientSuccess) {
        goto EXIT_POINT;
    }
EXIT_POINT:
    if (tc) {
        tc_destroy(tc);
    }
    if (fh) {
        fclose(fh);
    }
    if (status != TransferClientSuccess) {
        unlink(filename);
        return 1;
    }
    return 0;
}
#endif // TEST_MODE
