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

#include <unistd.h>
#include <inttypes.h>
#include <stdarg.h>
#include <getopt.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/un.h>
#include <ctype.h>
#include <netdb.h>
#include <dlfcn.h>
#include <assert.h>
#include <libgen.h>
#include <time.h>
#include <event2/thread.h>
#include "event.h"
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_xprt.h"
#include "config.h"

#include "ovis_rctrl/rctrl.h"
#include "ocm/ocm.h"

static char rctrl_replybuf[4096];

void __rctrl_recv_cb(rctrl_t ctrl)
{
	rctrl_replybuf[0] = '\0';
	struct ocm_cfg_buff *reply_msg = ocm_cfg_buff_new(1024 * 512, "");
	if (!reply_msg) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_config: Error %d: Failed to "
				"create a message sent back to the control.\n",
				ENOMEM);
		return;
	}
	char *buff = malloc(1024 * 256);
	if (!buff) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_config: Error %d: Failed "
				"to create a message sent back to the control.\n",
				ENOMEM);
		return;
	}

	struct attr_value_list *av_list = av_new(128);
	struct attr_value_list *kw_list = av_new(128);

	int ret = 0;
	const struct ocm_value *v;
	const char *_command;
	char *command = 0;
	struct ocm_value *ov;

	ocm_cfg_t cfg = ctrl->cfg;

	struct ocm_cfg_cmd_iter cmd_iter;
	ocm_cfg_cmd_iter_init(&cmd_iter, cfg);
	ocm_cfg_cmd_t cmd;
	while (0 == ocm_cfg_cmd_iter_next(&cmd_iter, &cmd)) {
		v = ocm_av_get_value(cmd, "cmd");
		if (!v) {
			ldmsd_log(LDMSD_LERROR, "ldmsd_config: The request "
					"is missing 'cmd'.\n");
			sprintf(rctrl_replybuf, "-%dThe request is missing "
					"'cmd'.\n", EINVAL);
			goto out;
		}
		_command = v->s.str;
		command = strdup(_command);
		v = ocm_av_get_value(cmd, "cmd_id");
		if (!v) {
			ldmsd_log(LDMSD_LERROR, "ldmsd_config: The request is "
					"missing cmd ID. '%s'\n", command);
			sprintf(rctrl_replybuf, "-%dThe request is missing cmd ID.\n",
					EINVAL);
			goto out;
		}
		long cmd_id = v->i32;
		ret = tokenize(command, kw_list, av_list);
		if (ret) {
			ldmsd_log(LDMSD_LERROR, "Memory allocation failure "
					"processing '%s'\n", command);
			sprintf(rctrl_replybuf, "-%dMemory allocation failure processing "
					"'%s'\n", ret, command);
			goto out;
		}

		if (cmd_id >= 0 && cmd_id <= LDMSCTL_LAST_COMMAND) {
			ret = cmd_table[cmd_id](rctrl_replybuf, av_list, kw_list);
			goto out;
		}
		sprintf(rctrl_replybuf, "-%dInvalid command Id %ld\n", 22, cmd_id);

out:
		if (command)
			free(command);

		ocm_cfg_buff_add_verb(reply_msg, "");
		memset(buff, 0, sizeof(buff));
		ov = (void *)buff;
		ocm_value_set_s(ov, (const char *)rctrl_replybuf);
		ocm_cfg_buff_add_av(reply_msg, "msg", ov);
	}
	rctrl_send_request(ctrl, reply_msg);
	free(av_list);
	free(kw_list);
	ocm_cfg_buff_free(reply_msg);
	free(buff);
}

void rctrl_recv_cb(enum rctrl_event ev, rctrl_t ctrl)
{
	int ret;
	switch (ev) {
	case RCTRL_EV_RECV_COMPLETE:
		__rctrl_recv_cb(ctrl);
		break;
	default:
		assert(0 == "Illegal rctrl event");
		break;
	}
}

int ldmsd_rctrl_init(const char *port, const char *secretword)
{
	int rc = 0;
	rctrl_t ctrl = rctrl_listener_setup("sock", port, rctrl_recv_cb,
						secretword, ldmsd_error_log);
	if (!ctrl)
		rc = errno;
	return rc;
}
