#include "boutput.h"
#include <string.h>

/**
 * metric_ids[comp_id] mapping for ME output.
 */
uint64_t *metric_ids;

char *__store_path = NULL;

const char *bget_store_path()
{
	return __store_path;
}

int bset_store_path(const char *path)
{
	if (__store_path)
		free(__store_path);
	__store_path = strdup(path);
	if (!__store_path)
		return ENOMEM;
	return 0;
}
