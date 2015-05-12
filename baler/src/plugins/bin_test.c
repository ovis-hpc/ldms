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
 * \file bin_test.c
 * \author Narate Taerat (narate at ogc dot us)
 *
 * \brief bin_test plugin, for basic plugin testing.
 *
 * This plugin does nothing but feed some number of input to the main balerd.
 * The number of input entries to feed can be configured (see plugin_config()
 * below).
 */
#include <assert.h>

#include "baler/binput.h"
#include "baler/butils.h"

struct bin_test_context {
	pthread_t thrd;
	int n;
};

static
int plugin_config(struct bplugin *this, struct bpair_str_head *arg_head)
{
	struct bpair_str *bpstr;
	struct bin_test_context *ctxt = this->context;
	int n;
	bpstr = bpair_str_search(arg_head, "n", NULL);
	if (bpstr) {
		n = atoi(bpstr->s1);
	} else {
		n = 1000000;
	}
	ctxt->n = n; /* abuse */
	return 0;
}

static
void *plugin_proc(void *arg)
{
	struct bplugin *this = arg;
	struct bin_test_context *ctxt = this->context;
	const char *test_tokens[] = {
		"This", " ", "is", " ", "a", " ", "test", "."
	};
	const char *test_host = "test_host";
	int i, j, m, len;
	m = sizeof(test_tokens)/sizeof(test_tokens[0]);
	for (i = 0; i < ctxt->n; i++) {
		struct bwq_entry *ent = calloc(1, sizeof(*ent));
		assert(ent);
		struct bstr_list_head *head = &ent->data.in.tokens;
		struct bstr_list_entry *tail = NULL;
		struct bstr_list_entry *lent;
		for (j = 0; j < m; j++) {
			len = strlen(test_tokens[j]);
			lent = bstr_list_entry_alloci(len, test_tokens[j]);
			assert(lent);
			if (tail) {
				LIST_INSERT_AFTER(tail, lent, link);
			} else {
				LIST_INSERT_HEAD(head, lent, link);
			}
			tail = lent;
		}
		ent->data.in.tok_count = m;
		ent->data.in.hostname = bstr_alloc_init_cstr(test_host);
		assert(ent->data.in.hostname);
		ent->data.in.type = BINQ_DATA_MSG;
		ent->data.in.tv.tv_sec = 1431388800;
		ent->data.in.tv.tv_usec = 123456;
		binq_post(ent);
	}
	binfo("bin_test: FINISHED");
	return 0;
}

static
int plugin_start(struct bplugin *this)
{
	struct bin_test_context *ctxt = this->context;
	return pthread_create(&ctxt->thrd, NULL, plugin_proc, this);
}

static
int plugin_stop(struct bplugin *this)
{
	struct bin_test_context *ctxt = this->context;
	pthread_cancel(ctxt->thrd);
	return 0;
}

static
int plugin_free(struct bplugin *this)
{
	free(this->context);
	free(this);
	return 0;
}

struct bplugin* create_plugin_instance()
{
	struct bplugin *p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;
	struct bin_test_context *ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt) {
		free(p);
		return NULL;
	}

	p->name = strdup("rsyslog_tcp");
	p->version = strdup("0.1a");
	p->config = plugin_config;
	p->start = plugin_start;
	p->stop = plugin_stop;
	p->free = plugin_free;
	p->context = ctxt;
	return p;
}
