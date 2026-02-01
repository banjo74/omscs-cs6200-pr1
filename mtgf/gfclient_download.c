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

    // add your threadpool creation here

    Sink sink;
    fsink_init(&sink);
    MultiThreadedClient* mtc = mtc_start(server, port, nthreads, &sink);
    for (int i = 0; i < nrequests; i++) {
        req_path = workload_get_path();

        if (strlen(req_path) > PATH_BUFFER_SIZE) {
            fprintf(stderr,
                    "Request path exceeded maximum of %d characters\n.",
                    PATH_BUFFER_SIZE);
            exit(EXIT_FAILURE);
        }

        localPath(req_path, local_path);
        mtc_process(mtc, req_path, local_path);
    }

    mtc_finish(mtc);

    gfc_global_cleanup(); /* use for any global cleanup for AFTER your thread
                           pool has terminated. */

    return 0;
}
#endif // TEST_MODE

/////////////////////////////////////////////////////////////
// Multi Threaded Client
/////////////////////////////////////////////////////////////

typedef struct {
    Sink*          sink;
    char*          server;
    unsigned short port;
} MtcWorkerData;

struct MultiThreadedClientTag {
    WorkerPool*   pool;
    MtcWorkerData workerData;
};

typedef struct {
    char* reqPath;
    char* localPath;
} MtcTask;

MtcTask* mtc_create_task_(char const* const reqPath,
                          char const* const localPath) {
    MtcTask* out   = (MtcTask*)calloc(1, sizeof(MtcTask));
    out->reqPath   = strdup(reqPath);
    out->localPath = strdup(localPath);
    return out;
}

void mtc_destroy_task_(MtcTask* task) {
    free(task->reqPath);
    free(task->localPath);
    free(task);
}

typedef struct {
    Sink* sink;
    void* session;
} MtcWriteFcnData;

void mtc_write_fcn_(void* buffer, size_t const size, void* data_) {
    MtcWriteFcnData* data = (MtcWriteFcnData*)data_;
    sink_send(data->sink, data->session, buffer, size);
}

void* mtc_create_worker_data_(void* workerData_) {
    return workerData_;
}

void mtc_do_request_(void* task_, void* workerData_) {
    MtcTask*       task        = (MtcTask*)task_;
    MtcWorkerData* workerData  = (MtcWorkerData*)workerData_;
    void* const    sinkSession = sink_start(workerData->sink, task->localPath);
    if (!sinkSession) {
        return;
    }

    gfcrequest_t* req = gfc_create();
    gfc_set_path(&req, task->reqPath);
    gfc_set_port(&req, workerData->port);
    gfc_set_server(&req, workerData->server);
    gfc_set_writefunc(&req, mtc_write_fcn_);
    MtcWriteFcnData data = {.sink = workerData->sink, .session = sinkSession};
    gfc_set_writearg(&req, &data);

    if (gfc_perform(&req) < 0 || gfc_get_status(&req) != GF_OK) {
        fprintf(stderr, "failed to read %s\n", task->reqPath);
        sink_cancel(workerData->sink, sinkSession);
        return;
    }
    sink_finish(workerData->sink, sinkSession);
    gfc_cleanup(&req);
    fprintf(stdout, "read %s\n", task->reqPath);

    mtc_destroy_task_(task);
}

MultiThreadedClient* mtc_start(char const*    server,
                               unsigned short port,
                               size_t         numThreads,
                               Sink*          sink) {
    MultiThreadedClient* out =
        (MultiThreadedClient*)calloc(1, sizeof(MultiThreadedClient));
    out->workerData.sink   = sink;
    out->workerData.server = strdup(server);
    out->workerData.port   = port;
    out->pool              = wp_start(
        numThreads, mtc_do_request_, mtc_create_worker_data_, &out->workerData);
    return out;
}

void mtc_process(MultiThreadedClient* mtc,
                 char const*          reqPath,
                 char const*          localPath) {
    wp_add_task(mtc->pool, mtc_create_task_(reqPath, localPath));
}

void mtc_finish(MultiThreadedClient* mtc) {
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

typedef struct {
    FILE* file;
    char* path;
} FileSinkSession;

FileSinkSession* fsink_session_create_(FILE* fh, char const* path) {
    FileSinkSession* session =
        (FileSinkSession*)calloc(1, sizeof(FileSinkSession));
    session->file = fh;
    session->path = strdup(path);
    return session;
}

void fsink_session_destroy_(FileSinkSession* session) {
    free(session->path);
    free(session);
}

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
                exit(EXIT_FAILURE);
            }
        }

        *cur = '/';
        prev = cur;
    }

    if (NULL == (ans = fopen(&path[0], "w"))) {
        perror("Unable to open file");
        exit(EXIT_FAILURE);
    }

    return ans;
}

// the start function for the file transfer sink.  Opens the file.  Use STDLIB
// file API instead of system.
static void* file_sink_start_(void* const sinkData, char const* const path) {

    (void)sinkData;
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

// close the file and then remove it.  ignore remove errors.
static void file_sink_cancel_(void* const sinkData, void* const session_) {
    (void)sinkData;
    FileSinkSession* session = (FileSinkSession*)session_;
    fclose(session->file);
    remove(session->path);
    fsink_session_destroy_(session);
}

// close the file
static int file_sink_finish_(void* const sinkData, void* const session_) {
    (void)sinkData;
    FileSinkSession* session = (FileSinkSession*)session_;
    fclose(session->file);
    fsink_session_destroy_(session);
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
