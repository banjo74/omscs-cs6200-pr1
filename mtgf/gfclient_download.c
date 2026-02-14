#define _POSIX_C_SOURCE 200809L
#include "gf-student.h"
#include "gfclient-student.h"
#include "gfclient.h"
#include "workload.h"

#include <sys/stat.h>

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_THREADS      1024
#define PATH_BUFFER_SIZE 512

#define USAGE                                                               \
    "usage:\n"                                                              \
    "  gfclient_download [options]\n"                                       \
    "options:\n"                                                            \
    "  -h                  Show this help message\n"                        \
    "  -s [server_addr]    Server address (Default: 127.0.0.1)\n"           \
    "  -p [server_port]    Server port (Default: 56726)\n"                  \
    "  -w [workload_path]  Path to workload file (Default: workload.txt)\n" \
    "  -t [nthreads]       Number of threads (Default 8 Max: 1024)\n"       \
    "  -n [num_requests]   Request download total (Default: 16)\n"

#if !defined(TEST_MODE)
/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"nrequests", required_argument, NULL, 'n'},
    {"port", required_argument, NULL, 'p'},
    {"nthreads", required_argument, NULL, 't'},
    {"server", required_argument, NULL, 's'},
    {"help", no_argument, NULL, 'h'},
    {"workload", required_argument, NULL, 'w'},
    {NULL, 0, NULL, 0}};

static void Usage() {
    fprintf(stderr, "%s", USAGE);
}

static void localPath(char* req_path, char* local_path) {
    static int counter = 0;

    sprintf(local_path, "%s-%06d", &req_path[1], counter++);
}

static void report_(void*        arg,
                    bool         success,
                    char const*  reqPath,
                    size_t const expected,
                    size_t const received) {
    if (!success) {
        fprintf(stderr,
                "failed to read %s.\nreceived %zu bytes\nexpecting %zu bytes\n",
                reqPath,
                received,
                expected);
        return;
    }
    fprintf(stdout, "received %s with %zu bytes\n", reqPath, received);
}

// if you're looking for openFile, it's been moved below and name changed to
// open_file_

/* Main ========================================================= */
int main(int argc, char** argv) {
    /* COMMAND LINE OPTIONS ============================================= */
    char*          workload_path = "workload.txt";
    char*          server        = "localhost";
    int            option_char   = 0;
    unsigned short port          = 56726;
    char*          req_path      = NULL;

    int  nthreads = 8;
    char local_path[PATH_BUFFER_SIZE];
    int  nrequests = 14;

    setbuf(stdout, NULL); // disable caching

    // Parse and set command line arguments
    while ((option_char = getopt_long(
                argc, argv, "p:n:hs:t:r:w:", gLongOptions, NULL)) != -1) {
        switch (option_char) {

        case 's': // server
            server = optarg;
            break;
        case 'w': // workload-path
            workload_path = optarg;
            break;
        case 'r': // nrequests
        case 'n': // nrequests
            nrequests = atoi(optarg);
            break;
        case 't': // nthreads
            nthreads = atoi(optarg);
            break;
        case 'p': // port
            port = atoi(optarg);
            break;
        default:
            Usage();
            exit(1);

        case 'h': // help
            Usage();
            exit(0);
        }
    }

    if (EXIT_SUCCESS != workload_init(workload_path)) {
        fprintf(stderr, "Unable to load workload file %s.\n", workload_path);
        exit(EXIT_FAILURE);
    }
    if (port > 65331) {
        fprintf(stderr, "Invalid port number\n");
        exit(EXIT_FAILURE);
    }
    if (nthreads < 1 || nthreads > MAX_THREADS) {
        fprintf(stderr, "Invalid amount of threads\n");
        exit(EXIT_FAILURE);
    }
    gfc_global_init();

    // setup a file sink.
    Sink sink;
    fsink_init(&sink);

    // start the client running, we're going to start making requests below.
    MultiThreadedClient* mtc =
        mtc_start(server, port, nthreads, &sink, report_, NULL);

    for (int i = 0; i < nrequests; i++) {
        req_path = workload_get_path();

        if (strlen(req_path) > PATH_BUFFER_SIZE) {
            fprintf(stderr,
                    "Request path exceeded maximum of %d characters\n.",
                    PATH_BUFFER_SIZE);
            exit(EXIT_FAILURE);
        }

        localPath(req_path, local_path);
        // post the request
        mtc_process(mtc, req_path, local_path);
    }

    // finish up the MultiThreadedClient.  All requests will be processed once
    // this call returns.
    mtc_finish(mtc);

    gfc_global_cleanup(); /* use for any global cleanup for AFTER your thread
                           pool has terminated. */

    return 0;
}
#endif // TEST_MODE

/////////////////////////////////////////////////////////////
// Multi Threaded Client
/////////////////////////////////////////////////////////////

// worker data used by all client workers.  there's no mutable dat in this
// worker data so they all share the same data.
typedef struct {
    Sink*          sink;
    char*          server;
    unsigned short port;
    ReportFcn      reportFcn;
    void*          reportFcnArg;
} MtcWorkerData;

// the actual MultiThreadedClient.  Holds the pool and the workerData.
struct MultiThreadedClientTag {
    WorkerPool*   pool;
    MtcWorkerData workerData;
};

// a single task that represents one request.  it owns the reqPath and
// localPath.
typedef struct {
    char* reqPath;
    char* localPath;
} MtcTask;

// create a task with reqPath and localPath.
static MtcTask* mtc_create_task_(char const* const reqPath,
                                 char const* const localPath) {
    MtcTask* out   = (MtcTask*)calloc(1, sizeof(MtcTask));
    out->reqPath   = strdup(reqPath);
    out->localPath = strdup(localPath);
    return out;
}

// and destroy a task, free'ing the strings and the structure itself.
static void mtc_destroy_task_(MtcTask* task) {
    free(task->reqPath);
    free(task->localPath);
    free(task);
}

// this is the object used as the argument for the writefunc in the gfcrequest.
// it's got what we need to call the sink.  it owns none of its data.  and is
// create on the stack below.
typedef struct {
    Sink* sink;
    void* session;
} MtcWriteFcnData;

// this is the function used as the writefunc in the gfcrequest.
static void mtc_write_fcn_(void* buffer, size_t const size, void* data_) {
    MtcWriteFcnData* data = (MtcWriteFcnData*)data_;
    sink_send(data->sink, data->session, buffer, size);
}

// this is used with the worker pool to "create" the worker data.  all workers
// share the same data so it just returns the provided argument.
static void* mtc_create_worker_data_(void* workerData_) {
    return workerData_;
}

static void mtc_report_(MtcWorkerData* workerData,
                        bool           success,
                        char const*    request,
                        size_t const   expected,
                        size_t const   received) {
    if (workerData->reportFcn) {
        workerData->reportFcn(
            workerData->reportFcnArg, success, request, expected, received);
    }
}

// where the actual request happens.  it's passed a task and the workerData
static void mtc_do_request_(void* task_, void* workerData_) {
    MtcTask*       task       = (MtcTask*)task_;
    MtcWorkerData* workerData = (MtcWorkerData*)workerData_;

    // start a sink session ready to write
    void* const sinkSession = sink_start(workerData->sink, task->localPath);
    if (!sinkSession) {
        return;
    }

    // create the request and setup all the data.
    gfcrequest_t* req = gfc_create();
    gfc_set_path(&req, task->reqPath);
    gfc_set_port(&req, workerData->port);
    gfc_set_server(&req, workerData->server);
    gfc_set_writefunc(&req, mtc_write_fcn_);
    MtcWriteFcnData data = {.sink = workerData->sink, .session = sinkSession};
    gfc_set_writearg(&req, &data);

    // perform the request and check that it went as expected.
    // do print some kind of status but keep it to just one call to fpintf
    // either way so we don't get jumbled output.
    if (gfc_perform(&req) < 0 || gfc_get_status(&req) != GF_OK) {
        mtc_report_(workerData,
                    false,
                    task->reqPath,
                    gfc_get_filelen(&req),
                    gfc_get_bytesreceived(&req));
        if (workerData->reportFcn) {
        }
        sink_cancel(workerData->sink, sinkSession);
        return;
    }
    sink_finish(workerData->sink, sinkSession);
    mtc_report_(workerData,
                true,
                task->reqPath,
                gfc_get_filelen(&req),
                gfc_get_bytesreceived(&req));
    gfc_cleanup(&req);
    mtc_destroy_task_(task);
}

MultiThreadedClient* mtc_start(char const*    server,
                               unsigned short port,
                               size_t         numThreads,
                               Sink*          sink,
                               ReportFcn      reportFcn,
                               void*          reportFcnArg) {
    MultiThreadedClient* out =
        (MultiThreadedClient*)calloc(1, sizeof(MultiThreadedClient));
    out->workerData.sink         = sink;
    out->workerData.server       = strdup(server);
    out->workerData.port         = port;
    out->workerData.reportFcn    = reportFcn;
    out->workerData.reportFcnArg = reportFcnArg;
    out->pool                    = wp_start(
        numThreads, mtc_do_request_, mtc_create_worker_data_, &out->workerData);
    return out;
}

void mtc_process(MultiThreadedClient* mtc,
                 char const*          reqPath,
                 char const*          localPath) {
    wp_add_task(mtc->pool, mtc_create_task_(reqPath, localPath));
}

void mtc_finish(MultiThreadedClient* mtc) {
    // workers have no data so no need for a destroy function.
    wp_finish(mtc->pool, NULL, NULL);
    free(mtc->workerData.server);
    free(mtc);
}

/////////////////////////////////////////////////////////
// Sink
/////////////////////////////////////////////////////////

void sink_initialize(Sink*         sink,
                     SinkStartFcn  startFcn,
                     SinkSendFcn   sendFcn,
                     SinkCancelFcn cancelFcn,
                     SinkFinishFcn finishFcn,
                     void*         sinkData) {
    memset(sink, 0, sizeof(Sink));
    sink->startFcn  = startFcn;
    sink->sendFcn   = sendFcn;
    sink->cancelFcn = cancelFcn;
    sink->finishFcn = finishFcn;
    sink->sinkData  = sinkData;
}

void* sink_start(Sink* sink, char const* path) {
    return sink->startFcn(sink->sinkData, path);
}

ssize_t sink_send(Sink* const       sink,
                  void* const       session,
                  void const* const buffer,
                  size_t const      n) {
    return sink->sendFcn(sink->sinkData, session, buffer, n);
}

void sink_cancel(Sink* sink, void* session) {
    sink->cancelFcn(sink->sinkData, session);
}

int sink_finish(Sink* sink, void* session) {
    return sink->finishFcn(sink->sinkData, session);
}

// a session for a sink.  we need to keep up with the path in addition to the
// file handle in case we need to unlink it on failure.  This session data owns
// the path and file handle.
typedef struct {
    FILE* file;
    char* path;
} FileSinkSession;

static FileSinkSession* fsink_session_create_(FILE* fh, char const* path) {
    FileSinkSession* session =
        (FileSinkSession*)calloc(1, sizeof(FileSinkSession));
    session->file = fh;
    session->path = strdup(path);
    return session;
}

static void fsink_session_destroy_(FileSinkSession* session) {
    free(session->path);
    free(session);
}

// copied from above but turn the exit's into return NULL.
static FILE* open_file_(char* const path) {
    char *cur, *prev;
    FILE* ans;

    /* Make the directory if it isn't there */
    prev = path;
    while (NULL != (cur = strchr(prev + 1, '/'))) {
        *cur = '\0';

        if (0 > mkdir(&path[0], S_IRWXU)) {
            if (errno != EEXIST) {
                perror("Unable to create directory");
                return NULL;
            }
        }

        *cur = '/';
        prev = cur;
    }

    if (NULL == (ans = fopen(&path[0], "w"))) {
        perror("Unable to open file");
    }

    return ans;
}

// the start function for the file transfer sink.  Opens the file.  defers to
// open_file_ to do the work.
static void* file_sink_start_(void* const sinkData, char const* const path) {

    (void)sinkData;
    // note, open_file_, above, modifies the path passed to it.  create a local
    // copy here just for open_file_.
    char* localPath = strdup(path);
    FILE* fh        = open_file_(localPath);
    free(localPath);
    if (fh) {
        return fsink_session_create_(fh, path);
    }
    return NULL;
}

// basically fwrite
static ssize_t file_sink_send_(void* const       sinkData,
                               void* const       session_,
                               void const* const buffer,
                               size_t const      nBytes) {
    (void)sinkData;
    FileSinkSession* session = (FileSinkSession*)session_;
    return (ssize_t)fwrite(buffer, 1, nBytes, session->file);
}

static void file_sink_done_(void* const sinkData,
                            void* const session_,
                            bool        keep) {
    (void)sinkData;
    FileSinkSession* session = (FileSinkSession*)session_;
    fclose(session->file);
    if (!keep) {
        remove(session->path);
    }
    fsink_session_destroy_(session);
}

// close the file and then remove it.  ignore remove errors.
static void file_sink_cancel_(void* const sinkData, void* const session) {
    file_sink_done_(sinkData, session, false);
}

// close the file
static int file_sink_finish_(void* const sinkData, void* const session) {
    file_sink_done_(sinkData, session, true);
    return 0;
}

void fsink_init(Sink* sink) {
    sink_initialize(sink,
                    file_sink_start_,
                    file_sink_send_,
                    file_sink_cancel_,
                    file_sink_finish_,
                    NULL);
}

