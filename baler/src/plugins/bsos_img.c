/**
 * \file bsos_img.c
 * \author Narate Taerat (narate at ogc dot us)
 */
#include "bsos_img.h"
#include <errno.h>
#include <stdlib.h>

#define BSOS_IMG_IDX_NAME "index"

bsos_img_t bsos_img_open(const char *path, int create)
{
	int rc;
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
