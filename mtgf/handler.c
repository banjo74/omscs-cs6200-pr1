#include "gf-student.h"
#include "gfserver.h"

#include <memory.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//
//  The purpose of this function is to handle a get request
//
//  The ctx is a pointer to the "context" operation and it contains connection
//  state The path is the path being retrieved The arg allows the registration
//  of context that is passed into this routine. Note: you don't need to use
//  arg. The test code uses it in some cases, but
//        not in others.
//
gfh_error_t gfs_handler(gfcontext_t** ctx, char const* path, void* arg) {
    MultiThreadedHandler* handler = (MultiThreadedHandler*)arg;
    mth_process(handler, (GfContext**)ctx, path);
    return 0;
}
