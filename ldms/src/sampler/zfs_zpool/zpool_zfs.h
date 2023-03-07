#include <string.h>
#include <getopt.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "/usr/include/libzfs/libzfs_impl.h"



/* Function prototypes */

int
get_stats(zpool_handle_t *zhp, void *data);
