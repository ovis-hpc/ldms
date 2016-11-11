/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-16 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-16 Sandia Corporation. All rights reserved.
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
 * \file bquery.c
 * \author Narate Taerat (narate at ogc dot us)
 */

#include "bquery.h"
#include "bquery_priv.h"
#include <getopt.h>
#include <errno.h>
#include <wordexp.h>
#include <ctype.h>

#include <time.h>
#include <dirent.h>
#include <stdlib.h>

#include "baler/butils.h"
#include "baler/btkn.h"
#include "baler/bhash.h"
#include "baler/bset.h"
#include "baler/bheap.h"
#include "baler/bmeta.h"

#include "assert.h"

#include "config.h"

static
uint32_t __bq_msg_entry_get_sec(struct bquery *q)
{
	return ((struct bmsgquery*)q)->msg->data.uint32_[BSOS_MSG_SEC];
}

static
uint32_t __bq_msg_entry_get_usec(struct bquery *q)
{
	return ((struct bmsgquery*)q)->msg->data.uint32_[BSOS_MSG_USEC];
}

static
uint32_t __bq_msg_entry_get_comp_id(struct bquery *q)
{
	return ((struct bmsgquery*)q)->msg->data.uint32_[BSOS_MSG_COMP_ID];
}

static
uint32_t __bq_msg_entry_get_ptn_id(struct bquery *q)
{
	return ((struct bmsgquery*)q)->msg->data.uint32_[BSOS_MSG_PTN_ID];
}

static
uint32_t __bq_img_entry_get_sec(struct bquery *q)
{
	struct bimgquery *imgq = (struct bimgquery *)q;
	return be32toh(imgq->img->data.uint32_[BSOS_IMG_SEC]);
}

static
uint32_t __bq_img_entry_get_usec(struct bquery *q)
{
	return 0;
}

static
uint32_t __bq_img_entry_get_comp_id(struct bquery *q)
{
	struct bimgquery *imgq = (struct bimgquery *)q;
	return be32toh(imgq->img->data.uint32_[BSOS_IMG_COMP_ID]);
}

static
uint32_t __bq_img_entry_get_ptn_id(struct bquery *q)
{
	struct bimgquery *imgq = (struct bimgquery *)q;
	return be32toh(imgq->img->data.uint32_[BSOS_IMG_PTN_ID]);
}

static
int __sos_iter_inf_last(sos_iter_t iter, sos_key_t key)
{
	int rc;
	sos_key_t sos_key;
	rc = sos_iter_inf(iter, key);
	if (rc)
		return rc;
	sos_key = sos_iter_key(iter);
	rc = sos_iter_find_last(iter, sos_key);
	sos_key_put(sos_key);
	return rc;
}

static inline
int fmt_ptn_prefix(struct bq_formatter *fmt, struct bdstr *bdstr,
			uint32_t ptn_id)
{
	if (fmt->ptn_prefix)
		return fmt->ptn_prefix(fmt, bdstr, ptn_id);
	return 0;
}

static inline
int fmt_ptn_suffix(struct bq_formatter *fmt, struct bdstr *bdstr)
{
	if (fmt->ptn_suffix)
		return fmt->ptn_suffix(fmt, bdstr);
	return 0;
}

static inline
int fmt_msg_prefix(struct bq_formatter *fmt, struct bdstr *bdstr)
{
	if (fmt->msg_prefix)
		return fmt->msg_prefix(fmt, bdstr);
	return 0;
}

static inline
int fmt_msg_suffix(struct bq_formatter *fmt, struct bdstr *bdstr)
{
	if (fmt->msg_suffix)
		return fmt->msg_suffix(fmt, bdstr);
	return 0;
}

static inline
int fmt_tkn_begin(struct bq_formatter *fmt, struct bdstr *bdstr)
{
	if (fmt->tkn_begin)
		return fmt->tkn_begin(fmt, bdstr);
	return 0;
}

static inline
int fmt_tkn_fmt(struct bq_formatter *fmt, struct bdstr *bdstr,
		const struct bstr *bstr, struct btkn_attr *attr,
		uint32_t tkn_id)
{
	if (fmt->tkn_fmt)
		return fmt->tkn_fmt(fmt, bdstr, bstr, attr, tkn_id);
	return 0;
}

static inline
int fmt_tkn_end(struct bq_formatter *fmt, struct bdstr *bdstr)
{
	if (fmt->tkn_end)
		return fmt->tkn_end(fmt, bdstr);
	return 0;
}

static inline
int fmt_date_fmt(struct bq_formatter *fmt, struct bdstr *bdstr,
		 struct timeval *tv)
{
	if (fmt->date_fmt)
		return fmt->date_fmt(fmt, bdstr, tv);
	return 0;
}

static inline
int fmt_host_fmt(struct bq_formatter *fmt, struct bdstr *bdstr,
		const struct bstr *bstr)
{
	if (fmt->host_fmt)
		return fmt->host_fmt(fmt, bdstr, bstr);
	return 0;
}

void bsos_wrap_close_free(struct bsos_wrap *bsw)
{
	sos_index_close(bsw->index, SOS_COMMIT_ASYNC);
	sos_container_close(bsw->sos, SOS_COMMIT_ASYNC);
	free(bsw->store_name);
	bsw->store_name = NULL;
	free(bsw);
}

void bq_store_close_free(struct bq_store *store)
{
	if (store->cmp_store) {
		btkn_store_close_free(store->cmp_store);
		store->cmp_store = NULL;
	}

	if (store->tkn_store) {
		btkn_store_close_free(store->tkn_store);
		store->tkn_store = NULL;
	}

	if (store->ptn_store) {
		bptn_store_close_free(store->ptn_store);
		store->ptn_store = NULL;
	}

	if (store->mptn_store) {
		bmptn_store_close_free(store->mptn_store);
		store->mptn_store = NULL;
	}

	free(store);
}

/**
 * Open baler store.
 *
 * Open baler store, which includes:
 * 	- comp_store (mapping hostname<->host_id)
 * 	- tkn_store (mapping token<->token_id)
 * 	- ptn_store (mapping pattern<->pattern_id)
 * 	- msg_store (sos for message data)
 *
 * \param path The path to the store.
 * \return 0 on success.
 * \return Error code on error.
 */
struct bq_store* bq_open_store(const char *path)
{
	struct bq_store *s = calloc(1, sizeof(*s));
	if (!s)
		return NULL;
	struct stat st;
	int rc;

	/* comp_store */
	snprintf(s->path, sizeof(s->path), "%s/comp_store", path);
	rc = stat(s->path, &st);
	if (rc || !S_ISDIR(st.st_mode)) {
		berr("Invalid store '%s': comp_store subdirectory does"
				" not exist", path);
		goto err;
	}
	s->cmp_store = btkn_store_open(s->path, O_CREAT|O_RDONLY);
	if (!s->cmp_store)
		goto err;

	/* tkn_store */
	snprintf(s->path, sizeof(s->path), "%s/tkn_store", path);
	rc = stat(s->path, &st);
	if (rc || !S_ISDIR(st.st_mode)) {
		berr("Invalid store '%s': tkn_store subdirectory does"
				" not exist", path);
		goto err;
	}
	s->tkn_store = btkn_store_open(s->path, O_CREAT|O_RDONLY);
	if (!s->tkn_store)
		goto err;

	/* ptn_store */
	snprintf(s->path, sizeof(s->path), "%s/ptn_store", path);
	rc = stat(s->path, &st);
	if (rc || !S_ISDIR(st.st_mode)) {
		berr("Invalid store '%s': ptn_store subdirectory does"
				" not exist", path);
		goto err;
	}
	s->ptn_store = bptn_store_open(s->path, O_RDONLY);
	if (!s->ptn_store)
		goto err;

	snprintf(s->path, sizeof(s->path), "%s/mptn_store", path);
	rc = stat(s->path, &st);
	if (rc == 0 && S_ISDIR(st.st_mode)) {
		s->mptn_store = bmptn_store_open(s->path, path, 0);
		/* It is OK to fail for meta-pattern store. */
	}

	snprintf(s->path, sizeof(s->path), "%s", path);

	/* msg_store and img_store will be opened at query time */
	return s;

err:
	berr("Cannot open %s", s->path);
	bq_store_close_free(s);
	return NULL;
}

int bq_local_host_routine(struct bq_store *s)
{
	int rc = 0;
	char buff[4096];
	uint32_t id = BMAP_ID_BEGIN;
	uint32_t next_id = s->cmp_store->map->hdr->next_id;
	printf("-------- --------------\n");
	printf(" host_id hostname\n");
	printf("-------- --------------\n");
	while (id<next_id) {
		rc = btkn_store_id2str_esc(s->cmp_store, id, buff, sizeof(buff));
		if (rc == 0)
			printf("%8d %s\n", bmapid2compid(id), buff);
		id++;
	}
	printf("-------- --------------\n");
	return rc;
}

struct bqprint {
	void *p;
	int (*print)(void* p, char *fmt, ...);
};

/**
 * Print message to \c buff.
 * \param s The store handle.
 * \param buff The output buffer.
 * \param buff_len The length of the output buffer.
 * \param msg The message (ptn_id, args).
 * \return 0 on success.
 * \return Error code on error.
 */
int bq_print_msg(struct bquery *q, struct bdstr *bdstr,
		 const struct bmsg *msg)
{
	int rc = 0;
	const struct bstr *ptn;
	const struct bstr *bstr;
	struct bq_store *s = q->store;
	struct bptn_store *ptn_store = s->ptn_store;
	struct btkn_store *tkn_store = s->tkn_store;
	ptn = bmap_get_bstr(ptn_store->map, msg->ptn_id);
	if (!ptn)
		return ENOENT;
	const uint32_t *msg_arg = msg->argv;
	const uint32_t *ptn_tkn = ptn->u32str;
	int len = ptn->blen;
	int blen;
	fmt_tkn_begin(q->formatter, bdstr);
	while (len) {
		struct btkn_attr attr;
		uint32_t tkn_id = *ptn_tkn++;
		if (tkn_id == BMAP_ID_STAR)
			tkn_id = *msg_arg++;
		bstr = btkn_store_get_bstr(tkn_store, tkn_id);
		if (!bstr) {
			rc = ENOENT;
			goto out;
		}
		attr = btkn_store_get_attr(tkn_store, tkn_id);
		fmt_tkn_fmt(q->formatter, bdstr, bstr, &attr, tkn_id);
		if (rc)
			goto out;
		len -= sizeof(*ptn_tkn);
	}
	fmt_tkn_end(q->formatter, bdstr);
out:
	return rc;
}

int bq_entry_msg_tkn(struct bquery *q,
		int (*cb)(uint32_t tkn_id, void *ctxt),
		void *ctxt)
{
	int rc = 0;
	struct bq_store *s = q->store;
	const struct bstr *ptn;
	const struct bstr *bstr;
	struct bmsg *msg = NULL;

	msg = bq_entry_get_msg(q);
	if (!msg)
		return errno;

	ptn = bmap_get_bstr(s->ptn_store->map, msg->ptn_id);
	if (!ptn) {
		rc = ENOENT;
		goto out;
	}

	const uint32_t *msg_arg = msg->argv;
	const uint32_t *ptn_tkn = ptn->u32str;
	int len = ptn->blen;
	while (len) {
		uint32_t tkn_id = *ptn_tkn++;
		if (tkn_id == BMAP_ID_STAR)
			tkn_id = *msg_arg++;
		rc = cb(tkn_id, ctxt);
		if (rc)
			goto out;
		len -= sizeof(*ptn_tkn);
	}
out:
	if (msg)
		free(msg);
	return rc;
}

/*
 * Clean-up resources
 */
static
void bquery_cleanup(struct bquery *q)
{
	struct brange_u32 *r;
	while ((r = TAILQ_FIRST(&q->hst_rngs))) {
		TAILQ_REMOVE(&q->hst_rngs, r, link);
		free(r);
	}
	if (q->hst_rng_itr) {
		brange_u32_iter_free(q->hst_rng_itr);
		q->hst_rng_itr = NULL;
	}
	while ((r = TAILQ_FIRST(&q->ptn_rngs))) {
		TAILQ_REMOVE(&q->ptn_rngs, r, link);
		free(r);
	}
	if (q->ptn_rng_itr) {
		brange_u32_iter_free(q->ptn_rng_itr);
		q->ptn_rng_itr = NULL;
	}
	if (q->obj) {
		sos_obj_put(q->obj);
		q->obj = NULL;
	}
	if (q->itr) {
		sos_iter_free(q->itr);
		q->itr = NULL;
	}
	if (q->bsos) {
		q->bsos_close(q->bsos, SOS_COMMIT_ASYNC);
		q->bsos = NULL;
	}
	if (q->hst_ids) {
		bset_u32_free(q->hst_ids);
		q->hst_ids = NULL;
	}
	if (q->ptn_ids) {
		bset_u32_free(q->ptn_ids);
		q->ptn_ids = NULL;
	}
}

static
void bquery_destroy(struct bquery *q)
{
	bquery_cleanup(q);
	free(q);
}

void bmsgquery_destroy(struct bmsgquery *msgq)
{
	if (msgq->bheap) {
		struct bq_msg_ptc_hent *hent;
		int n = msgq->bheap->len;
		int i;
		for (i = 0; i < n; i++) {
			hent = msgq->bheap->array[i];
			bq_msg_ptc_hent_free(hent);
		}
		bheap_free(msgq->bheap);
	}
	bquery_destroy(&msgq->base);
}

void bimgquery_destroy(struct bimgquery *imgq)
{
	free(imgq->store_name);
	bquery_destroy((void*)imgq);
}

int __default_tkn_fmt(struct bq_formatter *fmt, struct bdstr *bdstr,
			const struct bstr *tkn, struct btkn_attr *attr,
			uint32_t tkn_id)
{
	return bdstr_append_bstr(bdstr, tkn);
}

int __default_date_fmt(struct bq_formatter *fmt, struct bdstr *bdstr, const struct timeval *tv)
{
	char buff[64];
	int len;
	char *tmp;
	struct tm tm;
	localtime_r(&tv->tv_sec, &tm);
	len = strftime(buff, sizeof(buff), "%FT%T", &tm);
	tmp = buff + len;
	snprintf(buff+len, sizeof(buff)-len, ".%06d%+03d:%02d ",
					(int)tv->tv_usec,
					(int)tm.tm_gmtoff / 3600,
					(int) (tm.tm_gmtoff % 3600)/60);
	return bdstr_append(bdstr, buff);
}

int __default_host_fmt(struct bq_formatter *fmt, struct bdstr *bdstr, const struct bstr *bstr)
{
	int rc;
	rc = bdstr_append_bstr(bdstr, bstr);
	if (rc)
		return rc;
	return bdstr_append(bdstr, " ");
}

static struct bq_formatter default_formatter = {
	.tkn_fmt = __default_tkn_fmt,
	.date_fmt = __default_date_fmt,
	.host_fmt = __default_host_fmt,
};

struct bq_formatter *bquery_default_formatter()
{
	return &default_formatter;
}

void bq_set_formatter(struct bquery *bq, struct bq_formatter *fmt)
{
	bq->formatter = fmt;
}

static
time_t parse_ts(const char *ts)
{
	struct tm tm = {0};
	char *ts_ret;
	time_t t;
	ts_ret = strptime(ts, "%F %T", &tm);
	if (ts_ret != NULL) {
		tm.tm_isdst = -1;
		t = mktime(&tm);
	} else {
		/* try seconds since Epoch instead */
		t = strtol(ts, &ts_ret, 0);
		if (*ts_ret != '\0')
			return -1;
	}
	return t;
}

int bq_first_entry(struct bquery *q)
{
	return q->first_entry(q);
}

int bq_next_entry(struct bquery *q)
{
	return q->next_entry(q);
}

int bq_prev_entry(struct bquery *q)
{
	return q->prev_entry(q);
}

int bq_last_entry(struct bquery *q)
{
	return q->last_entry(q);
}

int bq_get_pos(struct bquery *q, struct bquery_pos *pos)
{
	return q->get_pos(q, pos);
}

int bq_set_pos(struct bquery *q, struct bquery_pos *pos)
{
	return q->set_pos(q, pos);
}

int bquery_pos_print(struct bquery_pos *pos, struct bdstr *bdstr)
{
	int rc = 0;
	int i;
	for (i = 0; i < sizeof(pos->data); i++) {
		rc = bdstr_append_printf(bdstr, "%02hhx", pos->data[i]);
		if (rc)
			return rc;
	}
	return rc;
}

int bquery_pos_from_str(struct bquery_pos *pos, const char *str)
{
	int i;
	for (i = 0; i < sizeof(pos->data); i++) {
		char s[5] ="0x00";
		if (!*str)
			return EINVAL;
		s[2] = *str++;
		if (!*str)
			return EINVAL;
		s[3] = *str++;
		pos->data[i] = strtol(s, NULL, 0);
	}

	return 0;
}

int bq_msg_ptc_first_entry(struct bquery *q);
int bq_msg_ptc_next_entry(struct bquery *q);
int bq_msg_ptc_prev_entry(struct bquery *q);
int bq_msg_ptc_last_entry(struct bquery *q);
int bq_msg_ptc_get_pos(struct bquery *q, struct bquery_pos *pos);
int bq_msg_ptc_set_pos(struct bquery *q, struct bquery_pos *pos);

int bq_msg_tc_first_entry(struct bquery *q);
int bq_msg_tc_next_entry(struct bquery *q);
int bq_msg_tc_prev_entry(struct bquery *q);
int bq_msg_tc_last_entry(struct bquery *q);
int bq_msg_tc_get_pos(struct bquery *q, struct bquery_pos *pos);
int bq_msg_tc_set_pos(struct bquery *q, struct bquery_pos *pos);

int bquery_init(struct bquery *q, struct bq_store *store, const char *hst_ids,
			     const char *ptn_ids, const char *ts0,
			     const char *ts1, int is_text, char sep)
{
	int rc = 0;
	ssize_t len = 0;

	/* Call tzset first for correct localtime_r() result in the program. */
	tzset();

	assert(q);
	bzero(q, sizeof(*q));

	if (!store) {
		rc = EINVAL;
		goto out;
	}

	q->store = store;
	q->stat = BQ_STAT_INIT;

	if (hst_ids) {
		q->hst_ids = bset_u32_from_numlist(hst_ids, MASK_HSIZE);
		if (!q->hst_ids) {
			rc = errno;
			goto err;
		}
		rc = bset_u32_to_brange_u32(q->hst_ids, &q->hst_rngs);
		if (rc)
			goto err;
		q->hst_rng_itr = brange_u32_iter_new(TAILQ_FIRST(&q->hst_rngs));
		if (!q->hst_rng_itr)
			goto err;
	}

	if (ptn_ids) {
		q->ptn_ids = bset_u32_from_numlist(ptn_ids, MASK_HSIZE);
		if (!q->ptn_ids) {
			rc = errno;
			goto err;
		}
		rc = bset_u32_to_brange_u32(q->ptn_ids, &q->ptn_rngs);
		if (rc)
			goto err;
		q->ptn_rng_itr = brange_u32_iter_new(TAILQ_FIRST(&q->ptn_rngs));
		if (!q->ptn_rng_itr)
			goto err;
	}

	if (ts0) {
		q->ts_0 = parse_ts(ts0);
		if (q->ts_0 == -1) {
			rc = EINVAL;
			goto err;
		}
	}
	if (ts1) {
		q->ts_1 = parse_ts(ts1);
		if (q->ts_1 == -1) {
			rc = EINVAL;
			goto err;
		}
	}

	q->text_flag = is_text;
        q->sep = (sep)?(sep):(' ');
	q->formatter = bquery_default_formatter();

	goto out;

err:
	bquery_cleanup(q);

out:
	return rc;
}

void bq_msg_ptc_hent_free(struct bq_msg_ptc_hent *hent)
{
	if (hent->iter)
		sos_iter_free(hent->iter);
	if (hent->hst_rng_itr)
		brange_u32_iter_free(hent->hst_rng_itr);
	free(hent);
}

struct bq_msg_ptc_hent *bq_msg_ptc_hent_new(struct bmsgquery *msgq, uint32_t ptn_id)
{
	struct bq_msg_ptc_hent *hent = calloc(1, sizeof(*hent));
	if (!hent)
		return NULL;
	hent->ptn_id = ptn_id;
	hent->msgq = msgq;
	if (msgq->base.hst_ids) {
		hent->hst_rng_itr = brange_u32_iter_new(TAILQ_FIRST(&msgq->base.hst_rngs));
		if (!hent->hst_rng_itr)
			goto err0;
	}
	hent->iter = sos_index_iter_new(((struct bsos_msg *)msgq->base.bsos)->index_ptc);
	if (!hent->iter)
		goto err1;

	return hent;
err1:
	brange_u32_iter_free(hent->hst_rng_itr);
err0:
	bq_msg_ptc_hent_free(hent);
	return NULL;
}

int bq_msg_ptc_hent_cmp_inc(struct bq_msg_ptc_hent *a, struct bq_msg_ptc_hent *b)
{
	if (a->sec_comp_id < b->sec_comp_id)
		return -1;
	if (a->sec_comp_id > b->sec_comp_id)
		return 1;
	if (a->msg->data.uint32_[BSOS_MSG_USEC] < b->msg->data.uint32_[BSOS_MSG_USEC])
		return -1;
	if (a->msg->data.uint32_[BSOS_MSG_USEC] > b->msg->data.uint32_[BSOS_MSG_USEC])
		return 1;
	if (a->ptn_id < b->ptn_id)
		return -1;
	if (a->ptn_id > b->ptn_id)
		return 1;
	return 0;
}

int bq_msg_ptc_hent_cmp_dec(struct bq_msg_ptc_hent *a, struct bq_msg_ptc_hent *b)
{
	if (a->sec_comp_id > b->sec_comp_id)
		return -1;
	if (a->sec_comp_id < b->sec_comp_id)
		return 1;
	if (a->msg->data.uint32_[BSOS_MSG_USEC] > b->msg->data.uint32_[BSOS_MSG_USEC])
		return -1;
	if (a->msg->data.uint32_[BSOS_MSG_USEC] < b->msg->data.uint32_[BSOS_MSG_USEC])
		return 1;
	if (a->ptn_id > b->ptn_id)
		return -1;
	if (a->ptn_id < b->ptn_id)
		return 1;
	return 0;
}

static inline
uint32_t bsos_msg_get_sec(sos_array_t msg)
{
	return msg->data.uint32_[BSOS_MSG_SEC];
}

static inline
uint32_t bsos_msg_get_usec(sos_array_t msg)
{
	return msg->data.uint32_[BSOS_MSG_USEC];
}

static inline
uint32_t bsos_msg_get_comp_id(sos_array_t msg)
{
	return msg->data.uint32_[BSOS_MSG_COMP_ID];
}

static inline
uint32_t bsos_msg_get_ptn_id(sos_array_t msg)
{
	return msg->data.uint32_[BSOS_MSG_PTN_ID];
}

static void bq_msg_ptc_hent_obj_update(struct bq_msg_ptc_hent *hent)
{
	if (hent->obj) {
		sos_obj_put(hent->obj);
		hent->obj = NULL;
		hent->msg = NULL;
		hent->sec_comp_id = 0;
	}
	hent->obj = sos_iter_obj(hent->iter);
	if (hent->obj) {
		hent->msg = sos_obj_ptr(hent->obj);
		hent->sec = bsos_msg_get_sec(hent->msg);
		hent->comp_id = bsos_msg_get_comp_id(hent->msg);
	}
}

static inline
int bq_msg_ptc_hent_check_cond(struct bq_msg_ptc_hent *hent)
{
	uint32_t ts = hent->msg->data.uint32_[BSOS_MSG_SEC];
	uint32_t comp_id = hent->msg->data.uint32_[BSOS_MSG_COMP_ID];
	uint32_t ptn_id = hent->msg->data.uint32_[BSOS_MSG_PTN_ID];

	if (hent->ptn_id != ptn_id)
		return BQ_CHECK_COND_PTN;
	if (hent->msgq->base.ts_0 && ts < hent->msgq->base.ts_0)
		return BQ_CHECK_COND_TS0;
	if (hent->msgq->base.ts_1 && hent->msgq->base.ts_1 < ts)
		return BQ_CHECK_COND_TS1;
	if (hent->msgq->base.hst_ids && !bset_u32_exist(hent->msgq->base.hst_ids, comp_id))
		return BQ_CHECK_COND_HST;
	return BQ_CHECK_COND_OK;
}

int bq_msg_ptc_hent_first(struct bq_msg_ptc_hent *hent)
{
	int rc;
	uint32_t sec, comp_id, ptn_id;
	SOS_KEY(key);
	struct bsos_msg_key_ptc k = {0};

	k.ptn_id = hent->ptn_id;

	if (hent->hst_rng_itr) {
		brange_u32_iter_begin(hent->hst_rng_itr, &k.comp_id);
	}

	if (hent->msgq->base.ts_0) {
		k.sec = hent->msgq->base.ts_0;
	}

	bsos_msg_key_ptc_htobe(&k);
	sos_key_set(key, &k, sizeof(k));
	rc = sos_iter_sup(hent->iter, key);
	if (rc)
		goto out;
	bq_msg_ptc_hent_obj_update(hent);
	rc = bq_msg_ptc_hent_check_cond(hent);
	if (rc)
		rc = bq_msg_ptc_hent_next(hent);

out:
	return rc;
}

int bq_msg_ptc_hent_next(struct bq_msg_ptc_hent *hent)
{
	SOS_KEY(key);
	struct bsos_msg_key_ptc k = {0};
	int rc;

	rc = sos_iter_next(hent->iter);
	if (rc)
		goto out;
again:
	bq_msg_ptc_hent_obj_update(hent);
	rc = bq_msg_ptc_hent_check_cond(hent);
	switch (rc) {
	case BQ_CHECK_COND_OK:
		goto out;
	case BQ_CHECK_COND_TS0:
		k.sec = hent->msgq->base.ts_0;
		k.comp_id = 0;
		break;
	case BQ_CHECK_COND_HST:
		k.sec = bsos_msg_get_sec(hent->msg);
		k.comp_id = bsos_msg_get_comp_id(hent->msg) + 1;
		if (!k.comp_id) {
			/* overflow */
			rc = ENOENT;
		} else {
			rc = brange_u32_iter_fwd_seek(hent->hst_rng_itr, &k.comp_id);
		}
		switch (rc) {
		case ENOENT:
			/* use next timestamp */
			k.sec++;
			/* let through */
		case EINVAL:
			brange_u32_iter_begin(hent->hst_rng_itr, &k.comp_id);
			break;
		}
		break;
	case BQ_CHECK_COND_TS1:
		/* Out of TS range, no need to continue */
		rc = ENOENT;
		goto out;
	case BQ_CHECK_COND_PTN:
		/* Out of PTN range, no need to continue */
		rc = ENOENT;
		goto out;
	}

	k.ptn_id = hent->ptn_id;
	bsos_msg_key_ptc_htobe(&k);
	sos_key_set(key, &k, sizeof(k));
	rc = sos_iter_sup(hent->iter, key); /* this will already be the first dup */
	if (!rc)
		goto again;
out:
	return rc;
}
int bq_msg_ptc_hent_last(struct bq_msg_ptc_hent *hent)
{
	int rc;
	uint32_t sec, comp_id, ptn_id;
	SOS_KEY(key);
	struct bsos_msg_key_ptc k = {-1, -1, -1};

	k.ptn_id = hent->ptn_id;

	if (hent->hst_rng_itr) {
		brange_u32_iter_end(hent->hst_rng_itr, &k.comp_id);
	}

	if (hent->msgq->base.ts_1) {
		k.sec = hent->msgq->base.ts_1;
	}

	bsos_msg_key_ptc_htobe(&k);
	sos_key_set(key, &k, sizeof(k));
	rc = __sos_iter_inf_last(hent->iter, key);
	if (rc)
		goto out;
	bq_msg_ptc_hent_obj_update(hent);
	rc = bq_msg_ptc_hent_check_cond(hent);
	if (rc)
		rc = bq_msg_ptc_hent_prev(hent);

out:
	return rc;
}

int bq_msg_ptc_hent_prev(struct bq_msg_ptc_hent *hent)
{
	SOS_KEY(key);
	struct bsos_msg_key_ptc k = {0};
	int rc;

	rc = sos_iter_prev(hent->iter);
	if (rc)
		goto out;
again:
	bq_msg_ptc_hent_obj_update(hent);
	rc = bq_msg_ptc_hent_check_cond(hent);
	switch (rc) {
	case BQ_CHECK_COND_OK:
		goto out;
	case BQ_CHECK_COND_TS1:
		k.sec = hent->msgq->base.ts_1;
		k.comp_id = 0XFFFFFFFF;
		break;
	case BQ_CHECK_COND_HST:
		k.sec = bsos_msg_get_sec(hent->msg);
		k.comp_id = bsos_msg_get_comp_id(hent->msg) - 1;
		if (k.comp_id == 0XFFFFFFFF) {
			/* underflow */
			rc = ENOENT;
		} else {
			rc = brange_u32_iter_bwd_seek(hent->hst_rng_itr, &k.comp_id);
		}
		switch (rc) {
		case ENOENT:
			/* use next timestamp */
			k.sec--;
			/* let through */
		case EINVAL:
			brange_u32_iter_end(hent->hst_rng_itr, &k.comp_id);
			break;
		}
		break;
	case BQ_CHECK_COND_TS0:
		/* Out of TS range, no need to continue */
		rc = ENOENT;
		goto out;
	case BQ_CHECK_COND_PTN:
		/* Out of PTN range, no need to continue */
		rc = ENOENT;
		goto out;
	}

	k.ptn_id = hent->ptn_id;
	bsos_msg_key_ptc_htobe(&k);
	sos_key_set(key, &k, sizeof(k));
	rc = __sos_iter_inf_last(hent->iter, key); /* this will already be the first dup */
	if (!rc)
		goto again;
out:
	return rc;
}

static
int __bq_msg_ptc_init(struct bmsgquery *msgq)
{
	int rc = 0;
	int n = msgq->base.ptn_ids->count;
	uint32_t ptn_id;
	struct bset_u32_iter *ptnid_iter = NULL;
	struct bq_msg_ptc_hent *hent;
	struct bheap *bheap;
	int i;

	ptnid_iter = bset_u32_iter_new(msgq->base.ptn_ids);
	if (!ptnid_iter) {
		rc = ENOMEM;
		goto out;
	}

	while (0 == (rc = bset_u32_iter_next(ptnid_iter, &ptn_id))) {
		hent = bq_msg_ptc_hent_new(msgq, ptn_id);
		if (!hent) {
			goto err0;
		}
		LIST_INSERT_HEAD(&msgq->hent_list, hent, link);
	}

	bheap = bheap_new(n, (void*)bq_msg_ptc_hent_cmp_inc);

	if (!bheap) {
		rc = errno;
		goto err0;
	}

	msgq->bheap = bheap;

	rc = 0;

	goto out;

err0:
	while (NULL != (hent = LIST_FIRST(&msgq->hent_list))) {
		LIST_REMOVE(hent, link);
		bq_msg_ptc_hent_free(hent);
	}

out:
	/* clean-up */
	if (ptnid_iter)
		bset_u32_iter_free(ptnid_iter);
	return rc;
}

static
int __bq_msg_tc_init(struct bmsgquery *msgq)
{
	bsos_msg_t bsos_msg = msgq->base.bsos;
	sos_iter_t itr = sos_index_iter_new(bsos_msg->index_tc);
	if (!itr)
		return errno;
	msgq->base.itr = itr;
	return 0;
}

static
int __bq_msg_open_bsos(struct bmsgquery *msgq)
{
	int rc = 0;
	bsos_msg_t bsos_msg = bsos_msg_open(msgq->base.sos_prefix, 0);
	if (!bsos_msg)
		return errno;
	msgq->base.bsos = bsos_msg;
	switch (msgq->idxtype) {
	case BMSGIDX_PTC:
		rc = __bq_msg_ptc_init(msgq);
		break;
	case BMSGIDX_TC:
		rc = __bq_msg_tc_init(msgq);
		break;
	default:
		assert(0 && "invalid msgq->idxtype");
	}

	if (rc) {
		msgq->base.bsos = NULL;
		bsos_msg_close(bsos_msg, SOS_COMMIT_ASYNC);
		return rc;
	}

	return 0;
}

struct bmsgquery* bmsgquery_create(struct bq_store *store, const char *hst_ids,
			     const char *ptn_ids, const char *ts0,
			     const char *ts1, int is_text, char sep, int *rc)
{
	int _rc = 0;
	ssize_t len;
	struct bmsgquery *msgq = calloc(1, sizeof(*msgq));

	if (!msgq) {
		_rc = ENOMEM;
		goto err;
	}

	_rc = bquery_init(&msgq->base, store, hst_ids, ptn_ids,
						ts0, ts1, is_text, sep);
	if (_rc)
		goto err;

	if (msgq->base.ptn_ids) {
		msgq->idxtype = BMSGIDX_PTC;
		msgq->base.next_entry = bq_msg_ptc_next_entry;
		msgq->base.prev_entry = bq_msg_ptc_prev_entry;
		msgq->base.first_entry = bq_msg_ptc_first_entry;
		msgq->base.last_entry = bq_msg_ptc_last_entry;
		msgq->base.get_pos = bq_msg_ptc_get_pos;
		msgq->base.set_pos = bq_msg_ptc_set_pos;
	} else {
		msgq->idxtype = BMSGIDX_TC;
		msgq->base.next_entry = bq_msg_tc_next_entry;
		msgq->base.prev_entry = bq_msg_tc_prev_entry;
		msgq->base.first_entry = bq_msg_tc_first_entry;
		msgq->base.last_entry = bq_msg_tc_last_entry;
		msgq->base.get_pos = bq_msg_tc_get_pos;
		msgq->base.set_pos = bq_msg_tc_set_pos;
	}

	msgq->base.get_sec = __bq_msg_entry_get_sec;
	msgq->base.get_usec = __bq_msg_entry_get_usec;
	msgq->base.get_ptn_id = __bq_msg_entry_get_ptn_id;
	msgq->base.get_comp_id = __bq_msg_entry_get_comp_id;

	msgq->base.bsos_open = (void*)bsos_msg_open;
	msgq->base.bsos_close = (void*)bsos_msg_close;

	len = snprintf(msgq->base.sos_prefix, PATH_MAX,
					"%s/msg_store/msg", store->path);

	_rc = __bq_msg_open_bsos(msgq);
	if (_rc)
		goto err;

	return msgq;

err:
	if (rc)
		*rc = _rc;
	bmsgquery_destroy(msgq);
	return NULL;
}

int bq_img_first_entry(struct bquery *q);
int bq_img_next_entry(struct bquery *q);
int bq_img_prev_entry(struct bquery *q);
int bq_img_last_entry(struct bquery *q);
int bq_img_get_pos(struct bquery *q, struct bquery_pos *pos);
int bq_img_set_pos(struct bquery *q, struct bquery_pos *pos);

int bimgquery_next_ptn(struct bimgquery *q);
int bimgquery_prev_ptn(struct bimgquery *q);

static
int __bq_img_open_bsos(struct bimgquery *imgq)
{
	struct bquery *q = (void*)imgq;
	sos_iter_t iter;
	bsos_img_t bsos_img;
	bsos_img = bsos_img_open(q->sos_prefix, 0);
	if (!bsos_img)
		return errno;
	iter = sos_index_iter_new(bsos_img->index);
	if (!iter) {
		bsos_img_close(bsos_img, SOS_COMMIT_ASYNC);
		return ENOMEM;
	}

	q->itr = iter;
	q->bsos = bsos_img;
	return 0;
}

struct bimgquery* bimgquery_create(struct bq_store *store, const char *hst_ids,
				const char *ptn_ids, const char *ts0,
				const char *ts1, const char *img_store_name,
				int *rc)
{
	int _rc = 0;
	ssize_t len;

	struct bimgquery *imgq = calloc(1, sizeof(*imgq));
	if (!imgq) {
		_rc = ENOMEM;
		return NULL;
	}

	_rc = bquery_init(&imgq->base, store, hst_ids, ptn_ids, ts0, ts1, 0, 0);
	if (_rc)
		goto err;

	imgq->base.get_sec = __bq_img_entry_get_sec;
	imgq->base.get_usec = __bq_img_entry_get_usec;
	imgq->base.get_comp_id = __bq_img_entry_get_comp_id;
	imgq->base.get_ptn_id = __bq_img_entry_get_ptn_id;

	imgq->base.first_entry = bq_img_first_entry;
	imgq->base.next_entry = bq_img_next_entry;
	imgq->base.prev_entry = bq_img_prev_entry;
	imgq->base.last_entry = bq_img_last_entry;
	imgq->base.get_pos = bq_img_get_pos;
	imgq->base.set_pos = bq_img_set_pos;

	imgq->base.bsos_open = (void*)bsos_img_open;
	imgq->base.bsos_close = (void*)bsos_img_close;

	len = snprintf(imgq->base.sos_prefix, PATH_MAX, "%s/img_store/%s",
						store->path, img_store_name);
	imgq->store_name = strdup(img_store_name);
	if (!imgq->store_name)
		goto err;

	_rc = __bq_img_open_bsos(imgq);

	if (_rc)
		goto err;

	return imgq;
err:
	if (rc)
		*rc = _rc;
	bimgquery_destroy(imgq);
	return NULL;
}

bq_stat_t bq_get_stat(struct bquery *q)
{
	return q->stat;
}

char* __str_combine(const char *str0, const char *str1)
{
	int len = strlen(str0) + strlen(str1);
	char *str = malloc(len + 1);
	if (!str)
		return NULL;
	sprintf(str, "%s%s", str0, str1);
	return str;
}


static inline
void __msg_ptc_obj_update(struct bmsgquery *msgq)
{
	struct bq_msg_ptc_hent *hent = bheap_get_top(msgq->bheap);
	if (hent) {
		msgq->base.obj = hent->obj;
		msgq->msg = hent->msg;
	} else {
		msgq->base.obj = NULL;
		msgq->msg = NULL;
	}
}

static inline
void __msg_obj_update(struct bmsgquery *msgq)
{
	if (msgq->base.obj) {
		sos_obj_put(msgq->base.obj);
		msgq->base.obj = NULL;
		msgq->msg = NULL;
	}
	msgq->base.obj = sos_iter_obj(msgq->base.itr);
	assert(msgq->base.obj);
	if (msgq->base.obj) {
		msgq->msg = sos_obj_ptr(msgq->base.obj);
	}
}

static inline
bq_check_cond_e bq_check_cond(struct bquery *q)
{
	uint32_t ts = bq_entry_get_sec(q);
	uint32_t comp_id = bq_entry_get_comp_id(q);
	uint32_t ptn_id = bq_entry_get_ptn_id(q);

	if (q->ts_0 && ts < q->ts_0)
		return BQ_CHECK_COND_TS0;
	if (q->ts_1 && q->ts_1 < ts)
		return BQ_CHECK_COND_TS1;
	if (q->hst_ids && !bset_u32_exist(q->hst_ids, comp_id))
		return BQ_CHECK_COND_HST;
	if (q->ptn_ids && !bset_u32_exist(q->ptn_ids, ptn_id))
		return BQ_CHECK_COND_PTN;
	return BQ_CHECK_COND_OK;
}

static void __img_obj_update(struct bimgquery *imgq)
{
	if (imgq->base.obj) {
		sos_obj_put(imgq->base.obj);
		imgq->base.obj = NULL;
		imgq->img = NULL;
	}
	imgq->base.obj = sos_iter_obj(imgq->base.itr);
	if (imgq->base.obj) {
		imgq->img = sos_obj_ptr(imgq->base.obj);
	}
}

int bq_img_first_entry(struct bquery *q)
{
	struct bimgquery *imgq = (void*)q;
	int rc;
	uint32_t sec, comp_id, ptn_id;
	SOS_KEY(key);
	struct bsos_img_key bsi_key;

	bsi_key.comp_id = 0;
	bsi_key.ts = 0;
	bsi_key.ptn_id = 0;

	if (q->ptn_rng_itr) {
		brange_u32_iter_begin(q->ptn_rng_itr, &bsi_key.ptn_id);
	}

	if (q->hst_rng_itr) {
		brange_u32_iter_begin(q->hst_rng_itr, &bsi_key.comp_id);
	}

	if (q->ts_0) {
		bsi_key.ts = q->ts_0;
	}

	bsos_img_key_htobe(&bsi_key);
	sos_key_set(key, &bsi_key, sizeof(bsi_key));
	rc = sos_iter_sup(q->itr, key);
	if (rc)
		goto out;
	__img_obj_update(imgq);
	if (bq_check_cond(q) != BQ_CHECK_COND_OK)
		rc = bq_next_entry(q);

out:
	return rc;
}

int bq_img_next_entry(struct bquery *q)
{
	struct bimgquery *imgq = (void*)q;
	SOS_KEY(key);
	struct bsos_img_key bsi_key;
	int rc;

	rc = sos_iter_next(q->itr);
	if (rc)
		goto out;
again:
	__img_obj_update(imgq);
	rc = bq_check_cond(q);
	switch (rc) {
	case BQ_CHECK_COND_OK:
		goto out;
	case BQ_CHECK_COND_TS0:
		bsi_key.ptn_id = bq_entry_get_ptn_id(q);
		bsi_key.ts = q->ts_0;
		bsi_key.comp_id = bq_entry_get_comp_id(q);
		break;
	case BQ_CHECK_COND_HST:
		bsi_key.ptn_id = bq_entry_get_ptn_id(q);
		bsi_key.ts = bq_entry_get_sec(q);
		bsi_key.comp_id = bq_entry_get_comp_id(q) + 1;
		rc = brange_u32_iter_fwd_seek(q->hst_rng_itr, &bsi_key.comp_id);
		switch (rc) {
		case ENOENT:
			/* use next timestamp */
			bsi_key.ts++;
			brange_u32_iter_begin(q->hst_rng_itr, &bsi_key.comp_id);
			break;
		case EINVAL:
			bsi_key.comp_id = q->hst_rng_itr->current_value;
			break;
		}
		break;
	case BQ_CHECK_COND_TS1:
	case BQ_CHECK_COND_PTN:
		/* End of current PTN, continue with next PTN */
		bsi_key.ptn_id = bq_entry_get_ptn_id(q) + 1;
		if (q->ptn_rng_itr) {
			rc = brange_u32_iter_fwd_seek(q->ptn_rng_itr,
							&bsi_key.ptn_id);
			if (rc == ENOENT)
				goto out;
			assert(rc == 0);
		}
		brange_u32_iter_begin(q->hst_rng_itr, &bsi_key.comp_id);
		bsi_key.ts = q->ts_0;
		break;
	}

	bsos_img_key_htobe(&bsi_key);
	sos_key_set(key, &bsi_key, sizeof(bsi_key));
	rc = sos_iter_sup(q->itr, key); /* this will already be the first dup */
	if (!rc)
		goto again;
out:
	return rc;
}

int bimgquery_next_ptn(struct bimgquery *imgq)
{
	SOS_KEY(key);
	struct bsos_img_key bsi_key;
	struct bquery *q = &imgq->base;
	bq_msg_ref_t ref;
	int rc;

	ref = bq_entry_get_ref(q);
	if (ref.ref[0] == 0 && ref.ref[1] == 0) {
		return ENOENT;
	}

	goto next_ptn;

again:
	__img_obj_update(imgq);
	rc = bq_check_cond(q);
	switch (rc) {
	case BQ_CHECK_COND_OK:
		goto out;
	case BQ_CHECK_COND_TS0:
		bsi_key.ptn_id = bq_entry_get_ptn_id(q);
		bsi_key.ts = q->ts_0;
		bsi_key.comp_id = bq_entry_get_comp_id(q);
		break;
	case BQ_CHECK_COND_HST:
		bsi_key.ptn_id = bq_entry_get_ptn_id(q);
		bsi_key.ts = bq_entry_get_sec(q);
		bsi_key.comp_id = bq_entry_get_comp_id(q) + 1;
		rc = brange_u32_iter_fwd_seek(q->hst_rng_itr, &bsi_key.comp_id);
		switch (rc) {
		case ENOENT:
			/* use next timestamp */
			bsi_key.ts++;
			brange_u32_iter_begin(q->hst_rng_itr, &bsi_key.comp_id);
			break;
		case EINVAL:
			bsi_key.comp_id = q->hst_rng_itr->current_value;
			break;
		}
		break;
	case BQ_CHECK_COND_TS1:
	case BQ_CHECK_COND_PTN:
next_ptn:
		/* End of current PTN, continue with next PTN */
		bsi_key.ptn_id = bq_entry_get_ptn_id(q) + 1;
		if (q->ptn_rng_itr) {
			rc = brange_u32_iter_fwd_seek(q->ptn_rng_itr,
							&bsi_key.ptn_id);
			if (rc == ENOENT)
				goto out;
			assert(rc == 0);
		}
		brange_u32_iter_begin(q->hst_rng_itr, &bsi_key.comp_id);
		bsi_key.ts = q->ts_0;
		break;
	}

	bsos_img_key_htobe(&bsi_key);
	sos_key_set(key, &bsi_key, sizeof(bsi_key));
	rc = sos_iter_sup(q->itr, key); /* this will already be the first dup */
	if (!rc)
		goto again;
out:
	return rc;
}

int bq_img_prev_entry(struct bquery *q)
{
	struct bimgquery *imgq = (void*)q;
	SOS_KEY(key);
	sos_key_t sos_key;
	struct bsos_img_key bsi_key;
	int rc;

	rc = sos_iter_prev(q->itr);
	if (rc)
		goto out;
again:
	__img_obj_update(imgq);
	rc = bq_check_cond(q);
	switch (rc) {
	case BQ_CHECK_COND_OK:
		goto out;
	case BQ_CHECK_COND_TS1:
		bsi_key.ptn_id = bq_entry_get_ptn_id(q);
		bsi_key.ts = q->ts_1;
		bsi_key.comp_id = bq_entry_get_comp_id(q);
		break;
	case BQ_CHECK_COND_HST:
		bsi_key.ptn_id = bq_entry_get_ptn_id(q);
		bsi_key.ts = bq_entry_get_sec(q);
		bsi_key.comp_id = bq_entry_get_comp_id(q) - 1;
		rc = brange_u32_iter_bwd_seek(q->hst_rng_itr, &bsi_key.comp_id);
		if (rc) {
			/* use next timestamp */
			bsi_key.ts--;
			brange_u32_iter_end(q->hst_rng_itr, &bsi_key.comp_id);
		}
		break;
	case BQ_CHECK_COND_TS0:
	case BQ_CHECK_COND_PTN:
		/* End of current PTN, continue with next PTN */
		bsi_key.ptn_id = bq_entry_get_ptn_id(q) - 1;
		if (q->ptn_rng_itr) {
			rc = brange_u32_iter_bwd_seek(q->ptn_rng_itr, &bsi_key.ptn_id);
			if (rc == ENOENT)
				goto out;
			assert(rc == 0);
		}
		brange_u32_iter_end(q->hst_rng_itr, &bsi_key.comp_id);
		bsi_key.ts = q->ts_1;
		break;
	}

	bsos_img_key_htobe(&bsi_key);
	sos_key_set(key, &bsi_key, sizeof(bsi_key));
	rc = __sos_iter_inf_last(q->itr, key); /* this point to the last dup */
	if (!rc)
		goto again;

out:
	return rc;
}

int bimgquery_prev_ptn(struct bimgquery *imgq)
{
	struct bquery *q = (void*)imgq;
	SOS_KEY(key);
	sos_key_t sos_key;
	struct bsos_img_key bsi_key;
	bq_msg_ref_t ref;
	int rc;

	ref = bq_entry_get_ref(q);
	if (ref.ref[0] == 0 && ref.ref[1] == 0) {
		return ENOENT;
	}

	goto prev_ptn;

again:
	__img_obj_update(imgq);
	rc = bq_check_cond(q);
	switch (rc) {
	case BQ_CHECK_COND_OK:
		goto out;
	case BQ_CHECK_COND_TS1:
		bsi_key.ptn_id = bq_entry_get_ptn_id(q);
		bsi_key.ts = q->ts_1;
		bsi_key.comp_id = bq_entry_get_comp_id(q);
		break;
	case BQ_CHECK_COND_HST:
		bsi_key.ptn_id = bq_entry_get_ptn_id(q);
		bsi_key.ts = bq_entry_get_sec(q);
		bsi_key.comp_id = bq_entry_get_comp_id(q) - 1;
		rc = brange_u32_iter_bwd_seek(q->hst_rng_itr, &bsi_key.comp_id);
		if (rc) {
			/* use next timestamp */
			bsi_key.ts--;
			brange_u32_iter_end(q->hst_rng_itr, &bsi_key.comp_id);
		}
		break;
	case BQ_CHECK_COND_TS0:
	case BQ_CHECK_COND_PTN:
prev_ptn:
		/* End of current PTN, continue with next PTN */
		bsi_key.ptn_id = bq_entry_get_ptn_id(q) - 1;
		if (q->ptn_rng_itr) {
			rc = brange_u32_iter_bwd_seek(q->ptn_rng_itr, &bsi_key.ptn_id);
			if (rc == ENOENT)
				goto out;
			assert(rc == 0);
		}
		brange_u32_iter_end(q->hst_rng_itr, &bsi_key.comp_id);
		bsi_key.ts = q->ts_1;
		break;
	}

	bsos_img_key_htobe(&bsi_key);
	sos_key_set(key, &bsi_key, sizeof(bsi_key));
	rc = __sos_iter_inf_last(q->itr, key); /* this point to the last dup */
	if (!rc)
		goto again;

out:
	return rc;
}

int bq_img_last_entry(struct bquery *q)
{
	struct bimgquery *imgq = (void*)q;
	int rc;
	uint32_t sec, comp_id, ptn_id;
	SOS_KEY(key);
	struct bsos_img_key bsi_key;

	/* setup key */
	bsi_key.comp_id = 0xFFFFFFFF;
	bsi_key.ts = 0xFFFFFFFF;
	bsi_key.ptn_id = 0xFFFFFFFF;

	if (q->ptn_rng_itr) {
		brange_u32_iter_end(q->ptn_rng_itr, &bsi_key.ptn_id);
	}

	if (q->hst_rng_itr) {
		brange_u32_iter_end(q->hst_rng_itr, &bsi_key.comp_id);
	}

	if (q->ts_1) {
		bsi_key.ts = q->ts_1;
	}

	bsos_img_key_htobe(&bsi_key);
	sos_key_set(key, &bsi_key, sizeof(bsi_key));
	/* seek */
	rc = __sos_iter_inf_last(q->itr, key);
	if (rc)
		goto out;
	__img_obj_update(imgq);
	if (bq_check_cond(q) != BQ_CHECK_COND_OK)
		rc = bq_prev_entry(q);

out:
	return rc;
}

int bq_img_get_pos(struct bquery *q, struct bquery_pos *pos)
{
	return sos_iter_pos(q->itr, &pos->pos);
}

int bq_img_set_pos(struct bquery *q, struct bquery_pos *pos)
{
	int rc = sos_iter_set(q->itr, &pos->pos);
	if (rc)
		return rc;
	__img_obj_update((void*)q);
	return 0;
}

int bq_msg_tc_next_entry(struct bquery *q)
{
	SOS_KEY(key);
	struct bsos_msg_key_tc k = {0};
	int rc;

	rc = sos_iter_next(q->itr);
	if (rc)
		goto out;
again:
	__msg_obj_update((struct bmsgquery *)q);
	rc = bq_check_cond(q);
	switch (rc) {
	case BQ_CHECK_COND_OK:
		goto out;
	case BQ_CHECK_COND_TS0:
		k.sec = q->ts_0;
		k.comp_id = 0;
		break;
	case BQ_CHECK_COND_HST:
		k.sec = bq_entry_get_sec(q);
		k.comp_id = bq_entry_get_comp_id(q) + 1;
		if (!k.comp_id) {
			/* overflow */
			rc = ENOENT;
		} else {
			rc = brange_u32_iter_fwd_seek(q->hst_rng_itr, &k.comp_id);
		}
		switch (rc) {
		case ENOENT:
			/* use next timestamp */
			k.sec++;
			/* let through */
		case EINVAL:
			brange_u32_iter_begin(q->hst_rng_itr, &k.comp_id);
			break;
		}
		break;
	case BQ_CHECK_COND_TS1:
		/* Out of TS range, no need to continue */
		rc = ENOENT;
		goto out;
	case BQ_CHECK_COND_PTN:
		assert(0 && "Unexpected BQ_CHECK_COND_PTN");
	}

	sos_key_set(key, &k, sizeof(k));
	rc = sos_iter_sup(q->itr, key); /* this will already be the first dup */
	if (!rc)
		goto again;
out:
	return rc;
}

int bq_msg_ptc_reverse_dir(struct bmsgquery *msgq)
{
	int rc = 0;
	int i;
	struct bq_msg_ptc_hent *hent;
	struct bq_msg_ptc_hent *hent_next;
	int (*hent_init)(struct bq_msg_ptc_hent *hent);
	int (*hent_step)(struct bq_msg_ptc_hent *step);

	switch (msgq->last_dir) {
	case BMSGQDIR_FWD:
		msgq->last_dir = BMSGQDIR_BWD;
		hent_init = bq_msg_ptc_hent_last;
		hent_step = bq_msg_ptc_hent_prev;
		msgq->bheap->cmp = (void*)bq_msg_ptc_hent_cmp_dec;
		break;
	case BMSGQDIR_BWD:
		msgq->last_dir = BMSGQDIR_FWD;
		hent_init = bq_msg_ptc_hent_first;
		hent_step = bq_msg_ptc_hent_next;
		msgq->bheap->cmp = (void*)bq_msg_ptc_hent_cmp_inc;
		break;
	}

	/* re-initialize entries in the used-up list first */
	LIST_FOREACH(hent, &msgq->hent_list, link) {
		rc = hent_init(hent);
		if (rc)
			goto out;
	}

	/* step the ones in the heap */
	for (i = 0; i < msgq->bheap->len; i++) {
		hent = msgq->bheap->array[i];
		rc = hent_step(hent);
		/* It's OK if step failed.
		 * It just means that hent is depleted. */
		LIST_INSERT_HEAD(&msgq->hent_list, hent, link);
	}

	/* re-heap */
	msgq->bheap->len = 0;
	hent = LIST_FIRST(&msgq->hent_list);
	while (hent) {
		hent_next = LIST_NEXT(hent, link);
		if (!hent->msg) /* process only valid (non-depleted) hent */
			goto next;
		rc = bheap_insert(msgq->bheap, hent);
		if (rc)
			goto out;
		LIST_REMOVE(hent, link);
	next:
		hent = hent_next;
	}
out:
	__msg_ptc_obj_update(msgq);
	return rc;
}

int bq_msg_ptc_next_entry(struct bquery *q)
{
	int rc = 0;
	struct bmsgquery *msgq = (void*)q;
	struct bq_msg_ptc_hent *hent;

	if (msgq->last_dir != BMSGQDIR_FWD) {
		rc = bq_msg_ptc_reverse_dir(msgq);
		goto out;
	}

	hent = bheap_get_top(msgq->bheap);
	if (!hent) {
		rc = ENOENT;
		goto out;
	}

	msgq->last_dir = BMSGQDIR_FWD;
	rc = bq_msg_ptc_hent_next(hent);
	switch (rc) {
	case 0:
		bheap_percolate_top(msgq->bheap);
		break;
	case ENOENT:
		bheap_remove_top(msgq->bheap);
		LIST_INSERT_HEAD(&msgq->hent_list, hent, link);
		hent = bheap_get_top(msgq->bheap);
		if (hent) {
			rc = 0;
		} else {
			rc = ENOENT;
		}
		break;
	default:
		/* this is bad */
		bwarn("Unexpexted rc from bq_msg_ptc_hent_next(): %d", rc);
		goto out;
	}
	__msg_ptc_obj_update(msgq);
out:
	return rc;
}

int bq_msg_tc_prev_entry(struct bquery *q)
{
	SOS_KEY(key);
	struct bmsgquery *msgq = (void*)q;
	struct bsos_msg_key_tc k = {0};
	int rc;

	rc = sos_iter_prev(q->itr);
	if (rc)
		goto out;
again:
	__msg_obj_update(msgq);
	rc = bq_check_cond(q);
	switch (rc) {
	case BQ_CHECK_COND_OK:
		goto out;
	case BQ_CHECK_COND_TS1:
		k.sec = q->ts_1;
		k.comp_id = 0xFFFFFFFF; /* max uint32_t */
		break;
	case BQ_CHECK_COND_HST:
		k.sec = bq_entry_get_sec(q);
		k.comp_id = bq_entry_get_comp_id(q) - 1;
		if (k.comp_id == 0xFFFFFFFF) {
			/* underflow */
			rc = ENOENT;
		} else {
			rc = brange_u32_iter_bwd_seek(q->hst_rng_itr, &k.comp_id);
		}
		switch (rc) {
		case ENOENT:
			/* use next timestamp */
			k.sec--;
			/* let trough */
		case EINVAL:
			brange_u32_iter_end(q->hst_rng_itr, &k.comp_id);
			break;
		}
		break;
	case BQ_CHECK_COND_TS0:
		/* Out of TS range, no need to continue */
		rc = ENOENT;
		goto out;
	case BQ_CHECK_COND_PTN:
		assert(0 && "Unexpected BQ_CHECK_COND_PTN");
	}

	sos_key_set(key, &k, sizeof(k));
	rc = __sos_iter_inf_last(q->itr, key); /* this is the last dup */
	if (!rc)
		goto again;
out:
	return rc;
}

int bq_msg_ptc_prev_entry(struct bquery *q)
{
	int rc = 0;
	struct bmsgquery *msgq = (void*)q;
	struct bq_msg_ptc_hent *hent;

	if (msgq->last_dir != BMSGQDIR_BWD) {
		rc = bq_msg_ptc_reverse_dir(msgq);
		goto out;
	}

	hent = bheap_get_top(msgq->bheap);
	if (!hent) {
		rc = ENOENT;
		goto out;
	}

	rc = bq_msg_ptc_hent_prev(hent);
	switch (rc) {
	case 0:
		bheap_percolate_top(msgq->bheap);
		break;
	case ENOENT:
		bheap_remove_top(msgq->bheap);
		LIST_INSERT_HEAD(&msgq->hent_list, hent, link);
		hent = bheap_get_top(msgq->bheap);
		if (hent) {
			rc = 0;
		} else {
			rc = ENOENT;
		}
		break;
	default:
		/* this is bad */
		bwarn("Unexpexted rc from bq_msg_ptc_hent_next(): %d", rc);
		goto out;
	}
	__msg_ptc_obj_update(msgq);
out:
	return rc;
}

int bq_msg_tc_first_entry(struct bquery *q)
{
	int rc;
	uint32_t sec, comp_id, ptn_id;
	SOS_KEY(key);
	struct bsos_msg_key_tc k = {0};

	assert(!q->ptn_rng_itr);

	if (q->hst_rng_itr) {
		brange_u32_iter_begin(q->hst_rng_itr, &k.comp_id);
	}

	if (q->ts_0) {
		k.sec = q->ts_0;
	}

	sos_key_set(key, &k, sizeof(k));
	rc = sos_iter_sup(q->itr, key);
	if (rc)
		goto out;
	__msg_obj_update((struct bmsgquery *)q);
	rc = bq_check_cond(q);
	if (rc)
		rc = bq_next_entry(q);

out:
	return rc;
}

void bq_msg_ptc_reset_heap(struct bquery *q)
{
	struct bmsgquery *msgq = (void*)q;
	struct bq_msg_ptc_hent *hent;
	int i;
	for (i = 0; i < msgq->bheap->len; i++) {
		hent = msgq->bheap->array[i];
		LIST_INSERT_HEAD(&msgq->hent_list, hent, link);
	}
	msgq->bheap->len = 0;
}

int bq_msg_ptc_first_entry(struct bquery *q)
{
	struct bmsgquery *msgq = (void*)q;
	struct bq_msg_ptc_hent *hent;
	int i;
	int rc = 0;

	bq_msg_ptc_reset_heap(q);

	/* forward iterator */
	msgq->last_dir = BMSGQDIR_FWD;
	msgq->bheap->cmp = (void*)bq_msg_ptc_hent_cmp_inc;

	while ((hent = LIST_FIRST(&msgq->hent_list))) {
		LIST_REMOVE(hent, link);
		rc = bq_msg_ptc_hent_first(hent);
		switch (rc) {
		case ENOENT:
			continue;
		case 0:
			break;
		default:
			goto out;
		}

		rc = bheap_insert(msgq->bheap, hent);
		if (rc)
			goto out;
	}
	__msg_ptc_obj_update(msgq);
out:
	return rc;
}

int bq_msg_tc_last_entry(struct bquery *q)
{
	int rc;
	uint32_t sec, comp_id, ptn_id;
	SOS_KEY(key);
	struct bsos_msg_key_tc k = {.sec = -1, .comp_id = -1};

	assert(!q->ptn_rng_itr);

	if (q->hst_rng_itr) {
		brange_u32_iter_end(q->hst_rng_itr, &k.comp_id);
	}

	if (q->ts_1) {
		k.sec = q->ts_1;
	}

	sos_key_set(key, &k, sizeof(k));
	rc = __sos_iter_inf_last(q->itr, key);
	if (rc)
		goto out;
	__msg_obj_update((struct bmsgquery *)q);
	rc = bq_check_cond(q);
	if (rc)
		rc = bq_prev_entry(q);

out:
	return rc;
}

int bq_msg_tc_get_pos(struct bquery *q, struct bquery_pos *pos)
{
	int rc;
	struct bmsgquery *msgq = (void*)q;
	pos->ptn_id = __bq_msg_entry_get_ptn_id(q);
	pos->dir = msgq->last_dir;
	rc = sos_iter_pos(q->itr, &pos->pos);
	return rc;
}

int bq_msg_tc_set_pos(struct bquery *q, struct bquery_pos *pos)
{
	struct bmsgquery *msgq = (void*)q;
	uint32_t ptn_id;
	uint32_t comp_id;
	int rc;

	rc = sos_iter_set(q->itr, &pos->pos);
	if (rc)
		return rc;
	__msg_obj_update(msgq);
	comp_id = __bq_msg_entry_get_comp_id(q);
	ptn_id = __bq_msg_entry_get_ptn_id(q);

	assert(ptn_id == pos->ptn_id);

	rc = bq_check_cond(q);
	if (rc)
		/* invalid position */
		return EINVAL;

	if (q->hst_rng_itr) {
		rc = brange_u32_iter_set_pos(q->hst_rng_itr, comp_id);
		if (rc)
			return rc;
	}

	if (q->ptn_rng_itr) {
		rc = brange_u32_iter_set_pos(q->ptn_rng_itr, ptn_id);
		if (rc)
			return rc;
	}

	return 0;
}

int bq_msg_ptc_last_entry(struct bquery *q)
{
	struct bmsgquery *msgq = (void*)q;
	struct bq_msg_ptc_hent *hent;
	int i;
	int rc = 0;

	bq_msg_ptc_reset_heap(q);

	/* backward iterator */
	msgq->last_dir = BMSGQDIR_BWD;
	msgq->bheap->cmp = (void*)bq_msg_ptc_hent_cmp_dec;

	while ((hent = LIST_FIRST(&msgq->hent_list))) {
		LIST_REMOVE(hent, link);
		rc = bq_msg_ptc_hent_last(hent);
		switch (rc) {
		case ENOENT:
			continue;
		case 0:
			break;
		default:
			goto out;
		}

		rc = bheap_insert(msgq->bheap, hent);
		if (rc)
			goto out;
	}
	__msg_ptc_obj_update(msgq);
out:
	return rc;
}

int bq_msg_ptc_get_pos(struct bquery *q, struct bquery_pos *pos)
{
	int rc;
	struct bmsgquery *msgq = (void*)q;
	struct bq_msg_ptc_hent *hent = bheap_get_top(msgq->bheap);
	if (!hent)
		return EINVAL; /* the iterator is in an invalid state */
	pos->ptn_id = __bq_msg_entry_get_ptn_id(q);
	pos->dir = msgq->last_dir;
	rc = sos_iter_pos(hent->iter, &pos->pos);
	return rc;
}

int bq_msg_ptc_hent_seek(struct bq_msg_ptc_hent *hent,
			 struct bsos_msg_key_ptc *k)
{
	int rc;
	int (*seek_fn)(sos_iter_t i, sos_key_t key);
	int (*range_next)(struct brange_u32_iter *itr, uint32_t *v);
	int (*range_begin)(struct brange_u32_iter *itr, uint32_t *v);
	int (*hent_cmp)(struct bq_msg_ptc_hent *a, struct bq_msg_ptc_hent *b);
	int (*hent_next)(struct bq_msg_ptc_hent *hent);
	SOS_KEY(sos_key);
	uint32_t comp_id;
	uint32_t next_sec;
	int over_under_flow = 0;

	struct bsos_msg_key_ptc curr_key, next_key;
	struct bsos_msg_key_ptc *tmp_key_p;

	curr_key = *k;
	curr_key.ptn_id = hent->ptn_id;
	next_key = curr_key;
	if (hent->hst_rng_itr) {
		rc = brange_u32_iter_set_pos(hent->hst_rng_itr, k->comp_id);
		if (rc)
			return EINVAL;
	}
	switch (hent->msgq->last_dir) {
	case BMSGQDIR_FWD:
		seek_fn = sos_iter_sup;
		next_key.comp_id++;
		if (next_key.comp_id == 0)
			over_under_flow = 1;
		next_sec = curr_key.sec + 1;
		range_next = brange_u32_iter_fwd_seek;
		range_begin = brange_u32_iter_begin;
		hent_cmp = bq_msg_ptc_hent_cmp_inc;
		hent_next = bq_msg_ptc_hent_next;
		break;
	case BMSGQDIR_BWD:
		seek_fn = __sos_iter_inf_last;
		next_key.comp_id--;
		if (next_key.comp_id == 0xFFFFFFFF)
			over_under_flow = 1;
		next_sec = curr_key.sec - 1;
		range_next = brange_u32_iter_bwd_seek;
		range_begin = brange_u32_iter_end;
		hent_cmp = bq_msg_ptc_hent_cmp_dec;
		hent_next = bq_msg_ptc_hent_prev;
		break;
	}

	if (hent->hst_rng_itr) {
		rc = range_next(hent->hst_rng_itr, &next_key.comp_id);
		if (rc) {
			next_key.sec = next_sec;
			range_begin(hent->hst_rng_itr,
						&next_key.comp_id);
		}
	} else {
		if (over_under_flow)
			next_key.sec = next_sec;
	}

	switch (hent->msgq->last_dir) {
	case BMSGQDIR_FWD:
		if (k->ptn_id < hent->ptn_id) {
			tmp_key_p = &curr_key;
		} else {
			tmp_key_p = &next_key;
		}
		break;
	case BMSGQDIR_BWD:
		if (k->ptn_id > hent->ptn_id) {
			tmp_key_p = &curr_key;
		} else {
			tmp_key_p = &next_key;
		}
		break;
	}

	/* do the seek (sup/inf) */
	bsos_msg_key_ptc_htobe(tmp_key_p);
	sos_key_set(sos_key, tmp_key_p, sizeof(*tmp_key_p));
	seek_fn(hent->iter, sos_key);
	bq_msg_ptc_hent_obj_update(hent);

	if (!hent->obj) {
		/* no object */
		return ENOENT;
	}

	rc = bq_msg_ptc_hent_check_cond(hent);
	switch (rc) {
	case BQ_CHECK_COND_OK:
		comp_id = bsos_msg_get_comp_id(hent->msg);
		if (hent->hst_rng_itr)
			brange_u32_iter_set_pos(hent->hst_rng_itr, comp_id);
		return 0;
	case BQ_CHECK_COND_HST:
		if (hent->hst_rng_itr)
			range_begin(hent->hst_rng_itr, &comp_id);
		return hent_next(hent);
	case BQ_CHECK_COND_TS0:
	case BQ_CHECK_COND_PTN:
	case BQ_CHECK_COND_TS1:
		return ENOENT;
	}

	assert(0);
	return 0;
}

int bq_msg_ptc_set_pos(struct bquery *q, struct bquery_pos *pos)
{
	struct bmsgquery *msgq = (void*)q;
	struct bq_msg_ptc_hent *hent, *tmp_hent;
	uint32_t ptn_id;
	uint32_t comp_id;
	uint32_t t;
	struct bsos_msg_key_ptc k = {0};
	int rc, i;

	bq_msg_ptc_reset_heap(q);
	msgq->last_dir = pos->dir;
	switch (msgq->last_dir) {
	case BMSGQDIR_FWD:
		msgq->bheap->cmp = (void*)bq_msg_ptc_hent_cmp_inc;
		break;
	case BMSGQDIR_BWD:
		msgq->bheap->cmp = (void*)bq_msg_ptc_hent_cmp_dec;
		break;
	default:
		assert(0 == "Invalid iterator direction.");
		break;
	}

	/* first: find and position the iterator matching the pos->ptn_id */
	LIST_FOREACH(hent, &msgq->hent_list, link) {
		if (hent->ptn_id != pos->ptn_id)
			continue;
		rc = sos_iter_set(hent->iter, &pos->pos);
		if (rc)
			return rc;
		bq_msg_ptc_hent_obj_update(hent);
		k.ptn_id = hent->ptn_id;
		k.sec = bsos_msg_get_sec(hent->msg);
		k.comp_id = bsos_msg_get_comp_id(hent->msg);
		if (hent->hst_rng_itr) {
			rc = brange_u32_iter_set_pos(hent->hst_rng_itr,
							k.comp_id);
			if (rc)
				return rc;
		}
		rc = bheap_insert(msgq->bheap, hent);
		if (rc)
			return rc;
		LIST_REMOVE(hent, link);
		break;
	}

	if (!hent) {
		/* invalid position, no iterator matching ptn_id. */
		return EINVAL;
	}

	/* position the rest of the iterators */
	hent = LIST_FIRST(&msgq->hent_list);
	while (hent) {
		rc = bq_msg_ptc_hent_seek(hent, &k);
		if (rc == ENOENT) {
			/* No need to put this into the heap */
			hent = LIST_NEXT(hent, link);
			continue;
		}
		assert(rc == 0);

		tmp_hent = LIST_NEXT(hent, link);

		rc = bheap_insert(msgq->bheap, hent);
		if (rc)
			return rc;
		LIST_REMOVE(hent, link);
		hent = tmp_hent;
	}

	__msg_ptc_obj_update(msgq);

	return 0;
}

uint32_t bq_entry_get_sec(struct bquery *q)
{
	return q->get_sec(q);
}

uint32_t bq_entry_get_usec(struct bquery *q)
{
	return q->get_usec(q);
}

uint32_t bq_entry_get_comp_id(struct bquery *q)
{
	return q->get_comp_id(q);
}

uint32_t bq_entry_get_ptn_id(struct bquery *q)
{
	return q->get_ptn_id(q);
}

struct bmsg *bq_entry_get_msg(struct bquery *q)
{
	struct bmsg *bmsg;
	size_t bmsg_sz;
	sos_array_t msg = ((struct bmsgquery *)q)->msg;

	bmsg_sz = sizeof(*bmsg) + ((msg->count - BSOS_MSG_ARGV_0) << 3);
	bmsg = malloc(bmsg_sz);

	bmsg->ptn_id = bq_entry_get_ptn_id(q);
	bmsg->argc = msg->count - BSOS_MSG_ARGV_0;
	memcpy(bmsg->argv, &msg->data.uint32_[BSOS_MSG_ARGV_0], bmsg->argc << 3);

	return bmsg;
}

bq_msg_ref_t bq_entry_get_ref(struct bquery *q)
{
	bq_msg_ref_t bq_ref;
	sos_obj_ref_t ref = sos_obj_ref(q->obj);
	bq_ref.ref[0] = ref.ref.ods;
	bq_ref.ref[1] = ref.ref.obj;
	return bq_ref;
}

uint32_t bq_img_entry_get_count(struct bimgquery *q)
{
	return q->img->data.uint32_[BSOS_IMG_COUNT];
}

int bq_img_entry_get_pixel(struct bimgquery *q, struct bpixel *p)
{
	p->ptn_id = bq_entry_get_ptn_id(&q->base);
	p->count = bq_img_entry_get_count(q);
	p->sec = bq_entry_get_sec(&q->base);
	p->comp_id = bq_entry_get_comp_id(&q->base);
	return 0;
}

char *bq_entry_print(struct bquery *q, struct bdstr *bdstr)
{
	int detach = 0;
	int rc = 0;
	char *ret = NULL;
	const struct bstr *bstr;

	if (!bdstr) {
		bdstr = bdstr_new(256);
		if (!bdstr)
			return NULL;
		detach = 1;
	}

	rc = fmt_msg_prefix(q->formatter, bdstr);
	if (rc)
		goto out;
	struct timeval tv = {
		.tv_sec = bq_entry_get_sec(q),
		.tv_usec = bq_entry_get_usec(q)
	};
	rc = fmt_date_fmt(q->formatter, bdstr, &tv);
	if (rc)
		goto out;
	bstr = btkn_store_get_bstr(q->store->cmp_store,
				bcompid2mapid(bq_entry_get_comp_id(q)));
	if (!bstr)  {
		rc = ENOENT;
		goto out;
	}
	rc = fmt_host_fmt(q->formatter, bdstr, bstr);
	if (rc)
		goto out;
	struct bmsg *bmsg = bq_entry_get_msg(q);
	rc = bq_print_msg(q, bdstr, bmsg);
	free(bmsg);
	if (rc)
		goto out;
	fmt_msg_suffix(q->formatter, bdstr);

	ret = bdstr->str;
	if (detach) {
		bdstr_detach_buffer(bdstr);
		bdstr_free(bdstr);
	}
out:
	if (rc)
		errno = rc;
	return ret;
}

#define STR_SZ 65536
int expand_buff(size_t *bufsz, char **buf, char **str, size_t *str_len)
{
	*bufsz += STR_SZ;
	char *newbuf = realloc(*buf, *bufsz);
	if (!newbuf)
		goto err;
	*str = newbuf + (*str - *buf);
	*str_len = *bufsz - (*str - newbuf);
	*buf = newbuf;
	return 0;
err:
	return ENOMEM;
}

char* bq_get_all_ptns(struct bq_store *s)
{
	uint32_t id = bptn_store_first_id(s->ptn_store);
	uint32_t last_id = bptn_store_last_id(s->ptn_store);
	int rc = 0;
	char *buf = malloc(STR_SZ);
	char *str = buf;
	char *newbuf;
	size_t bufsz = STR_SZ; /* Tracking the total allocated size */
	size_t str_len = STR_SZ; /* The left over bytes to write */
	int len;
	int wlen;
loop:
	if (id > last_id)
		goto out;
	len = snprintf(str, str_len, "%d\t", id);
	if (len >= str_len) {
		rc = expand_buff(&bufsz, &buf, &str, &str_len);
		if (rc)
			goto err;
		goto loop;
	}
	str_len -= len;
	str += len;
middle_loop:
	rc = bptn_store_id2str(s->ptn_store, s->tkn_store, id, str, str_len);
	switch (rc) {
		case 0:
			len = strlen(str);
			if (len + 2 <= str_len)
				break;
			/* else, treat as ENOMEM */
		case ENOMEM:
			/* Expand buf */
			rc = expand_buff(&bufsz, &buf, &str, &str_len);
			if (rc)
				goto err;
			goto loop;
			break;
		default:
			goto err;
	}
	/* This is valid because of case 0 */
	str[len] = '\n';
	str[len+1] = '\0';
	str_len -= len+1;
	str += len+1;
	id++;
	goto loop;
out:
	*(str - 1) = '\0';
	return buf;

err:
	free(buf);
	return NULL;
}

int bq_get_all_ptns_r(struct bq_store *s, char *buf, size_t buflen)
{
	uint32_t id = bptn_store_first_id(s->ptn_store);
	uint32_t last_id = bptn_store_last_id(s->ptn_store);
	int rc = 0;
	while (id <= last_id && buflen > 1) {
		rc = bptn_store_id2str(s->ptn_store, s->tkn_store, id, buf,
									buflen);
		if (rc)
			return rc;
		int len = strlen(buf);
		buflen -= len;
		buf += len;
		if (buflen > 1) {
			buflen--;
			*buf++ = '\n';
		}
		id++;
	}
	buf[buflen-1] = '\0';
	return rc;
}

int bq_is_metric_pattern(struct bq_store *store, int ptn_id)
{
	const struct bstr *ptn = bptn_store_get_ptn(store->ptn_store, ptn_id);
	if (!ptn)
		return 0;
	const struct bstr *lead = btkn_store_get_bstr(store->tkn_store, ptn->u32str[0]);
	if (!lead)
		return 0;
	if (0 == bstr_cmp(lead, BMETRIC_LEAD_TKN_BSTR)) {
		return 1;
	}
	return 0;
}

char* bq_get_ptn_tkns(struct bq_store *store, int ptn_id, int arg_idx)
{
	struct btkn_store *tkn_store = store->tkn_store;
	struct bptn_store *ptn_store = store->ptn_store;
	uint64_t attr_off = ptn_store->attr_idx->bvec->data[ptn_id];
	struct bptn_attrM *attrM = BMPTR(ptn_store->mattr, attr_off);
	if (!attrM || arg_idx > attrM->argc) {
		errno = EINVAL;
		return NULL;
	}
	uint64_t arg_off = attrM->arg_off[arg_idx];
	struct bmlnode_u32 *node;
	struct bdstr *bdstr = bdstr_new(65536);
	char buf[4096+2]; /* 4096 should be more than enough for a token, and
			   * the +2 is for \n and \0 */
	int rc = 0;
	int len;
	BMLIST_FOREACH(node, arg_off, link, ptn_store->marg) {
		/* node->data is token ID */
		rc = btkn_store_id2str(tkn_store, node->data, buf, 4096);
		if (rc)
			goto err;
		len = strlen(buf);
		buf[len] = '\n';
		buf[len+1] = '\0';
		rc = bdstr_append(bdstr, buf);
		if (rc)
			goto err;
	}
	/* Keep only the string in bdstr, and throw away the wrapper */
	char *str = bdstr_detach_buffer(bdstr);
	bdstr_free(bdstr);
	return str;
err:
	free(bdstr->str);
	free(bdstr);
	errno = rc;
	return NULL;
}

int bq_get_comp_id(struct bq_store *store, const char *hostname)
{
	char buff[128];
	struct bstr *str = (void*)buff;
	bstr_set_cstr(str, (char*)hostname, 0);
	uint32_t id = bmap_get_id(store->cmp_store->map, str);
	if (id < BMAP_ID_BEGIN)
		return -1;
	return bmapid2compid(id);
}

struct bsos_wrap* bsos_wrap_find(struct bsos_wrap_head *head,
				 const char *store_name)
{
	struct bsos_wrap *bsw;
	LIST_FOREACH(bsw, head, link) {
		if (strcmp(bsw->store_name, store_name) == 0)
			return bsw;
	}
	return NULL;
}

struct btkn_store *bq_get_cmp_store(struct bq_store *store)
{
	return store->cmp_store;
}

struct btkn_store *bq_get_tkn_store(struct bq_store *store)
{
	return store->tkn_store;
}

struct bptn_store *bq_get_ptn_store(struct bq_store *store)
{
	return store->ptn_store;
}

int bq_get_cmp(struct bq_store *store, int cmp_id, struct bdstr *out)
{
	int rc = 0;
	const struct bstr *cmp = btkn_store_get_bstr(store->cmp_store,
							bcompid2mapid(cmp_id));
	if (!cmp) {
		rc = ENOENT;
		goto out;
	}
	bdstr_reset(out);
	rc = bdstr_append_printf(out, "%.*s", cmp->blen, cmp->cstr);
out:
	return rc;
}

int bq_print_ptn(struct bq_store *store, struct bq_formatter *formatter, int ptn_id, struct bdstr *out)
{
	int rc = 0;
	uint32_t i;
	uint32_t n;
	struct bptn_store *ptn_store = store->ptn_store;
	struct btkn_store *tkn_store = store->tkn_store;
	const struct bstr *ptn = bptn_store_get_ptn(ptn_store, ptn_id);
	const struct bstr *tkn;
	struct btkn_attr attr;
	if (!ptn)
		return ENOENT;
	if (!formatter)
		formatter = &default_formatter;
	rc = bdstr_reset(out);
	if (rc)
		return rc;
	rc = fmt_ptn_prefix(formatter, out, ptn_id);
	if (rc)
		return rc;
	rc = fmt_tkn_begin(formatter, out);
	if (rc)
		return rc;
	n = ptn->blen / sizeof(*ptn->u32str);
	for (i = 0; i < n; i++) {
		attr = btkn_store_get_attr(tkn_store, ptn->u32str[i]);
		tkn = btkn_store_get_bstr(tkn_store, ptn->u32str[i]);
		assert(tkn);
		rc = fmt_tkn_fmt(formatter, out, tkn, &attr, ptn->u32str[i]);
		if (rc)
			return rc;
	}
	rc = fmt_tkn_end(formatter, out);
	if (rc)
		return rc;
	rc = fmt_ptn_suffix(formatter, out);
	if (rc)
		return rc;
	return rc;
}

int bq_store_refresh(struct bq_store *store)
{
	int rc;
	rc = bptn_store_refresh(store->ptn_store);
	if (rc)
		return rc;
	rc = btkn_store_refresh(store->tkn_store);
	if (rc)
		return rc;
	rc = btkn_store_refresh(store->cmp_store);
	if (rc)
		return rc;
	return 0;
}

int bq_imgstore_iterate(struct bq_store *store, void (*cb)(const char *imgstore_name, void *ctxt), void *ctxt)
{
	int rc = 0;
	int len = 0;
	int sz;
	int i;
	wordexp_t wexp = {0};
	len = strlen(store->path);
	sz = sizeof(store->path) - len;
	snprintf(store->path + len, sz, "/img_store/*");
	rc = wordexp(store->path, &wexp, 0);
	if (rc) {
		berr("wordexp() error, rc: %d (%s:%d)", rc, __FILE__, __LINE__);
		goto out;
	}

	for (i = 0; i < wexp.we_wordc; i++) {
		const char *name = strrchr(wexp.we_wordv[i], '/') + 1;
		sscanf(name, "%*d-%*d%n", &sz);
		if (sz != strlen(name)) {
			berr("Unrecognized img store name '%s'\n", name);
			continue;
		}

		cb(name, ctxt);
	}

out:
	/* recover path */
	if (len)
		store->path[len] = 0;
	/* destroy wordexp */
	if (wexp.we_wordv)
		wordfree(&wexp);
	return rc;
}

/**
 * \page bquery Baler query command-line interface
 *
 * \section synopsis SYNOPSIS
 *
 * \b bquery \b -t PTN \b -s STORE [\b -v]
 *
 * \b bquery \b -t HOST \b -s STORE
 *
 * \b bquery \b -t MSG \b -s STORE [\b -B TS ] [\b -E TS]
 *          [\b -H NUM_LIST] [\b -P NUM_LIST] [\b -v] [\b -R]
 *
 * \b bquery \b -t LIST_IMG \b -s STORE
 *
 * \b bquery \b -t IMG \b -s STORE \b -I IMG_STORE [\b -B TS ] [\b -E TS]
 *          [\b -H NUM_LIST] [\b -P NUM_LIST] [\b -R]
 *
 * \b bquery \b -t PTN_STAT \b -s STORE [\b -B TS ] [\b -E TS]
 *          [\b -H NUM_LIST] [\b -P NUM_LIST]
 *
 * \b bquery \b -t MPTN \b -s STORE [\b -v]
 *
 * \section description DESCRIPTION
 *
 * \b bquery is a command-line interface to query data from balerd store. It
 * supports the following query types:
 *
 * \subsection bquery_ptn PTN - Pattern listing
 *
 * With option <b>-t PTN</b>, bquery will list all patterns in the store. With
 * additional <b>-v</b> option, the occurence count, first-seen and last seen
 * statistics of the patterns will also be printed.
 *
 * \subsection bquery_host HOST - Host listing
 *
 * With option <b>-t HOST</b>, bquery will list all hosts.
 *
 * \subsection bquery_msg MSG - Message querying
 *
 * With option <b>-t MSG</b>, bquery will list all messages matching the given
 * query criteria. The criteria include begin timestamp (\b -B), end timestamp
 * (\b -E), list of host IDs (\b -H), and list of pattern IDs (\b -P). Should
 * any criterion is omitted, it will not be used in the criteria test. For
 * example, if host IDs criterion is not given, bquery will list all messages
 * from all hosts that match the other specified criteria. If no criterion is
 * specified, bquery will list all messages in the store.
 *
 * The format of the <b>timestamp</b> can either be Unix timestamp (the number
 * of seconds since Epoch) or "yyyy-mm-dd HH:MM:SS".
 *
 * The format for both the list of <b>pattern IDs</b> and the list of
 * <b>host IDs</b> is comma-separated ranges and single numbers. For example
 * "1,3-9,20-30,100".
 *
 * bquery will query messages in the reverse order if the option -R is given.
 *
 * \subsection bquery_list_img LIST_IMG - Image store listing
 *
 * With option <b>-t LIST_IMG</b>, bquery will list all available image stores.
 * Image store is needed in order to query image pixels (see \ref bquery_img).
 *
 * \subsection bquery_img IMG - Image querying
 *
 * With option <b>-t IMG</b>, bquery will list image pixels that matched the
 * query criteria. The criteria for image query is the same as the criteria for
 * message querying above. The image store option <b>-I IMG_STORE</b> is needed.
 * To list all available image store, please see section \ref bquery_list_img.
 *
 * \subsection bquery_ptn_stat PTN_STAT - Pattern statistics
 *
 * With option <b>-t PTN_STAT</b>, bquery will list pattern statistics by
 * comp_id. The statistics calculation can be limited by criteria of <b>-B, -E,
 * -H, -P</b> options. The output is in CSV format. Time stamp format (\b -F)
 * can be given and bquery will format the timestamp accordingly.
 *
 * \b PTN_STAT uses '3600-1' image store to quickly calculate the statistics
 * with a little sacrifice of timestamp granularity (to the hour level).
 *
 * \subsection bquery_mptn MPTN - Meta-pattern listing
 *
 * With option <b>-t MPTN</b>, bquery will list all meta-pattern classes in
 * the store. With the additional <b>-v</b> option, for each meta-pattern class,
 * the number of patterns belonging to the class, the first-seen time, the
 * last-seen time and the meta-pattern will be printed. Moreover, in each
 * meta-pattern class, the patterns in the class, their pattern IDs, and the
 * first-seen as well as the last-seen statistics of each pattern will be reported.
 *
 * \section options OPTIONS
 *
 * \par -t,--type QUERY_TYPE
 * Specify the type of the query. The QUERY_TYPE can be one of the following
 * (case INsensitive): PTN, HOST, MSG, LIST_IMG, and IMG.
 *
 * \par -I,--image-store-name IMG_STORE
 * Specify the image store name. IMG_STORE must be one of the image stores
 * listed by bquery with option <b>-t LIST_IMG</b>.
 *
 * \par -H,--host-mask NUM_LIST
 * Specify the list of hosts for the query. This option only applies to <b>-t
 * IMG</b> and <b>-t MSG</b>. When the host mask is specified, only the entries
 * matching the hosts in the list will be reported.
 * \par
 * <b>NUM_LIST</b> is the comma-separated list of numbers or ranges, e.g.
 * 1,3-5,7-10,12. No space is allowed in the list.
 *
 * \par -P,--ptn_id-mask NUM_LIST
 * Specify the list of pattern IDs for the query. This option only applies to
 * <b>-t IMG</b> and <b>-t MSG</b>. When the pattern ID mask is specified, only
 * the entries matching the pattern IDs in the list wil be reported.
 * \par
 * <b>NUM_LIST</b> is the comma-separated list of numbers or ranges, e.g.
 * 1,3-5,7-10,12. No space is allowed in the list.
 *
 * \par -B,--begin TS
 * Specify the begin time stamp for the query. This option only applies to
 * <b>-t IMG</b> and <b>-t MSG</b>. This option will filter out the entries
 * having the time stamp before the specified beginning timestamp.
 * \par
 * <b>TS</b> can be Unix timestamp (the number of seconds since the Epoch) or
 * "yyyy-mm-dd HH:MM:SS".
 *
 * \par -E,--end TS
 * Specify the end time stamp for the query. This option only applies to
 * <b>-t IMG</b> and <b>-t MSG</b>. This option will filter out the entries
 * having the time stamp after the specified beginning timestamp.
 * \par
 * <b>TS</b> can be Unix timestamp (the number of seconds since the Epoch) or
 * "yyyy-mm-dd HH:MM:SS".
 *
 * \par -F,--ts-format FORMAT
 * OUTPUT format of the timestamp field for <b>-t MSG</b> option. The default
 * format is to follow the new syslog timestamp format (see RFC5424 section
 * 6.2.3.1 example 4) to the micro-second.
 * \par
 * The <b>FORMAT</b> string must comply the format string in <b>strftime</b>(3).
 * One frequently used format is '%s' (the number of seconds since Epoch).
 *
 * \par -v,--verbose
 * The verbose flag, which only applies to only <b>-t MSG</b> and <b>-t PTN</b>.
 * \par
 * For <b>-t MSG</b>, the verbose flag will cause <b>bquery</b> to print pattern
 * ID in the front of each output message.
 * \par
 * For <b>-t PTN</b>, the verbose flag will cause <b>bquery</b> to print count,
 * first-seen, and last-seen statistics of each pattern.
 * \par
 * For <b>-t MPTN</b>, the verbose flag will cause <b>bquery</b> to print
 * count of patterns in each meta-pattern class, the patterns and the pattern IDs
 * in each class and the first-seen and the last-seen statistics.
 *
 * \par -V,--version
 * Print bquery version and exit.
 *
 *
 * \section examples EXAMPLES
 *
 * Get a list of hosts (or components):
 * \par
 * \code
 * bquery -s store/ -t HOST
 * \endcode
 *
 * Get a list of patterns:
 * \par
 * \code
 * # No statistics
 * bquery -s store/ -t PTN
 *
 * # With pattern statistics
 * bquery -s store/ -t PTN -v
 * \endcode
 *
 * Message query examples:
 * \par
 * \code
 * # All messages from host 10, 12, 13, 14, and 15
 * bquery -s store/ -t MSG -H 10,12-15
 *
 * # All messages from all hosts from a specific time window
 * bquery -s store/ -t MSG -B "2014-12-31 08:00:00" -E "2014-12-31 18:00:00"
 *
 * # Get all messages from some specific patterns
 * bquery -s store/ -t MSG -P 128,130-135
 *
 * # Mixed criteria
 * bquery -s store/ -t MSG -P 128,130-135 -H 10,12-15 \\
 *                  -B "2014-12-31 08:00:00" -E "2014-12-31 18:00:00"
 * \endcode
 *
 * List available image stores:
 * \par
 * \code
 * bquery -s store/ -t LIST_IMG
 * \endcode
 *
 * Image pixel query examples:
 * \par
 * \code
 * # All image pixels from image store "3600-1" matching host 10, and 12-15
 * bquery -s store/ -t IMG -I 3600-1 -H 10,12-15
 *
 * # All image pixels from image store "3600-1" matching a specific time window
 * bquery -s store/ -t IMG -I 3600-1 -B "2014-12-31 08:00:00" \\
 *                  -E "2014-12-31 18:00:00"
 *
 * # Get all image pixels from the image store 3600-1 matching some
 * # specific patterns
 * bquery -s store/ -t IMG -I 3600-1 -P 128,130-135
 *
 * # Mixed criteria (from the image store 3600-1)
 * bquery -s store/ -t IMG -I 3600-1 -P 128,130-135 -H 10,12-15 \\
 *                  -B "2014-12-31 08:00:00" -E "2014-12-31 18:00:00"
 * \endcode
 */

#ifdef BIN

enum BQ_TYPE {
	BQ_TYPE_UNKNOWN,
	BQ_TYPE_MSG,
	BQ_TYPE_PTN,
	BQ_TYPE_HOST,
	BQ_TYPE_IMG,
	BQ_TYPE_LIST_IMG,
	BQ_TYPE_PTN_STAT,
	BQ_TYPE_MPTN,
	BQ_TYPE_LAST
};

const char *BQ_TYPE_STR[] = {
	[BQ_TYPE_UNKNOWN]   =  "BQ_TYPE_UNKNOWN",
	[BQ_TYPE_MSG]       =  "MSG",
	[BQ_TYPE_PTN]       =  "PTN",
	[BQ_TYPE_HOST]      =  "HOST",
	[BQ_TYPE_IMG]       =  "IMG",
	[BQ_TYPE_LIST_IMG]  =  "LIST_IMG",
	[BQ_TYPE_PTN_STAT]  =  "PTN_STAT",
	[BQ_TYPE_MPTN]      =  "MPTN",
	[BQ_TYPE_LAST]      =  "BQ_TYPE_LAST"
};

const char* bq_type_str(enum BQ_TYPE type)
{
	if (type <= 0 || type >= BQ_TYPE_LAST)
		return BQ_TYPE_STR[BQ_TYPE_UNKNOWN];
	return BQ_TYPE_STR[type];
}

enum BQ_TYPE bq_type(const char *str)
{
	int i;
	for (i=0; i<BQ_TYPE_LAST; i++)
		if (strcasecmp(BQ_TYPE_STR[i], str)==0)
			return i;
	return BQ_TYPE_UNKNOWN;
}

enum BQ_TYPE query_type = BQ_TYPE_MSG;
char *store_path = NULL;
int daemon_flag = 0;
char *remote_host = NULL;
enum {
	XPRT_NONE = 0,
	XPRT_SOCKET,
	XPRT_RDMA
} xprt_type = XPRT_NONE;
uint32_t port = 0;

char *hst_ids = NULL;
char *ptn_ids = NULL;
char *ts_begin = NULL;
char *ts_end = NULL;

char *img_store_name = "3600-1";

struct btkn_store *tkn_store = NULL;
struct btkn_store *comp_store = NULL;
struct bptn_store *ptn_store = NULL;

enum {
	BQ_MODE_INVAL = -1, /* invalid mode */
	BQ_MODE_LOCAL,   /* locally query once */
	BQ_MODE_DAEMON,  /* daemon mode */
	BQ_MODE_REMOTE,  /* remotely query once */
} running_mode = BQ_MODE_LOCAL;

enum {
	SORT_PTN_BY_ID,
	SORT_PTN_BY_ENG,
	SORT_PTN_BY_N,
} sort_ptn_t;

const char *sort_ptn_by_str[] = {
	"ID",
	"ENG",
};

int verbose = 0;
int reverse = 0;
int escape = 0;
int sort_ptn_by = SORT_PTN_BY_ID;

const char *ts_format = NULL;

void show_help()
{
	printf(
"Usages\n"
#if 0
"    (single query): Read data from the baler store. Data are filtered by\n"
"	specified query options.\n"
"\n"
#endif
"	bquery --store-path <path> [QUERY_OPTIONS]\n"
"\n"
#if 0
"\n"
"    (daemon mode): Run bquery in daemon mode. Providing data from the store\n"
"	to other bquery over the network.\n"
"\n"
"	bquery --daemon --store-path <path> [XPRT_OPTIONS]\n"
"\n"
"\n"
"    (single query through daemon): Like single query, but get the data from\n"
"	the bquery daemon instead.\n"
"\n"
"	bquery --remote-host <host> [XPRT_OPTIONS] [QUERY_OPTIONS]\n"
"\n"
"\n"
"XPRT_OPTIONS:\n"
"    --xprt,-x (sock|rdma)	Network transport type.\n"
"    --port,-p NUMBER		Port number to listen to (daemon mode) or \n"
"				connect to (query through daemon).\n"
"\n"
#endif
"QUERY_OPTIONS:\n\
    --type,-t TYPE		The TYPE of the query.\n\
				* PTN will list all log patterns with their\n\
				  pattern_ids. These pattern_ids are to be \n\
				  used in ptn_id-mask option when querying\n\
				  for MSG.\n\
				* HOST will list all hostnames with their\n\
				  host_ids. These host_ids are to be used\n\
				  with host-mask option when querying for\n\
				  MSG.\n\
				* MSG will query messages from the store.\n\
				  Users can give host-mask, begin, end, \n\
				  ptn_id-mask to filter the message query.\n\
				* LIST_IMG will list all available image\n\
				  stores.\n\
				* IMG will query image information from\n\
				  image store (specified by '-I' option).\n\
				  The pattern/host/time filtering conditions\n\
				  are also applied.\n\
				* PTN_STAT will list pattern statistics \n\
				  by component ID. Statistic calculation\n\
				  can be limited by option -B,-E,-H,-P\n\
				  similar to MSG query.\n\
				* MPTN will list all meta patterns with their \n\
				  pattern_ids.\n\
    --image-store-name,-I IMG_STORE_NAME\n\
				The image store to query against.\n\
    --host-mask,-H NUMBER,...	The comma-separated list of numbers of\n\
				required hosts. The NUMBER can be in X-Y\n\
				format. (example: -H 1-10,20,30-50)\n\
				If --host-mask is not specified, all hosts\n\
				are included in the query.\n\
    --begin,-B T1		T1 is the beginning of the time window.\n\
    --end,-E T2			T2 is the ending of the time window.\n\
				If T1 is empty, bquery will obtain all data\n\
				up until T2. Likewise, if T2 is empty, bquery\n\
				obtains all data from T1 onward. Example:\n\
				-B \"2012-01-01 00:00:00\" \n\
				-E \"2012-12-31 23:59:59\" \n\
				If --begin and --end are not specified,\n\
				there is no time window condition.\n\
    --ptn_id-mask,-P NUMBER,...\n\
				The number format is similar to --host-mask\n\
				option. The list of numbers specify\n\
				Pattern IDs to be queried. If ptn_id-mask is\n\
				is not specified, all patterns are included.\n\
    --ts-format,-F FMT		Time stamp output format for '-t MSG'.\n\
				The default is the new syslog time format\n\
				with microseconds information (see RFC5424\n\
				section 6.2.3.1 example 4). Another\n\
				frequently used format is \"%%s\", or the \n\
				number of seconds since epoch.\n\
				Please see strftime(3) man page for format\n\
				information.\n\
    --verbose,-v		Verbose mode. For '-t MSG', this will print\n\
				[PTN_ID] before the actual message.\n\
				For '-t PTN', this will also print pattern \n\
				statistics (count, first seen, last seen).\n\
				For '-t MPTN' this will also print the number of \n\
				patterns in each meta-pattern class, \n\
				the patterns and their pattern_ids in each class, \n\
				the first-seen and the last-seen statistics \n\
    --sort-ptn-by,-S OPT        This option only applied to PTN query. \n\
				It tells bquery to sort the output patterns \n\
				by given OPT.  OPT canbe ID or ENG. Sort-by \n\
				ID is self-described. If sort-ptn-by ENG is \n\
				given, bquery will sort the output patterns \n\
				by count(ENG tokens)/count(tokens) ratio. \n\
				(default: ID) \n\
    --reverse,-R		For '-t MSG' or '-t IMG', query the messages \n\
				or the images, respectively, in \n\
				the reverse chronological order. \n\
    --escape,-e			Escape non-printable and spaces.\n\
    --version,-V		Print version information.\n\
\n"
#if 0
"Other OPTIONS:\n"
"    --store-path,s PATH	The path to the baler store. Using this\n"
"				option without --daemon implies single query\n"
"				mode.\n"
"    --daemon,d			The daemon mode flag. Require --store-path\n"
"				option.\n"
"    --remote-host,-r HOST	The hostname or IP address that another\n"
"				bquery resides. Using this option implies\n"
"				single query through other daemon mode.\n"
"				--xprt and --port can be used with this\n"
"				option to specify transport type and port\n"
"				number of the remote host.\n"
"\n"
#endif
	      );
}

/********** Options **********/
char *short_opt = "hs:dr:x:p:t:H:B:E:P:vI:F:RS:eV";
struct option long_opt[] = {
	{"help",              no_argument,        0,  'h'},
	{"store-path",        required_argument,  0,  's'},
	{"daemon",            no_argument,        0,  'd'},
	{"remote-host",       required_argument,  0,  'r'},
	{"xprt",              required_argument,  0,  'x'},
	{"port",              required_argument,  0,  'p'},
	{"type",              required_argument,  0,  't'},
	{"host-mask",         required_argument,  0,  'H'},
	{"begin",             required_argument,  0,  'B'},
	{"end",               required_argument,  0,  'E'},
	{"ptn_id-mask",       required_argument,  0,  'P'},
	{"image-store-name",  required_argument,  0,  'I'},
	{"ts-format",         required_argument,  0,  'F'},
	{"verbose",           no_argument,        0,  'v'},
	{"reverse",           no_argument,        0,  'R'},
	{"sort-ptn-by",       required_argument,  0,  'S'},
	{"escape",            no_argument,        0,  'e'},
	{"version",           no_argument,        0,  'V'},
	{0,                   0,                  0,  0}
};

/**
 * Determine the running mode from the program options.
 * \param mode[out] The output variable for running mode.
 * \return One of the BQ_MODE_LOCAL, BQ_MODE_DAEMON and BQ_MODE_REMOTE on
 * 	success.
 * \return BQ_MODE_INVAL on error.
 */
int bq_get_mode()
{
	return BQ_MODE_LOCAL;

#if 0
	/* Enable this in the future, when all of these options are
	 * supported. */
	if (store_path) {
		if (daemon_flag)
			return BQ_MODE_DAEMON;
		return BQ_MODE_LOCAL;
	}
	if (remote_host) {
		return BQ_MODE_REMOTE;
	}

	return BQ_MODE_INVAL;
#endif
}

void process_args(int argc, char **argv)
{
	char c;
	int i;
	int __idx=0;
	int rc;

next_arg:
	c = getopt_long(argc, argv, short_opt, long_opt, &__idx);
	switch (c) {
	case -1:
		goto out;
		break;
	case 'h': /* help */
		show_help();
		exit(0);
		break;
	case 's': /* store-path */
		store_path = strdup(optarg);
		break;
	case 'd': /* daemon */
		daemon_flag = 1;
		break;
	case 'r': /* remote-host */
		remote_host = strdup(optarg);
		break;
	case 't': /* type */
		query_type = bq_type(optarg);
		if (query_type == BQ_TYPE_UNKNOWN) {
			berr("Unknown --type %s\n", optarg);
			exit(-1);
		}
		break;
	case 'x': /* xprt */
		if (strcmp(optarg, "sock") == 0) {
			xprt_type = XPRT_SOCKET;
		} else if (strcmp(optarg, "rdma") == 0) {
			xprt_type = XPRT_RDMA;
		} else {
			printf("Unknown xprt: %s\n", argv[optind - 1]);
			exit(-1);
		}
		break;
	case 'p': /* port */
		port = atoi(optarg);
		break;
	case 'H': /* host-mask */
		hst_ids = strdup(optarg);
		if (!hst_ids) {
			perror("hst_ids: strdup");
			exit(-1);
		}
		break;
	case 'B': /* begin (time) */
		ts_begin = strdup(optarg);
		if (!ts_begin) {
			perror("ts_begin: strdup");
			exit(-1);
		}
		break;
	case 'E': /* end (time) */
		ts_end = strdup(optarg);
		if (!ts_end) {
			perror("ts_end: strdup");
			exit(-1);
		}
		break;
	case 'P': /* ptn_id-mask */
		ptn_ids = strdup(optarg);
		if (!ptn_ids) {
			perror("ptn_ids: strdup");
			exit(-1);
		}
		break;
	case 'I':
		img_store_name = optarg;
		break;
	case 'v':
		verbose = 1;
		break;
	case 'R':
		reverse = 1;
		break;
	case 'S':
		for (i = 0; i < SORT_PTN_BY_N; i++) {
			if (strcasecmp(optarg, sort_ptn_by_str[i]) == 0) {
				sort_ptn_by = i;
				break;
			}
		}
		if (i == SORT_PTN_BY_N) {
			printf("Unknown sort-ptn-by value: %s\n", optarg);
			exit(-1);
		}
		break;
	case 'F':
		ts_format = optarg;
		break;
	case 'e':
		escape = 1;
		break;
	case 'V':
		printf("bquery Version: %s\n", PACKAGE_VERSION);
		printf("git-SHA: %s\n", OVIS_GIT_LONG);
		exit(0);
		break;
	default:
		fprintf(stderr, "Unknown argument %s\n", argv[optind - 1]);
	}
	goto next_arg;
out:
	return;
}

static
struct bq_msg_fmt {
	struct bq_formatter base;
	const char *ts_fmt;
} __bq_msg_fmt;

static
int __bq_msg_fmt_ts(struct bq_formatter *_fmt, struct bdstr *bdstr, const struct timeval *tv)
{
	struct bq_msg_fmt *fmt = (void*)_fmt;
	char buff[256];
	struct tm tm;
	localtime_r(&tv->tv_sec, &tm);
	strftime(buff, sizeof(buff), fmt->ts_fmt, &tm);
	return bdstr_append_printf(bdstr, "%s ", buff);
}

int bq_local_msg_routine(struct bq_store *s)
{
	int rc = 0;
	struct bdstr *bdstr = NULL;
	struct bquery *q = NULL;

	__bq_msg_fmt.base = *bquery_default_formatter();
	if (ts_format) {
		__bq_msg_fmt.base.date_fmt = __bq_msg_fmt_ts;
		__bq_msg_fmt.ts_fmt = ts_format;
	}

	bdstr = bdstr_new(4096);
	if (!bdstr) {
		rc = errno;
		goto out;
	}

	q = (void*)bmsgquery_create(s, hst_ids, ptn_ids,
						ts_begin, ts_end, 1, 0, &rc);
	if (rc)
		goto out;
	bq_set_formatter(q, &__bq_msg_fmt.base);

	if (reverse) {
		rc = bq_last_entry(q);
	} else {
		rc = bq_first_entry(q);
	}
loop:
	switch (rc) {
	case 0:
		/* OK */
		break;
	case ENOENT:
		rc = 0;
	default:
		goto out;
	}

	if (verbose)
		printf("[%d] ", bq_entry_get_ptn_id(q));

	bdstr_reset(bdstr);
	rc = bq_store_refresh(q->store);
	if (rc) {
		berr("bq_store_refresh() rc: %d", rc);
		goto out;
	}
	bq_entry_print(q, bdstr);
	printf("%.*s\n", (int)bdstr->str_len, bdstr->str);
	if (reverse) {
		rc = bq_prev_entry(q);
	} else {
		rc = bq_next_entry(q);
	}
	goto loop;
out:
	if (q)
		bmsgquery_destroy((void*)q);
	if (bdstr)
		bdstr_free(bdstr);
	return rc;
}

static
int __ptn_cmp_by_id(const void *a, const void *b, void *arg)
{
	uint32_t id_a = *(uint32_t*)a;
	uint32_t id_b = *(uint32_t*)b;
	return id_a - id_b;
}

static
float __ptn_eng_ratio(const struct bstr *ptn, struct bq_store *s)
{
	const uint32_t *tkn_id;
	int bytes;
	int eng_count = 0;
	for (bytes = 0, tkn_id = ptn->u32str;
			bytes < ptn->blen;
			bytes += sizeof(*tkn_id), tkn_id++) {
		struct btkn_attr a = btkn_store_get_attr(s->tkn_store, *tkn_id);
		if (a.type == BTKN_TYPE_ENG) {
			eng_count++;
		}
	}
	return eng_count/(float)(ptn->blen/sizeof(*tkn_id));
}

static
int __ptn_cmp_by_eng(const void *a, const void *b, void *arg)
{
	uint32_t id_a = *(uint32_t*)a;
	uint32_t id_b = *(uint32_t*)b;
	struct bq_store *s = arg;
	const struct bstr *ptn_a = bptn_store_get_ptn(s->ptn_store, id_a);
	const struct bstr *ptn_b = bptn_store_get_ptn(s->ptn_store, id_b);

	float ra = __ptn_eng_ratio(ptn_a, s);
	float rb = __ptn_eng_ratio(ptn_b, s);

	if (ra < rb)
		return 1;
	if (ra > rb)
		return -1;
	return 0;
}

int bq_local_ptn_routine(struct bq_store *s)
{
	struct bdstr *bdstr;
	uint32_t first_id = bptn_store_first_id(s->ptn_store);
	uint32_t last_id = bptn_store_last_id(s->ptn_store);
	uint32_t n = last_id - first_id + 1;
	uint32_t id;
	uint64_t msg_count = 0;
	int i, j;


	char *ptn_str, *tmp, *endp;
	if (ptn_ids) {
		tmp = strdup(ptn_ids);
		n = 0;
		ptn_str = strtok_r(tmp, ",", &endp);
		while (ptn_str) {
			n++;
			ptn_str = strtok_r(NULL, ",", &endp);
		}
		free(tmp);
	}

	uint32_t *ids = malloc(sizeof(*ids) * n);
	if (!ids)
		return ENOMEM;

	if (ptn_ids) {
		int i = 0;
		tmp = strdup(ptn_ids);
		ptn_str = strtok_r(tmp, ",", &endp);
		while (ptn_str) {
			ids[i++] = strtoul(ptn_str, NULL, 0);
			ptn_str = strtok_r(NULL, ",", &endp);
		}
		free(tmp);
	} else {
		for (i = 0, id = first_id; i < n; i++, id++) {
			ids[i] = id;
		}
	}

	int rc = 0;

	int col_width[] = {
		8, 20, 32, 32, 10
	};

	int need_verbose[] = {
		0, 1, 1, 1, 0
	};

	const char *col_hdr[] = {
		"ptn_id",
		"count",
		"first-seen",
		"last-seen",
		"pattern"
	};
	int col_width_len = sizeof(col_width)/sizeof(col_width[0]);

	rc = bq_store_refresh(s);
	if (rc) {
		berr("bq_store_refresh() rc: %d", rc);
		return rc;
	}

	bdstr = bdstr_new(16*1024*1024);
	if (!bdstr) {
		berror("bdstr_new()");
		return errno;
	}

	if (sort_ptn_by == SORT_PTN_BY_ENG) {
		qsort_r(ids, n, sizeof(*ids), __ptn_cmp_by_eng, s);
	}

	for (i = 0; i < col_width_len; i++) {
		if (need_verbose[i] && !verbose)
			continue;
		for (j = 0; j < col_width[i]; j++) {
			printf("-");
		}
		if (i == col_width_len - 1) {
			printf("\n");
		} else {
			printf(" ");
		}
	}
	for (i = 0; i < col_width_len; i++) {
		if (need_verbose[i] && !verbose)
			continue;
		printf("%-*s", col_width[i], col_hdr[i]);
		if (i == col_width_len - 1) {
			printf("\n");
		} else {
			printf(" ");
		}
	}
	for (i = 0; i < col_width_len; i++) {
		if (need_verbose[i] && !verbose)
			continue;
		for (j = 0; j < col_width[i]; j++) {
			printf("-");
		}
		if (i == col_width_len - 1) {
			printf("\n");
		} else {
			printf(" ");
		}
	}

	for (i = 0; i < n; i++) {
		id = ids[i];
		bdstr_reset(bdstr);
		bdstr_append_printf(bdstr, "%*d ", col_width[0], id);

		if (verbose) {
			const struct bptn_attrM *attrM =
					bptn_store_get_attrM(s->ptn_store, id);
			if (!attrM)
				goto skip;

			bdstr_append_printf(bdstr, "%*lu ",
						col_width[1], attrM->count);
			msg_count += attrM->count;
			__default_date_fmt(NULL, bdstr, &attrM->first_seen);
			__default_date_fmt(NULL, bdstr, &attrM->last_seen);
		}

		if (escape) {
			rc = bptn_store_id2str_esc(s->ptn_store, s->tkn_store,
					id, bdstr->str + bdstr->str_len,
					bdstr->alloc_len - bdstr->str_len);
		} else {
			rc = bptn_store_id2str(s->ptn_store, s->tkn_store, id,
					bdstr->str + bdstr->str_len,
					bdstr->alloc_len - bdstr->str_len);
		}

		switch (rc) {
		case 0:
			/* do nothing, just continue the execution. */
			break;
		case ENOENT:
			/* skip a loop for no entry */
			goto skip;
		default:
			return rc;
		}

		printf("%s\n", bdstr->str);
	skip:
		continue;
	}

	for (i = 0; i < col_width_len; i++) {
		if (need_verbose[i] && !verbose)
			continue;
		for (j = 0; j < col_width[i]; j++) {
			printf("-");
		}
		if (i == col_width_len - 1) {
			printf("\n");
		} else {
			printf(" ");
		}
	}

	if (verbose) {
		printf("Total: %ld messages\n", msg_count);
	}

	return rc;
}

struct ptn_entry {
	uint32_t ptn_id;
	TAILQ_ENTRY(ptn_entry) link;
};

struct mptn_data {
	struct bptn_attrM attr;
	TAILQ_HEAD(, ptn_entry) tailq;
};

int __mptn_print(struct bq_store *s, struct bdstr *bdstr, int *col_width,
			struct mptn_data *mptn_data, uint32_t mptn_id)
{
	int rc;
	uint64_t msg_count = 0;
	struct ptn_entry *ent;
	const struct bstr *cname = bmptn_get_cluster_name(s->mptn_store, mptn_id);

	bdstr_reset(bdstr);
	bdstr_append_printf(bdstr, "%-*d ", col_width[0], mptn_id);
	if (verbose) {
		const struct bptn_attrM *attrM =
					&mptn_data[mptn_id].attr;
		bdstr_append_printf(bdstr, "%*lu ",
				col_width[1], attrM->count);
		msg_count += attrM->count;
		__default_date_fmt(NULL, bdstr, &attrM->first_seen);
		__default_date_fmt(NULL, bdstr, &attrM->last_seen);
	}
	if (!cname) {
		bdstr_append_printf(bdstr, "*");
	} else {
		bdstr_append_printf(bdstr, "%.*s", cname->blen, cname->cstr);
	}

	printf("%s\n", bdstr->str);

	if (!verbose)
		return 0;

	TAILQ_FOREACH(ent, &mptn_data[mptn_id].tailq, link) {
		bdstr_reset(bdstr);
		bdstr_append_printf(bdstr, "%+*d ", col_width[0], ent->ptn_id);

		const struct bptn_attrM *attrM =
			bptn_store_get_attrM(s->ptn_store, ent->ptn_id);
		if (!attrM)
			goto skip;

		bdstr_append_printf(bdstr, "%*lu ",
				col_width[1], attrM->count);
		msg_count += attrM->count;
		__default_date_fmt(NULL, bdstr, &attrM->first_seen);
		__default_date_fmt(NULL, bdstr, &attrM->last_seen);

		if (escape) {
			rc = bptn_store_id2str_esc(s->ptn_store,
				s->tkn_store, ent->ptn_id,
				bdstr->str + bdstr->str_len,
				bdstr->alloc_len - bdstr->str_len);
		} else {
			rc = bptn_store_id2str(s->ptn_store,
				s->tkn_store, ent->ptn_id,
				bdstr->str + bdstr->str_len,
				bdstr->alloc_len - bdstr->str_len);
		}
		switch (rc) {
		case 0:
			/* do nothing, just continue the execution. */
			break;
		case ENOENT:
			/* skip a loop for no entry */
			goto skip;
		default:
			return rc;
		}

		printf("%s\n", bdstr->str);
	skip:
		continue;
	}
	return 0;
}

int bq_local_mptn_routine(struct bq_store *s)
{
	int rc = 0;
	struct bdstr *bdstr;
	uint32_t first_id = bptn_store_first_id(s->ptn_store);
	uint32_t last_id = bptn_store_last_id(s->ptn_store);
	uint32_t n = last_id - first_id + 1;
	uint32_t first_class_id = 0;
	uint32_t last_class_id = 0;
	uint32_t id, mptn_id;
	uint64_t msg_count = 0;
	struct bset_u32 *set = NULL;
	struct brange_u32_head mpr = {0};
	struct brange_u32 *rng;
	struct brange_u32_iter *itr = NULL;
	int i, j;
	struct mptn_data *mptn_data = NULL;
	struct ptn_entry *entries = NULL;

	if (!s->mptn_store) {
		berr("No meta pattern store. Please run `bmeta_cluster` to create them.");
		return ENOENT;
	}

	last_class_id = bmptn_store_get_last_cls_id(s->mptn_store);

	/*
	 * Specific meta-pattern IDs are given.
	 */
	char *mptn_id_str, *tmp, *endp;
	int num_ids = 0;
	if (ptn_ids) {
		set = bset_u32_from_numlist(ptn_ids, 0);
		if (!set) {
			berror("bset_u32_from_numlist()");
			rc = errno;
			goto cleanup;
		}
		rc = bset_u32_to_brange_u32(set, &mpr);
		if (rc) {
			berr("bset_u32_to_brange_u32() returns %d", rc);
			goto cleanup;
		}
	}

	mptn_data = calloc(last_class_id + 1, sizeof(*mptn_data));
	if (!mptn_data) {
		rc = ENOMEM;
		goto cleanup;
	}

	for (i = 0; i < last_class_id + 1; i++) {
		mptn_data[i].attr.first_seen.tv_sec = 0x7FFFFFFFFFFFFFFFL;
		TAILQ_INIT(&mptn_data[i].tailq);
	}

	entries = calloc(sizeof(*entries), last_id + 1);
	if (!entries) {
		rc = ENOMEM;
		goto cleanup;
	}

	for (id = first_id; id <= last_id; id++) {
		entries[id].ptn_id = id;
		mptn_id = bmptn_store_get_class_id(s->mptn_store, id);
		assert(mptn_id <= last_class_id);
		TAILQ_INSERT_TAIL(&mptn_data[mptn_id].tailq, &entries[id], link);
		const struct bptn_attrM *attrM = bptn_store_get_attrM(s->ptn_store, id);
		if (!attrM)
			continue;
		mptn_data[mptn_id].attr.count += attrM->count;
		if (timercmp(&attrM->first_seen,
			     &mptn_data[mptn_id].attr.first_seen,
			     <)) {
			mptn_data[mptn_id].attr.first_seen =
				attrM->first_seen;
		}
		if (timercmp(&attrM->last_seen,
			     &mptn_data[mptn_id].attr.last_seen,
			     >)) {
			mptn_data[mptn_id].attr.last_seen =
				attrM->last_seen;
		}
	}

	int col_width[] = {
		8, 15, 32, 32, 10
	};

	int need_verbose[] = {
		0, 1, 1, 1, 0
	};

	const char *col_hdr[] = {
		"ptn_id",
		"count",
		"first-seen",
		"last-seen",
		"pattern"
	};
	int col_width_len = sizeof(col_width)/sizeof(col_width[0]);

	rc = bq_store_refresh(s);
	if (rc) {
		berr("bq_store_refresh() rc: %d", rc);
		goto cleanup;
	}

	bdstr = bdstr_new(16*1024*1024);
	if (!bdstr) {
		berror("bdstr_new()");
		rc = errno;
		goto cleanup;
	}

	for (i = 0; i < col_width_len; i++) {
		if (need_verbose[i] && !verbose)
			continue;
		for (j = 0; j < col_width[i]; j++) {
			printf("-");
		}
		if (i == col_width_len - 1) {
			printf("\n");
		} else {
			printf(" ");
		}
	}
	for (i = 0; i < col_width_len; i++) {
		if (need_verbose[i] && !verbose)
			continue;
		printf("%-*s", col_width[i], col_hdr[i]);
		if (i == col_width_len - 1) {
			printf("\n");
		} else {
			printf(" ");
		}
	}
	for (i = 0; i < col_width_len; i++) {
		if (need_verbose[i] && !verbose)
			continue;
		for (j = 0; j < col_width[i]; j++) {
			printf("-");
		}
		if (i == col_width_len - 1) {
			printf("\n");
		} else {
			printf(" ");
		}
	}

	if (ptn_ids) {
		itr = brange_u32_iter_new(TAILQ_FIRST(&mpr));
		if (!itr) {
			rc = errno;
			goto cleanup;
		}
		rc = brange_u32_iter_begin(itr, &mptn_id);
		while (rc == 0 && mptn_id <= last_class_id) {
			rc = __mptn_print(s, bdstr, col_width,
						mptn_data, mptn_id);
			if (rc)
				return rc;
			rc = brange_u32_iter_next(itr, &mptn_id);
		}
	} else {
		for (mptn_id = 0; mptn_id <= last_class_id; mptn_id++) {
			rc = __mptn_print(s, bdstr, col_width,
					mptn_data, mptn_id);
			if (rc) {
				berr("__mptn_print(%d): %d", mptn_id, rc);
				return rc;
			}
		}
	}

	for (i = 0; i < col_width_len; i++) {
		if (need_verbose[i] && !verbose)
			continue;
		for (j = 0; j < col_width[i]; j++) {
			printf("-");
		}
		if (i == col_width_len - 1) {
			printf("\n");
		} else {
			printf(" ");
		}
	}

cleanup:
	if (entries)
		free(entries);
	if (mptn_data)
		free(mptn_data);
	if (set)
		bset_u32_free(set);
	if (itr)
		brange_u32_iter_free(itr);
	while ((rng = TAILQ_FIRST(&mpr))) {
		TAILQ_REMOVE(&mpr, rng, link);
		free(rng);
	}
	return rc;
}

static
void img_store_list_cb(const char *name, void *arg)
{
	printf("\t%s\n", name);
}

static
void __bq_list_available_img(struct bq_store *s)
{
	printf("Available Image Store:\n");
	bq_imgstore_iterate(s, img_store_list_cb, NULL);
}

int bq_local_img_routine(struct bq_store *s)
{
	int rc = 0;
	struct bimgquery *imgq = bimgquery_create(s, hst_ids, ptn_ids,
					ts_begin, ts_end, img_store_name, &rc);
	struct bpixel p;
	uint64_t count = 0;

	if (!imgq) {
		berr("Cannot create imqge query, rc: %d", rc);
		return rc;
	}

	if (reverse) {
		rc = bq_last_entry(&imgq->base);
	} else {
		rc = bq_first_entry(&imgq->base);
	}

loop:
	if (rc)
		goto out;

	bq_img_entry_get_pixel(imgq, &p);
	printf("%u, %u, %u, %u\n", p.ptn_id, p.sec, p.comp_id, p.count);
	count++;

	if (reverse) {
		rc = bq_prev_entry(&imgq->base);
	} else {
		rc = bq_next_entry(&imgq->base);
	}
	goto loop;

out:
	if (count == 0) {
		bwarn("No data matching the query.");
	}
	bdebug("Matched pixels: %lu", count);
	if (rc == ENOENT)
		rc = 0;
	bimgquery_destroy(imgq);
	return 0;
}

struct __ptn_stat_key {
	uint32_t ptn_id;
	uint32_t comp_id;
};

struct __ptn_stat_value {
	struct __ptn_stat_key k;
	uint32_t min_ts;
	uint32_t max_ts;
	uint64_t count;
};

int __ptn_stat_value_cmp(const void *a, const void *b)
{
	const struct __ptn_stat_value *va = *(const struct __ptn_stat_value**)a;
	const struct __ptn_stat_value *vb = *(const struct __ptn_stat_value**)b;
	if (va->k.ptn_id > vb->k.ptn_id)
		return 1;
	if (va->k.ptn_id < vb->k.ptn_id)
		return -1;
	if (va->k.comp_id > vb->k.comp_id)
		return 1;
	if (va->k.comp_id < vb->k.comp_id)
		return -1;
	return 0;
}

int bq_local_ptn_stat_routine(struct bq_store *s)
{
	struct __ptn_stat_key k;
	struct __ptn_stat_value **varray = NULL;
	int i, _rc, rc = 0;
	struct bhash *bhash = NULL;
	struct bimgquery *imgq = NULL;
	struct bpixel p;
	uint64_t count = 0;
	uint32_t comp_id, ptn_id;

	imgq = bimgquery_create(s, hst_ids, ptn_ids, ts_begin, ts_end,
							img_store_name, &rc);
	if (!imgq) {
		berr("Cannot create imqge query, rc: %d", rc);
		return rc;
	}

	bhash = bhash_new(65539, 11, NULL);
	if (!bhash) {
		berror("bhash_new()");
		rc = errno;
		goto cleanup;
	}

	rc = bq_first_entry(&imgq->base);

loop:
	if (rc)
		goto out;

	bq_img_entry_get_pixel(imgq, &p);
	k.ptn_id = p.ptn_id;
	k.comp_id = p.comp_id;
	struct bhash_entry *hent = bhash_entry_get(bhash, (void*)&k, sizeof(k));
	if (hent) {
		struct __ptn_stat_value *v = (void*)hent->value;
		if (p.sec < v->min_ts)
			v->min_ts = p.sec;
		if (p.sec > v->max_ts)
			v->max_ts = p.sec;
		v->count += p.count;
	} else {
		struct __ptn_stat_value *v = malloc(sizeof(*v));
		if (!v) {
			goto cleanup;
		}
		v->k.ptn_id = p.ptn_id;
		v->k.comp_id = p.comp_id;
		v->min_ts = v->max_ts = p.sec;
		v->count = p.count;
		hent = bhash_entry_set(bhash, (void*)&v->k,
					sizeof(v->k), (uint64_t)v);
		if (!hent) {
			berror("bhash_entry_set()");
			rc = errno;
			goto cleanup;
		}
		count++;
	}

	rc = bq_next_entry(&imgq->base);
	goto loop;

out:
	if (count == 0) {
		bwarn("No data matching the query.");
		goto cleanup;
	}
	if (rc == ENOENT)
		rc = 0;
	varray = malloc(sizeof(varray[0]) * count);
	if (!varray) {
		berror("malloc()");
		goto cleanup;
	}
	i = 0;
	struct bhash_iter *itr = bhash_iter_new(bhash);
	_rc = bhash_iter_begin(itr);
	while (!_rc) {
		varray[i++] = (void*)bhash_iter_entry(itr)->value;
		_rc = bhash_iter_next(itr);
	}

	qsort(varray, count, sizeof(*varray), __ptn_stat_value_cmp);

	const char *fmt = "%F %T";
	if (ts_format) {
		fmt = ts_format;
	}

	printf("ptn_id,comp_id,min_ts,max_ts,count\n");
	for (i = 0; i < count; i++) {
		struct tm tm;
		char buff0[128], buff1[128];
		int len;
		time_t ts0, ts1;
		ts0 = varray[i]->min_ts;
		ts1 = varray[i]->max_ts;
		bzero(&tm, sizeof(tm));
		localtime_r(&ts0, &tm);
		len = strftime(buff0, sizeof(buff0), fmt, &tm);
		bzero(&tm, sizeof(tm));
		localtime_r(&ts1, &tm);
		len = strftime(buff1, sizeof(buff1), fmt, &tm);
		printf("%u,%u,%s,%s,%lu\n", varray[i]->k.ptn_id,
					  varray[i]->k.comp_id,
					  buff0, buff1, varray[i]->count);
	}

cleanup:
	if (varray) {
		free(varray);
	}
	if (imgq)
		bimgquery_destroy(imgq);
	if (bhash) {
		struct bhash_iter *itr = bhash_iter_new(bhash);
		int _rc;
		_rc = bhash_iter_begin(itr);
		while (_rc == 0) {
			struct bhash_entry *hent = bhash_iter_entry(itr);
			free((void*)hent->value);
			_rc = bhash_iter_next(itr);
		}
		bhash_free(bhash);
	}
	return rc;
}

int bq_local_routine()
{
	int rc = 0;
	struct bq_store *s;
	if (!store_path) {
		berr("store-path is not given.\n");
		rc = EINVAL;
		goto out;
	}
	if ((s = bq_open_store(store_path)) == NULL) {
		berr("bq_open_store error, store: %s", store_path);
		rc = errno;
		goto out;
	}

	comp_store = s->cmp_store;
	tkn_store = s->tkn_store;
	ptn_store = s->ptn_store;

	switch (query_type) {
	case BQ_TYPE_MSG:
		rc = bq_local_msg_routine(s);
		break;
	case BQ_TYPE_PTN:
		rc = bq_local_ptn_routine(s);
		break;
	case BQ_TYPE_HOST:
		rc = bq_local_host_routine(s);
		break;
	case BQ_TYPE_IMG:
		rc = bq_local_img_routine(s);
		break;
	case BQ_TYPE_LIST_IMG:
		__bq_list_available_img(s);
		break;
	case BQ_TYPE_PTN_STAT:
		rc = bq_local_ptn_stat_routine(s);
		break;
	case BQ_TYPE_MPTN:
		rc = bq_local_mptn_routine(s);
		break;
	default:
		rc = EINVAL;
	}
	bq_store_close_free(s);
out:
	return rc;
}

int bq_daemon_routine()
{
	/* Consider using Ruby or other scripting language to handle this. */
	berr("bq_daemon_routine unimplemented!!!\n");
	return 0;
}

int bq_remote_routine()
{
	/* Deprecated design ... left here just for a chance of resurviving. */
	berr("bq_remote_routine unimplemented!!!\n");
	return 0;
}

int main(int argc, char **argv)
{
	int rc;
	process_args(argc, argv);
	running_mode = bq_get_mode();
	switch (running_mode) {
	case BQ_MODE_LOCAL:
		rc = bq_local_routine();
		break;
	case BQ_MODE_DAEMON:
		rc = bq_daemon_routine();
		break;
	case BQ_MODE_REMOTE:
		rc = bq_remote_routine();
		break;
	default:
		rc = EINVAL;
		printf("Cannot determine the running mode from the "
				"arguments.\n");
	}
	return rc;
}
#endif /*BIN*/
