/*
 * Copyright (c) 2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
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

#include <limits.h>
#include <string.h>
#include <dirent.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ibnet_data.h"
#include <pthread.h>

#define _GNU_SOURCE

/** ibnet sampler
 * This sampler produces sets per-port of IB lid/port data and if requested
 * an overall timing set.
 * The schemas are named to be automatically unique from a base name
 * the user can override with the schema= option.
 */

static ldmsd_msg_log_f msg_log;
static pthread_mutex_t only_lock = PTHREAD_MUTEX_INITIALIZER;
static struct ibnet_data *only = NULL;
static char *usage = NULL;

static int config_ibnet(struct ldmsd_plugin *self,
		  struct attr_value_list *kwl, struct attr_value_list *avl)
{
	pthread_mutex_lock(&only_lock);
	int rc = 0;
	errno = 0;
	if (only)
		ibnet_data_delete(only);
	else
		only = ibnet_data_new(msg_log, avl, kwl);

	if (!only) {
		msg_log(LDMSD_LDEBUG, SAMP" config() called, error returned %d.\n", errno);
		rc = 1;
	}  else {
		rc = 0;
	}
	pthread_mutex_unlock(&only_lock);
	return rc;
}

static int sample_ibnet(struct ldmsd_sampler *self)
{
	pthread_mutex_lock(&only_lock);
	struct ibnet_data *inst = only;

	if (!inst) {
		msg_log(LDMSD_LERROR, SAMP " not properly configured.\n");
	} else {
		ibnet_data_sample(inst);
	}

	pthread_mutex_unlock(&only_lock);
	return 0;
}

static void term_ibnet(struct ldmsd_plugin *self)
{
	pthread_mutex_lock(&only_lock);
	msg_log(LDMSD_LDEBUG, SAMP " term() called\n");
	ibnet_data_delete(only);
	only = NULL;
	if (usage) {
		free(usage);
		usage = NULL;
	}
	pthread_mutex_unlock(&only_lock);
}

static ldms_set_t get_set_ibnet(struct ldmsd_sampler *self)
{
	return NULL;
}

static const char *usage_ibnet(struct ldmsd_plugin *self)
{
	msg_log(LDMSD_LDEBUG, SAMP " usage() called\n");
	if (!usage)
		usage = ibnet_data_usage();
	return usage;
}

static struct ldmsd_sampler ibnet_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term_ibnet,
		.config = config_ibnet,
		.usage = usage_ibnet,
	},
	.get_set = get_set_ibnet,
	.sample = sample_ibnet,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msg_log = pf;
	msg_log(LDMSD_LDEBUG, SAMP" get_plugin() called\n");
	return &ibnet_plugin.base;
}
