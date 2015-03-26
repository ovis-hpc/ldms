/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
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
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <assert.h>
#include <semaphore.h>
#include "ocm.h"

const char *short_opt = "?h:p:P:x:";
struct option long_opt[] = {
	{"host", 1, 0, 'h'},
	{"port", 1, 0, 'p'},
	{"PORT", 1, 0, 'P'},
	{"xprt", 1, 0, 'x'},
	{0, 0, 0, 0}
};

char *host = NULL;
uint16_t port = 0;
uint16_t PORT = 54321;
char *xprt = "sock";

ocm_t ocm;

sem_t done;

void print_usage()
{
	printf(
"Usage: ocmtest [-h HOST] [-p port] [-P listen_port] [-x Transport]\n"
	);
}

void handle_arg(int argc, char **argv)
{
	char c;
loop:
	c = getopt(argc, argv, short_opt);
	switch (c) {
	case 'h':
		host = optarg;
		break;
	case 'p':
		port = atoi(optarg);
		break;
	case 'P':
		PORT = atoi(optarg);
		break;
	case 'x':
		xprt = optarg;
		break;
	case -1:
		return;
		break;
	default:
		print_usage();
		exit(-1);
	}
	goto loop;
}

int req_cb(struct ocm_event *e)
{
	const char *key = ocm_cfg_req_key(e->req);
	if (!host) {
		/* I am a client, reject everything */
		ocm_event_resp_err(e, -1, key, "I am a receiver.");
		return 0;
	}
	if (strcmp(key, "config_me")) {
		ocm_event_resp_err(e, -1, key, "REJECTED!!!");
		return 0;
	}
	struct ocm_cfg_buff *buff = ocm_cfg_buff_new(4096, key);
	char _buff[4096];
	char name[128];
	int i;
	struct ocm_value *v = (void*)_buff;

	/* first test */
	ocm_cfg_buff_add_verb(buff, "add_host");

	ocm_value_set_s(v, "abc.def.ghi");
	ocm_cfg_buff_add_av(buff, "host", v);

	v->type = OCM_VALUE_UINT16;
	v->u16 = 12345;
	ocm_cfg_buff_add_av(buff, "port", v);

	/* another test */
	ocm_cfg_buff_add_verb(buff, "test_values");

	ocm_value_set(v, OCM_VALUE_INT8, INT8_MAX);
	ocm_cfg_buff_add_av(buff, "int8", v);
	ocm_value_set(v, OCM_VALUE_INT16, INT16_MAX);
	ocm_cfg_buff_add_av(buff, "int16", v);
	ocm_value_set(v, OCM_VALUE_INT32, INT32_MAX);
	ocm_cfg_buff_add_av(buff, "int32", v);
	ocm_value_set(v, OCM_VALUE_INT64, INT64_MAX);
	ocm_cfg_buff_add_av(buff, "int64", v);
	ocm_value_set(v, OCM_VALUE_UINT8, UINT8_MAX);
	ocm_cfg_buff_add_av(buff, "uint8", v);
	ocm_value_set(v, OCM_VALUE_UINT16, UINT16_MAX);
	ocm_cfg_buff_add_av(buff, "uint16", v);
	ocm_value_set(v, OCM_VALUE_UINT32, UINT32_MAX);
	ocm_cfg_buff_add_av(buff, "uint32", v);
	ocm_value_set(v, OCM_VALUE_UINT64, UINT64_MAX);
	ocm_cfg_buff_add_av(buff, "uint64", v);
	ocm_value_set(v, OCM_VALUE_FLOAT, 0.55);
	ocm_cfg_buff_add_av(buff, "float", v);
	ocm_value_set(v, OCM_VALUE_DOUBLE, 0.5555);
	ocm_cfg_buff_add_av(buff, "double", v);
	ocm_value_set(v, OCM_VALUE_STR, "test test test");
	ocm_cfg_buff_add_av(buff, "str", v);

	/* Nested test */
	ocm_cfg_buff_add_verb(buff, "nested");

	ocm_value_set_s(v, "Nested attributes.");
	ocm_cfg_buff_add_av(buff, "desc", v);

		/* buff2 is for constructing nested cmd */
		/* intentionally indented to make it clear */
		struct ocm_cfg_buff *buff2 = ocm_cfg_buff_new(4096, "");
		ocm_cfg_buff_add_verb(buff2, ""); /* no real verb */

//		for (i = 0; i < 65536; i++) {
		for (i = 0; i < 10; i++) {
			snprintf(name, sizeof(name), "name%d", i);
			ocm_value_set(v, OCM_VALUE_UINT64, i);
			ocm_cfg_buff_add_av(buff2, name, v);
		}

	ocm_cfg_buff_add_cmd_as_av(buff, "metric_ids",
					ocm_cfg_buff_curr_cmd(buff2));
	ocm_cfg_buff_free(buff2);

#if 0
	/* alternative to using ocm_value_set function */
	v->type = OCM_VALUE_INT8;
	v->i8 = 127;
	ocm_cfg_buff_add_av(buff, "int8", v);

	v->type = OCM_VALUE_INT16;
	v->i16 = 32767;
	ocm_cfg_buff_add_av(buff, "int16", v);

	v->type = OCM_VALUE_INT32;
	v->i32 = 2147483647;
	ocm_cfg_buff_add_av(buff, "int32", v);

	v->type = OCM_VALUE_INT64;
	v->i64 = INT64_MAX;
	ocm_cfg_buff_add_av(buff, "int64", v);

	v->type = OCM_VALUE_UINT8;
	v->u8 = 255;
	ocm_cfg_buff_add_av(buff, "uint8", v);

	v->type = OCM_VALUE_UINT16;
	v->u16 = 65535;
	ocm_cfg_buff_add_av(buff, "uint16", v);

	v->type = OCM_VALUE_UINT32;
	v->u32 = 4294967295;
	ocm_cfg_buff_add_av(buff, "uint32", v);

	v->type = OCM_VALUE_UINT64;
	v->u64 = UINT64_MAX;
	ocm_cfg_buff_add_av(buff, "uint64", v);

	v->type = OCM_VALUE_FLOAT;
	v->f = 0.5;
	ocm_cfg_buff_add_av(buff, "float", v);

	v->type = OCM_VALUE_DOUBLE;
	v->d = 0.55;
	ocm_cfg_buff_add_av(buff, "double", v);

	ocm_value_set_s(v, "Test string");
	ocm_cfg_buff_add_av(buff, "string", v);
#endif

	ocm_event_resp_cfg(e, buff->buff);

	ocm_cfg_buff_free(buff);
}

int __send_update(struct sockaddr *sa, socklen_t sa_len, const char *key)
{
	printf("INFO: sending update\n");
	struct ocm_cfg_buff *buff = ocm_cfg_buff_new(4096, key);
	char _buff[4096];
	char name[128];
	int rc = 0;
	struct ocm_value *v = (void*)_buff;

	/* first test */
	ocm_cfg_buff_add_verb(buff, "update_host");

	ocm_value_set_s(v, "abc.def.ghi");
	ocm_cfg_buff_add_av(buff, "host", v);

	v->type = OCM_VALUE_UINT16;
	v->u16 = 12345;
	ocm_cfg_buff_add_av(buff, "port", v);

	/* another test */
	ocm_cfg_buff_add_verb(buff, "test_values");

	ocm_value_set(v, OCM_VALUE_STR, "update update update");
	ocm_cfg_buff_add_av(buff, "str", v);

	rc = ocm_notify_cfg(ocm, sa, sa_len, buff->buff);
	if (rc == EPERM)
		printf("ERROR: client is disconnected\n");

	ocm_cfg_buff_free(buff);
}

void print_cmd(ocm_cfg_cmd_t cmd, int level);

void print_ocm_value(const struct ocm_value *v, int level)
{
	switch (v->type) {
	case OCM_VALUE_INT8:
		printf("%"PRIi8, v->i8);
		break;
	case OCM_VALUE_INT16:
		printf("%"PRIi16, v->i16);
		break;
	case OCM_VALUE_INT32:
		printf("%"PRIi32, v->i32);
		break;
	case OCM_VALUE_INT64:
		printf("%"PRIi64, v->i64);
		break;
	case OCM_VALUE_UINT8:
		printf("%"PRIu8, v->u8);
		break;
	case OCM_VALUE_UINT16:
		printf("%"PRIu16, v->u16);
		break;
	case OCM_VALUE_UINT32:
		printf("%"PRIu32, v->u32);
		break;
	case OCM_VALUE_UINT64:
		printf("%"PRIu64, v->u64);
		break;
	case OCM_VALUE_FLOAT:
		printf("%f", v->f);
		break;
	case OCM_VALUE_DOUBLE:
		printf("%lf", v->d);
		break;
	case OCM_VALUE_STR:
		printf("%s", v->s.str);
		break;
	case OCM_VALUE_CMD:
		printf("\n");
		print_cmd(v->cmd, level);
		break;
	}
}

void indent(int level)
{
	int i;
	for (i = 0; i < level; i++)
		printf("\t");
}

void print_cmd(ocm_cfg_cmd_t cmd, int level)
{
	struct ocm_av_iter av_iter;
	ocm_av_iter_init(&av_iter, cmd);
	const char *attr;
	const struct ocm_value *value;
	const char *verb = ocm_cfg_cmd_verb(cmd);
	if (verb[0]) {
		indent(level);
		printf("verb: %s\n", verb);
	}
	while (ocm_av_iter_next(&av_iter, &attr, &value) == 0) {
		indent(level+1);
		printf("%s: ", attr);
		print_ocm_value(value, level+1);
		printf("\n");
	}
}

int cfg_cb(struct ocm_event *e);
void cfg_received(struct ocm_event *e)
{
	printf("Receiving configuration: %s\n", ocm_cfg_key(e->cfg));
	struct ocm_cfg_cmd_iter cmd_iter;
	ocm_cfg_cmd_iter_init(&cmd_iter, e->cfg);
	ocm_cfg_cmd_t cmd;
	while (ocm_cfg_cmd_iter_next(&cmd_iter, &cmd) == 0) {
		print_cmd(cmd, 1);
	}
	printf("End of Configuration: %s\n", ocm_cfg_key(e->cfg));
	if (0 == strcmp(ocm_cfg_key(e->cfg), "config_me")) {
		printf("Waiting for update\n");
		int rc = ocm_register(ocm, "update_me", cfg_cb);
		assert(rc == 0);
	}
}

void err_received(struct ocm_event *e) {
	printf("Receiving error, key: %s msg: %s\n", ocm_err_key(e->err),
			ocm_err_msg(e->err));
}

int cfg_cb(struct ocm_event *e)
{
	switch (e->type) {
	case OCM_EVENT_ERROR:
		err_received(e);
		break;
	case OCM_EVENT_CFG_RECEIVED:
		cfg_received(e);
		break;
	default:
		printf("Unhandled event: %d\n", e->type);
	}
	sem_post(&done);
}

void client()
{
	int rc = 0;
	rc = ocm_register(ocm, "config_me", cfg_cb);
	assert(rc == 0);
	rc = ocm_register(ocm, "reject_me", cfg_cb);
	assert(rc == 0);

	rc = ocm_enable(ocm);
	if (rc) {
		printf("ocm_enable failed: %d\n", rc);
		exit(-1);
	}

	sem_wait(&done); /* one for config_me */
	sem_wait(&done); /* another one for reject_me */
	sem_wait(&done); /* last one for update_me */
}

void server()
{
	int rc = 0;
	char p[16];
	struct addrinfo *ai;

	sprintf(p, "%u", port);
	rc = getaddrinfo(host, p, NULL, &ai);
	if (rc) {
		printf("getaddrinfo error %d\n", rc);
		exit(-1);
	}
	rc = ocm_add_receiver(ocm, ai->ai_addr, ai->ai_addrlen);
	assert(rc == 0);
	printf("INFO: Server will forever run. Ctrl-C to terminate it.\n");

	rc = ocm_enable(ocm);
	if (rc) {
		printf("ocm_enable failed: %d\n", rc);
		exit(-1);
	}

	sleep(5);
	__send_update(ai->ai_addr, ai->ai_addrlen, "update_me");
	__send_update(ai->ai_addr, ai->ai_addrlen, "update_me");
	sleep(5);
	__send_update(ai->ai_addr, ai->ai_addrlen, "update_me");
	freeaddrinfo(ai);

	sem_wait(&done); /* run forever */
}

int main(int argc, char **argv)
{
	int rc;
	struct addrinfo *ai;

	sem_init(&done, 0, 0);

	handle_arg(argc, argv);
	ocm = ocm_create(xprt, PORT, req_cb, NULL);
	if (!ocm) {
		printf("Cannot create ocm.\n");
		exit(-1);
	}

	if (host && port)
		server();
	else
		client();

	return 0;
}
