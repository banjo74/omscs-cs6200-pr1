#ifndef __GF_STUDENT_H__
#define __GF_STUDENT_H__

#include <sys/types.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////
// Concurrent Queue
/////////////////////////////////////////////////////////////

typedef struct QueueTag Queue;
typedef void*           QueueItem;

// create a concurrent queue
Queue* queue_create();

// atomically push many values onto the queue
void queue_enqueue_n(Queue*, QueueItem*, size_t numItems);

// push a value onto the queue
void queue_enqueue(Queue*, QueueItem);

// block until an item is available on the queue.  remove and return the
// earliest inserted item.
QueueItem queue_dequeue(Queue*);

// return true if empty
bool queue_empty(Queue const*);

// destroy the queue, woe betide anybody trying to enqueue or dequeue at the
// same time.
void queue_destroy(Queue*);

/////////////////////////////////////////////////////////////
// Worker Pool
/////////////////////////////////////////////////////////////

typedef struct WorkerPoolTag WorkerPool;
typedef void*                WpTask;

typedef void* (*WpCreateWorkerData)(void* globalData);
typedef void (*WpDestroyWorkerData)(void* workerData, void* globalData);
typedef void (*WpWork)(WpTask, void* workerData);

// Start a WorkerPool thread pool with numWorkers.
// work is the function to apply to each task.  when the Worker Pool is ready,
// it will invoke work on a task.
//
// Each worker can have its own local data.  This is created with
// createWorkerData and is invoked once, sequentially, for each worker.
WorkerPool* wp_start(size_t             numWorkers,
                     WpWork             work,
                     WpCreateWorkerData createWorkerData,
                     void*              globalData);

// Atomically added numTasks to the set of tasks to be done.  Tasks will be
// *started* in the order in which they were added.
void wp_add_tasks(WorkerPool* wp, WpTask* tasks, size_t numTasks);

// Add one task to the set of tasks to be done.  Tasks will be
// *started* in the order in which they were added.
void wp_add_task(WorkerPool* wp, WpTask);

// Finish processing all tasks and, destroy worker data with destroyWorkerData
// (sequentailly) and destroy the WorkerPool
void wp_finish(WorkerPool*,
               WpDestroyWorkerData destroyWorkerData,
               void*               globalData);

/////////////////////////////////////////////////////////////
// Multi Threaded Client
/////////////////////////////////////////////////////////////

typedef struct MultiThreadedClientTag MultiThreadedClient;
typedef struct SinkTag                Sink;

MultiThreadedClient* mtc_start(char const*    server,
                               unsigned short port,
                               size_t         numThreads,
                               Sink*          sink);

void mtc_process(MultiThreadedClient* mtc,
                 char const*          reqPath,
                 char const*          localPath);

void mtc_finish(MultiThreadedClient*);

/////////////////////////////////////////////////////////////
// Multi Threaded Handler
/////////////////////////////////////////////////////////////

typedef struct MultiThreadedHandlerTag MultiThreadedHandler;
typedef struct HandlerClientTag        HandlerClient;
typedef struct SourceTag               Source;

// I'm not that proud of this but gfserver.h and gfclient.h cannot peacfully
// coexist in one translation unit because GF_OK and friends are enum values in
// gfclient.h but preprocessor macros in gfserver.h.  So, I cant get the actual
// definitino of gfcontext_t into this translation unit.  Anywhere you see
// GfContext, think gfcontext_t.  gfs_handler does the cast.
typedef struct GfContextTag GfContext;
typedef int                 GfStatus;

MultiThreadedHandler* mth_start(size_t         numThreads,
                                HandlerClient* handlerClient,
                                Source*        source);

void mth_process(MultiThreadedHandler* mtc, GfContext** ctx, char const* path);

void mth_finish(MultiThreadedHandler*);

/////////////////////////////////////////////////////////////
// Handler Client
/////////////////////////////////////////////////////////////

typedef ssize_t (*HcSendHeader)(GfContext**,
                                GfStatus status,
                                size_t   fileLen,
                                void*);
typedef ssize_t (*HcSend)(GfContext**, void const* data, size_t size, void*);
typedef void (*HcAbort)(GfContext**, void*);

struct HandlerClientTag {
    HcSendHeader sendHeader;
    HcSend       send;
    HcAbort      abort;
    void*        clientData;
};

void hc_initialize(HandlerClient*,
                   HcSendHeader sendHeader,
                   HcSend       send,
                   HcAbort      abort,
                   void*        clientData);

ssize_t hc_send_header(HandlerClient*,
                       GfContext**,
                       GfStatus status,
                       size_t   fileLen);

ssize_t hc_send(HandlerClient*, GfContext**, void const* data, size_t size);

void hc_abort(HandlerClient*, GfContext**);

/////////////////////////////////////////////////////////
// Sink
/////////////////////////////////////////////////////////

// see Sink below
typedef void* (*SinkStartFcn)(void* sinkData, char const* path);

// see Sink below
typedef ssize_t (*SinkSendFcn)(void*       sinkData,
                               void*       sessionData,
                               void const* buffer,
                               size_t      n);

// see Sink below
typedef void (*SinkCancelFcn)(void* sinkData, void* sessionData);

// see Sink below
typedef int (*SinkFinishFcn)(void* sinkData, void* sessionData);

/*!
 Sink is an abstraction of a data sink
 */
struct SinkTag {
    // session = sink->startFcn(sink->sinkData, path);
    // starts a data read session of path, the returned value may be used
    // with sendFcn.  sink_start is a convenience call to this function.
    SinkStartFcn startFcn;

    // nWritten = sink->sendFcn(sink, session, buffer, nBytes)
    // sends nBytes pointed to by buffer to the sink for this session.
    // upon success nWritten == nBytes
    //
    // sink_send is a convenience call to this function
    SinkSendFcn sendFcn;

    // sink->cancelFcn(sink->sinkData, session)
    // cancels the current session leaving no side effects
    //
    // sink_cancel is a convenience call to this function
    SinkCancelFcn cancelFcn;

    // sink->finishFcn(sink->sinkData, session)
    // successfully finishes the session.
    //
    // sink_finish is a convenience call to this function
    SinkFinishFcn finishFcn;

    // client data
    void* sinkData;
};

// initialize an empty sink with the provided start, send, cancel, finish, and
// data.
void sink_initialize(Sink* emptyClient,
                     SinkStartFcn,
                     SinkSendFcn,
                     SinkCancelFcn,
                     SinkFinishFcn,
                     void* sinkData);

// see startFcn in Sink above
void* sink_start(Sink* sink, char const* path);

// see sendFcn in Sink above
ssize_t sink_send(Sink* sink, void*, void const*, size_t);

// see cancelFcn in Sink above
void sink_cancel(Sink* sink, void*);

// see finishFcn in Sink above
int sink_finish(Sink* sink, void*);

// initialize a sink to be a file sink
void fsink_init(Sink*);

/////////////////////////////////////////////////////////
// Source
/////////////////////////////////////////////////////////

// See Source below.
typedef void* (*SourceStartFcn)(void* sourceData, char const* path, size_t*);

// See Source below.
typedef ssize_t (*SourceReadFcn)(void*  sourceData,
                                 void*  sessionData,
                                 void*  buffer,
                                 size_t max);

// See Source below.
typedef int (*SourceFinishFcn)(void* sourceData, void* sessionData);

/*!
 Source is an abstraction of a data source
 */
struct SourceTag {
    // session = source->startFcn(source->sourceData);
    // starts a data read session, the returned value may be used
    // with readFcn.  source_start is a convenience call to this function.
    // upon success returns a non-NULL value and sets *size to the number of
    // bytes that are available.
    SourceStartFcn startFcn;

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
    SourceReadFcn readFcn;

    // source->finishFcn(source->sourceData, session)
    // terminates the session.  releases any resources used by the
    // session and invalidates session
    //
    // source_finish is a convenience call to this function.
    SourceFinishFcn finishFcn;

    // client data.
    void* sourceData;
};

// initialize a Source with the provided, start, read, finish, and data.
void source_initialize(Source* emptyClient,
                       SourceStartFcn,
                       SourceReadFcn,
                       SourceFinishFcn,
                       void* sourceData);

// start a session.  see startFcn in Source above
void* source_start(Source* source, char const*, size_t*);

// read some data.  see readFcn in Source above
ssize_t source_read(Source* source, void*, void*, size_t);

// finish a session, see finishFcn in Source above
int source_finish(Source* source, void*);

// initialize a source to read files from the disk
void content_source_init(Source*);

#ifdef __cplusplus
}
#endif

#endif // __GF_STUDENT_H__
