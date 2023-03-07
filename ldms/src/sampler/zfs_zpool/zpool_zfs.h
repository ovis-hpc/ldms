#include <string.h>
#include <getopt.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <libzfs/libzfs.h>



/* Function prototypes */

int
get_stats(zpool_handle_t *zhp, void *data);
