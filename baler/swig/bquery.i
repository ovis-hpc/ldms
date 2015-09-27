/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
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

%module bquery

%include "cpointer.i"
%include "cstring.i"
%include "carrays.i"
%include "stdint.i"

struct bpixel {
	uint32_t sec;
	uint32_t comp_id;
	uint32_t ptn_id;
	uint32_t count;
};

/* Store open/close interfaces */
struct bq_store* bq_open_store(const char *path);
void bq_store_close_free(struct bq_store *store);
int bq_store_refresh(struct bq_store *store);


/* Message query interfaces */
struct bmsgquery* bmsgquery_create(struct bq_store *store, const char *hst_ids,
			     const char *ptn_ids, const char *ts0,
			     const char *ts1, int is_text, char sep, int *rc);
void bmsgquery_destroy(struct bmsgquery *q);

/* msg query navigation */
int bq_first_entry(struct bquery *q);
int bq_next_entry(struct bquery *q);
int bq_prev_entry(struct bquery *q);
int bq_last_entry(struct bquery *q);

/* msg query data access */
%newobject bq_entry_get_msg_str;
char *bq_entry_get_msg_str(struct bquery *q);
uint32_t bq_entry_get_sec(struct bquery *q);
uint32_t bq_entry_get_usec(struct bquery *q);
uint32_t bq_entry_get_comp_id(struct bquery *q);
uint32_t bq_entry_get_ptn_id(struct bquery *q);

/* msg query print utility */
%newobject bq_entry_print;
char *bq_entry_print(struct bquery *q, struct bdstr *bdstr);


/* Image query interfaces */
struct bimgquery* bimgquery_create(struct bq_store *store, const char *hst_ids,
				const char *ptn_ids, const char *ts0,
				const char *ts1, const char *img_store_name,
				int *rc);
void bimgquery_destroy(struct bimgquery *q);

/* msg query navigation */
int bmq_first_entry(struct bmsgquery *q);
int bmq_next_entry(struct bmsgquery *q);
int bmq_prev_entry(struct bmsgquery *q);
int bmq_last_entry(struct bmsgquery *q);

struct bquery *bmq_to_bq(struct bmsgquery *bmq);

/* img query navigation */
int biq_first_entry(struct bimgquery *q);
int biq_next_entry(struct bimgquery *q);
int biq_prev_entry(struct bimgquery *q);
int biq_last_entry(struct bimgquery *q);

/* img query data access */
struct bpixel biq_entry_get_pixel(struct bimgquery *q);


/* Other interfaces */
%newobject bq_get_all_ptns;
char* bq_get_all_ptns(struct bq_store *store);
%newobject bq_get_ptn;
char* bq_get_ptn(struct bq_store *store, uint32_t ptn_id);
int bq_get_comp_id(struct bq_store *store, const char *name);
%newobject bq_get_comp_name;
char* bq_get_comp_name(struct bq_store *store, uint32_t comp_id);

/* Additional Implementation */
%{
#include "baler/butils.h"
#include "query/bquery.h"
#include "query/bquery_priv.h"

int bq_print_msg(struct bquery *q, struct bdstr *bdstr,
		 const struct bmsg *msg);

char *bq_entry_get_msg_str(struct bquery *q)
{
	struct bmsg *bmsg = NULL;
	struct bdstr *bdstr = NULL;
	char *s = NULL;
	int rc = 0;

	bmsg = bq_entry_get_msg(q);
	if (!bmsg)
		goto cleanup;
	bdstr = bdstr_new(256);
	if (!bdstr)
		goto cleanup;
	rc = bq_print_msg(q, bdstr, bmsg);
	if (rc)
		goto cleanup;
	s = bdstr_detach_buffer(bdstr);
cleanup:
	if (bdstr)
		bdstr_free(bdstr);
	return s;
}

char* bq_get_ptn(struct bq_store *store, uint32_t ptn_id)
{
	struct bdstr *bdstr = NULL;
	char *s = NULL;
	int rc;

	bdstr = bdstr_new(256);
	if (!bdstr)
		goto cleanup;
	rc = bq_print_ptn(store, NULL, ptn_id, bdstr);
	if (rc)
		goto cleanup;
	s = bdstr_detach_buffer(bdstr);
cleanup:
	if (bdstr)
		bdstr_free(bdstr);
	return s;
}

char* bq_get_comp_name(struct bq_store *store, uint32_t comp_id)
{
	struct bdstr *bdstr = NULL;
	char *s = NULL;
	int rc;

	bdstr = bdstr_new(256);
	if (!bdstr)
		goto cleanup;
	rc = bq_get_cmp(store, comp_id, bdstr);
	if (rc)
		goto cleanup;
	s = bdstr_detach_buffer(bdstr);
cleanup:
	if (bdstr)
		bdstr_free(bdstr);
	return s;
}

struct bpixel biq_entry_get_pixel(struct bimgquery *q)
{
	struct bpixel bpx = {0};
	bq_img_entry_get_pixel(q, &bpx);
	return bpx;
}

int bmq_first_entry(struct bmsgquery *q)
{
	return bq_first_entry((void*)q);
}

int bmq_next_entry(struct bmsgquery *q)
{
	return bq_next_entry((void*)q);
}

int bmq_prev_entry(struct bmsgquery *q)
{
	return bq_prev_entry((void*)q);
}

int bmq_last_entry(struct bmsgquery *q)
{
	return bq_last_entry((void*)q);
}

int biq_first_entry(struct bimgquery *q)
{
	return bq_first_entry((void*)q);
}

int biq_next_entry(struct bimgquery *q)
{
	return bq_next_entry((void*)q);
}

int biq_prev_entry(struct bimgquery *q)
{
	return bq_prev_entry((void*)q);
}

int biq_last_entry(struct bimgquery *q)
{
	return bq_last_entry((void*)q);
}

struct bquery *bmq_to_bq(struct bmsgquery *bmq)
{
	return &bmq->base;
}

%}
