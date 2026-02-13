/*
 *  This file is for use by students to define anything they wish.  It is used
 * by the gf server implementation
 */
#ifndef __GF_SERVER_STUDENT_H__
#define __GF_SERVER_STUDENT_H__
// gfserver.h not standalone
#include <sys/types.h>

#include <stddef.h>
// gfserver.h not standalone

#include "gfserver.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*ContinueFcn)(void* continueArg);

void gfserver_set_continue_fcn(gfserver_t**, ContinueFcn, void* continueFcnArg);

// put the server into listening mode based on the current value of port.  if
// gfserver_serve is called before gfserver_listen is called, it effectively
// calls gfserver_listen.
int gfserver_listen(gfserver_t**);

// get the port of the server
unsigned short gfserver_port(gfserver_t**);

// destroy server
void gfserver_destroy(gfserver_t**);

/////////////////////////////////////////////////////////////
// Multi Threaded Handler
/////////////////////////////////////////////////////////////

typedef struct MultiThreadedHandlerTag MultiThreadedHandler;
typedef struct HandlerClientTag        HandlerClient;
typedef struct SourceTag               Source;

// Start's a multi threaded request handler with numThreads.  The HandlerClient
// abstracts the send header, send, and abort callbacks so that
// MultiThreadedHandler may be unit tested without firing up a server.
//
// Source is the provider of file content.
MultiThreadedHandler* mth_start(size_t         numThreads,
                                HandlerClient* handlerClient,
                                Source*        source);

// process a single request.  The request is not necessarily processed before
// calls to mth_process return.  See mth_finish below.
void mth_process(MultiThreadedHandler* mtc,
                 gfcontext_t**         ctx,
                 char const*           path);

// finish processing all requests and return.  note, in production, the server
// doesn't shut down but for testing purposes, we need to be able to shudown the
// handler.
void mth_finish(MultiThreadedHandler*);

/////////////////////////////////////////////////////////////
// Handler Client
/////////////////////////////////////////////////////////////

// HandlerClient is an abstraction of the functions that the hander is supposed
// to call.  Normally these would be implemented by the server but we want to be
// able to test the MultiThreadedHandler without having a server around
// (especially because the production server can't be shut down).

// see sendHeader in HandlerClient
typedef ssize_t (*HcSendHeader)(gfcontext_t**,
                                gfstatus_t status,
                                size_t     fileLen,
                                void*);
// see send in HandlerClient
typedef ssize_t (*HcSend)(gfcontext_t**, void const* data, size_t size, void*);
// see abort in HandlerClient
typedef void (*HcAbort)(gfcontext_t**, void*);

struct HandlerClientTag {
    // send a header to the client.  if status != GF_OK, expect the client to
    // take ownership of the ctx.
    HcSendHeader sendHeader;
    // If status is GF_OK then send data with send.  send is expected to take
    // ownership of ctx once it's received a number of bytes that matches the
    // size provided to sendHeader.
    HcSend send;
    // abort the session, taking ownership of the context.
    HcAbort abort;
    // client-specific data.
    void* clientData;
};

void hc_initialize(HandlerClient*,
                   HcSendHeader sendHeader,
                   HcSend       send,
                   HcAbort      abort,
                   void*        clientData);

// convenience call to sendHeader passing along the clientData as well.
ssize_t hc_send_header(HandlerClient*,
                       gfcontext_t**,
                       gfstatus_t status,
                       size_t     fileLen);

// convenience call to send passing along the clientData as well.
ssize_t hc_send(HandlerClient*, gfcontext_t**, void const* data, size_t size);

// convenience call to abort passing along the clientData as well.
void hc_abort(HandlerClient*, gfcontext_t**);

// initialize a HandlerClient that calls the actual gfs_sendheader, gfs_send,
// and gfs_abort.
void hc_init_native(HandlerClient* client);

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

// Initialize a source to read data from the content oracle.  See content.h.
void content_source_init(Source*);

#ifdef __cplusplus
}
#endif
#endif // __GF_SERVER_STUDENT_H__
