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
 * \file bhttpd_list.c
 */

#include "bhttpd.h"
#include "bq_fmt_json.h"

#include "query/bquery.h"

#include <wordexp.h>

static
void bhttpd_handle_list(struct bhttpd_req_ctxt *ctxt)
{
	wordexp_t p;
	int first = 1;
	int rc, i, len;
	const char *str;
	struct bdstr *bdstr = bdstr_new(512);
	if (!bdstr) {
		bhttpd_req_ctxt_errprintf(ctxt, HTTP_INTERNAL,
				"Not enough memory.");
		goto cleanup;
	}
	bdstr_append_printf(bdstr, "%s/img_store/*_sos.OBJ", store_path);
	evbuffer_add_printf(ctxt->evbuffer, "{\"img_stores\": [");
	rc = wordexp(bdstr->str, &p, 0);
	if (rc)
		goto end;
	for (i = 0; i < p.we_wordc; i++) {
		if (first)
			first = 0;
		else
			evbuffer_add_printf(ctxt->evbuffer, ", ");
		str = strrchr(p.we_wordv[i], '/') + 1;
		len = strstr(str, "_sos.OBJ") - str;
		evbuffer_add_printf(ctxt->evbuffer, "\"%.*s\"", len, str);
	}
end:
	evbuffer_add_printf(ctxt->evbuffer, "]}");
cleanup:
	bdstr_free(bdstr);
}

static __attribute__((constructor))
void __init()
{
	set_uri_handle("/list_img_store", bhttpd_handle_list);
}
