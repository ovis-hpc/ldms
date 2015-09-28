#include "bsos_msg.h"
#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>

#define BSOS_MSG_IDX_PTH_NAME "index_pth"
#define BSOS_MSG_IDX_TH_NAME "index_th"

static
sos_t __sos_container_open(const char *path, int create)
{
	int rc;
	sos_t sos;
	sos_part_t part;
	char buff[16];
	struct timeval tv;
retry:
	sos = sos_container_open(path, SOS_PERM_RW);
	if (!sos && create) {
		rc = sos_container_new(path, 0660);
		if (!rc)
			goto retry;
		errno = rc;
		goto err0;
	}
	/* Create/set active partition */
	rc = gettimeofday(&tv, NULL);
	if (rc)
		goto err1;
	/* make tv_sec into day alignment */
	tv.tv_sec /= (24*3600);
	tv.tv_sec *= 24*3600;
	snprintf(buff, sizeof(buff), "%ld", tv.tv_sec);
part_retry:
	part = sos_part_find(sos, buff);
	if (!part) {
		rc = sos_part_create(sos, buff, NULL);
		if (rc) {
			errno = rc;
			goto err1;
		}
		goto part_retry;
	}
	rc = sos_part_state_set(part, SOS_PART_STATE_PRIMARY);
	sos_part_put(part);
	if (rc) {
		errno = rc;
		goto err1;
	}
out:
	return sos;
err1:
	sos_container_close(sos, SOS_COMMIT_ASYNC);
err0:
	return NULL;
}

static
sos_index_t __sos_index_open(sos_t sos, int create, const char *name, const char *index,
				const char *type,
				const char *opt)
{
	int rc;
	sos_index_t idx = NULL;
retry:
	idx = sos_index_open(sos, name);
	if (!idx && create) {
		rc = sos_index_new(sos, name, index, type, opt);
		if (!rc)
			goto retry;
		goto out;
	}
out:
	return idx;
}

bsos_msg_t bsos_msg_open(const char *path, int create)
{
	int rc;
	bsos_msg_t bsos_msg = calloc(1, sizeof(*bsos_msg));
	if (!bsos_msg)
		goto out;

	bsos_msg->sos = __sos_container_open(path, create);
	if (!bsos_msg->sos)
		goto err;

	bsos_msg->index_ptc = __sos_index_open(bsos_msg->sos, create,
			BSOS_MSG_IDX_PTH_NAME, "BXTREE", "UINT96", "ORDER=5");
	if (!bsos_msg->index_ptc)
		goto err;
	bsos_msg->index_tc = __sos_index_open(bsos_msg->sos, create,
			BSOS_MSG_IDX_TH_NAME, "BXTREE", "UINT64", "ORDER=5");
	if (!bsos_msg->index_tc)
		goto err;

	return bsos_msg;

err:
	bsos_msg_close(bsos_msg, SOS_COMMIT_ASYNC);
out:
	return NULL;
}

void bsos_msg_close(bsos_msg_t bsos_msg, sos_commit_t commit)
{
	if (bsos_msg->index_ptc)
		sos_index_close(bsos_msg->index_ptc, commit);
	if (bsos_msg->index_tc)
		sos_index_close(bsos_msg->index_tc, commit);
	if (bsos_msg->sos)
		sos_container_close(bsos_msg->sos, commit);
	free(bsos_msg);
}
