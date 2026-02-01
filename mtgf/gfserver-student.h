/*
 *  This file is for use by students to define anything they wish.  It is used
 * by the gf server implementation
 */
#ifndef __GF_SERVER_STUDENT_H__
#define __GF_SERVER_STUDENT_H__

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

MultiThreadedHandler* mth_start(size_t         numThreads,
                                HandlerClient* handlerClient,
                                Source*        source);

void mth_process(MultiThreadedHandler* mtc,
                 gfcontext_t**         ctx,
                 char const*           path);

void mth_finish(MultiThreadedHandler*);

/////////////////////////////////////////////////////////////
// Handler Client
/////////////////////////////////////////////////////////////

typedef ssize_t (*HcSendHeader)(gfcontext_t**,
                                gfstatus_t status,
                                size_t     fileLen,
                                void*);
typedef ssize_t (*HcSend)(gfcontext_t**, void const* data, size_t size, void*);
typedef void (*HcAbort)(gfcontext_t**, void*);

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
                       gfcontext_t**,
                       gfstatus_t status,
                       size_t     fileLen);

ssize_t hc_send(HandlerClient*, gfcontext_t**, void const* data, size_t size);

void hc_abort(HandlerClient*, gfcontext_t**);

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

// initialize a source to read files from the disk
void content_source_init(Source*);

#ifdef __cplusplus
}
#endif
#endif // __GF_SERVER_STUDENT_H__
