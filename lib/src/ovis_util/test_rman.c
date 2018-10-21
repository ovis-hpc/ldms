/**
 * Copyright (c) 2015-2017 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015-2017 Sandia Corporation. All rights reserved.
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
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <rmaninfo.h>

struct priv {
	int tnext;
	int tinc;
};

// this assumes data allocation is managed by registrant.
int rim_update_test(struct resource_info *self, enum rim_task t, void * tinfo)
{
	struct priv *data;
	struct attr_value_list *config_args;
	switch (t) {
	case rim_init:
		printf("rim_init\n");
		config_args = tinfo;
		char *value = av_value(config_args, "interval");
		if (!value)
			return EINVAL;
		int delay = atoi(value);
		((struct priv*)self->data)->tnext = 0;
		((struct priv*)self->data)->tinc = delay;
		self->v.i64 = 0;
		return 0;
	case rim_update: {
		struct timespec *tp = tinfo;
		printf("rim_update(%d)\n",(int)tp->tv_sec);
		data = self->data;
		if (!self->generation || ! data->tnext) {
			data->tnext = tp->tv_sec;
		}
		if (tp->tv_sec < data->tnext)
			return 0;
		self->v.i64 += 10;
		data->tnext += data->tinc;
		self->generation++;
		return 0;
	}
	case rim_final:
		printf("rim_final\n");
		self->data = NULL;
		break;
	default:
		return EINVAL;
	}
	return 0;
}

int main(int argc, char **argv)
{

	struct attr_value_list *av_list, *kw_list;
	av_list = av_new(128);
	kw_list = av_new(128);
	char *s=strdup("interval=3");
	int rc = tokenize(s, kw_list, av_list);

	if (rc) {
		printf("failed tokenize\n");
		free(s);
		exit(EINVAL);
	}

	int i = 0;
	resource_info_manager rim = create_resource_info_manager();
	if (!rim)
		exit(ENOMEM);
	struct priv *dp, p;
	dp = &p;
	int err;
	err = register_resource_info(rim, "test", "node", av_list, rim_update_test, dp);
	if (err)
		exit(EINVAL);

	struct resource_info *testri = get_resource_info(rim, "test");
	if (!testri)
		exit(EINVAL);

	clear_resource_info_manager(rim);

	while (i<12) {
		int err =  update_resource_info(testri);
		if (err) {
			return err;
		}
		printf("%d %" PRIu64 " %" PRId64 "\n", p.tnext,
			testri->generation, testri->v.i64);
		sleep(1);
		i++;
	}

	release_resource_info(testri);

	free(s);
	av_free(av_list);
	av_free(kw_list);

	return 0;
}
