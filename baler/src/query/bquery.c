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
#include <getopt.h>
#include <errno.h>

#include <time.h>
#include <dirent.h>

/********** Global Variables **********/
/**
 * \param[in] num_list List of numbers (e.g. "1,2,5-7")
 * \param[out] _set Pointer to pointer to ::bset_u32. \c (*_set) will point to
 * 	the newly created ::bset_u32.
 * \returns 0 on success.
 * \returns Error code on error.
 */
int strnumlist2set(const char *num_lst, struct bset_u32 **_set)
{
	int rc = 0;
	char *buff = strdup(num_lst);
	if (!buff) {
		rc = errno;
		goto err0;
	}
	struct bset_u32 *set = bset_u32_alloc(MASK_HSIZE);
	if (!set) {
		rc = errno;
		goto err1;
	}
	*_set = set;
	char *tok = strtok(buff, ",");
	while (tok) {
		int a, b, i;
		a = atoi(tok);
		b = a;
		char *tok2 = strchr(tok, '-');
		if (tok2)
			b = atoi(tok2 + 1);
		for (i=a; i<=b; i++) {
			int _rc;
			_rc = bset_u32_insert(set, i);
			if (_rc && _rc != EEXIST) {
				rc = _rc;
				goto err2;
			}
		}
		tok = strtok(NULL, ",");
	}
	return 0;
err2:
	bset_u32_free(set);
err1:
	free(buff);
err0:
	return rc;
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
	s->path = strdup(path);
	char spath[PATH_MAX];
	struct stat st;
	int rc;

	/* comp_store */
	sprintf(spath, "%s/comp_store", path);
	rc = stat(spath, &st);
	if (rc || !S_ISDIR(st.st_mode)) {
		berr("Invalid store '%s': comp_store subdirectory does"
				" not exist", path);
		goto err0;
	}
	s->cmp_store = btkn_store_open(spath);
	if (!s->cmp_store)
		goto err0;

	/* tkn_store */
	sprintf(spath, "%s/tkn_store", path);
	rc = stat(spath, &st);
	if (rc || !S_ISDIR(st.st_mode)) {
		berr("Invalid store '%s': tkn_store subdirectory does"
				" not exist", path);
		goto err1;
	}
	s->tkn_store = btkn_store_open(spath);
	if (!s->tkn_store)
		goto err1;

	/* ptn_store */
	sprintf(spath, "%s/ptn_store", path);
	rc = stat(spath, &st);
	if (rc || !S_ISDIR(st.st_mode)) {
		berr("Invalid store '%s': ptn_store subdirectory does"
				" not exist", path);
		goto err2;
	}
	s->ptn_store = bptn_store_open(spath);
	if (!s->ptn_store)
		goto err2;

	/* msg_store and img_store will be opened at query time */

out:
	return s;

err2:
	btkn_store_close_free(s->tkn_store);
err1:
	btkn_store_close_free(s->cmp_store);
err0:
	berr("Cannot open %s", spath);
	free(s);
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
		if (rc)
			return rc;
		printf("%8d %s\n", id, buff);
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
int bq_print_msg(struct bq_store *s,  char *buff, int buff_len,
		 struct bmsg *msg)
{
	int rc = 0;
	const struct bstr *ptn;
	struct bptn_store *ptn_store = s->ptn_store;
	struct btkn_store *tkn_store = s->tkn_store;
	ptn = bmap_get_bstr(ptn_store->map, msg->ptn_id);
	if (!ptn)
		return ENOENT;
	uint32_t *msg_arg = msg->argv;
	const uint32_t *ptn_tkn = ptn->u32str;
	int len = ptn->blen;
	int blen;
	while (len) {
		uint32_t tkn_id = *ptn_tkn++;
		if (tkn_id == BMAP_ID_STAR)
			tkn_id = *msg_arg++;
		rc = btkn_store_id2str(tkn_store, tkn_id, buff, buff_len);
		if (rc)
			goto out;
		len -= sizeof(uint32_t);
		blen = strlen(buff);
		buff += blen;
		buff_len -= blen;
	}
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
	while (q->hst_rngs && (r = LIST_FIRST(q->hst_rngs))) {
		LIST_REMOVE(r, link);
		free(r);
	}
	free(q->hst_rngs);
	bquery_destroy((void*)q);
}

struct bquery* bquery_create(struct bq_store *store, const char *hst_ids,
			     const char *ptn_ids, const char *ts0,
			     const char *ts1, int is_text, char sep, int *rc)
{
	int _rc = 0;
	ssize_t len = 0;
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

	if (hst_ids) {
		_rc = strnumlist2set(hst_ids, &q->hst_ids);
		if (_rc)
			goto err;
	}

	if (ptn_ids) {
		_rc = strnumlist2set(ptn_ids, &q->ptn_ids);
		if (_rc)
			goto err;
	}

	struct tm tm;
	char *ts_ret;
	if (ts0) {
		bzero(&tm, sizeof(tm));
		ts_ret = strptime(ts0, "%F %T", &tm);
		if (ts_ret == NULL) {
			_rc = EINVAL;
			goto err;
		}
		tm.tm_isdst = -1;
		q->ts_0 = mktime(&tm);
	}

	if (ts1) {
		bzero(&tm, sizeof(tm));
		ts_ret = strptime(ts1, "%F %T", &tm);
		if (ts_ret == NULL) {
			_rc = EINVAL;
			goto err;
		}
		tm.tm_isdst = -1;
		q->ts_1 = mktime(&tm);
	}

	q->text_flag = is_text;
        q->sep = (sep)?(sep):(' ');

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

struct bimgquery* bimgquery_create(struct bq_store *store, const char *hst_ids,
				const char *ptn_ids, const char *ts0,
				const char *ts1, const char *img_store_name,
				int *rc)
{
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
	len = snprintf(bi->base.sos_prefix, PATH_MAX, "%s/img_store/%s",
						store->path, img_store_name);
	bi->base.sos_prefix_end = bi->base.sos_prefix + len;
	bi->store_name = strdup(img_store_name);
	if (!bi->store_name)
		goto err;
	if (bi->base.hst_ids) {
		bi->hst_rngs = bset_u32_to_brange_u32(bi->base.hst_ids);
		if (!bi->hst_rngs)
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

char* bq_query_generic(void* q, int *rc, int buffsiz, int(fn)(void*,char*,int))
{
	int _rc = 0;
	char *buff = malloc(buffsiz); /* 1024 characters should be enough for a
				    * message */
	if (!buff) {
		_rc = ENOMEM;
		goto out;
	}

	_rc = fn(q, buff, buffsiz);

out:
	if (_rc) {
		free(buff);
		buff = NULL;
	}
	if (rc)
		*rc = _rc;
	return buff;
}

char* bq_query(struct bquery *q, int *rc)
{
	return bq_query_generic(q, rc, 1024, (void*)bq_query_r);
}

char* bq_imgquery(struct bimgquery *q, int *rc)
{
	return bq_query_generic(q, rc, 64, (void*)bq_imgquery_r);
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

static int __bq_next_store(struct bquery *q)
{
	/* destroy the old store's itr first */
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
	struct bout_sos_img_key imgkey = {q->ts_0, 0};
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
 * Move the iterator to the next entry.
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

int bq_query_r(struct bquery *q, char *buff, size_t bufsz)
{
	int rc = 0;
	uint32_t comp_id;
	uint32_t sec;
	uint32_t usec;
	struct bmsg *msg;
	sos_obj_t obj;
	int len;
	sos_t msg_sos;

	buff[0] = 0;

next:
	rc = __bq_next_entry(q);
	if (rc)
		goto out;
	obj = sos_iter_obj(q->itr);
	msg_sos = q->bsos->sos;
	SOS_OBJ_ATTR_GET(sec, msg_sos, SOS_MSG_SEC, obj);
	if (q->ts_1 && sec > q->ts_1) {
		/* Out of time window, no need to continue */
		rc = ENOENT;
		goto out;
	}
	SOS_OBJ_ATTR_GET(usec, msg_sos, SOS_MSG_USEC, obj);
	SOS_OBJ_ATTR_GET(comp_id, msg_sos, SOS_MSG_COMP_ID, obj);
	if (q->hst_ids && !bset_u32_exist(q->hst_ids, comp_id))
		goto next;
	sos_blob_obj_t blob = sos_obj_attr_get(msg_sos, SOS_MSG_MSG, obj);
	msg = blob->data;
	if (q->ptn_ids && !bset_u32_exist(q->ptn_ids, msg->ptn_id))
		goto next;
	if (q->text_flag) {
		struct tm tm;
		time_t t = sec;
		localtime_r(&t, &tm);
		strftime(buff, 64, "%Y-%m-%d %T", &tm);
		len = strlen(buff);
		const struct bstr *bstr = bmap_get_bstr(
						q->store->cmp_store->map,
						comp_id + BMAP_ID_BEGIN - 1);
		if (bstr)
			len += sprintf(buff+len, ".%06d%c%.*s%c", usec, q->sep,
                                        bstr->blen, bstr->cstr, q->sep);
		else
                        len += sprintf(buff+len, ".%06d%cNULL%c", usec, q->sep,
                                        q->sep);
	} else {
                len = sprintf(buff, "%u.%06u%c%u%c", sec, usec, q->sep, comp_id,
                                q->sep);
	}
	rc = bq_print_msg(q->store, buff+len, bufsz-len, msg);
out:
	return rc;
}

int bq_imgquery_r(struct bimgquery *q, char *buff, size_t bufsz)
{
	int rc = 0;
	struct bquery *_q = (void*)&q->base;

	buff[0] = 0;

	struct bout_sos_img_key k;
	uint32_t comp_id;
	uint32_t sec;
	uint32_t usec;
	uint32_t ptn_id;
	uint32_t count;
	struct bmsg *msg;
	sos_obj_t obj;
	sos_t img_sos;
	int len;
next:
	rc = __bq_next_entry(_q);
	if (rc)
		goto out;
	img_sos = _q->bsos->sos;
	obj = sos_iter_obj(_q->itr);
	SOS_OBJ_ATTR_GET(k, img_sos, 0, obj);
	sec = k.ts;
	comp_id = k.comp_id;
	if (_q->ts_1 && sec > _q->ts_1) {
		/* Out of time window, no need to continue */
		rc = ENOENT;
		goto out;
	}
	if (_q->hst_ids && !bset_u32_exist(_q->hst_ids, comp_id))
		goto next;
	SOS_OBJ_ATTR_GET(ptn_id, img_sos, 1, obj);
	if (_q->ptn_ids && !bset_u32_exist(_q->ptn_ids, ptn_id))
		goto next;
	SOS_OBJ_ATTR_GET(count, img_sos, 2, obj);
	len = sprintf(buff, "%u %u %u %u", sec, comp_id, ptn_id, count);

out:
	return rc;
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

char* bq_get_ptns(struct bq_store *s)
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

int bq_get_ptns_r(struct bq_store *s, char *buf, size_t buflen)
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
	char *str = bdstr->str;
	free(bdstr);
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

#ifdef BIN

enum BQ_TYPE {
	BQ_TYPE_UNKNOWN,
	BQ_TYPE_MSG,
	BQ_TYPE_PTN,
	BQ_TYPE_HOST,
	BQ_TYPE_LAST
};

const char *BQ_TYPE_STR[] = {
	[BQ_TYPE_UNKNOWN]  =  "BQ_TYPE_UNKNOWN",
	[BQ_TYPE_MSG]      =  "MSG",
	[BQ_TYPE_PTN]      =  "PTN",
	[BQ_TYPE_HOST]     =  "HOST",
	[BQ_TYPE_LAST]     =  "BQ_TYPE_LAST"
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

struct btkn_store *tkn_store = NULL;
struct btkn_store *comp_store = NULL;
struct bptn_store *ptn_store = NULL;

enum {
	BQ_MODE_INVAL = -1, /* invalid mode */
	BQ_MODE_LOCAL,   /* locally query once */
	BQ_MODE_DAEMON,  /* daemon mode */
	BQ_MODE_REMOTE,  /* remotely query once */
} running_mode = BQ_MODE_LOCAL;

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
char *short_opt = "hs:dr:x:p:t:H:B:E:P:";
struct option long_opt[] = {
	{"help",         no_argument,        0,  'h'},
	{"store-path",   required_argument,  0,  's'},
	{"daemon",       no_argument,        0,  'd'},
	{"remote-host",  required_argument,  0,  'r'},
	{"xprt",         required_argument,  0,  'x'},
	{"port",         required_argument,  0,  'p'},
	{"type",         required_argument,  0,  't'},
	{"host-mask",    required_argument,  0,  'H'},
	{"begin",        required_argument,  0,  'B'},
	{"end",          required_argument,  0,  'E'},
	{"ptn_id-mask",  required_argument,  0,  'P'},
#if 0
	/* This part of code shall be enabled when image query support is
	 * available */
	{"image",        required_argument,  0,  'I'},
	{"message",      required_argument,  0,  'M'},
#endif
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
	const static int N = 4096;
	char buff[N];
	if (rc)
		goto out;
loop:
	rc = bq_query_r(q, buff, N);
	if (rc)
		goto out;
	printf("%s\n", buff);
	goto loop;
out:
	if (rc == ENOENT)
		rc = 0;
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

