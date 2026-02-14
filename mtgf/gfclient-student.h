/*
 *  This file is for use by students to define anything they wish.  It is used
 * by the gf client implementation
 */
#ifndef __GF_CLIENT_STUDENT_H__
#define __GF_CLIENT_STUDENT_H__

#include <sys/types.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////
// Multi Threaded Client
/////////////////////////////////////////////////////////////

typedef struct MultiThreadedClientTag MultiThreadedClient;
typedef struct SinkTag                Sink;
typedef void (*ReportFcn)(void*,
                          bool        success,
                          char const* reqPath,
                          size_t      expected,
                          size_t      received);

// Start a multi-threaded get file client that will be fetching files from
// server on port.  The client will run on numThreads and will send files it
// receives to sink. (See sink below).
//
// To ask for a file, call mtc_process.  And when done, call mtc_finish.
MultiThreadedClient* mtc_start(char const*    server,
                               unsigned short port,
                               size_t         numThreads,
                               Sink*          sink,
                               ReportFcn      reportFcn,
                               void*          reportFcnArg);

// ask for a single file from the server specified for mtc.  reqPath is the path
// to use in the request to the server,  localPath is the path passed to the
// sink to start writing.  Note, mtc_process schedules the request to be
// processed but the processing will not necessarily be complete when
// mtc_process returns.  See mtc_finish below.
void mtc_process(MultiThreadedClient* mtc,
                 char const*          reqPath,
                 char const*          localPath);

// Finish processing all requests, close down and destroy the mtc.  After this
// function finishes, all requests should be completed.
// TODO: Consider returning a log of all the requests and there statii.
void mtc_finish(MultiThreadedClient* mtc);

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

// Initialize a sink to be a file sink, i.e., actually writes the file to the
// disk.  The startFcn of this sink creates any directories necessary in the
// path and opens the file.  The sendFcn writes data to the file.  The cancelFcn
// will close the file and then unlink it (but doesn't remove andy directories
// it created).  And the finishFcn will close the file leaving it there.
void fsink_init(Sink*);

#ifdef __cplusplus
}
#endif
#endif // __GF_CLIENT_STUDENT_H__
