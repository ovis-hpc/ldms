#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>

#include "sos.h"
#include "obj_idx.h"

SOS_OBJ_BEGIN(ovis_metric_class, "OvisMetric")
	SOS_OBJ_ATTR_WITH_KEY("tv_sec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR("tv_usec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_KEY("metric_id", SOS_TYPE_UINT64),
	SOS_OBJ_ATTR("value", SOS_TYPE_UINT64)
SOS_OBJ_END(4);

void remove_data(sos_t sos, uint32_t sec, uint32_t limit)
{
	sos_obj_t obj;
	uint32_t t;
	sos_iter_t iter;
	int rc;

	iter = sos_iter_new(sos, 0);
	assert(iter);
	rc = sos_iter_begin(iter);
	if (rc)
		goto exit;
loop:
	obj = sos_iter_obj(iter);
	if (!obj)
		goto exit; /* no more object */
	t = sos_obj_attr_get_uint32(sos, 0, obj);
	if (sec - t < limit)
		goto exit;
	sos_iter_obj_remove(iter);
	sos_obj_delete(sos, obj);
	goto loop;
exit:
	sos_iter_free(iter);
}

int main(int argc, char **argv)
{
	char buf[BUFSIZ];
	uint32_t sec, usec, t;
	uint64_t metric_id, value;
	int n;
	int rc;
	int limit = 5;
	const char *path = "store/store";
	char *s;
	sos_iter_t iter;
	sos_t sos = sos_open(path, O_RDWR|O_CREAT, 0600, &ovis_metric_class);
	sos_obj_t obj;
	assert(sos);

	if (argc == 2) {
		limit = atoi(argv[1]);
	}

	while ((s = fgets(buf, sizeof(buf), stdin)) != NULL) {
		n = sscanf(buf, "%"PRIu32".%"PRIu32" %"PRIu64" %"PRIu64,
				&sec, &usec, &metric_id, &value);
		if (n != 4)
			break;

		obj = sos_obj_new(sos);
		assert(obj);

		sos_obj_attr_set_uint32(sos, 0, obj, sec);
		sos_obj_attr_set_uint32(sos, 1, obj, usec);
		sos_obj_attr_set_uint64(sos, 2, obj, metric_id);
		sos_obj_attr_set_uint64(sos, 3, obj, value);

		/* Add it to the indexes */
		rc = sos_obj_add(sos, obj);
		assert(rc == 0);
		remove_data(sos, sec, limit);
	}

	sos_close(sos, ODS_COMMIT_SYNC);

	return 0;
}
