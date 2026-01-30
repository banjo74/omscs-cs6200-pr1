// HEADER_START
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <sys/types.h>

#include <stdbool.h>
#include <stddef.h>

/////////////////////////////////////////////////////////
// TransferServer
/////////////////////////////////////////////////////////

typedef struct TransferServerTag TransferServer;
typedef struct TransferSourceTag TransferSource;

enum TransferServerStatusTag {
    TransferServerSuccess,
    TransferServerFailedToBind,
    TransferServerFailedToListen,
    TransferServerFailedToSelect,
    TransferServerFailedToAccept,
    TransferServerFailedToOpen,
    TransferServerFailedToRead,
    TransferServerFailedToSend,
};
typedef enum TransferServerStatusTag TransferServerStatus;

// Create an TransferServer listening on port.  Upon success, the server will be
// returned and will be in listening mode and should be accessible for connect
// () (call to tc_receive).
//
// Upon failure the returned value is NULL and statusOut is set to the failure
// status. statusOut must point to a valid address even on success.
TransferServer* ts_create(TransferServerStatus* statusOut,
                          unsigned short        port,
                          size_t                maximumConnections);

// get the port
unsigned short ts_port(TransferServer const*);

// function used with ts_run, below, to decide whether to continue running
// or exit.
typedef bool (*ContinueFcn)(void*);

// function used with ts_run, below, to log error conditions
typedef void (*LogFcn)(void*, TransferServerStatus);

// Start running the server.  The server is in listen mode after calling
// ts_create but no connections will be accepted until ts_run is executed.
// ts_run will continue accepting connections from clients or until
// continueFcn(continueFcnData) returns false.  If continueFcn is null,
// continueFcnData is ignored and the server runs forever.
//
// with each connection, the server sends all data provied from source (see
// TransferSource below).
//
// If server enounters an error while running (send error, receive error, etc.)
// logFcn(logFcnData, status) is called.  If logFcn is null, there is no
// logging.
TransferServerStatus ts_run(TransferServer*,
                            TransferSource* source,
                            ContinueFcn     continueFcn,
                            void*           continueFcnData,
                            LogFcn          logFcn,
                            void*           logFcnData);

// destroy the server.
void ts_destroy(TransferServer*);

// given a status return a descriptive error message
char const* ts_error_message(TransferServerStatus);

/////////////////////////////////////////////////////////
// TransferSource
/////////////////////////////////////////////////////////

// See TransferSource below.
typedef void* (*TransferSourceStartFcn)(void* sourceData);

// See TransferSource below.
typedef ssize_t (*TransferSourceReadFcn)(void*  sourceData,
                                         void*  sessionData,
                                         void*  buffer,
                                         size_t max);

// See TransferSource below.
typedef int (*TransferSourceFinishFcn)(void* sourceData, void* sessionData);

/*!
 TransferSource is an abstraction of a data source
 */
struct TransferSourceTag {
    // session = source->startFcn(source->sourceData);
    // starts a data read session, the returned value may be used
    // with readFcn.  source_start is a convenience call to this function.
    TransferSourceStartFcn startFcn;

    // nRead = source->readFcn(source->sourceData, session, buffer, maxToRead);
    // reads up to maxToRead bytes into buffer for session.
    // should update session state so that the next call to readFcn
    // will read the next bytes.
    //
    // returns the number of bytes read.
    // returns 0 to indicate that its done reading
    //
    // returns negative value on failure
    //
    // source_read is a convenience call to this function.
    TransferSourceReadFcn readFcn;

    // source->finishFcn(source->sourceData, session)
    // terminates the session.  releases any resources used by the
    // session and invalidates session
    //
    // source_finish is a convenience call to this function.
    TransferSourceFinishFcn finishFcn;

    // client data.
    void* sourceData;
};

// initialize a TransferSource with the provided, start, read, finish, and data.
void source_initialize(TransferSource* emptyClient,
                       TransferSourceStartFcn,
                       TransferSourceReadFcn,
                       TransferSourceFinishFcn,
                       void* sourceData);

// start a session.  see startFcn in TransferSource above
void* source_start(TransferSource* source);

// read some data.  see readFcn in TransferSource above
ssize_t source_read(TransferSource* source, void*, void*, size_t);

// finish a session, see finishFcn in TransferSource above
int source_finish(TransferSource* source, void*);

// a wrapper around TransferSource to for file reading
typedef struct FileTransferSourceTag FileTransferSource;

/////////////////////////////////////////////////////////
// FileTransferSource
/////////////////////////////////////////////////////////

// create a FileTransferSource that reads fileName with each session
// will succeed even if fileName doesn't exist but start on the underlying
// source will fail
FileTransferSource* fsource_create(char const* fileName);

// get the underlying TransferSource
TransferSource* fsource_source(FileTransferSource*);

// destroy the file transfer source
void fsource_destroy(FileTransferSource*);

// HEADER_END
#include <netinet/in.h>
#include <sys/select.h>
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

#define USAGE                                              \
    "usage:\n"                                             \
    "  transferserver [options]\n"                         \
    "options:\n"                                           \
    "  -f                  Filename (Default: 6200.txt)\n" \
    "  -p                  Port (Default: 61321)\n"        \
    "  -h                  Show this help message\n"

#ifndef TEST_MODE
/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"port", required_argument, NULL, 'p'},
    {"filename", required_argument, NULL, 'f'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}};

int main(int argc, char** argv) {
    int                 option_char;
    int                 portno   = 61321;      /* port to listen on */
    char*               filename = "6200.txt"; /* file to transfer */
    TransferServer*     ts       = NULL;
    FileTransferSource* source   = NULL;

    setbuf(stdout, NULL); // disable buffering

    // Parse and set command line arguments
    while ((option_char =
                getopt_long(argc, argv, "p:hf:x", gLongOptions, NULL)) != -1) {
        switch (option_char) {
        case 'p': // listen-port
            portno = atoi(optarg);
            break;
        case 'f': // file to transfer
            filename = optarg;
            break;
        case 'h': // help
            fprintf(stdout, "%s", USAGE);
            exit(0);
            break;
        default:
            fprintf(stderr, "%s", USAGE);
            exit(1);
        }
    }

    if ((portno < 1025) || (portno > 65535)) {
        fprintf(stderr,
                "%s @ %d: invalid port number (%d)\n",
                __FILE__,
                __LINE__,
                portno);
        exit(1);
    }

    if (NULL == filename) {
        fprintf(stderr, "%s @ %d: invalid filename\n", __FILE__, __LINE__);
        exit(1);
    }

    TransferServerStatus status = TransferServerSuccess;
    ts                          = ts_create(&status, portno, 5);
    if (status != TransferServerSuccess) {
        goto EXIT_POINT;
    }
    source = fsource_create(filename);
    status = ts_run(ts, fsource_source(source), NULL, NULL, NULL, NULL);
EXIT_POINT:
    if (ts) {
        ts_destroy(ts);
    }
    if (source) {
        fsource_destroy(source);
    }
}
#endif

/////////////////////////////////////////////////////////
// TransferServer
/////////////////////////////////////////////////////////

struct TransferServerTag {
    int              socketId;
    unsigned short   portNumber;
    struct addrinfo* addrInfo;     // the addrinfo to destroy
    struct addrinfo* usedAddrInfo; // the one to accept on

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

// helper to make a timeval given a total number of microseconds
static struct timeval make_timeval_(suseconds_t usec) {
    struct timeval tv;
    memset(&tv, 0, sizeof(tv));
    tv.tv_sec  = usec / 1000;
    tv.tv_usec = usec % 1000;
    return tv;
}

TransferServer* ts_create(TransferServerStatus* const status,
                          unsigned short const        port,
                          size_t const                maximumConnections) {
    assert(status);

    *status = TransferServerSuccess;

    TransferServer* ts = (TransferServer*)calloc(1, sizeof(TransferServer));
    ts->portNumber     = port;
    ts->addrInfo       = resolve_address_info_(NULL, port);
    // for now just assert this is successful.  It's difficult to create a
    // testing environment where this would fail.
    assert(ts->addrInfo);
    if ((ts->socketId = create_and_bind_to_socket_(ts->addrInfo,
                                                   &ts->usedAddrInfo)) == -1) {
        *status = TransferServerFailedToBind;
        goto EXIT_POINT;
    }
    assert(ts->usedAddrInfo);

    // ts_create should leave it listening at exit so its ready to accept
    if ((listen(ts->socketId, (int)maximumConnections)) == -1) {
        *status = TransferServerFailedToListen;
        goto EXIT_POINT;
    }

    // we'll use these for select, below
    FD_SET(ts->socketId, &ts->listenSet);
    ts->timeout = make_timeval_(1000);

EXIT_POINT:
    if (*status != TransferServerSuccess) {
        ts_destroy(ts);
        return NULL;
    }
    return ts;
}

unsigned short ts_port(TransferServer const* ts) {
    return ts->portNumber;
}

static void log_(LogFcn                     logFcn,
                 void* const                logFcnData,
                 TransferServerStatus const status) {
    if (logFcn) {
        logFcn(logFcnData, status);
    }
}

// send all data through socketId until send fails or all data is sent
static ssize_t send_all_(int const            socketId,
                         uint8_t const* const buffer,
                         size_t const         size) {
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

// if fcn is null, return true.  otherwise, return fcn(data)
static bool continue_(ContinueFcn fcn, void* data) {
    return fcn == NULL || fcn(data);
}

// wait ts->timeout for a connect.  returns 0 if timeout. returns < 0 for error.
// returns > 0 for pending connection.
static bool select_(TransferServer* ts, LogFcn logFcn, void* logFcnData) {
    fd_set         localFdSet   = ts->listenSet;
    struct timeval localTimeout = ts->timeout;
    int            result =
        select(ts->socketId + 1, &localFdSet, NULL, NULL, &localTimeout);
    if (result < 0) {
        log_(logFcn, logFcnData, TransferServerFailedToSelect);
    }
    // zero just means it timed out
    return result > 0;
}

// cleanly shutdown the send.  indicate to the reader that there's nothing more
// coming and then flush our read buffer
static void shutdown_(int const socketId) {
    shutdown(socketId, SHUT_WR);
    uint8_t buffer[1024];
    while (recv(socketId, buffer, sizeof(buffer), 0) > 0) {
    }
    close(socketId);
}

static TransferServerStatus handle_connection_(TransferServer* const ts,
                                               TransferSource*       source,
                                               int const acceptedSocket) {
    TransferServerStatus status        = TransferServerSuccess;
    void*                sourceSession = source_start(source);
    if (!sourceSession) {
        status = TransferServerFailedToOpen;
        goto EXIT_POINT;
    }

    ssize_t numRead = 0;
    uint8_t buffer[BUFSIZE];
    while ((numRead = source_read(
                source, sourceSession, buffer, sizeof(buffer))) > 0) {
        int const sendResult = send_all_(acceptedSocket, buffer, numRead);
        if (sendResult == -1) {
            status = TransferServerFailedToSend;
            goto EXIT_POINT;
        }
    }
    if (numRead < 0) {
        status = TransferServerFailedToRead;
        goto EXIT_POINT;
    }
EXIT_POINT:
    if (sourceSession) {
        source_finish(source, sourceSession);
    }
    return status;
}

TransferServerStatus ts_run(TransferServer* ts,
                            TransferSource* source,
                            ContinueFcn     continueFcn,
                            void*           continueFcnData,
                            LogFcn          logFcn,
                            void*           logFcnData) {
    assert(ts->socketId != -1);
    assert(ts->usedAddrInfo);
    while (continue_(continueFcn, continueFcnData)) {
        // wait for a connection
        if (!select_(ts, logFcn, logFcnData)) {
            continue;
        }

        // accept the pending connection
        int const acceptedSocket = accept(ts->socketId,
                                          ts->usedAddrInfo->ai_addr,
                                          &ts->usedAddrInfo->ai_addrlen);
        if (acceptedSocket == -1) {
            log_(logFcn, logFcnData, TransferServerFailedToAccept);
            continue;
        }

        // do the transfer
        TransferServerStatus const status =
            handle_connection_(ts, source, acceptedSocket);
        if (status != TransferServerSuccess) {
            log_(logFcn, logFcnData, status);
        }

        // shut it down
        shutdown_(acceptedSocket);
    }
    return TransferServerSuccess;
}

void ts_destroy(TransferServer* ts) {
    if (ts) {
        if (ts->socketId != -1) {
            close(ts->socketId);
        }
        if (ts->addrInfo) {
            freeaddrinfo(ts->addrInfo);
        }
        free(ts);
    }
}

char const* ts_error_message(TransferServerStatus const status) {
    static struct {
        TransferServerStatus status;
        char const*          message;
    } const messages[] = {
        {TransferServerFailedToBind, "failed to bind"},
        {TransferServerFailedToListen, "failed to listen"},
        {TransferServerFailedToSelect, "failed to select"},
        {TransferServerFailedToAccept, "failed to accept"},
        {TransferServerFailedToOpen, "failed to open"},
        {TransferServerFailedToRead, "failed to read"},
        {TransferServerFailedToSend, "failed to send"},
    };

    for (size_t i = 0; i < sizeof(messages) / sizeof(messages[0]); ++i) {
        if (messages[i].status == status) {
            return messages[i].message;
        }
    }
    assert(false);
    return "";
}

/////////////////////////////////////////////////////////
// TransferSource
/////////////////////////////////////////////////////////

void source_initialize(TransferSource*         source,
                       TransferSourceStartFcn  startFcn,
                       TransferSourceReadFcn   readFcn,
                       TransferSourceFinishFcn finishFcn,
                       void*                   sourceData) {
    memset(source, 0, sizeof(TransferSource));
    source->startFcn   = startFcn;
    source->readFcn    = readFcn;
    source->finishFcn  = finishFcn;
    source->sourceData = sourceData;
}

void* source_start(TransferSource* source) {
    return source->startFcn(source->sourceData);
}

ssize_t source_read(TransferSource* const source,
                    void* const           session,
                    void* const           buffer,
                    size_t const          n) {
    return source->readFcn(source->sourceData, session, buffer, n);
}

int source_finish(TransferSource* source, void* session) {
    return source->finishFcn(source->sourceData, session);
}

/////////////////////////////////////////////////////////
// FileTransferSource
/////////////////////////////////////////////////////////

struct FileTransferSourceTag {
    TransferSource base;
    char*          fileName;
};

static void* file_source_start_(void* const sourceData) {
    FileTransferSource* const fsource = (FileTransferSource*)sourceData;
    return fopen(fsource->fileName, "r");
}

static ssize_t file_source_read_(void* const  sourceData,
                                 void* const  sessionData,
                                 void* const  buffer,
                                 size_t const nBytes) {
    (void)sourceData;
    FILE* const fh = (FILE*)sessionData;
    return (ssize_t)fread(buffer, 1, nBytes, fh);
}

static int file_source_finish_(void* const sourceData,
                               void* const sessionData) {
    (void)sourceData;
    FILE* const fh = (FILE*)sessionData;
    fclose(fh);
    return 1;
}

FileTransferSource* fsource_create(char const* fileName) {
    FileTransferSource* out =
        (FileTransferSource*)calloc(1, sizeof(FileTransferSource));
    out->fileName = strdup(fileName);
    source_initialize(&out->base,
                      file_source_start_,
                      file_source_read_,
                      file_source_finish_,
                      out);
    return out;
}

TransferSource* fsource_source(FileTransferSource* fsource) {
    return &fsource->base;
}

void fsource_destroy(FileTransferSource* fsource) {
    free(fsource->fileName);
    free(fsource);
}
