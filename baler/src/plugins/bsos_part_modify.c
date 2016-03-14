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
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <assert.h>
#include <getopt.h>

#include "baler/butils.h"
#include "sos/sos.h"
#include "bsos_msg.h"
#include "bout_sos_msg.h"
#include "bsos_img.h"
#include "bout_sos_img.h"

char *short_opt = "hC:p:s:v";
struct option long_opt[] = {
	{"help",       0,  0,  'h'},
	{"container",  1,  0,  'C'},
	{"state",      1,  0,  's'},
	{"verbose",    0,  0,  'v'},
	{0,            0,  0,  0}
};

const char *cont = NULL;
const char *part = NULL;
sos_index_t index_ptc = NULL; /* PTH: PatternID, Timestamp, CompID */
sos_index_t index_tc = NULL; /* TH: Timestamp, CompID */
sos_index_t index_img = NULL; /* image index */
sos_part_state_t cmd_state = -1;
int verbose = 0;

#define DUTY_CYCLE 500000

struct duty_ctxt {
	sos_part_obj_iter_fn_t cb;
	void *cb_arg;
	struct timeval tv0;
	struct timeval tv1;
	struct timeval wtv;
	uint64_t count;
};

int is_msg;

void usage()
{
	printf("bsos_part_modify -C <container> -s <state> <name>\n");
	printf("    -C <path>   The path to the container.\n");
	printf("    -s <state>  Modify the state of a partition. Valid states are:\n"
	       "                primary  - All new allocations go in this partition.\n"
	       "                active   - Objects are accessible, the partition does not grow\n"
	       "                offline  - Object references are invalid; the partition\n"
	       "                           may be moved or deleted.\n");
	printf("    <name>	The partition name.\n");
}

sos_part_state_t str_to_sos_part_state(const char *str);

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
	case 's':
		cmd_state = str_to_sos_part_state(optarg);
		switch (cmd_state) {
		case SOS_PART_STATE_ACTIVE:
		case SOS_PART_STATE_OFFLINE:
		case SOS_PART_STATE_PRIMARY:
			/* ok */
			break;
		default:
			berr("Unknown partition state: %s", optarg);
			exit(-1);
		}
		break;
	case 'v':
		verbose = 1;
		break;
	case 'h':
	default:
		usage();
		exit(0);
	}
	goto loop;
out:
	if (optind < argc) {
		part = argv[optind];
	} else {
		part = NULL;
	}
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

int msg_reindex_cb(sos_part_t part, sos_obj_t sos_obj, void *arg)
{
	SOS_KEY(tc_key);
	SOS_KEY(ptc_key);
	sos_array_t msg;
	struct bsos_msg_key_ptc ptc_k;
	struct bsos_msg_key_tc tc_k;
	int rc;

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
	return 0;
}

int msg_rmindex_cb(sos_part_t part, sos_obj_t sos_obj, void *arg)
{
	SOS_KEY(tc_key);
	SOS_KEY(ptc_key);
	sos_array_t msg;
	struct bsos_msg_key_ptc ptc_k;
	struct bsos_msg_key_tc tc_k;
	int rc;

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

	rc = sos_index_remove(index_tc, tc_key, sos_obj);
	if (rc) {
		/* The object couldn't be indexed for some reason */
	}

	sos_key_set(ptc_key, &ptc_k, 12);
	rc = sos_index_remove(index_ptc, ptc_key, sos_obj);
	if (rc) {
		/* The object couldn't be indexed for some reason */
	}

	sos_obj_put(sos_obj);
	return 0;
}

int img_reindex_cb(sos_part_t part, sos_obj_t sos_obj, void *arg)
{
	SOS_KEY(ok);
	struct bsos_img_key bk;
	sos_array_t img;
	int rc;

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
	return 0;
}


int img_rmindex_cb(sos_part_t part, sos_obj_t sos_obj, void *arg)
{
	SOS_KEY(ok);
	struct bsos_img_key bk;
	sos_array_t img;
	int rc;

	img = sos_obj_ptr(sos_obj);

	/* creat key */
	bk.ptn_id = img->data.uint32_[BSOS_IMG_PTN_ID];
	bk.ts = img->data.uint32_[BSOS_IMG_SEC];
	bk.comp_id = img->data.uint32_[BSOS_IMG_COMP_ID];

	sos_key_set(ok, &bk, sizeof(bk));

	rc = sos_index_remove(index_img, ok, sos_obj);
	if (rc) {
		/* The object couldn't be indexed for some reason */
	}

	sos_obj_put(sos_obj);
	return 0;
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

sos_part_state_t str_to_sos_part_state(const char *str)
{
	if (0 == strcasecmp(str, "active")) {
		return SOS_PART_STATE_ACTIVE;
	}
	if (0 == strcasecmp(str, "offline")) {
		return SOS_PART_STATE_OFFLINE;
	}
	if (0 == strcasecmp(str, "primary")) {
		return SOS_PART_STATE_PRIMARY;
	}
	return -1;
}

int duty_cb(sos_part_t part, sos_obj_t sos_obj, void *arg)
{
	struct duty_ctxt *ctxt = arg;
	ctxt->cb(part, sos_obj, arg);
	ctxt->count++;
	gettimeofday(&ctxt->tv1, NULL);
	if (timercmp(&ctxt->tv1, &ctxt->wtv, <))
		return 0;
	return 1; /* pause if current time hit the wall time */
}

void part_duty(sos_part_t p, sos_part_obj_iter_fn_t cb, void *cb_arg)
{
	struct sos_part_obj_iter_pos_s pos;
	int rc = 0;
	sos_part_obj_iter_pos_init(&pos);
	struct duty_ctxt ctxt = {0};
	struct timeval dtv = {0, DUTY_CYCLE};
	ctxt.cb = cb;
	ctxt.cb_arg = cb_arg;
	ctxt.wtv.tv_sec = 0;
	ctxt.wtv.tv_usec = DUTY_CYCLE;
	do {
		/* calculate wall time */
		if (verbose)
			binfo("resume duty cycle ...");
		gettimeofday(&ctxt.tv0, NULL);
		timeradd(&ctxt.tv0, &dtv, &ctxt.wtv);
		ctxt.count = 0;
		rc = sos_part_obj_iter(p, &pos, duty_cb, &ctxt);
		/* rc is 0 when done, otherwise, the iterator is interrupted */
		if (rc) {
			if (verbose) {
				struct timeval dt;
				timersub(&ctxt.tv1, &ctxt.tv0, &dt);
				binfo("sleeping ... processed "
					"%ld objects in %ld.%06ld seconds",
					ctxt.count, dt.tv_sec, dt.tv_usec);
			}
			usleep(1000000 - DUTY_CYCLE);
		}
	} while (rc);
}

void handle_cmd_active(sos_t sos, sos_part_t p)
{
	sos_part_state_t pst = sos_part_state(p);
	int rc;
	if (pst != SOS_PART_STATE_OFFLINE) {
		berr("partition '%s' not in OFFLINE state", part);
		return;
	}
	rc = sos_part_state_set(p, SOS_PART_STATE_ACTIVE);
	if (rc) {
		berr("sos_part_state_set() error, rc: %d", rc);
		return;
	}
	is_msg = cont_is_msg(cont);
	if (is_msg) {
		msg_idx_open(sos);
		part_duty(p, msg_reindex_cb, NULL);
	} else {
		img_idx_open(sos);
		part_duty(p, img_reindex_cb, NULL);
	}
}

void handle_cmd_offline(sos_t sos, sos_part_t p)
{
	sos_part_state_t pst = sos_part_state(p);
	int rc;
	if (pst != SOS_PART_STATE_ACTIVE) {
		berr("only ACTIVE partition can be brought OFFLINE");
		return;
	}
	is_msg = cont_is_msg(cont);
	if (is_msg) {
		msg_idx_open(sos);
		part_duty(p, msg_rmindex_cb, NULL);
	} else {
		img_idx_open(sos);
		part_duty(p, img_rmindex_cb, NULL);
	}
	rc = sos_part_state_set(p, cmd_state);
	if (rc) {
		berr("sos_part_state_set() error, rc: %d", rc);
		return;
	}
}

void handle_cmd_primary(sos_t sos, sos_part_t p)
{
	sos_part_state_t pst = sos_part_state(p);
	int rc;
	switch (pst) {
	case SOS_PART_STATE_ACTIVE:
		rc = sos_part_state_set(p, SOS_PART_STATE_PRIMARY);
		if (rc) {
			berr("sos_part_state_set() error, rc: %d", rc);
			return;
		}
		break;
	default:
		berr("only ACTIVE partition can be PRIMARY");
		break;
	}
}

int main(int argc, char **argv)
{
	int rc = 0;
	sos_t sos = NULL;
	sos_part_t p = NULL;
	sos_part_state_t pst;

	handle_args(argc, argv);

	if (!cont) {
		berr("container (-C) is not specified");
		rc = EINVAL;
		goto out;
	}

	if (-1 == (uint64_t)cmd_state) {
		berr("state (-s) is not specified");
		goto out;
	}

	if (!part) {
		berr("please specify partition");
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

	switch (cmd_state) {
	case SOS_PART_STATE_ACTIVE:
		handle_cmd_active(sos, p);
		break;
	case SOS_PART_STATE_OFFLINE:
		handle_cmd_offline(sos, p);
		break;
	case SOS_PART_STATE_PRIMARY:
		handle_cmd_primary(sos, p);
		break;
	default:
		assert(0); /* impossible (see handle_args()) */
		break;
	}

out:
	if (index_ptc)
		sos_index_close(index_ptc, SOS_COMMIT_ASYNC);
	if (index_tc)
		sos_index_close(index_tc, SOS_COMMIT_ASYNC);
	if (index_img)
		sos_index_close(index_img, SOS_COMMIT_ASYNC);
	if (p)
		sos_part_put(p);
	if (sos)
		sos_container_close(sos, SOS_COMMIT_ASYNC);

	return 0;
}
