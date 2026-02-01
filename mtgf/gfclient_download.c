#include "gf-student.h"
#include "gfclient-student.h"

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
