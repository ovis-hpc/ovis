
#include <stdlib.h>
#include <ctype.h>
#include <glob.h>
#include <string.h>
#include <getopt.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <unistd.h>
#include "/usr/include/mntent.h"
#include <libzfs.h>


/* Function prototypes */

int
get_stats(zpool_handle_t *zhp, void *data);
