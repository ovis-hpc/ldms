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
#include <sys/stat.h>
#include <sys/queue.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/un.h>
#include <ctype.h>
#include <netdb.h>
#include <pthread.h>
#include <libgen.h>
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
 * active nid00016 50000 sock 1000000
 * active mzlogin01-priv 50000 sock 1000000
 * passive mzlogin01e 50000 sock 1000000
 * bridge ovis-shepherd.sandia.gov 8192 sock 1000000
 */

char *av_name(struct attr_value_list *av_list, int idx)
{
	if (idx < av_list->count)
		return av_list->list[idx].name;
	return NULL;
}

char *av_value(struct attr_value_list *av_list, char *name)
{
	int i;
	for (i = 0; i < av_list->count; i++)
		if (0 == strcmp(name, av_list->list[i].name))
			return av_list->list[i].value;
	return NULL;
}

char *av_value_at_idx(struct attr_value_list *av_list, int i)
{
	if (i < av_list->count)
		return av_list->list[i].value;
	return NULL;
}

#define ARRAY_SIZE(__a) (sizeof(__a) / sizeof(__a[0]))

int tokenize(char *cmd,
	     struct attr_value_list *kw_list,
	     struct attr_value_list *av_list)
{
	char *token, *next_token;
	struct attr_value *av;
	int next_av, next_kw;
	next_av = next_kw = 0;
	for (token = strtok(cmd, " \t\n"); token;) {
		char *value = strstr(token, "=");
		next_token = strtok(NULL, " \t\n");
		if (value) {
			if (next_av >= av_list->size)
				goto err;
			av = &(av_list->list[next_av++]);
			av->name = token;
			*value = '\0';
			value++;
			av->value = value;
		} else {
			if (next_kw >= kw_list->size)
				goto err;
			kw_list->list[next_kw].name = token;
			kw_list->list[next_kw].value = NULL;
			next_kw++;
		}
		token = next_token;
	}
	kw_list->count = next_kw;
	av_list->count = next_av;
	return 0;
 err:
	return ENOMEM;
}

struct attr_value_list *av_new(size_t size)
{
	size_t bytes = sizeof(struct attr_value_list) +
		       (sizeof(struct attr_value) * size);
	struct attr_value_list *avl = malloc(bytes);

	if (avl) {
		memset(avl, 0, bytes);
		avl->size = size;
	}
	return avl;
}

static int send_req(struct ctrlsock *sock, char *data, ssize_t data_len)
{
	struct msghdr reply;
	struct iovec iov;

	reply.msg_name = sock->sa;
	reply.msg_namelen = sock->sa_len;
	iov.iov_base = data;
	iov.iov_len = data_len;
	reply.msg_iov = &iov;
	reply.msg_iovlen = 1;
	reply.msg_control = NULL;
	reply.msg_controllen = 0;
	reply.msg_flags = 0;
	return sendmsg(sock->sock, &reply, 0);
}

static int recv_rsp(struct ctrlsock *sock, char *data, ssize_t data_len)
{
	struct msghdr msg;
	int msglen;
	struct iovec iov;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;

	iov.iov_base = data;
	iov.iov_len = data_len;

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	msg.msg_control = NULL;
	msg.msg_controllen = 0;

	msg.msg_flags = 0;

	msglen = recvmsg(sock->sock, &msg, 0);
	return msglen;
}

void ctrl_close(struct ctrlsock *sock)
{
	struct sockaddr_un sun;
	socklen_t salen = sizeof(sun);
	if (!getsockname(sock->sock, (struct sockaddr *)&sun, &salen)) {
		if (unlink(sun.sun_path))
			perror("unlink: ");
	} else
		perror("getsockname: ");
	close(sock->sock);
	free(sock);
}

static char msg_buf[4096];
static char arg[1024];

int ctrl_request(struct ctrlsock *sock, int cmd_id,
		 struct attr_value_list *avl, char *err_str)
{
	int rc;
	int status;
	int cnt;

	sprintf(msg_buf, "%d ", cmd_id);
	for (rc = 0; rc < avl->count; rc++) {
		sprintf(arg, "%s=%s ", avl->list[rc].name, avl->list[rc].value);
		strcat(msg_buf, arg);
	}
	strcat(msg_buf, "\n");
	rc = send_req(sock, msg_buf, strlen(msg_buf)+1);
	if (rc < 0) {
		sprintf(err_str, "Error %d sending request.\n", rc);
		return -1;
	}
	rc = recv_rsp(sock, msg_buf, sizeof(msg_buf));
	if (rc <= 0) {
		sprintf(err_str, "Error %d receiving reply.\n", rc);
		return -1;
	}
	err_str[0] = '\0';
	rc = sscanf(msg_buf, "%d%n", &status, &cnt);
	strcpy(err_str, &msg_buf[cnt]);
	return status;
}

struct ctrlsock *ctrl_inet_connect(struct sockaddr_in *sin)
{
	int rc;
	struct ctrlsock *sock;

	sock = calloc(1, sizeof *sock);
	if (!sock)
		return NULL;

	sock->sin = *sin;
	sock->sock = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (sock->sock < 0)
		goto err;

	return sock;
 err:
	free(sock);
	return NULL;
}

struct ctrlsock *ctrl_connect(char *my_name, char *sockname)
{
	int rc;
	struct sockaddr_un my_un;
	char *mn = strdup(my_name);
	char *sockpath;
	struct ctrlsock *sock;

	sock = calloc(1, sizeof *sock);
	if (!sock)
		return NULL;

	sockpath = getenv("LDMSD_SOCKPATH");
	if (!sockpath)
		sockpath = "/var/run";

	sock->rem_sun.sun_family = AF_UNIX;
	sprintf(my_un.sun_path, "%s/%s", sockpath, sockname);
	strncpy(sock->rem_sun.sun_path, my_un.sun_path,
		sizeof(struct sockaddr_un) - sizeof(short));

	/* Create control socket */
	sock->sock = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (sock->sock < 0)
		goto err;

	pid_t pid = getpid();
	sock->lcl_sun.sun_family = AF_UNIX;

	sprintf(my_un.sun_path, "%s/%s", sockpath, basename(mn));
	free(mn);

	mkdir(my_un.sun_path, 0755);
	sprintf(sock->lcl_sun.sun_path, "%s/%d", my_un.sun_path, pid);

	/* Bind to our public name */
	rc = bind(sock->sock, (struct sockaddr *)&sock->lcl_sun,
		  sizeof(struct sockaddr_un));
	if (rc < 0) {
		printf("Error creating '%s'\n", sock->lcl_sun.sun_path);
		close(sock->sock);
		goto err;
	}
	sock->sa = (struct sockaddr *)&sock->rem_sun;
	sock->sa_len = sizeof(sock->rem_sun);
	return sock;
 err:
	free(sock);
	return NULL;
}

