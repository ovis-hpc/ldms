/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015 Sandia Corporation. All rights reserved.
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
 * \author Narate Taerat (narate at ogc dot us)
 * \file bhttpd_meta.c
 */

#include <float.h>

#include "bhttpd.h"
#include "baler/bmeta.h"

pthread_mutex_t meta_mutex = PTHREAD_MUTEX_INITIALIZER; /* Generic mutex for bhttpd */
int meta_cluster_in_progress = 0;

static
void meta_cluster_work_routine(void *arg)
{
	int rc;
	bmptn_store_state_e state = bmptn_store_get_state(mptn_store);
	switch (state) {
	case BMPTN_STORE_STATE_NA:
	case BMPTN_STORE_STATE_ERROR:
	case BMPTN_STORE_STATE_DONE:
		/* In these states, reinit before clustering */
		rc = bmptn_store_reinit(mptn_store);
		if (rc) {
			berr("meta_cluster_work_routine(),"
				" bmptn_store_reinit() error, rc: %d\n", rc);
			return;
		}

		/* intentionally let-through */

	case BMPTN_STORE_STATE_INITIALIZED:
		rc = bmptn_cluster(mptn_store, arg);
		if (rc) {
			berr("meta_cluster_work_routine(),"
				" bmptn_cluster() error, rc: %d\n", rc);
			return;
		}
		break;

	case BMPTN_STORE_STATE_META_1:
	case BMPTN_STORE_STATE_META_2:
	case BMPTN_STORE_STATE_REFINING:
	case BMPTN_STORE_STATE_LAST:
		/* this should not happen */
		berr("meta_cluster_work_routine() bad state ...");
		break;
	}

out:
	pthread_mutex_lock(&meta_mutex);
	meta_cluster_in_progress = 0;
	pthread_mutex_unlock(&meta_mutex);
	free(arg);
}

static
struct bmeta_cluster_param *__get_meta_cluster_param(struct bhttpd_req_ctxt *ctxt)
{
	struct bmeta_cluster_param *p = calloc(1, sizeof(*p));
	float f;
	const char *str;
	int i;
	if (!p) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
				"Out of memory");
		goto err;
	}
	struct {
		float *var;
		float dfault;
		const char *key;
		float min;
		float max;
	} table[] = {
		{&p->refinement_speed,  2.0,  "refinement_speed",  1.0,  FLT_MAX},
		{&p->looseness,         0.3,  "looseness",         0.0,  1.0},
		{&p->diff_ratio,        0.3,  "diff_ratio",        0.0,  1.0},
		{0, 0, 0, 0, 0},
	};
	for (i = 0; table[i].var; i++) {
		str = bpair_str_value(&ctxt->kvlist, table[i].key);
		f = (str)?(atof(str)):(table[i].dfault);
		if (f < table[i].min || table[i].max < f) {
			bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
					"Invalid value of '%s': %f",
					table[i].key, f);
			goto err;
		}
		*(table[i].var) = f;
	}
	return p;
err:
	free(p);
	return NULL;
}

static
void bhttpd_handle_meta_cluster_op_run(struct bhttpd_req_ctxt *ctxt)
{
	int rc;
	pthread_mutex_lock(&meta_mutex);
	if (meta_cluster_in_progress) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
						"Operation in progress...");
	} else {
		meta_cluster_in_progress = 1;
		struct bmeta_cluster_param *p = __get_meta_cluster_param(ctxt);
		if (!p)
			goto out;
		rc = submit_work(meta_cluster_work_routine, p);
		if (rc) {
			bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
							"rc: %d", rc);
		}
	}
out:
	pthread_mutex_unlock(&meta_mutex);
}

static
void bhttpd_handle_meta_cluster_op_get_status(struct bhttpd_req_ctxt *ctxt)
{
	const char *state = bmptn_store_get_state_str(mptn_store);
	uint32_t percent = bmptn_store_get_percent(mptn_store);
	struct evkeyvalq *hdr = evhttp_request_get_output_headers(ctxt->req);
	evhttp_add_header(hdr, "Content-Type", "application/json");
	evbuffer_add_printf(ctxt->evbuffer,
				"{\"state\": \"%s\", \"percent\": %d}",
				state, percent);
}

static
void bhttpd_handle_meta_cluster_op_update(struct bhttpd_req_ctxt *ctxt)
{
	/* TODO IMPLEMENT ME */
	bhttpd_req_ctxt_errprintf(ctxt, HTTP_NOTIMPLEMENTED, "Not implemented.");
}

static
void bhttpd_handle_meta_cluster(struct bhttpd_req_ctxt *ctxt)
{
	int i, rc;
	static const char *meta_op_table[] = {
		"get_status",
		"run",
		"update",
	};

	enum {
		META_OP_GET_STATUS,
		META_OP_RUN,
		META_OP_UPDATE,
		META_OP_LAST,
	} ope;

	evhttp_add_header(ctxt->hdr, "content-type", "application/json");

	struct bpair_str *op = bpair_str_search(&ctxt->kvlist, "op", NULL);
	const char *_op;
	if (!op) {
		_op = "get_status";
	} else {
		_op = op->s1;
	}

	for (ope = 0; ope < META_OP_LAST; ope++) {
		if (0 == strcmp(meta_op_table[ope], _op)) {
			break;
		}
	}

	switch (ope) {
	case META_OP_GET_STATUS:
		bhttpd_handle_meta_cluster_op_get_status(ctxt);
		break;
	case META_OP_RUN:
		bhttpd_handle_meta_cluster_op_run(ctxt);
		break;
	case META_OP_UPDATE:
		bhttpd_handle_meta_cluster_op_update(ctxt);
		break;
	default:
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_BADREQUEST, "Unknown op: %s", _op);
	}
}

static __attribute__((constructor))
void __init()
{
	bdebug("Adding /meta_cluster handler");
	set_uri_handle("/meta_cluster", bhttpd_handle_meta_cluster);
}
