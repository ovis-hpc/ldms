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
/*
 * These are muxr_un helper routines.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <libgen.h>

static int un_muxr_s = -1;
static int un_quiet = 0;
static char msg_buf[4096];

void un_set_quiet(int q)
{
	un_quiet = q;
}

void un_say(const char *fmt, ...)
{
	va_list ap;

	if (un_quiet)
		return;

	va_start(ap, fmt);
	vprintf(fmt, ap);
}

int un_send_req(int sock, struct sockaddr *sa, ssize_t sa_len,
		char *data, ssize_t data_len)
{
	struct msghdr reply;
	struct iovec iov;

	reply.msg_name = sa;
	reply.msg_namelen = sa_len;
	iov.iov_base = data;
	iov.iov_len = data_len;
	reply.msg_iov = &iov;
	reply.msg_iovlen = 1;
	reply.msg_control = NULL;
	reply.msg_controllen = 0;
	reply.msg_flags = 0;
	un_say("Sending  : %s", data);
	return sendmsg(sock, &reply, 0);
}

int un_recv_rsp(int sock, struct sockaddr *sa, ssize_t sa_len,
		char *data, ssize_t data_len)
{
	struct msghdr msg;
	int msglen;
	struct iovec iov;

	msg.msg_name = sa;
	msg.msg_namelen = sa_len;

	iov.iov_base = data;
	iov.iov_len = data_len;

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	msg.msg_control = NULL;
	msg.msg_controllen = 0;

	msg.msg_flags = 0;

	msglen = recvmsg(un_muxr_s, &msg, 0);
	un_say("Received : %s", data);
	return msglen;
}

static struct sockaddr_un un_sun;

void un_close(void)
{
	struct sockaddr_un sun;
	socklen_t salen = sizeof(sun);
	if (!getsockname(un_muxr_s, (struct sockaddr *)&sun, &salen)) {
		if (unlink(sun.sun_path))
			perror("unlink: ");
	} else
		perror("getsockname: ");
	close(un_muxr_s);
}

int un_connect(char *my_name, char *sockname)
{
	int rc;
	struct sockaddr_un my_un;
	char *mn = strdup(my_name);
	char *sockpath = getenv("LDMS_SOCKPATH");
	if (!sockpath)
		sockpath = "/var/run";
	memset(&un_sun, 0, sizeof(un_sun));
	un_sun.sun_family = AF_UNIX;
	strncpy(un_sun.sun_path, sockname,
		sizeof(struct sockaddr_un) - sizeof(short));

	/* Create muxr socket */
	un_muxr_s = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (un_muxr_s < 0) {
		perror("socket: ");
		return un_muxr_s;
	}

	pid_t pid = getpid();
	my_un.sun_family = AF_UNIX;

	mn = basename(mn);
	sprintf(my_un.sun_path, "%s/%s", sockpath, mn);
	free(mn);

	mkdir(my_un.sun_path, 0755);
	sprintf(my_un.sun_path, "%s/%d", my_un.sun_path, pid);

	/* Bind to our public name */
	rc = bind(un_muxr_s, (struct sockaddr *)&my_un, sizeof(struct sockaddr_un));
	if (rc < 0) {
		printf("Error creating '%s'\n", my_un.sun_path);
		close(un_muxr_s);
		return -1;
	}
	return un_muxr_s;
}

int un_def_set(char *set_name, ssize_t set_size)
{
	int rc;
	int set_no;
	char junk[128];
	sprintf(msg_buf, "DS %s %zu\n", set_name, set_size);
	rc = un_send_req(un_muxr_s, (struct sockaddr *)&un_sun, sizeof(un_sun),
			 msg_buf, strlen(msg_buf)+1);
	if (rc < 0) {
		un_say("Error %d sending request.\n", rc);
		goto out;
	}
	rc = un_recv_rsp(un_muxr_s, NULL, 0, msg_buf, sizeof(msg_buf));
	if (rc <= 0) {
		un_say("Error %d receiving reply.\n", rc);
		goto out;
	}
	rc = sscanf(msg_buf, "%s %d\n", junk, &set_no);
	if (rc == 2)
		return set_no;
 out:
	return -1;
}

int un_rem_set(int set_no)
{
	int ret;
	int rc;
	char junk[128];
	sprintf(msg_buf, "RS %d\n", set_no);
	rc = un_send_req(un_muxr_s, (struct sockaddr *)&un_sun, sizeof(un_sun),
			 msg_buf, strlen(msg_buf)+1);
	if (rc < 0) {
		un_say("Error %d sending request.\n", rc);
		goto out;
	}
	rc = un_recv_rsp(un_muxr_s, NULL, 0, msg_buf, sizeof(msg_buf));
	if (rc <= 0) {
		un_say("Error %d receiving reply.\n", rc);
		goto out;
	}
	rc = sscanf(msg_buf, "%s %d\n", junk, &ret);
	if (rc == 2)
		return ret;
 out:
	return -1;
}

int un_def_metric(int set_no, char *metric_name, char *metric_type)
{
	int rc;
	char junk[128];
	int metric_no;

	sprintf(msg_buf, "DM %d %s %s\n", set_no, metric_type, metric_name);
	rc = un_send_req(un_muxr_s, (struct sockaddr *)&un_sun, sizeof(un_sun),
			 msg_buf, strlen(msg_buf)+1);
	if (rc < 0) {
		un_say("Error %d sending DM request.\n", rc);
		goto out;
	}
	rc = un_recv_rsp(un_muxr_s, NULL, 0, msg_buf, sizeof(msg_buf));
	if (rc <= 0) {
		un_say("Error %d receiving DM reply.\n", rc);
		goto out;
	}
	rc = sscanf(msg_buf, "%s %d\n", junk, &metric_no);
	if (rc == 2)
		return metric_no;
 out:
	return -1;
}

static uint64_t _un_set()
{
	uint64_t gn;
	int rc;
	char junk[128];

	rc = un_send_req(un_muxr_s, (struct sockaddr *)&un_sun, sizeof(un_sun),
			 msg_buf, strlen(msg_buf)+1);
	if (rc < 0) {
		un_say("Error %d sending SM request.\n", rc);
		return -1L;
	}
	rc = un_recv_rsp(un_muxr_s, NULL, 0, msg_buf, sizeof(msg_buf));
	if (rc < 0) {
		un_say("Error %d receiving SM reply.\n", rc);
		return -1;
	}
	sscanf(msg_buf, "%s %" PRIu64 "\n", junk, &gn);
	if (rc < 0) {
		un_say("Error parsing SM reply.\n");
		return -1;
	}
	return gn;
}

uint64_t un_set_u8(int set_no, int metric_no, uint8_t u8)
{
	sprintf(msg_buf, "SM %d %d %hhu\n", set_no, metric_no, u8);
	return _un_set();
}

uint64_t un_set_s8(int set_no, int metric_no, int8_t s8)
{
	sprintf(msg_buf, "SM %d %d %hhd\n", set_no, metric_no, s8);
	return _un_set();
}

uint64_t un_set_u16(int set_no, int metric_no, uint16_t u16)
{
	sprintf(msg_buf, "SM %d %d %hu\n", set_no, metric_no, u16);
	return _un_set();
}

uint64_t un_set_s16(int set_no, int metric_no, int16_t s16)
{
	sprintf(msg_buf, "SM %d %d %hd\n", set_no, metric_no, s16);
	return _un_set();
}

uint64_t un_set_u32(int set_no, int metric_no, uint32_t u32)
{
	sprintf(msg_buf, "SM %d %d %u\n", set_no, metric_no, u32);
	return _un_set();
}

uint64_t un_set_s32(int set_no, int metric_no, int32_t s32)
{
	sprintf(msg_buf, "SM %d %d %d\n", set_no, metric_no, s32);
	return _un_set();
}

uint64_t un_set_u64(int set_no, int metric_no, uint64_t u64)
{
	sprintf(msg_buf, "SM %d %d %" PRIu64 "\n", set_no, metric_no, u64);
	return _un_set();
}

uint64_t un_set_s64(int set_no, int metric_no, int64_t s64)
{
	sprintf(msg_buf, "SM %d %d %" PRId64 "\n", set_no, metric_no, s64);
	return _un_set();
}

int un_load_plugin(char *plugin, char *err_str)
{
	int rc;
	int status;
	char junk[128];

	sprintf(msg_buf, "PL %s\n", plugin);
	rc = un_send_req(un_muxr_s, (struct sockaddr *)&un_sun, sizeof(un_sun),
			 msg_buf, strlen(msg_buf)+1);
	if (rc < 0) {
		un_say("Error %d sending request.\n", rc);
		goto out;
	}
	rc = un_recv_rsp(un_muxr_s, NULL, 0, msg_buf, sizeof(msg_buf));
	if (rc <= 0) {
		un_say("Error %d receiving reply.\n", rc);
		goto out;
	}
	rc = sscanf(msg_buf, "%s %d %[^\n]", junk, &status, err_str);
	if (rc >= 2)
		return status;
 out:
	return -1;
}

int un_ls_plugins(char *buf, size_t buf_sz)
{
	int rc;

	sprintf(msg_buf, "LS\n");
	rc = un_send_req(un_muxr_s, (struct sockaddr *)&un_sun, sizeof(un_sun),
			 msg_buf, strlen(msg_buf)+1);
	if (rc < 0) {
		un_say("Error %d sending request.\n", rc);
		goto out;
	}
	rc = un_recv_rsp(un_muxr_s, NULL, 0, msg_buf, sizeof(msg_buf));
	if (rc <= 0) {
		un_say("Error %d receiving reply.\n", rc);
		goto out;
	}
	strncpy(buf, msg_buf, buf_sz);
	return 0;
 out:
	return -1;
}

int un_init_plugin(char *plugin, char *set_name, char *err_str)
{
	int rc;
	int status;
	char junk[128];

	sprintf(msg_buf, "PI %s %s\n", plugin, set_name);
	rc = un_send_req(un_muxr_s, (struct sockaddr *)&un_sun, sizeof(un_sun),
			 msg_buf, strlen(msg_buf)+1);
	if (rc < 0) {
		un_say("Error %d sending request.\n", rc);
		goto out;
	}
	rc = un_recv_rsp(un_muxr_s, NULL, 0, msg_buf, sizeof(msg_buf));
	if (rc <= 0) {
		un_say("Error %d receiving reply.\n", rc);
		goto out;
	}
	rc = sscanf(msg_buf, "%s %d %[^\n]", junk, &status, err_str);
	if (rc >= 2)
		return status;
 out:
	return -1;
}

int un_term_plugin(char *plugin_name, char *err_str)
{
	int rc;
	int status;
	char junk[128];

	sprintf(msg_buf, "PT %s\n", plugin_name);
	rc = un_send_req(un_muxr_s, (struct sockaddr *)&un_sun, sizeof(un_sun),
			 msg_buf, strlen(msg_buf)+1);
	if (rc < 0) {
		un_say("Error %d sending request.\n", rc);
		goto out;
	}
	rc = un_recv_rsp(un_muxr_s, NULL, 0, msg_buf, sizeof(msg_buf));
	if (rc <= 0) {
		un_say("Error %d receiving reply.\n", rc);
		goto out;
	}
	rc = sscanf(msg_buf, "%s %d %[^\n]", junk, &status, err_str);
	if (rc >= 2)
		return status;
 out:
	return -1;
}

int un_start_plugin(char *plugin, unsigned long period, char *err_str)
{
	int rc;
	int status;
	char junk[128];

	sprintf(msg_buf, "PS %s %ld\n", plugin, period);
	rc = un_send_req(un_muxr_s, (struct sockaddr *)&un_sun, sizeof(un_sun),
			 msg_buf, strlen(msg_buf)+1);
	if (rc < 0) {
		un_say("Error %d sending request.\n", rc);
		goto out;
	}
	rc = un_recv_rsp(un_muxr_s, NULL, 0, msg_buf, sizeof(msg_buf));
	if (rc <= 0) {
		un_say("Error %d receiving reply.\n", rc);
		goto out;
	}
	rc = sscanf(msg_buf, "%s %d %[^\n]", junk, &status, err_str);
	if (rc >= 2)
		return status;
 out:
	return -1;
}

int un_stop_plugin(char *plugin, char *err_str)
{
	int rc;
	int status;
	char junk[128];
	sprintf(msg_buf, "PX %s\n", plugin);
	rc = un_send_req(un_muxr_s, (struct sockaddr *)&un_sun, sizeof(un_sun),
			 msg_buf, strlen(msg_buf)+1);
	if (rc < 0) {
		un_say("Error %d sending request.\n", rc);
		goto out;
	}
	rc = un_recv_rsp(un_muxr_s, NULL, 0, msg_buf, sizeof(msg_buf));
	if (rc <= 0) {
		un_say("Error %d receiving reply.\n", rc);
		goto out;
	}
	rc = sscanf(msg_buf, "%s %d %[^\n]", junk, &status, err_str);
	if (rc >= 2)
		return status;
 out:
	return -1;
}

int un_config_plugin(char *plugin, char *config_str, char *err_str)
{
	int rc;
	int status;
	char junk[128];
	sprintf(msg_buf, "PC %s %s\n", plugin, config_str);
	rc = un_send_req(un_muxr_s, (struct sockaddr *)&un_sun, sizeof(un_sun),
			 msg_buf, strlen(msg_buf)+1);
	if (rc < 0) {
		un_say("Error %d sending request.\n", rc);
		goto out;
	}
	rc = un_recv_rsp(un_muxr_s, NULL, 0, msg_buf, sizeof(msg_buf));
	if (rc <= 0) {
		un_say("Error %d receiving reply.\n", rc);
		goto out;
	}
	rc = sscanf(msg_buf, "%s %d \"%[^\"]\"", junk, &status, err_str);
	if (rc >= 2)
		return status;
 out:
	return -1;
}

