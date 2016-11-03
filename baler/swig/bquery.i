/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013,2015-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013,2015-2016 Sandia Corporation. All rights reserved.
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

PyObject *bmq_get_PyMessage(struct bmsgquery *q);

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
int bmq_set_pos(struct bmsgquery *q, const char *_pos);

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

PyObject *getPatterns(struct bq_store *store);
PyObject *getHosts(struct bq_store *s);

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
	if (!q) {
		printf("########### NULL!!!! ############\n");
		PyErr_Format(PyExc_ValueError, "parameter cannot be None");
		return EINVAL;
	}
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

int bmq_set_pos(struct bmsgquery *q, const char *_pos)
{
	int rc;
	struct bquery_pos pos;
	rc = bquery_pos_from_str(&pos, _pos);
	if (rc)
		goto err0;
	rc = bq_set_pos((void*)q, &pos);
	return rc;

err0:
	return rc;
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

PyObject *PyToken(struct bq_store *s, uint32_t tkn_id)
{
	int rc;
	const struct bstr *bstr = btkn_store_get_bstr(s->tkn_store, tkn_id);
	if (!bstr)
		return NULL;
	const struct btkn_attr attr = btkn_store_get_attr(s->tkn_store, tkn_id);
	PyObject *tuple = PyTuple_New(2); /* type, str */
	if (!tuple) {
		return NULL;
	}
	PyObject *type = PyInt_FromLong(attr.type);
	if (!type) {
		goto err;
	}
	rc = PyTuple_SetItem(tuple, 0, type);
	if (rc) {
		Py_DECREF(type);
		goto err;
	}
	PyObject *str = PyString_FromStringAndSize(bstr->cstr, bstr->blen);
	if (!str) {
		goto err;
	}
	rc = PyTuple_SetItem(tuple, 1, str);
	if (rc) {
		Py_DECREF(str);
		goto err;
	}
	return tuple;
err:
	Py_DECREF(tuple);
	return NULL;
}

PyObject *PyHost(struct bq_store *s, uint32_t comp_id)
{
	int rc;
	struct bdstr *bdstr = NULL;
	PyObject *str = NULL;

	bdstr = bdstr_new(128);
	if (!bdstr) {
		PyErr_NoMemory(); /* set enomem exception */
		goto cleanup;
	}
	rc = bq_get_cmp(s, comp_id, bdstr);
	if (rc) {
		PyErr_NoMemory(); /* set enomem exception */
		goto cleanup;
	}
	str = PyString_FromString(bdstr->str);
cleanup:
	if (bdstr)
		bdstr_free(bdstr);
	return str;
}

PyObject *PyTimeval(const struct timeval *tv)
{
	int rc;
	PyObject *obj = PyTuple_New(2);
	if (!obj)
		return NULL;

	PyObject *_sec = PyInt_FromLong(tv->tv_sec);
	if (!_sec)
		goto err;
	rc = PyTuple_SetItem(obj, 0, _sec);
	if (rc) {
		Py_DECREF(_sec);
		goto err;
	}

	PyObject *_usec = PyInt_FromLong(tv->tv_usec);
	if (!_usec)
		goto err;
	rc = PyTuple_SetItem(obj, 1, _usec);
	if (rc) {
		Py_DECREF(_usec);
		goto err;
	}
	return obj;

err:
	Py_DECREF(obj);
	return NULL;
}

PyObject *PyPattern(struct bq_store *s, uint32_t ptn_id)
{
	const struct bstr *bstr = bptn_store_get_ptn(s->ptn_store, ptn_id);
	int rc;
	if (!bstr)
		return NULL;
	const struct bptn_attrM *attrM = bptn_store_get_attrM(s->ptn_store, ptn_id);
	if (!attrM)
		return NULL;

	PyObject *tuple = PyTuple_New(5);
	if (!tuple)
		return NULL;

	/* ptn_id */
	PyObject *_ptn_id = PyInt_FromLong(ptn_id);
	if (!_ptn_id)
		goto err;
	rc = PyTuple_SetItem(tuple, 0, _ptn_id);
	if (rc) {
		Py_DECREF(_ptn_id);
		goto err;
	}

	/* count */
	PyObject *_count = PyInt_FromLong(attrM->count);
	if (!_count) {
		goto err;
	}
	rc = PyTuple_SetItem(tuple, 1, _count);
	if (rc) {
		Py_DECREF(_count);
		goto err;
	}

	/* first-seen */
	PyObject *tv0 = PyTimeval(&attrM->first_seen);
	if (!tv0)
		goto err;
	rc = PyTuple_SetItem(tuple, 2, tv0);
	if (rc) {
		Py_DECREF(tv0);
		goto err;
	}

	/* last-seen */
	PyObject *tv1 = PyTimeval(&attrM->last_seen);
	if (!tv1)
		goto err;
	rc = PyTuple_SetItem(tuple, 3, tv1);
	if (rc) {
		Py_DECREF(tv1);
		goto err;
	}

	/* tokens */
	PyObject *list = PyList_New(0);
	if (!list)
		goto err;
	int i;
	int len = bstr->blen/sizeof(uint32_t);
	for (i = 0; i < len; i++) {
		PyObject *tkn = PyToken(s, bstr->u32str[i]);
		rc = PyList_Append(list, tkn);
		if (rc) {
			Py_DECREF(tkn);
			goto err;
		}
	}
	rc = PyTuple_SetItem(tuple, 4, list);

	if (rc) {
		Py_DECREF(list);
		goto err;
	}

	return tuple;

err:
	Py_DECREF(tuple); /* tkns in `list` will be freed. */
	return NULL;
}

struct __bq_entry_msg_tkn_ctxt {
	struct bq_store *s;
	PyObject *tkns;
};

int __bq_entry_msg_tkn_cb(uint32_t tkn_id, void *_ctxt)
{
	struct __bq_entry_msg_tkn_ctxt *ctxt = _ctxt;
	struct bq_store *s = ctxt->s;
	PyObject *tkns = ctxt->tkns; /* PyList */
	int rc;

	PyObject *tkn = PyToken(s, tkn_id);
	if (!tkn)
		return ENOMEM;
	rc = PyList_Append(tkns, tkn);
	if (rc) {
		Py_DECREF(tkn);
		return rc;
	}
	return 0;
}

PyObject *bmq_get_PyMessage(struct bmsgquery *q)
{
	PyObject *pyMsg = PyTuple_New(5);
	PyObject *pyTv;
	PyObject *pyHost;
	PyObject *pyTkns; /* List of tokens */
	PyObject *pyPos; /* string encoding query iterator position */
	PyObject *pyPtnId; /* Int */

	int rc;

	struct timeval tv;
	uint32_t comp_id;
	struct bdstr *bdstr = NULL;


	bdstr = bdstr_new(128);
	if (!bdstr)
		goto err0;

	pyMsg = PyTuple_New(5);
	if (!pyMsg)
		goto err0;

	/* 0: timeval */
	tv.tv_sec = bq_entry_get_sec((void*)q);
	tv.tv_usec = bq_entry_get_usec((void*)q);
	pyTv = PyTimeval(&tv);
	if (!pyTv)
		goto err1;
	rc = PyTuple_SetItem(pyMsg, 0, pyTv);
	if (rc) {
		Py_DECREF(pyTv);
		goto err1;
	}

	/* 1: host */
	comp_id = bq_entry_get_comp_id((void*)q);
	pyHost = PyHost(q->base.store, comp_id);
	if (!pyHost)
		goto err1;
	rc = PyTuple_SetItem(pyMsg, 1, pyHost);
	if (rc) {
		Py_DECREF(pyHost);
		goto err1;
	}

	/* 2: tokens */
	pyTkns = PyList_New(0);
	if (!pyTkns)
		goto err1;
	struct __bq_entry_msg_tkn_ctxt ctxt = {q->base.store, pyTkns};
	rc = bq_entry_msg_tkn((void*)q, __bq_entry_msg_tkn_cb, &ctxt);
	if (rc) {
		Py_DECREF(pyTkns);
		goto err1;
	}
	rc = PyTuple_SetItem(pyMsg, 2, pyTkns);
	if (rc) {
		Py_DECREF(pyTkns);
		goto err1;
	}

	/* 3: position */
	struct bquery_pos pos;
	rc = bq_get_pos((void*)q, &pos);
	if (rc)
		goto err1;
	bdstr_reset(bdstr);
	rc = bquery_pos_print(&pos, bdstr);
	if (rc)
		goto err1;
	pyPos = PyString_FromString(bdstr->str);
	if (!pyPos)
		goto err1;
	rc = PyTuple_SetItem(pyMsg, 3, pyPos);
	if (rc) {
		Py_DECREF(pyPos);
		goto err1;
	}

	/* 4: Pattern ID */
	pyPtnId = PyInt_FromLong(bq_entry_get_ptn_id((void*)q));
	if (!pyPtnId)
		goto err1;
	rc = PyTuple_SetItem(pyMsg, 4, pyPtnId);
	if (rc) {
		Py_DECREF(pyPtnId);
		goto err1;
	}

	/* cleanup */
	bdstr_free(bdstr);

	return pyMsg;

err1:
	Py_DECREF(pyMsg);
err0:
	if (bdstr)
		bdstr_free(bdstr);
	return NULL;
}

PyObject *getPatterns(struct bq_store *s)
{
	uint32_t last_id = bptn_store_last_id(s->ptn_store);
	uint32_t first_id = bptn_store_first_id(s->ptn_store);
	uint32_t ptn_id;
	int rc;
	PyObject *array = PyList_New(0);
	if (!array) {
		return NULL;
	}
	for (ptn_id = first_id; ptn_id <= last_id; ptn_id++) {
		PyObject *ptn = PyPattern(s, ptn_id);
		if (!ptn)
			goto err;
		rc = PyList_Append(array, ptn);
		if (rc) {
			Py_DECREF(ptn);
			goto err;
		}
	}
	return array;
err:
	Py_DECREF(array);
	return NULL;
}

PyObject *getHosts(struct bq_store *s)
{
	int rc = 0;
	char buff[512];
	uint32_t id = BMAP_ID_BEGIN;
	uint32_t next_id = s->cmp_store->map->hdr->next_id;
	PyObject *dict = PyDict_New();
	PyObject *pyStr;
	PyObject *pyCompId;
	if (!dict)
		return NULL;
	while (id<next_id) {
		rc = btkn_store_id2str_esc(s->cmp_store, id, buff, sizeof(buff));
		if (rc)
			goto next;
		pyStr = PyString_FromString(buff);
		if (!pyStr)
			goto err1;
		pyCompId = PyInt_FromLong(bmapid2compid(id));
		if (!pyCompId)
			goto err2;
		rc = PyDict_SetItem(dict, pyCompId, pyStr);
		if (rc)
			goto err3;
	next:
		id++;
	}
	return dict;
err3:
	Py_DECREF(pyCompId);
err2:
	Py_DECREF(pyStr);
err1:
	Py_DECREF(dict);
err0:
	return NULL;
}

%}
