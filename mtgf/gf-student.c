#define _POSIX_C_SOURCE 200809L
#include "gf-student.h"

#include "content.h"
#include "gfclient.h"
#include "steque.h"

#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <memory.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/////////////////////////////////////////////////////////////
// Concurrent Queue
/////////////////////////////////////////////////////////////

struct QueueTag {
    steque_t        base;
    pthread_mutex_t mutex;
    pthread_cond_t  notEmpty;
};

Queue* queue_create() {
    Queue* out = (Queue*)calloc(1, sizeof(Queue));
    steque_init(&out->base);
    pthread_mutex_init(&out->mutex, NULL);
    pthread_cond_init(&out->notEmpty, NULL);
    return out;
}

void queue_enqueue_n(Queue* const q, QueueItem* const items, size_t const n) {
    if (n == 0) {
        return;
    }
    pthread_mutex_lock(&q->mutex);
    for (size_t i = 0; i < n; ++i) {
        steque_enqueue(&q->base, items[i]);
    }
    pthread_mutex_unlock(&q->mutex);
    if (n == 1) {
        pthread_cond_signal(&q->notEmpty);
    } else {
        pthread_cond_broadcast(&q->notEmpty);
    }
}

void queue_enqueue(Queue* const q, QueueItem item) {
    queue_enqueue_n(q, &item, 1);
}

QueueItem queue_dequeue(Queue* q) {
    pthread_mutex_lock(&q->mutex);
    while (steque_isempty(&q->base)) {
        pthread_cond_wait(&q->notEmpty, &q->mutex);
    }
    QueueItem const out = steque_pop(&q->base);
    pthread_mutex_unlock(&q->mutex);
    return out;
}

bool queue_empty(Queue const* q) {
    pthread_mutex_lock((pthread_mutex_t*)&q->mutex);
    bool const out = steque_isempty((steque_t*)&q->base);
    pthread_mutex_unlock((pthread_mutex_t*)&q->mutex);
    return out;
}

void queue_destroy(Queue* q) {
    pthread_mutex_lock(&q->mutex);
    steque_destroy(&q->base);
    pthread_cond_destroy(&q->notEmpty);
    pthread_mutex_unlock(&q->mutex);
    pthread_mutex_destroy(&q->mutex);
    free(q);
}

/////////////////////////////////////////////////////////////
// Worker Pool
/////////////////////////////////////////////////////////////

typedef struct {
    WpWork work;
    void*  workerData;
    Queue* queue;
} WpWorker;

static void* const worker_pill_ = (void*)UINTPTR_MAX;

static void worker_init_(WpWorker*    worker,
                         WpWork       work,
                         void* const  workerData,
                         Queue* const queue) {
    memset(worker, 0, sizeof(WpWorker));
    worker->work       = work;
    worker->workerData = workerData;
    worker->queue      = queue;
}

static void worker_do_task_(WpWorker* const worker, WpTask task) {
    worker->work(task, worker->workerData);
}

static void* worker_work_(void* worker_) {
    WpWorker* const worker = (WpWorker*)worker_;
    WpTask          task   = worker_pill_;
    while ((task = queue_dequeue(worker->queue)) != worker_pill_) {
        worker_do_task_(worker, task);
    }
    return NULL;
}

struct WorkerPoolTag {
    size_t    numWorkers;
    WpWorker* workers;

    Queue*     queue;
    pthread_t* threads;
};

WorkerPool* wp_start(size_t const       numWorkers,
                     WpWork             work,
                     WpCreateWorkerData createWorkerData,
                     void* const        globalData) {
    assert(numWorkers > 0);
    WorkerPool* out = (WorkerPool*)calloc(1, sizeof(WorkerPool));
    out->numWorkers = numWorkers;
    out->workers    = (WpWorker*)calloc(out->numWorkers, sizeof(WpWorker));
    out->queue      = queue_create();
    out->threads    = (pthread_t*)calloc(out->numWorkers, sizeof(pthread_t));
    for (size_t i = 0; i < out->numWorkers; ++i) {
        void* const workerData =
            createWorkerData ? createWorkerData(globalData) : NULL;
        worker_init_(out->workers + i, work, workerData, out->queue);
        pthread_create(out->threads + i, NULL, worker_work_, out->workers + i);
    }
    return out;
}

void wp_add_tasks(WorkerPool* const wp,
                  WpTask* const     tasks,
                  size_t const      numTasks) {
    queue_enqueue_n(wp->queue, tasks, numTasks);
}

void wp_add_task(WorkerPool* const wp, WpTask const task) {
    queue_enqueue(wp->queue, task);
}

void wp_finish(WorkerPool* const   wp,
               WpDestroyWorkerData destroyWorkerData,
               void*               globalData) {
    WpTask* pills = (WpTask*)calloc(wp->numWorkers, sizeof(WpTask));
    for (size_t i = 0; i < wp->numWorkers; ++i) {
        pills[i] = worker_pill_;
    }
    wp_add_tasks(wp, pills, wp->numWorkers);
    free(pills);
    for (size_t i = 0; i < wp->numWorkers; ++i) {
        pthread_join(wp->threads[i], NULL);
        if (destroyWorkerData) {
            destroyWorkerData(wp->workers[i].workerData, globalData);
        }
    }
    free(wp->threads);
    free(wp->workers);
    queue_destroy(wp->queue);
    free(wp);
}

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

/////////////////////////////////////////////////////////////
// Multi Threaded Handler
/////////////////////////////////////////////////////////////

typedef struct {
    HandlerClient* handlerClient;
    Source*        source;
} MthWorkerData;

typedef struct {
    GfContext* ctx;
    char*      path;
} MthTask;

MthTask* mth_task_create_(GfContext** ctx, char const* const path) {
    MthTask* task = (MthTask*)calloc(1, sizeof(MthTask));
    task->ctx     = *ctx;
    *ctx          = NULL;
    task->path    = strdup(path);
    return task;
}

void mth_task_destroy_(MthTask* task) {
    free(task->path);
    free(task);
}

struct MultiThreadedHandlerTag {
    WorkerPool*   pool;
    MthWorkerData workerData;
};

static size_t min_(size_t a, size_t b) {
    return a > b ? b : a;
}

static void* mth_create_worker_data_(void* workerData) {
    return workerData;
}

static void mth_do_work_(void* task_, void* workerData_) {
#define LOCAL_GF_OK             200
#define LOCAL_GF_FILE_NOT_FOUND 400
#define LOCAL_GF_ERROR          500
#define LOCAL_GF_INVALID        600
    MthTask*       task       = (MthTask*)task_;
    MthWorkerData* workerData = (MthWorkerData*)workerData_;
    size_t         size       = 0;
    void* session = source_start(workerData->source, task->path, &size);
    if (!session) {
        hc_send_header(workerData->handlerClient,
                       &task->ctx,
                       LOCAL_GF_FILE_NOT_FOUND,
                       size);
        goto EXIT_POINT;
    }
    hc_send_header(workerData->handlerClient, &task->ctx, LOCAL_GF_OK, size);

    size_t sent = 0;
    while (sent < size) {
        uint8_t       buffer[1024];
        ssize_t const read = source_read(workerData->source,
                                         session,
                                         buffer,
                                         min_(sizeof(buffer), size - sent));
        if (read < 0) {
            hc_abort(workerData->handlerClient, &task->ctx);
            goto EXIT_POINT;
        }
        hc_send(workerData->handlerClient, &task->ctx, buffer, (size_t)read);
        sent += (size_t)read;
    }
EXIT_POINT:
    if (session) {
        source_finish(workerData->source, session);
    }
    mth_task_destroy_(task);
#undef LOCAL_GF_OK
#undef LOCAL_GF_FILE_NOT_FOUND
#undef LOCAL_GF_ERROR
#undef LOCAL_GF_INVALID
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

void mth_process(MultiThreadedHandler* mtc, GfContext** ctx, char const* path) {
    wp_add_task(mtc->pool, mth_task_create_(ctx, path));
}

void mth_finish(MultiThreadedHandler* mtc) {
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
                       GfContext**    ctx,
                       GfStatus       status,
                       size_t         fileLen) {
    return client->sendHeader(ctx, status, fileLen, client->clientData);
}

ssize_t hc_send(HandlerClient* client,
                GfContext**    ctx,
                void const*    data,
                size_t         size) {
    return client->send(ctx, data, size, client->clientData);
}

void hc_abort(HandlerClient* client, GfContext** ctx) {
    client->abort(ctx, client->clientData);
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
        perror("fstat");
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
    int const fid = *(int*)sessionData;
    // don't close contents fids! just move them back to the beginning
    lseek(fid, 0, SEEK_SET);
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
