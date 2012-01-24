/*
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
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
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
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
 *
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */
#ifndef __LDMSD_H__
#define __LDMSD_H__

#include <sys/queue.h>

struct hostspec;
struct hostset
{
	struct hostspec *host;
	ldms_set_t set;
	LIST_ENTRY(hostset) entry;
};

struct hostspec
{
	struct sockaddr_in sin;	/* host address */
	char *hostname;		/* host name */
	char *xprt_name;	/* transport name */
	int interval;		/* sample interval */
	enum {
		ACTIVE, PASSIVE, BRIDGING
	} type;
	ldms_t x;		/* !0 when valid and connected */
	pthread_mutex_t lock;
	LIST_HEAD(set_list, hostset) set_list;
	LIST_ENTRY(hostspec) link;
};

extern char *skip_space(char *s);
extern int parse_cfg(const char *config_file);
extern struct hostspec *host_first(void);
extern struct hostspec *host_next(struct hostspec *hs);

#define LDMS_MAX_PLUGIN_NAME_LEN 64
#define LDMS_MAX_CONFIG_STR_LEN 256
struct ldms_plugin {
	char name[LDMS_MAX_PLUGIN_NAME_LEN];
	int (*config)(char *str);
	ldms_set_t (*get_set)();
	int (*init)(const char *path);
	int (*sample)(void);
	void (*term)(void);
};
typedef void (*ldmsd_msg_log_f)(const char *fmt, ...);
typedef struct ldms_plugin *(*ldmsd_plugin_get_f)(ldmsd_msg_log_f pf);
#endif
