/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2016 Sandia Corporation. All rights reserved.
 *
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
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <assert.h>
#include <getopt.h>

#include "baler/butils.h"
#include "../../../sos/ods/include/ods/ods.h"
#include "../../../sos/sos/src/sos_priv.h"
#include "sos/sos.h"
#include "bsos_msg.h"
#include "bout_sos_msg.h"
#include "bsos_img.h"
#include "bout_sos_img.h"

char *short_opt = "hC:p:";
struct option long_opt[] = {
	{"help",       0,  0,  'h'},
	{"container",  1,  0,  'C'},
	{"part",       1,  0,  'p'},
	{0,            0,  0,  0}
};

const char *cont = NULL;
const char *part = NULL;
sos_index_t index_ptc; /* PTH: PatternID, Timestamp, CompID */
sos_index_t index_tc; /* TH: Timestamp, CompID */
sos_index_t index_img; /* image index */

int is_msg;

void usage()
{
	printf("usage: bsos_msg_reindex -C <container> -p <part>\n");
	printf("\n");
	printf("***NOTE: the partition must be offline.");
}

void handle_args(int argc, char **argv)
{
	char c;
loop:
	c = getopt_long(argc, argv, short_opt, long_opt, NULL);
	switch (c) {
	case -1:
		goto out;
		break;
	case 'C':
		cont = optarg;
		break;
	case 'p':
		part = optarg;
		break;
	case 'h':
	default:
		usage();
		exit(0);
	}
	goto loop;
out:
	return;
}

int cont_is_msg(const char *cont)
{
	const char *x;
	x = strrchr(cont, '/');
	if (!x)
		return 0;
	return 0 == strcmp(x+1, "msg");
}

void msg_reindex_cb(ods_t ods, ods_obj_t obj, void *arg)
{
	SOS_KEY(tc_key);
	SOS_KEY(ptc_key);
	sos_array_t msg;
	struct bsos_msg_key_ptc ptc_k;
	struct bsos_msg_key_tc tc_k;
	int rc;
	sos_obj_ref_t ref;
	sos_obj_t sos_obj;
	sos_part_t part = arg;
	sos_obj_data_t sos_obj_data = obj->as.ptr;
	sos_schema_t schema = __sos_get_ischema(SOS_TYPE_UINT32_ARRAY);
	if (!schema)
		/* This is a garbage object that should not be here */
		return;
	ref.ref.ods = SOS_PART(part->part_obj)->part_id;
	ref.ref.obj = ods_obj_ref(obj);
	sos_obj = __sos_init_obj(part->sos, schema, obj, ref);
	msg = sos_obj_ptr(sos_obj);

	/* creat key */
	ptc_k.comp_id = msg->data.uint32_[BSOS_MSG_COMP_ID];
	ptc_k.sec = msg->data.uint32_[BSOS_MSG_SEC];
	ptc_k.ptn_id = msg->data.uint32_[BSOS_MSG_PTN_ID];

	tc_k.comp_id = ptc_k.comp_id;
	tc_k.sec = ptc_k.sec;

	/* need to convert only PTC (due to memcmp()) */
	bsos_msg_key_ptc_htobe(&ptc_k);

	sos_key_set(tc_key, &tc_k, 8);

	rc = sos_index_insert(index_tc, tc_key, sos_obj);
	if (rc) {
		/* The object couldn't be indexed for some reason */
	}

	sos_key_set(ptc_key, &ptc_k, 12);
	rc = sos_index_insert(index_ptc, ptc_key, sos_obj);
	if (rc) {
		/* The object couldn't be indexed for some reason */
	}

	sos_obj_put(sos_obj);
}

void img_reindex_cb(ods_t ods, ods_obj_t obj, void *arg)
{
	SOS_KEY(ok);
	struct bsos_img_key bk;
	sos_array_t img;

	int rc;
	sos_obj_ref_t ref;
	sos_obj_t sos_obj;
	sos_part_t part = arg;
	sos_obj_data_t sos_obj_data = obj->as.ptr;
	sos_schema_t schema = __sos_get_ischema(SOS_TYPE_UINT32_ARRAY);
	if (!schema)
		/* This is a garbage object that should not be here */
		return;
	ref.ref.ods = SOS_PART(part->part_obj)->part_id;
	ref.ref.obj = ods_obj_ref(obj);
	sos_obj = __sos_init_obj(part->sos, schema, obj, ref);
	img = sos_obj_ptr(sos_obj);

	/* creat key */
	bk.ptn_id = img->data.uint32_[BSOS_IMG_PTN_ID];
	bk.ts = img->data.uint32_[BSOS_IMG_SEC];
	bk.comp_id = img->data.uint32_[BSOS_IMG_COMP_ID];

	sos_key_set(ok, &bk, sizeof(bk));

	rc = sos_index_insert(index_img, ok, sos_obj);
	if (rc) {
		/* The object couldn't be indexed for some reason */
	}

	sos_obj_put(sos_obj);
}
static char buff[4096];
void msg_idx_open(sos_t sos)
{
	index_tc = sos_index_open(sos, BSOS_MSG_IDX_TH_NAME);
	if (!index_tc) {
		berr("msg: cannot open index: %s", BSOS_MSG_IDX_TH_NAME);
		exit(-1);
	}
	index_ptc = sos_index_open(sos, BSOS_MSG_IDX_PTH_NAME);
	if (!index_tc) {
		berr("msg: cannot open index: %s", BSOS_MSG_IDX_PTH_NAME);
		exit(-1);
	}
}

void img_idx_open(sos_t sos)
{
	index_img = sos_index_open(sos, BSOS_IMG_IDX_NAME);
	if (!index_img) {
		berr("img: cannot open index: %s", BSOS_IMG_IDX_NAME);
		exit(-1);
	}
}

int main(int argc, char **argv)
{
	int rc = 0;
	handle_args(argc, argv);
	sos_t sos = NULL;
	sos_part_t p = NULL;
	sos_part_state_t pst;

	if (!cont) {
		berr("-C option is not specified");
		rc = EINVAL;
		goto out;
	}

	if (!part) {
		berr("-p option is not specified");
		rc = EINVAL;
		goto out;
	}

	sos = sos_container_open(cont, O_RDWR);
	if (!sos) {
		berr("cannot open sos container: %s, errno: %d", cont, errno);
		rc = errno;
		goto out;
	}

	p = sos_part_find(sos, part);
	if (!p) {
		berr("partition not found: %s", part);
		rc = errno;
		goto out;
	}

	pst = sos_part_state(p);
	switch (pst) {
	case SOS_PART_STATE_OFFLINE:
		rc = sos_part_state_set(p, SOS_PART_STATE_ACTIVE);
		if (rc) {
			berr("sos_part_state_set() error, rc: %d", rc);
			goto out;
		}
		break;
	case SOS_PART_STATE_MOVING:
	case SOS_PART_STATE_ACTIVE:
	case SOS_PART_STATE_PRIMARY:
		berr("partition '%s' not in OFFLINE state", part);
		rc = EBUSY;
		goto out;
		break;
	}

	is_msg = cont_is_msg(cont);
	if (is_msg) {
		msg_idx_open(sos);
		ods_iter(p->obj_ods, msg_reindex_cb, p);
	} else {
		img_idx_open(sos);
		ods_iter(p->obj_ods, img_reindex_cb, p);
	}

out:
	if (p)
		sos_part_put(p);
	if (sos)
		sos_container_close(sos, SOS_COMMIT_ASYNC);

	return 0;
}
