/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015-2016 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of Sandia nor the names of any contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * \file bsos_img.c
 * \author Narate Taerat (narate at ogc dot us)
 */
#include "bsos_img.h"
#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>
#include <assert.h>

bsos_img_t bsos_img_open(const char *path, int create)
{
	int rc;
	char buff[16];
	struct timeval tv;
	sos_part_t part;
	sos_part_iter_t piter;
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
	rc = gettimeofday(&tv, NULL);
	tv.tv_sec /= (24*3600);
	tv.tv_sec *= 24*3600;
	snprintf(buff, sizeof(buff), "%ld", tv.tv_sec);

part_retry:
	piter = sos_part_iter_new(bsos_img->sos);
	if (!piter) {
		goto err;
	}
	part = sos_part_first(piter);
	while (part && sos_part_state(part) != SOS_PART_STATE_PRIMARY) {
		sos_part_put(part);
		part = sos_part_next(piter);
	}
	sos_part_iter_free(piter);
	if (part) {
		sos_part_put(part);
	} else if (create) {
		/* no active partition and this is a create call */
		rc = sos_part_create(bsos_img->sos, buff, NULL);
		if (rc) {
			errno = rc;
			goto err;
		}
		part = sos_part_find(bsos_img->sos, buff);
		assert(part);
		rc = sos_part_state_set(part, SOS_PART_STATE_PRIMARY);
		sos_part_put(part);
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
