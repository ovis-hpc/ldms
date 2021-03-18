/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2017,2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2017,2019 Open Grid Computing, Inc. All rights reserved.
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

#include "ctrl.h"
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

#define ARRAY_SIZE(__a) (sizeof(__a) / sizeof(__a[0]))

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

struct ctrlsock *ctrl_connect(char *my_name, char *sockname,
					const char *sock_envpath)
{
	int rc;
	struct sockaddr_un my_un;
	char *mn;
	char *sockpath;
	char *_sockname = NULL;
	struct ctrlsock *sock;
	int len;

	sock = calloc(1, sizeof *sock);
	if (!sock)
		return NULL;

	sock->rem_sun.sun_family = AF_UNIX;

	if (sockname[0] == '/') {
		_sockname = strdup(sockname);
		if (!_sockname)
			goto err;
		sockpath = dirname(_sockname);
		if (strlen(sockpath) + 1 > sizeof(my_un.sun_path)) {
			printf("sockpath '%s' too long\n", sockpath);
			errno = EINVAL;
			goto err;
		}
		strcpy(my_un.sun_path, sockname);
	} else {
		sockpath = getenv(sock_envpath);
		if (!sockpath)
			sockpath = "/var/run";
		if (strlen(sockpath) + 2 + strlen(sockname) > sizeof(my_un.sun_path)) {
			printf("sockpath '%s/%s' too long\n", sockpath, sockname);
			errno = EINVAL;
			goto err;
		}
		sprintf(my_un.sun_path, "%s/%s", sockpath, sockname);
	}

	strncpy(sock->rem_sun.sun_path, my_un.sun_path,
		sizeof(struct sockaddr_un) - sizeof(short));

	/* Create control socket */
	sock->sock = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (sock->sock < 0)
		goto err;

	pid_t pid = getpid();
	sock->lcl_sun.sun_family = AF_UNIX;

	mn = strdup(my_name);
	if (!mn) {
		close(sock->sock);
		goto err;
	}
	sprintf(my_un.sun_path, "%s/%s", sockpath, basename(mn));
	free(mn);

	rc = mkdir(my_un.sun_path, 0755);
	if (rc < 0) {
		/* Ignore the error if the path already exists */
		if (errno != EEXIST) {
			printf("Error creating '%s: %s\n", my_un.sun_path,
							STRERROR(errno));
			close(sock->sock);
			goto err;
		}
	}

	len = snprintf(sock->lcl_sun.sun_path, sizeof(sock->lcl_sun.sun_path),
			"%s/%d", my_un.sun_path, pid);
	if (len >= sizeof(sock->lcl_sun.sun_path)) {
		close(sock->sock);
		errno = ENAMETOOLONG;
		goto err;
	}

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
	if (_sockname)
		free(_sockname);
	return sock;
 err:
	free(sock);
	if (_sockname)
		free(_sockname);
	return NULL;
}
