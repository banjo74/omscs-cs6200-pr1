#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include "gfserver-student.h"
#include <getopt.h>
#include <netdb.h>
#include <string.h>
#include <regex.h>
#include <errno.h>
#include <netinet/in.h>
#include "gfserver.h"

#include <stdio.h>
#include "content.h"
#include <unistd.h>

#define USAGE                                                                                  \
  "usage:\n"                                                                                   \
  "  gfserver_main [options]\n"                                                                \
  "options:\n"                                                                                 \
  "  -h          		Show this help message.\n"              		                       \
  "  -m [content_file]  Content file mapping keys to content filea (Default: 'content.txt')\n" \
  "  -p [listen_port]   Listen port (Default: 52507)\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"port", required_argument, NULL, 'p'},
    {"help", no_argument, NULL, 'h'},
    {"content", required_argument, NULL, 'm'},
    {NULL, 0, NULL, 0}};

/* Main ========================================================= */
int main(int argc, char **argv) {
  gfserver_t *gfs = NULL;
  char *content_map_file = "content.txt";
  unsigned short port = 52507;
  int option_char = 0;


  setbuf(stdout, NULL);  // disable caching of standpard output

  // Parse and set command line arguments
  while ((option_char = getopt_long(argc, argv, "hal:p:m:", gLongOptions, NULL)) != -1) {
    switch (option_char) {

      case 'p':  /* listen-port */
        port = atoi(optarg);
        break;
      case 'm':  /* file-path */
        content_map_file = optarg;
        break;
      case 'h':  /* help */
        fprintf(stdout, "%s", USAGE);
        exit(0);
        break;
      default:
        fprintf(stderr, "%s", USAGE);
        exit(1);
    }
  }

  content_init(content_map_file);

  if (port > 65331) {
    fprintf(stderr, "Invalid port number\n");
    exit(EXIT_FAILURE);
  }

  /*Initializing server*/
  gfs = gfserver_create();

  /*Setting options*/
  gfserver_set_handler(&gfs, gfs_handler);
  gfserver_set_port(&gfs, port);
  gfserver_set_maxpending(&gfs, 25);

  /* this implementation does not pass any extra state, so it uses NULL. */
  /* this value could be non-NULL.  You might want to test that in your own */
  /* code. */
  gfserver_set_handlerarg(&gfs, NULL);

  // Run forever
  gfserver_serve(&gfs);
}
