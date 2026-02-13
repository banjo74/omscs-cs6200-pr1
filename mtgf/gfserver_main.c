#define _POSIX_C_SOURCE 200809L
#include "content.h"
#include "gf-student.h"
#include "gfserver-student.h"

#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if !defined(TEST_MODE)
#define USAGE                                                                 \
    "usage:\n"                                                                \
    "  gfserver_main [options]\n"                                             \
    "options:\n"                                                              \
    "  -h                  Show this help message.\n"                         \
    "  -t [nthreads]       Number of threads (Default: 16)\n"                 \
    "  -m [content_file]   Content file mapping keys to content files "       \
    "(Default: content.txt\n"                                                 \
    "  -p [listen_port]    Listen port (Default: 56726)\n"                    \
    "  -d [delay]          Delay in content_get, default 0, range 0-5000000 " \
    "(microseconds)\n "

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"help", no_argument, NULL, 'h'},
    {"delay", required_argument, NULL, 'd'},
    {"nthreads", required_argument, NULL, 't'},
    {"port", required_argument, NULL, 'p'},
    {"content", required_argument, NULL, 'm'},
    {NULL, 0, NULL, 0}};

extern unsigned long int content_delay;

extern gfh_error_t gfs_handler(gfcontext_t** ctx, char const* path, void* arg);

static void _sig_handler(int signo) {
    if ((SIGINT == signo) || (SIGTERM == signo)) {
        exit(signo);
    }
}

/* Main ========================================================= */
int main(int argc, char** argv) {
    char*          content_map = "content.txt";
    gfserver_t*    gfs         = NULL;
    int            nthreads    = 16;
    unsigned short port        = 56726;
    int            option_char = 0;

    setbuf(stdout, NULL);

    if (SIG_ERR == signal(SIGINT, _sig_handler)) {
        fprintf(stderr, "Can't catch SIGINT...exiting.\n");
        exit(EXIT_FAILURE);
    }

    if (SIG_ERR == signal(SIGTERM, _sig_handler)) {
        fprintf(stderr, "Can't catch SIGTERM...exiting.\n");
        exit(EXIT_FAILURE);
    }

    // Parse and set command line arguments
    while ((option_char = getopt_long(
                argc, argv, "p:d:rhm:t:", gLongOptions, NULL)) != -1) {
        switch (option_char) {
        case 'h': /* help */
            fprintf(stdout, "%s", USAGE);
            exit(0);
            break;
        case 'p': /* listen-port */
            port = atoi(optarg);
            break;
        case 'd': /* delay */
            content_delay = (unsigned long int)atoi(optarg);
            break;
        case 't': /* nthreads */
            nthreads = atoi(optarg);
            break;
        case 'm': /* file-path */
            content_map = optarg;
            break;
        default:
            fprintf(stderr, "%s", USAGE);
            exit(1);
        }
    }

    /* not useful, but it ensures the initial code builds without warnings */
    if (nthreads < 1) {
        nthreads = 1;
    }

    if (content_delay > 5000000) {
        fprintf(stderr,
                "Content delay must be less than 5000000 (microseconds)\n");
        exit(__LINE__);
    }

    content_init(content_map);

    /* Initialize thread management */

    /*Initializing server*/
    gfs = gfserver_create();

    // Setting options
    gfserver_set_port(&gfs, port);
    gfserver_set_maxpending(&gfs, 24);

    // setup the source to get file content from get_content.
    Source source;
    content_source_init(&source);

    // and use the native handler client to call back into gfserver.
    HandlerClient handlerClient;
    hc_init_native(&handlerClient);

    // start the handler
    MultiThreadedHandler* handler =
        mth_start(nthreads, &handlerClient, &source);

    // cause gfs_handler knows to call a MutiThreadedHandler, but it needs to
    // know which one.
    gfserver_set_handler(&gfs, gfs_handler);
    gfserver_set_handlerarg(&gfs, handler);

    // run it
    gfserver_serve(&gfs);

    // for testing purposes, the server may be shut down.  do the remaining
    // cleanup carefully.  Finish processing first.
    mth_finish(handler);

    // then destroy the server
    // this part closes the listening socket
    gfserver_destroy(&gfs);
}
#endif

/////////////////////////////////////////////////////////////
// Multi Threaded Handler
/////////////////////////////////////////////////////////////

// this is the data that's created for each worker.  it's actually shared by all
// workers in this case.  there's no persistent local state for any of the
// workers.
typedef struct {
    // the handler client we talk to
    HandlerClient* handlerClient;
    // and the source of files.
    Source* source;
} MthWorkerData;

// an individual task for a worker.  the task takes ownership of the ctx and
// owns the path.
typedef struct {
    gfcontext_t* ctx;
    char*        path;
} MthTask;

// create a task, duplicating the string and taking ownership of the ctx.
MthTask* mth_task_create_(gfcontext_t** ctx, char const* const path) {
    MthTask* task = (MthTask*)calloc(1, sizeof(MthTask));
    task->ctx     = *ctx;
    *ctx          = NULL;
    task->path    = strdup(path);
    return task;
}

void mth_task_destroy_(MthTask* task) {
    assert(!task->ctx);
    free(task->path);
    free(task);
}

// the handler itself
struct MultiThreadedHandlerTag {
    // the pool we're going to use
    WorkerPool* pool;
    // and the workerData shared by all workers.
    MthWorkerData workerData;
};

static size_t min_(size_t a, size_t b) {
    return a > b ? b : a;
}

static void* mth_create_worker_data_(void* workerData) {
    // workerData is passed into wp_start as the global data and this just
    // copies that pointer to output
    return workerData;
}

// where the actual work goes down.  it's passed a task and the worker data.
static void mth_do_work_(void* task_, void* workerData_) {
    MthTask*       task       = (MthTask*)task_;
    MthWorkerData* workerData = (MthWorkerData*)workerData_;
    // ask the source for the file and get its size
    size_t size    = 0;
    void*  session = source_start(workerData->source, task->path, &size);
    if (!session) {
        // something wrong, assume file not found
        hc_send_header(
            workerData->handlerClient, &task->ctx, GF_FILE_NOT_FOUND, size);
        goto EXIT_POINT;
    }

    // send the header
    hc_send_header(workerData->handlerClient, &task->ctx, GF_OK, size);

    size_t sent = 0;
    while (sent < size) {
        // read some and write some
        uint8_t       buffer[1024];
        ssize_t const read = source_read(workerData->source,
                                         session,
                                         buffer,
                                         min_(sizeof(buffer), size - sent));
        if (read < 0) {
            // bad read, abort should take the context here.
            hc_abort(workerData->handlerClient, &task->ctx);
            goto EXIT_POINT;
        }
        // send should take the context when we're done
        hc_send(workerData->handlerClient, &task->ctx, buffer, (size_t)read);
        sent += (size_t)read;
    }
EXIT_POINT:
    if (session) {
        // finish up the source
        source_finish(workerData->source, session);
    }
    // and destroy the task
    mth_task_destroy_(task);
}

MultiThreadedHandler* mth_start(size_t         numThreads,
                                HandlerClient* handlerClient,
                                Source*        source) {
    MultiThreadedHandler* out =
        (MultiThreadedHandler*)calloc(1, sizeof(MultiThreadedHandler));
    out->workerData.handlerClient = handlerClient;
    out->workerData.source        = source;
    out->pool                     = wp_start(
        numThreads, mth_do_work_, mth_create_worker_data_, &out->workerData);
    return out;
}

void mth_process(MultiThreadedHandler* mtc,
                 gfcontext_t**         ctx,
                 char const*           path) {
    wp_add_task(mtc->pool, mth_task_create_(ctx, path));
}

void mth_finish(MultiThreadedHandler* mtc) {
    // finish up.  there's no worker data to destroy.
    wp_finish(mtc->pool, NULL, NULL);
    free(mtc);
}

/////////////////////////////////////////////////////////
// Handler Client
/////////////////////////////////////////////////////////

void hc_initialize(HandlerClient* client,
                   HcSendHeader   sendHeader,
                   HcSend         send,
                   HcAbort        abort,
                   void*          clientData) {
    client->sendHeader = sendHeader;
    client->send       = send;
    client->abort      = abort;
    client->clientData = clientData;
}

ssize_t hc_send_header(HandlerClient* client,
                       gfcontext_t**  ctx,
                       gfstatus_t     status,
                       size_t         fileLen) {
    return client->sendHeader(ctx, status, fileLen, client->clientData);
}

ssize_t hc_send(HandlerClient* client,
                gfcontext_t**  ctx,
                void const*    data,
                size_t         size) {
    return client->send(ctx, data, size, client->clientData);
}

void hc_abort(HandlerClient* client, gfcontext_t** ctx) {
    client->abort(ctx, client->clientData);
}

static ssize_t hc_native_send_header_(gfcontext_t** ctx,
                                      gfstatus_t    status,
                                      size_t        fileLen,
                                      void*         clientData) {
    (void)clientData;
    return gfs_sendheader((gfcontext_t**)ctx, status, fileLen);
}

static ssize_t hc_native_send_(gfcontext_t** ctx,
                               void const*   data,
                               size_t const  size,
                               void*         clientData) {
    (void)clientData;
    return gfs_send((gfcontext_t**)ctx, data, size);
}

static void hc_native_abort_(gfcontext_t** ctx, void* clientData) {
    (void)clientData;
    gfs_abort((gfcontext_t**)ctx);
}

void hc_init_native(HandlerClient* client) {
    hc_initialize(client,
                  hc_native_send_header_,
                  hc_native_send_,
                  hc_native_abort_,
                  NULL);
}

/////////////////////////////////////////////////////////
// Source
/////////////////////////////////////////////////////////

void source_initialize(Source*         source,
                       SourceStartFcn  startFcn,
                       SourceReadFcn   readFcn,
                       SourceFinishFcn finishFcn,
                       void*           sourceData) {
    memset(source, 0, sizeof(Source));
    source->startFcn   = startFcn;
    source->readFcn    = readFcn;
    source->finishFcn  = finishFcn;
    source->sourceData = sourceData;
}

void* source_start(Source* source, char const* path, size_t* size) {
    return source->startFcn(source->sourceData, path, size);
}

ssize_t source_read(Source* const source,
                    void* const   session,
                    void* const   buffer,
                    size_t const  n) {
    return source->readFcn(source->sourceData, session, buffer, n);
}

int source_finish(Source* source, void* session) {
    return source->finishFcn(source->sourceData, session);
}

// session object used for reading from the content repository, we need to keep
// up with the fid and where we are (because other requests could be accessing
// the same file).
typedef struct {
    int   fid;
    off_t numRead;
} ContentSession;

static void* content_source_start_(void*             sourceData,
                                   char const* const path,
                                   size_t* const     size) {
    (void)sourceData;
    int fid = content_get(path);
    if (fid == -1) {
        return NULL;
    }
    struct stat st;
    if (fstat(fid, &st) == -1) {
        return NULL;
    }
    *size = (size_t)st.st_size;
    ContentSession* session =
        (ContentSession*)calloc(1, sizeof(ContentSession));
    session->fid     = fid;
    session->numRead = 0;
    return session;
}

static ssize_t content_source_read_(void* const  sourceData,
                                    void* const  sessionData,
                                    void* const  buffer,
                                    size_t const nBytes) {
    (void)sourceData;
    ContentSession* session = (ContentSession*)sessionData;
    ssize_t const   out = pread(session->fid, buffer, nBytes, session->numRead);
    if (out > 0) {
        session->numRead += out;
    }
    return out;
}

static int content_source_finish_(void* const sourceData,
                                  void* const sessionData) {
    (void)sourceData;
    // mustn't close the fid, that's managed by the content oracle.
    free(sessionData);
    return 0;
}

void content_source_init(Source* source) {
    source_initialize(source,
                      content_source_start_,
                      content_source_read_,
                      content_source_finish_,
                      NULL);
}
