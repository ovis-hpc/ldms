/**
 * \file bsos_img.c
 * \author Narate Taerat (narate at ogc dot us)
 */
#include "bsos_img.h"
#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>

#define BSOS_IMG_IDX_NAME "index"

bsos_img_t bsos_img_open(const char *path, int create)
{
	int rc;
	char buff[16];
	struct timeval tv;
	sos_part_t part;
	bsos_img_t bsos_img = calloc(1, sizeof(*bsos_img));
	if (!bsos_img)
		goto out;

sos_retry:
	bsos_img->sos = sos_container_open(path, SOS_PERM_RW);
	if (!bsos_img->sos) {
		if (!create)
			goto err;
		rc = sos_container_new(path, 0660);
		if (!rc)
			goto sos_retry;
		goto err;
	}

	/* make tv_sec into day alignment */
	tv.tv_sec /= (24*3600);
	tv.tv_sec *= 24*3600;
	snprintf(buff, sizeof(buff), "%ld", tv.tv_sec);
part_retry:
	part = sos_part_find(bsos_img->sos, buff);
	if (!part) {
		rc = sos_part_create(bsos_img->sos, buff, NULL);
		if (rc) {
			errno = rc;
			goto err;
		}
		goto part_retry;
	}
	rc = sos_part_state_set(part, SOS_PART_STATE_PRIMARY);
	sos_part_put(part);
	if (rc) {
		errno = rc;
		goto err;
	}

index_retry:
	bsos_img->index = sos_index_open(bsos_img->sos, BSOS_IMG_IDX_NAME);
	if (!bsos_img->index) {
		if (!create)
			goto err;
		rc = sos_index_new(bsos_img->sos, BSOS_IMG_IDX_NAME,
					"BXTREE", "UINT96", "ORDER=5");
		if (!rc)
			goto index_retry;
		goto err;
	}

	return bsos_img;

err:
	bsos_img_close(bsos_img, SOS_COMMIT_ASYNC);
out:
	return NULL;
}

void bsos_img_close(bsos_img_t bsos_img, sos_commit_t commit)
{
	if (bsos_img->index)
		sos_index_close(bsos_img->index, commit);
	if (bsos_img->sos)
		sos_container_close(bsos_img->sos, commit);
	free(bsos_img);
}
