/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015-216 Sandia Corporation. All rights reserved.
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
 * \file bq_fmt_json.c
 * \brief Baler Query JSON formatter.
 * \note This code is compiled as a part of bhttpd.
 */
#include "query/bquery.h"
#include "baler/btypes.h"
#include "baler/butils.h"
#include "baler/bptn.h"
#include "bq_fmt_json.h"

#include <time.h>
#include <ctype.h>

struct bqfmt_json {
	struct bq_formatter base;
	int first_tkn;
	struct bq_store *bq_store;
	struct bquery *q;
	int (*ts_fmt)(struct bdstr *bdstr, const struct timeval *tv);
};

static
int __datetime_fmt(struct bdstr *bdstr, const struct timeval *tv)
{
	struct tm tm;
	char tmp[64];
	localtime_r(&tv->tv_sec, &tm);
	strftime(tmp, sizeof(tmp), "%F %T", &tm);
	return bdstr_append_printf(bdstr, "\"%s.%06d\"", tmp, tv->tv_usec);
}

static
int __ts_fmt(struct bdstr *bdstr, const struct timeval *tv)
{
	return bdstr_append_printf(bdstr, "{\"sec\": %d, \"usec\": %d}",
						tv->tv_sec, tv->tv_usec);
}

int __bqfmt_json_ptn_prefix(struct bq_formatter *fmt, struct bdstr *bdstr, uint32_t ptn_id)
{
	int rc = 0;
	char buff[32];
	struct bqfmt_json *fj = (void*)fmt;
	struct bqfmt_json *jfmt = (void*)fmt;
	const struct bptn_attrM *attrM =
		bptn_store_get_attrM(bq_get_ptn_store(jfmt->bq_store), ptn_id);
	static struct bptn_attrM _attrM = {0};

	if (!attrM) {
		/*
		 * The pattern inserted by a slave and its messages had never
		 * locally appeared on master will not have attrM object.
		 */
		attrM = &_attrM;
	}

	rc = bdstr_append_printf(bdstr,
			"{ \"type\": \"PTN\", "
			"\"ptn_id\": %d, "
			"\"count\": %d, "
			"\"first_seen\": "
					, ptn_id, attrM->count);
	if (rc)
		return rc;

	rc = jfmt->ts_fmt(bdstr, &attrM->first_seen);
	if (rc)
		return rc;

	rc = bdstr_append_printf(bdstr, ", \"last_seen\": ");
	if (rc)
		return rc;

	rc = jfmt->ts_fmt(bdstr, &attrM->last_seen);
	if (rc)
		return rc;
	return 0;
}

int __bqfmt_json_ptn_suffix(struct bq_formatter *fmt, struct bdstr *bdstr)
{
	return bdstr_append(bdstr, "}");
}

int __bqfmt_json_msg_prefix(struct bq_formatter *fmt, struct bdstr *bdstr)
{
	int rc;
	struct bqfmt_json *f = (void*)fmt;
	uint32_t ptn_id = bq_entry_get_ptn_id(f->q);
	bq_msg_ref_t msg_ref = bq_entry_get_ref(f->q);
	BQUERY_POS(pos);

	rc = bq_get_pos(f->q, pos);
	if (rc)
		return rc;

	rc = bdstr_append_printf(bdstr, "{ \"type\": \"MSG\", "
			"\"ref\": \"0x%lx%016lx\", \"ptn_id\": %lu",
			msg_ref.ref[0], msg_ref.ref[1], ptn_id);
	if (rc)
		return rc;
	rc = bdstr_append_printf(bdstr, ", \"pos\": \"");
	if (rc)
		return rc;
	rc = bquery_pos_print(pos, bdstr);
	if (rc)
		return rc;
	rc = bdstr_append_printf(bdstr, "\"");
	return rc;
}

int __bqfmt_json_msg_suffix(struct bq_formatter *fmt, struct bdstr *bdstr)
{
	return bdstr_append(bdstr, "}");
}

int __bqfmt_json_tkn_begin(struct bq_formatter *fmt, struct bdstr *bdstr)
{
	((struct bqfmt_json*)fmt)->first_tkn = 1;
	return bdstr_append(bdstr, ", \"msg\": [");
}

int __bqfmt_json_tkn_fmt(struct bq_formatter *fmt, struct bdstr *bdstr,
		const struct bstr *bstr, struct btkn_attr *attr,
		uint32_t tkn_id)
{
	static const char __map[256] = {
		['\\'] = '\\',
		['"'] = '"',
		['/'] = '/',
		['\b'] = 'b',
		['\f'] = 'f',
		['\n'] = 'n',
		['\r'] = 'r',
		['\t'] = 't',
	};
	int rc;
	char buff[128];
	const int lim = sizeof(buff) - 2;
	int i, j;

	if (((struct bqfmt_json*)fmt)->first_tkn) {
		((struct bqfmt_json*)fmt)->first_tkn = 0;
	} else {
		rc = bdstr_append(bdstr, ", ");
		if (rc)
			return rc;
	}
	rc = bdstr_append_printf(bdstr, "{\"tok_type\": \"%s\", "
			"\"text\": \"", btkn_attr_type_str(attr->type));
	if (rc)
		return rc;
	i = 0;
loop:
	j = 0;
	while (j < lim && i < bstr->blen) {
		switch (bstr->cstr[i]) {
		case '\\':
		case '"':
		case '/':
		case '\b':
		case '\f':
		case '\n':
		case '\r':
		case '\t':
			/* These are the characters that need to be escaped. */
			buff[j++] = '\\';
			buff[j++] = __map[bstr->cstr[i++]];
			break;
		default:
			/* skip control characters */
			if (!iscntrl(bstr->cstr[i]))
				buff[j++] = bstr->cstr[i];
			i++;
		}
	}
	rc = bdstr_append_printf(bdstr, "%.*s", j, buff);
	if (rc)
		return rc;
	if (i < bstr->blen)
		goto loop;
	rc = bdstr_append_printf(bdstr, "\"}");
	return rc;
}

int __bqfmt_json_tkn_end(struct bq_formatter *fmt, struct bdstr *bdstr)
{
	return bdstr_append(bdstr, "]");
}

int __bqfmt_json_date_fmt(struct bq_formatter *fmt, struct bdstr *bdstr,
		const struct timeval *tv)
{
	int rc = bdstr_append_printf(bdstr, ", \"ts\": ");
	if (rc)
		return rc;
	return ((struct bqfmt_json *)fmt)->ts_fmt(bdstr, tv);
}

int __bqfmt_json_host_fmt(struct bq_formatter *fmt, struct bdstr *bdstr,
		const struct bstr *bstr)
{
	int rc;
	rc = bdstr_append(bdstr, ", \"host\": \"");
	if (rc)
		return ENOMEM;
	rc = bdstr_append_bstr(bdstr, bstr);
	if (rc)
		return ENOMEM;
	rc = bdstr_append(bdstr, "\"");
	return rc;
}

void bqfmt_json_ts_use_ts(struct bq_formatter *fmt)
{
	struct bqfmt_json *f = (void*)fmt;
	f->ts_fmt = __ts_fmt;
}

void bqfmt_json_ts_use_datetime(struct bq_formatter *fmt)
{
	struct bqfmt_json *f = (void*)fmt;
	f->ts_fmt = __datetime_fmt;
}

struct bq_formatter *bqfmt_json_new(struct bq_store *bq_store, struct bquery *q)
{
	struct bqfmt_json *fmt = calloc(1, sizeof(*fmt));
	if (!fmt)
		return NULL;
	fmt->base.ptn_prefix = __bqfmt_json_ptn_prefix;
	fmt->base.ptn_suffix = __bqfmt_json_ptn_suffix;
	fmt->base.msg_prefix = __bqfmt_json_msg_prefix;
	fmt->base.msg_suffix = __bqfmt_json_msg_suffix;
	fmt->base.tkn_begin = __bqfmt_json_tkn_begin;
	fmt->base.tkn_fmt = __bqfmt_json_tkn_fmt;
	fmt->base.tkn_end = __bqfmt_json_tkn_end;
	fmt->base.date_fmt = __bqfmt_json_date_fmt;
	fmt->base.host_fmt = __bqfmt_json_host_fmt;
	fmt->bq_store = bq_store;
	fmt->q = q;
	fmt->ts_fmt = __ts_fmt;
	return &fmt->base;
}

void bqfmt_json_free(struct bq_formatter *fmt)
{
	free(fmt);
}
