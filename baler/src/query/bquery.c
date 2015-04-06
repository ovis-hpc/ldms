/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-14 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-14 Sandia Corporation. All rights reserved.
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
 * \brief Baler storage query.
 *
 * bquery queries Baler database for:
 * 	- token
 * 	- patterns (in given node-time window)
 * 	- messages (in given node-time window)
 * 	- image pixels (in given node-time window in various granularity)
 *
 * tokens and patterns information are obtained from baler store (specified by
 * store path).
 *
 * bquery requires balerd to use bout_sos_img for image pixel query and
 * bout_sos_msg for message query. The sos files for img's and msg's are assumed
 * to be in the store path as well.
 *
 * Other storage format for various output plugins are not supported.
 *
 * bqeury can also run in daemon mode to eliminate the open-file overhead before
 * every query. A client can stay connected to bquery daemon and query
 * information repeatedly.
 *
 * XXX Issue: store should contain several images of various granularity. Users
 * can configure multiple bout_sos_img to store multiple images as they pleased.
 * However, bquery does not know about those configuration. I need to solve
 * this, either with baler_store configuration or other things. But, for now, I
 * will only support message query to test the balerd. Image query support shall
 * be revisited soon.
 *
 */

#include "bquery.h"
#include "bquery_priv.h"
#include <getopt.h>
#include <errno.h>
#include <wordexp.h>
#include <ctype.h>

#include <time.h>
#include <dirent.h>

#include <sos/obj_idx.h>

#include "baler/butils.h"
#include "baler/btkn.h"

#include "assert.h"

static
uint32_t __bq_entry_get_sec(struct bquery *q)
{
	return sos_obj_attr_get_uint32(q->bsos->sos, SOS_MSG_SEC,
							sos_iter_obj(q->itr));
}

static
uint32_t __bq_entry_get_usec(struct bquery *q)
{
	return sos_obj_attr_get_uint32(q->bsos->sos, SOS_MSG_USEC,
							sos_iter_obj(q->itr));
}

static
uint32_t __bq_entry_get_comp_id(struct bquery *q)
{
	return sos_obj_attr_get_uint32(q->bsos->sos, SOS_MSG_COMP_ID,
							sos_iter_obj(q->itr));
}

static
uint32_t __bq_entry_get_ptn_id(struct bquery *q)
{
	sos_blob_obj_t blob = sos_obj_attr_get(q->bsos->sos, SOS_MSG_MSG,
							sos_iter_obj(q->itr));
	if (!blob)
		return 0;
	return ((struct bmsg*)blob->data)->ptn_id;
}

static inline
const struct bout_sos_img_key* __bq_img_entry_get_key(struct bimgquery *q)
{
	sos_obj_t obj = sos_iter_obj(q->base.itr);
	if (!obj)
		return NULL;
	return sos_obj_attr_get(q->base.bsos->sos, SOS_IMG_KEY, obj);
}

static
uint32_t __bq_img_entry_get_sec(struct bquery *q)
{
	struct bout_sos_img_key *k = sos_obj_attr_get(q->bsos->sos, SOS_IMG_KEY,
							sos_iter_obj(q->itr));
	return be32toh(k->ts);
}

static
uint32_t __bq_img_entry_get_usec(struct bquery *q)
{
	return 0;
}

static
uint32_t __bq_img_entry_get_comp_id(struct bquery *q)
{
	struct bout_sos_img_key *k = sos_obj_attr_get(q->bsos->sos, SOS_IMG_KEY,
							sos_iter_obj(q->itr));
	return be32toh(k->comp_id);
}

static
uint32_t __bq_img_entry_get_ptn_id(struct bquery *q)
{
	struct bout_sos_img_key *k = sos_obj_attr_get(q->bsos->sos, SOS_IMG_KEY,
							sos_iter_obj(q->itr));
	return be32toh(k->ptn_id);
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
int fmt_date_fmt(struct bq_formatter *fmt, struct bdstr *bdstr, time_t ts)
{
	if (fmt->date_fmt)
		return fmt->date_fmt(fmt, bdstr, ts);
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

struct bsos_wrap* bsos_wrap_open(const char *path)
{
	struct bsos_wrap *bsw = calloc(1, sizeof(*bsw));
	if (!bsw)
		goto err0;
	bsw->sos = sos_open(path, O_RDWR);
	if (!bsw->sos)
		goto err1;

	char *bname = basename(path);
	if (!bname)
		goto err2;
	bsw->store_name = strdup(bname);
	if (!bsw->store_name)
		goto err2;
	return bsw;
err2:
	sos_close(bsw->sos, ODS_COMMIT_ASYNC);
err1:
	free(bsw);
err0:
	return NULL;
}

void bsos_wrap_close_free(struct bsos_wrap *bsw)
{
	sos_close(bsw->sos, ODS_COMMIT_ASYNC);
	free(bsw->store_name);
	free(bsw);
}

void bq_store_close_free(struct bq_store *store)
{
	if (store->cmp_store) {
		btkn_store_close_free(store->cmp_store);
	}

	if (store->tkn_store) {
		btkn_store_close_free(store->tkn_store);
	}

	if (store->ptn_store) {
		bptn_store_close_free(store->ptn_store);
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

	snprintf(s->path, sizeof(s->path), "%s", path);

	/* msg_store and img_store will be opened at query time */
	return s;

err:
	bq_store_close_free(s);
	berr("Cannot open %s", s->path);
	return NULL;
}

int bq_local_ptn_routine(struct bq_store *s)
{
	char buff[4096];
	uint32_t id = bptn_store_first_id(s->ptn_store);
	uint32_t last_id = bptn_store_last_id(s->ptn_store);
	int rc = 0;
	printf("-------- --------------\n");
	printf("  ptn_id pattern\n");
	printf("-------- --------------\n");
	while (id <= last_id) {
		rc = bptn_store_id2str(s->ptn_store, s->tkn_store, id, buff,
									4096);
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

		printf("%8d %s\n", id, buff);
	skip:
		id++;
	}
	printf("-------- --------------\n");
	return rc;
}

int bq_local_host_routine(struct bq_store *s)
{
	int rc = 0;
	char buff[4096];
	uint32_t id = BMAP_ID_BEGIN;
	uint32_t count = s->cmp_store->map->hdr->count;
	printf("-------- --------------\n");
	printf(" host_id hostname\n");
	printf("-------- --------------\n");
	while (count) {
		rc = btkn_store_id2str(s->cmp_store, id, buff, sizeof(buff));
		printf("%8d %s\n", id - (BMAP_ID_BEGIN - 1), buff);
		id++;
		count--;
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

void bquery_destroy(struct bquery *q)
{
	if (q->itr)
		sos_iter_free(q->itr);
	if (q->hst_ids)
		bset_u32_free(q->hst_ids);
	if (q->ptn_ids)
		bset_u32_free(q->ptn_ids);
	if (q->bsos)
		bsos_wrap_close_free(q->bsos);
	free(q);
}

void bimgquery_destroy(struct bimgquery *q)
{
	free(q->store_name);
	struct brange_u32 *r;
	while ((r = LIST_FIRST(&q->hst_rngs))) {
		LIST_REMOVE(r, link);
		free(r);
	}
	if (q->hst_rng_itr)
		brange_u32_iter_free(q->hst_rng_itr);
	while ((r = LIST_FIRST(&q->ptn_rngs))) {
		LIST_REMOVE(r, link);
		free(r);
	}
	if (q->ptn_rng_itr)
		brange_u32_iter_free(q->ptn_rng_itr);
	bquery_destroy((void*)q);
}

int __default_tkn_fmt(struct bq_formatter *fmt, struct bdstr *bdstr,
			const struct bstr *tkn, struct btkn_attr *attr,
			uint32_t tkn_id)
{
	return bdstr_append_bstr(bdstr, tkn);
}

int __default_date_fmt(struct bq_formatter *fmt, struct bdstr *bdstr, time_t ts)
{
	char buff[64];
	struct tm tm;
	localtime_r(&ts, &tm);
	strftime(buff, sizeof(buff), "%FT%T%z ", &tm);
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

int bq_msg_first_entry(struct bquery *q);
int bq_msg_next_entry(struct bquery *q);
int bq_msg_prev_entry(struct bquery *q);
int bq_msg_last_entry(struct bquery *q);

struct bquery* bquery_create(struct bq_store *store, const char *hst_ids,
			     const char *ptn_ids, const char *ts0,
			     const char *ts1, int is_text, char sep, int *rc)
{
	int _rc = 0;
	ssize_t len = 0;

	/* Call tzset first for correct localtime_r() result in the program. */
	tzset();

	if (!store) {
		_rc = EINVAL;
		goto out;
	}

	struct bquery *q = calloc(1, sizeof(*q));
	if (!q) {
		_rc = errno;
		goto out;
	}

	q->store = store;
	q->stat = BQ_STAT_INIT;

	q->get_sec = __bq_entry_get_sec;
	q->get_usec = __bq_entry_get_usec;
	q->get_ptn_id = __bq_entry_get_ptn_id;
	q->get_comp_id = __bq_entry_get_comp_id;

	q->next_entry = bq_msg_next_entry;
	q->prev_entry = bq_msg_prev_entry;
	q->first_entry = bq_msg_first_entry;
	q->last_entry = bq_msg_last_entry;

	if (hst_ids) {
		q->hst_ids = bset_u32_from_numlist(hst_ids, MASK_HSIZE);
		if (!q->hst_ids) {
			_rc = errno;
			goto err;
		}
	}

	if (ptn_ids) {
		q->ptn_ids = bset_u32_from_numlist(ptn_ids, MASK_HSIZE);
		if (!q->ptn_ids) {
			_rc = errno;
			goto err;
		}
	}

	struct tm tm;
	char *ts_ret;
	if (ts0) {
		q->ts_0 = parse_ts(ts0);
		if (q->ts_0 == -1) {
			_rc = EINVAL;
			goto err;
		}
	}
	if (ts1) {
		q->ts_1 = parse_ts(ts1);
		if (q->ts_1 == -1) {
			_rc = EINVAL;
			goto err;
		}
	}

	q->text_flag = is_text;
        q->sep = (sep)?(sep):(' ');
	q->formatter = bquery_default_formatter();

	len = snprintf(q->sos_prefix, PATH_MAX, "%s/msg_store/msg", store->path);
	q->sos_prefix_end = q->sos_prefix + len;

	goto out;

err:
	bquery_destroy(q);
	q = NULL;
out:
	if (rc)
		*rc = _rc;
	return q;
}

int bq_img_first_entry(struct bquery *q);
int bq_img_next_entry(struct bquery *q);
int bq_img_prev_entry(struct bquery *q);
int bq_img_last_entry(struct bquery *q);

struct bimgquery* bimgquery_create(struct bq_store *store, const char *hst_ids,
				const char *ptn_ids, const char *ts0,
				const char *ts1, const char *img_store_name,
				int *rc)
{
	int _rc;
	ssize_t len;
        struct bquery *bq = bquery_create(store, hst_ids, ptn_ids, ts0, ts1, 0,
                                                0, rc);
	if (!bq)
		return NULL;
	struct bimgquery *bi = calloc(1, sizeof(*bi));

	/* Transfer the contents of bq to bi, and free (not destroy) the unused
	 * bq */
	bi->base = *bq;
	free(bq);

	bi->base.get_sec = __bq_img_entry_get_sec;
	bi->base.get_usec = __bq_img_entry_get_usec;
	bi->base.get_comp_id = __bq_img_entry_get_comp_id;
	bi->base.get_ptn_id = __bq_img_entry_get_ptn_id;

	bi->base.first_entry = bq_img_first_entry;
	bi->base.next_entry = bq_img_next_entry;
	bi->base.prev_entry = bq_img_prev_entry;
	bi->base.last_entry = bq_img_last_entry;

	len = snprintf(bi->base.sos_prefix, PATH_MAX, "%s/img_store/%s",
						store->path, img_store_name);
	bi->base.sos_prefix_end = bi->base.sos_prefix + len;
	bi->store_name = strdup(img_store_name);
	if (!bi->store_name)
		goto err;
	if (bi->base.hst_ids) {
		_rc = bset_u32_to_brange_u32(bi->base.hst_ids, &bi->hst_rngs);
		if (_rc)
			goto err;
		bi->hst_rng_itr = brange_u32_iter_new(LIST_FIRST(&bi->hst_rngs));
		if (!bi->hst_rng_itr)
			goto err;
	}

	if (bi->base.ptn_ids) {
		_rc = bset_u32_to_brange_u32(bi->base.ptn_ids, &bi->ptn_rngs);
		if (_rc)
			goto err;
		bi->ptn_rng_itr = brange_u32_iter_new(LIST_FIRST(&bi->ptn_rngs));
		if (!bi->ptn_rng_itr)
			goto err;
	}
	return bi;
err:
	bimgquery_destroy(bi);
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

static int __bq_init(struct bquery *q)
{
	q->bsos = bsos_wrap_open(q->sos_prefix);
	if (!q->bsos) {
		return errno;
	}
	return 0;
}

static int __bq_open_bsos(struct bquery *q)
{
	struct bsos_wrap *bsos;
	sos_iter_t iter;
	if (q->sos_number) {
		snprintf(q->sos_prefix_end,
			PATH_MAX - (q->sos_prefix_end - q->sos_prefix),
			".%d", q->sos_number);

	} else {
		*q->sos_prefix_end = 0;
	}
	bsos = bsos_wrap_open(q->sos_prefix);
	if (!bsos)
		return errno;
	iter = sos_iter_new(bsos->sos, 0);
	if (!iter) {
		bsos_wrap_close_free(bsos);
		return ENOMEM;
	}
	q->bsos = bsos;
	q->itr = iter;
	return 0;
}

static int __bq_prev_store(struct bquery *q)
{
	int rc;
	sos_iter_t old_itr = q->itr;
	struct bsos_wrap *old_bsos = q->bsos;

	q->sos_number++;
	rc = __bq_open_bsos(q);

	if (!rc) {
		/* clean old stuff */
		if (old_itr)
			sos_iter_free(old_itr);
		if (old_bsos)
			bsos_wrap_close_free(old_bsos);
	}
	return rc;
}

static int __bq_next_store(struct bquery *q)
{
	int rc;
	sos_iter_t old_itr = q->itr;
	struct bsos_wrap *old_bsos = q->bsos;
	if (!q->sos_number)
		return ENOENT;

	q->sos_number--;
	rc = __bq_open_bsos(q);

	if (!rc) {
		/* clean old stuff */
		if (old_itr)
			sos_iter_free(old_itr);
		if (old_bsos)
			bsos_wrap_close_free(old_bsos);
	}
	return rc;
}

static int __bq_last_entry(struct bquery *q)
{
	int rc = 0;

loop:
	if (!q->ts_1) {
		rc = sos_iter_end(q->itr);
		goto out;
	}

	sos_attr_t attr = sos_obj_attr_by_id(q->bsos->sos, 0);
	size_t ksz = attr->attr_size_fn(attr, 0);
	struct bout_sos_img_key imgkey = {.ts = q->ts_1};
	void *p;

	switch (attr->type) {
	case SOS_TYPE_UINT32:
		ksz = 4;
		p = &imgkey.ts;
		break;
	case SOS_TYPE_UINT64:
		ksz = 8;
		p = &imgkey;
		break;
	default:
		/* do nothing */ ;
	}
	obj_key_t key = obj_key_new(ksz);
	if (!key) {
		rc = ENOMEM;
		goto out;
	}
	obj_key_set(key, p, ksz);

	if (0 != sos_iter_seek_inf(q->itr, key)) {
		rc = ENOENT;
	}
	obj_key_delete(key);
	if (rc == ENOENT) {
		rc = __bq_next_store(q);
		if (rc)
			goto out;
		goto loop;
	}
out:
	return rc;
}

static int __bq_first_entry(struct bquery *q)
{
	int rc = 0;

loop:
	if (!q->ts_0) {
		rc = sos_iter_begin(q->itr);
		goto out;
	}

	sos_attr_t attr = sos_obj_attr_by_id(q->bsos->sos, 0);
	size_t ksz = attr->attr_size_fn(attr, 0);
	struct bout_sos_img_key imgkey = {.ts = q->ts_0, .comp_id = 0};
	void *p;

	switch (attr->type) {
	case SOS_TYPE_UINT32:
		ksz = 4;
		p = &imgkey.ts;
		break;
	case SOS_TYPE_UINT64:
		ksz = 8;
		p = &imgkey;
		break;
	default:
		/* do nothing */ ;
	}
	obj_key_t key = obj_key_new(ksz);
	if (!key) {
		rc = ENOMEM;
		goto out;
	}
	obj_key_set(key, p, ksz);

	if (0 != sos_iter_seek_sup(q->itr, key)) {
		rc = ENOENT;
	}
	obj_key_delete(key);
	if (rc == ENOENT) {
		rc = __bq_next_store(q);
		if (rc)
			goto out;
		goto loop;
	}
out:
	return rc;
}

static int __get_max_sos_number(const char *sos_path)
{
	int i;
	struct stat _st;
	int rc;
	char *path = malloc(PATH_MAX);
	if (!path)
		return 0;
	for (i = 1; i < 65536; i++) {
		snprintf(path, PATH_MAX, "%s.%d_sos.PG", sos_path, i);
		rc = stat(path, &_st);
		if (rc)
			break;
	}
	free(path);
	return i - 1;
}

/**
 * Move the sos iterator to the previous entry.
 */
static int __bq_prev_entry(struct bquery *q)
{
	int rc = 0;

	rc = sos_iter_prev(q->itr);
	while (rc == ENOENT) {
		/* try again with the prev store */
		rc = __bq_prev_store(q);
		if (rc)
			goto out;
		rc = sos_iter_end(q->itr);
	}
out:
	return rc;
}

/**
 * Move the sos iterator to the next entry.
 */
static int __bq_next_entry(struct bquery *q)
{
	int rc = 0;
	if (!q->bsos) {
		q->sos_number = __get_max_sos_number(q->sos_prefix);
		rc = __bq_open_bsos(q);
		if (rc) {
			if (!q->sos_number)
				goto out;
			rc = __bq_next_store(q);
			if (rc)
				goto out;
		}
		/* first call */
		rc = __bq_first_entry(q);
		goto out;
	}

	rc = sos_iter_next(q->itr);
	while (rc == ENOENT) {
		/* try again with the next store */
		rc = __bq_next_store(q);
		if (rc)
			goto out;
		rc = sos_iter_begin(q->itr);
	}
out:
	return rc;
}

typedef enum bq_check_cond {
	BQ_CHECK_COND_OK    =  0x0,
	BQ_CHECK_COND_TS0   =  0x1,
	BQ_CHECK_COND_TS1   =  0x2,
	BQ_CHECK_COND_HST  =  0x4,
	BQ_CHECK_COND_PTN   =  0x8,
} bq_check_cond_e;

static inline
bq_check_cond_e bq_check_cond(struct bquery *q)
{
	uint32_t ts = bq_entry_get_sec(q);
	uint32_t comp_id = bq_entry_get_comp_id(q);
	uint32_t ptn_id = bq_entry_get_ptn_id(q);;

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

int bq_img_first_entry(struct bquery *q)
{
	struct bimgquery *imgq = (void*)q;
	int rc;
	uint32_t sec, comp_id, ptn_id;
	char buff[sizeof(struct bout_sos_img_key) + sizeof(struct obj_key)];
	obj_key_t obj_key = (void*)buff;
	struct bout_sos_img_key *bsi_key = (void*)obj_key->value;
	const struct bout_sos_img_key *cbsi_key;

	obj_key->len = sizeof(*bsi_key);
	bsi_key->sos_blob.len = sizeof(*bsi_key) - sizeof(bsi_key->sos_blob);

	if (q->bsos) {
		bsos_wrap_close_free(q->bsos);
	}
	q->sos_number = __get_max_sos_number(q->sos_prefix);
	rc = __bq_open_bsos(q);
	if (rc) {
		if (!q->sos_number)
			goto out;
		rc = __bq_next_store(q);
		if (rc)
			goto out;
	}

	bsi_key->comp_id = 0;
	bsi_key->ts = 0;
	bsi_key->ptn_id = 0;

	if (imgq->ptn_rng_itr) {
		brange_u32_iter_begin(imgq->ptn_rng_itr, &bsi_key->ptn_id);
	}

	if (imgq->hst_rng_itr) {
		brange_u32_iter_begin(imgq->hst_rng_itr, &bsi_key->comp_id);
	}

	if (q->ts_0) {
		bsi_key->ts = q->ts_0;
	}

	bout_sos_img_key_convert(bsi_key);

again:
	rc = sos_iter_seek_sup(q->itr, obj_key);

	if (rc) {
		if (rc != ENOENT)
			goto out;
		rc = __bq_next_store(q);
		if (rc)
			goto out;
		goto again;
	}

	cbsi_key = __bq_img_entry_get_key(imgq);

	if (bq_check_cond(q) != BQ_CHECK_COND_OK)
		rc = bq_next_entry(q);

out:
	return rc;
}

int bq_img_next_entry(struct bquery *q)
{
	struct bimgquery *imgq = (void*)q;
	const struct bout_sos_img_key *cbsi_key;
	char key_buff[sizeof(struct bout_sos_img_key) + sizeof(struct obj_key)];
	struct obj_key *obj_key = (void*)key_buff;
	struct bout_sos_img_key *bsi_key = (void*)obj_key->value;
	int rc;

	obj_key->len = sizeof(*bsi_key);
	bsi_key->sos_blob.len = sizeof(*bsi_key) - sizeof(bsi_key->sos_blob);

	rc = sos_iter_next(q->itr);

check:
	if (rc) {
		/* end of current sos store */
		goto next_store;
	}

	cbsi_key = __bq_img_entry_get_key(imgq);
	rc = bq_check_cond(q);
	switch (rc) {
	case BQ_CHECK_COND_OK:
		goto out;
	case BQ_CHECK_COND_TS0:
		bsi_key->ptn_id = bq_entry_get_ptn_id(q);
		bsi_key->ts = q->ts_0;
		bsi_key->comp_id = bq_entry_get_comp_id(q);
		break;
	case BQ_CHECK_COND_HST:
		bsi_key->ptn_id = bq_entry_get_ptn_id(q);
		bsi_key->ts = bq_entry_get_sec(q);
		bsi_key->comp_id = bq_entry_get_comp_id(q) + 1;
		rc = brange_u32_iter_fwd_seek(imgq->hst_rng_itr, &bsi_key->comp_id);
		if (rc == ENOENT) {
			/* use next timestamp */
			bsi_key->ts++;
			brange_u32_iter_begin(imgq->hst_rng_itr, &bsi_key->comp_id);
		}
		break;
	case BQ_CHECK_COND_TS1:
	case BQ_CHECK_COND_PTN:
		/* End of current PTN, continue with next PTN */
		bsi_key->ptn_id = bq_entry_get_ptn_id(q) + 1;
		rc = brange_u32_iter_fwd_seek(imgq->ptn_rng_itr, &bsi_key->ptn_id);
		if (rc == ENOENT) {
			goto next_store;
		}
		assert(rc == 0);
		brange_u32_iter_begin(imgq->hst_rng_itr, &bsi_key->comp_id);
		bsi_key->ts = q->ts_0;
		break;
	}

seek:
	bout_sos_img_key_convert(bsi_key);
	rc = sos_iter_seek_sup(q->itr, obj_key);
	goto check;

next_store:
	rc = __bq_next_store(q);
	if (rc)
		goto out;
	brange_u32_iter_begin(imgq->ptn_rng_itr, &bsi_key->ptn_id);
	brange_u32_iter_begin(imgq->hst_rng_itr, &bsi_key->comp_id);
	bsi_key->ts = q->ts_0;
	goto seek;

out:
	return rc;
}

int bq_img_prev_entry(struct bquery *q)
{
	struct bimgquery *imgq = (void*)q;
	berr("bimgquery only supports forward iterator (for now)");
	return ENOSYS;
}

int bq_img_last_entry(struct bquery *q)
{
	struct bimgquery *imgq = (void*)q;
	berr("bimgquery only supports forward iterator (for now)");
	return ENOSYS;
}

int bq_msg_next_entry(struct bquery *q)
{
	int rc;
	uint32_t sec, comp_id, ptn_id;
next:
	rc = __bq_next_entry(q);
	if (rc)
		return rc;
	sec = bq_entry_get_sec(q);
	if (q->ts_1 && q->ts_1 < sec)
		return ENOENT;
	comp_id = bq_entry_get_comp_id(q);
	if (q->hst_ids && !bset_u32_exist(q->hst_ids, comp_id))
		goto next;
	ptn_id = bq_entry_get_ptn_id(q);
	if (q->ptn_ids && !bset_u32_exist(q->ptn_ids, ptn_id))
		goto next;
	return 0;
}

int bq_next_entry(struct bquery *q)
{
	return q->next_entry(q);
}

int bq_msg_prev_entry(struct bquery *q)
{
	int rc;
	uint32_t sec, comp_id, ptn_id;
prev:
	rc = __bq_prev_entry(q);
	if (rc)
		return rc;
	sec = bq_entry_get_sec(q);
	if (q->ts_0 &&  sec < q->ts_0)
		return ENOENT;
	comp_id = bq_entry_get_comp_id(q);
	if (q->hst_ids && !bset_u32_exist(q->hst_ids, comp_id))
		goto prev;
	ptn_id = bq_entry_get_ptn_id(q);
	if (q->ptn_ids && !bset_u32_exist(q->ptn_ids, ptn_id))
		goto prev;
	return 0;
}

int bq_prev_entry(struct bquery *q)
{
	return q->prev_entry(q);
}

int bq_first_entry(struct bquery *q)
{
	return q->first_entry(q);
}

int bq_msg_first_entry(struct bquery *q)
{
	int rc;
	uint32_t sec, comp_id, ptn_id;
	if (q->bsos) {
		bsos_wrap_close_free(q->bsos);
	}
	q->sos_number = __get_max_sos_number(q->sos_prefix);
	rc = __bq_open_bsos(q);
	if (rc) {
		if (!q->sos_number)
			goto out;
		rc = __bq_next_store(q);
		if (rc)
			goto out;
	}

	rc = __bq_first_entry(q);
	if (rc)
		return rc;
loop:
	sec = bq_entry_get_sec(q);
	if (q->ts_1 && q->ts_1 < sec)
		return ENOENT;
	comp_id = bq_entry_get_comp_id(q);
	if (q->hst_ids && !bset_u32_exist(q->hst_ids, comp_id))
		goto next;
	ptn_id = bq_entry_get_ptn_id(q);
	if (q->ptn_ids && !bset_u32_exist(q->ptn_ids, ptn_id))
		goto next;
	/* good condition */
	goto out;
next:
	rc = __bq_next_entry(q);
	if (rc)
		return rc;
	goto loop;
out:
	return rc;
}

int bq_last_entry(struct bquery *q)
{
	return q->last_entry(q);
}

int bq_msg_last_entry(struct bquery *q)
{
	int rc;
	uint32_t sec, comp_id, ptn_id;
	if (q->bsos) {
		bsos_wrap_close_free(q->bsos);
	}
	q->sos_number = 0;
	rc = __bq_open_bsos(q);
	if (rc) {
		goto out;
	}

	rc = __bq_last_entry(q);
	if (rc)
		goto out;

loop:
	sec = bq_entry_get_sec(q);
	if (q->ts_0 &&  sec < q->ts_0)
		return ENOENT;
	comp_id = bq_entry_get_comp_id(q);
	if (q->hst_ids && !bset_u32_exist(q->hst_ids, comp_id))
		goto prev;
	ptn_id = bq_entry_get_ptn_id(q);
	if (q->ptn_ids && !bset_u32_exist(q->ptn_ids, ptn_id))
		goto prev;
	/* good condition */
	goto out;

prev:
	rc = __bq_prev_entry(q);
	if (rc)
		return rc;
	goto loop;

out:
	return rc;
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

const struct bmsg *bq_entry_get_msg(struct bquery *q)
{
	sos_blob_obj_t blob = sos_obj_attr_get(q->bsos->sos, SOS_MSG_MSG,
							sos_iter_obj(q->itr));
	return (void*)blob->data;
}

uint64_t bq_entry_get_ref(struct bquery *q)
{
	return sos_iter_ref(q->itr);
}

uint32_t bq_img_entry_get_count(struct bimgquery *q)
{
	return sos_obj_attr_get_uint32(q->base.bsos->sos, SOS_IMG_COUNT,
						sos_iter_obj(q->base.itr));
}

int bq_img_entry_get_pixel(struct bimgquery *q, struct bpixel *p)
{
	sos_obj_t obj = sos_iter_obj(q->base.itr);
	sos_t sos = q->base.bsos->sos;
	if (!obj)
		return EINVAL;
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
	rc = fmt_date_fmt(q->formatter, bdstr, bq_entry_get_sec(q));
	if (rc)
		goto out;
	bstr = btkn_store_get_bstr(q->store->cmp_store,
				bq_entry_get_comp_id(q) + BMAP_ID_BEGIN - 1);
	if (!bstr)  {
		rc = ENOENT;
		goto out;
	}
	rc = fmt_host_fmt(q->formatter, bdstr, bstr);
	if (rc)
		goto out;
	rc = bq_print_msg(q, bdstr, bq_entry_get_msg(q));
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

int bq_get_host_id(struct bq_store *store, const char *hostname)
{
	char buff[128];
	struct bstr *str = (void*)buff;
	bstr_set_cstr(str, (char*)hostname, 0);
	uint32_t id = bmap_get_id(store->cmp_store->map, str);
	if (id < BMAP_ID_BEGIN)
		return 0;
	return id - (BMAP_ID_BEGIN - 1);
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
							cmp_id + BMAP_ID_BEGIN);
	if (!cmp) {
		rc = ENOENT;
		goto out;
	}
	bdstr_reset(out);
	rc = bdstr_append_printf(out, "%.*s", cmp->blen, cmp->cstr);
out:
	return rc;
}

int bq_get_ptn(struct bquery *q, int ptn_id, struct bdstr *out)
{
	int rc = 0;
	uint32_t i;
	uint32_t n;
	struct bptn_store *ptn_store = q->store->ptn_store;
	struct btkn_store *tkn_store = q->store->tkn_store;
	const struct bstr *ptn = bptn_store_get_ptn(ptn_store, ptn_id);
	const struct bstr *tkn;
	struct btkn_attr attr;
	if (!ptn)
		return ENOENT;
	rc = bdstr_reset(out);
	if (rc)
		return rc;
	rc = fmt_ptn_prefix(q->formatter, out, ptn_id);
	if (rc)
		return rc;
	rc = fmt_tkn_begin(q->formatter, out);
	if (rc)
		return rc;
	n = ptn->blen / sizeof(*ptn->u32str);
	for (i = 0; i < n; i++) {
		attr = btkn_store_get_attr(tkn_store, ptn->u32str[i]);
		tkn = btkn_store_get_bstr(tkn_store, ptn->u32str[i]);
		assert(tkn);
		rc = fmt_tkn_fmt(q->formatter, out, tkn, &attr, ptn->u32str[i]);
		if (rc)
			return rc;
	}
	rc = fmt_tkn_end(q->formatter, out);
	if (rc)
		return rc;
	rc = fmt_ptn_suffix(q->formatter, out);
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
	snprintf(store->path + len, sz, "/img_store/*_sos.PG");
	rc = wordexp(store->path, &wexp, 0);
	if (rc) {
		berr("wordexp() error, rc: %d (%s:%d)", rc, __FILE__, __LINE__);
		goto out;
	}

	for (i = 0; i < wexp.we_wordc; i++) {
		const char *name = strrchr(wexp.we_wordv[i], '/') + 1;
		char *term = strrchr(name, '_');
		const char *dot;
		*term = 0;
		/* check format */
		sscanf(name, "%*d-%*d%n", &sz);
		if (sz != strlen(name))
			goto next;
		cb(name, ctxt);
	next:
		/* recover */
		*term = '_';
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

#ifdef BIN

enum BQ_TYPE {
	BQ_TYPE_UNKNOWN,
	BQ_TYPE_MSG,
	BQ_TYPE_PTN,
	BQ_TYPE_HOST,
	BQ_TYPE_IMG,
	BQ_TYPE_LIST_IMG,
	BQ_TYPE_LAST
};

const char *BQ_TYPE_STR[] = {
	[BQ_TYPE_UNKNOWN]   =  "BQ_TYPE_UNKNOWN",
	[BQ_TYPE_MSG]       =  "MSG",
	[BQ_TYPE_PTN]       =  "PTN",
	[BQ_TYPE_HOST]      =  "HOST",
	[BQ_TYPE_IMG]       =  "IMG",
	[BQ_TYPE_LIST_IMG]  =  "LIST_IMG",
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

int verbose = 0;

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
"QUERY_OPTIONS:\n"
"    --type,-t TYPE             The TYPE of the query, can be MSG, PTN or HOST.\n"
"                                 * PTN will list all log patterns with their\n"
"                                   pattern_ids. These pattern_ids are to be \n"
"                                   used in ptn_id-mask option when querying\n"
"                                   for MSG.\n"
"                                 * HOST will list all hostnames with their\n"
"                                   host_ids. These host_ids are to be used\n"
"                                   with host-mask option when querying for\n"
"                                   MSG.\n"
"                                 * MSG will query messages from the store.\n"
"                                   Users can give host-mask, begin, end, \n"
"                                   ptn_id-mask to filter the message query.\n"
"                                 * LIST_IMG will list all available image\n"
"                                   stores.\n"
"                                 * IMG will query image information from\n"
"                                   image store (specified by '-I' option).\n"
"                                   The pattern/host/time filtering conditions\n"
"                                   are also applied.\n"
"    --image-store-name,-I IMG_STORE_NAME\n"
"				The image store to query against.\n"
"    --host-mask,-H NUMBER,...	The comma-separated list of numbers of\n"
"				required hosts. The NUMBER can be in X-Y\n"
"				format. (example: -H 1-10,20,30-50)\n"
"				If --host-mask is not specified, all hosts\n"
"				are included in the query.\n"
"    --begin,-B T1		T1 is the beginning of the time window.\n"
"    --end,-E T2		T2 is the ending of the time window.\n"
"				If T1 is empty, bquery will obtain all data\n"
"				up until T2. Likewise, if T2 is empty, bquery\n"
"				obtains all data from T1 onward. Example:\n"
"				-B \"2012-01-01 00:00:00\" \n"
"				-E \"2012-12-31 23:59:59\" \n"
"				If --begin and --end are not specified,\n"
"				there is no time window condition.\n"
"    --ptn_id-mask,-P NUMBER,...\n"
"				The number format is similar to --host-mask\n"
"				option. The list of numbers specify\n"
"				Pattern IDs to be queried. If ptn_id-mask is\n"
"				is not specified, all patterns are included.\n"
"    --verbose,-v		Verbose mode. For '-t MSG', this will print\n"
"				[PTN_ID] before the actual message\n"
"\n"
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
char *short_opt = "hs:dr:x:p:t:H:B:E:P:vI:";
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
	{"verbose",           required_argument,  0,  'v'},
	{0,              0,                  0,  0}
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
	default:
		fprintf(stderr, "Unknown argument %s\n", argv[optind - 1]);
	}
	goto next_arg;
out:
	return;
}

int bq_local_msg_routine(struct bq_store *s)
{
	int rc = 0;
	struct bquery *q = bquery_create(s, hst_ids, ptn_ids, ts_begin, ts_end,
					 1, 0, &rc);
	struct bdstr *bdstr = bdstr_new(4096);
	const struct bmsg *bmsg;
	if (rc)
		goto out;
	rc = bq_first_entry(q);
loop:
	if (rc)
		goto out;
	if (verbose) {
		uint32_t ptn_id;
		sos_blob_obj_t blob = sos_obj_attr_get(q->bsos->sos,
					SOS_MSG_MSG, sos_iter_obj(q->itr));
		ptn_id = ((struct bmsg*)blob->data)->ptn_id;
		printf("[%d] ", ptn_id);
	}
	bmsg = bq_entry_get_msg(q);
	bdstr_reset(bdstr);
	bq_entry_print(q, bdstr);
	printf("%.*s\n", (int)bdstr->str_len, bdstr->str);
	rc = bq_next_entry(q);
	goto loop;
out:
	if (rc == ENOENT)
		rc = 0;
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

	rc = bq_first_entry(&imgq->base);

loop:
	if (rc)
		goto out;

	bq_img_entry_get_pixel(imgq, &p);
	printf("%u, %u, %u, %u\n", p.ptn_id, p.sec, p.comp_id, p.count);
	count++;

	rc = bq_next_entry(&imgq->base);
	goto loop;

out:
	if (count == 0) {
		bwarn("No data matching the query.");
	}
	bdebug("Matched pixels: %lu", count);
	if (rc == ENOENT)
		rc = 0;
	return 0;
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
	default:
		rc = EINVAL;
	}
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
	process_args(argc, argv);
	running_mode = bq_get_mode();
	switch (running_mode) {
	case BQ_MODE_LOCAL:
		bq_local_routine();
		break;
	case BQ_MODE_DAEMON:
		bq_daemon_routine();
		break;
	case BQ_MODE_REMOTE:
		bq_remote_routine();
		break;
	default:
		printf("Cannot determine the running mode from the "
				"arguments.\n");
	}
	return 0;
}
#endif /*BIN*/
