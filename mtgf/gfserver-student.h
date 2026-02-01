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

#ifdef __cplusplus
}
#endif
#endif // __GF_SERVER_STUDENT_H__
