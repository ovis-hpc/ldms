/* -*- c-basic-offset: 8 -*-
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
/*
 * This is an AF_UNIX version of the muxr program.
 */
#include <unistd.h>
#include <inttypes.h>
#include <stdarg.h>
#include <getopt.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/un.h>
#include <ctype.h>
#include <netdb.h>
#include "ldms.h"
#include "ldms_xprt.h"
#include "ldmsd.h"
/*
 * The '#' char indicates a comment line. Empty lines are ignored.
 * The keywords are relay, passive, and bridge as follows:
 *
 * active - Connect to the specified host and collect its metrics at the
 *         specified interval
 *
 * passive - Listen for incoming connect requests from the specified host
 *           and when connected collect it's metrics at the specified
 *	     interval.
 *
 * bridge - Just connect to the specified host on the specified port. This is
 *          intended to be the active half of a brdige across a firewall that
 *          only allows outgoing connections.
 *
 * The general syntax of a line is as follows:
 * host-type host-name transport-name port-number sample-interval (usecs)
 * An example configuration file,
 *
 * # this is the comment
 * active nid00016 50000 sock1000000
 * active mzlogin01-priv 50000 sock 1000000
 * passive mzlogin01e 50000 sock 1000000
 * bridge ovis-shepherd.sandia.gov 8192 sock 1000000
 */


LIST_HEAD(host_list_s, hostspec) host_list;
struct hostspec *host_first()
{
	return LIST_FIRST(&host_list);
}

struct hostspec *host_next(struct hostspec *hs)
{
	if (hs->link.le_next == host_list.lh_first)
		return NULL;
	return hs->link.le_next;
}

int resolve(const char *hostname, struct sockaddr_in *sin)
{
	struct hostent *h;

	h = gethostbyname(hostname);
	if (!h) {
		printf("Error resolving hostname '%s'\n", hostname);
		return -1;
	}

	if (h->h_addrtype != AF_INET) {
		printf("Hostname '%s' resolved to an unsupported address family\n",
		       hostname);
		return -1;
	}

	memset(sin, 0, sizeof *sin);
	sin->sin_addr.s_addr = *(unsigned int *)(h->h_addr_list[0]);
	sin->sin_family = h->h_addrtype;
	return 0;
}

char host_str[80];
char ival_str[80];
char xprt_str[80];
char kw[80];
int port_no;
int interval;
struct hostspec *_add_host(char *s, int type)
{
	struct sockaddr_in sin;
	struct hostspec *hs;
	int rc = sscanf(s, "%80s %80s %d %80s %d",
			kw, host_str, &port_no, xprt_str, &interval);
	if (rc != 5) {
		printf("Config file syntax error on line %s\n", s);
		return NULL;
	}
	rc = resolve(host_str, &sin);
	if (rc)
		return NULL;

	hs = calloc(1, sizeof(*hs));
	if (!hs) {
		printf("Memory allocation error in %s.\n",
		       __func__);
		return NULL;
	}
	hs->hostname = strdup(host_str);
	sin.sin_port = htons(port_no);
	hs->sin = sin;
	hs->xprt_name = strdup(xprt_str);
	hs->interval = interval;
	hs->type = type;
	LIST_INSERT_HEAD(&host_list, hs, link);
	return hs;
}

int handle_active_host(char *s)
{
	struct hostspec *hs = _add_host(s, ACTIVE);
	if (!hs)
		return -1;
	return 0;
}

int handle_passive_host(char *s)
{
	struct hostspec *hs = _add_host(s, PASSIVE);
	if (!hs)
		return -1;
	return 0;
}

int handle_bridge_host(char *s)
{
	struct hostspec *hs = _add_host(s, BRIDGING);
	if (!hs)
		return -1;
	return 0;
}

#define ACTIVE_HOST_KW "active"
#define PASSIVE_HOST_KW "passive"
#define BRIDGE_HOST_KW "bridge"
int parse_line(char *s)
{
	int rc = -1;
	s = skip_space(s);

	/* ignore blank line */
	if (!s)
		return 0;

	/* ignore comment line */
	if (*s == '#')
		return 0;

	if (0 == strncmp(s, ACTIVE_HOST_KW, sizeof(ACTIVE_HOST_KW)-1))
		rc = handle_active_host(s);
	else if (0 == strncmp(s, PASSIVE_HOST_KW, sizeof(PASSIVE_HOST_KW)-1))
		rc = handle_passive_host(s);
	else if (0 == strncmp(s, BRIDGE_HOST_KW, sizeof(BRIDGE_HOST_KW)-1))
		rc = handle_bridge_host(s);
	else
		printf("Unrecognized keyword on line '%s'\n", s);

	return rc;
}

char rl[1024];
int parse_cfg(const char *config_file)
{
	int rc = 0;
	char *s;
	FILE *fp;
	if (!config_file)
		return 0;
	fp = fopen(config_file, "r");
	if (!fp)
		return ENOENT;

	while ((s = fgets(rl, sizeof(rl), fp)) != NULL) {
		rc = parse_line(s);
		if (rc)
			break;
	}
	return rc;
}
