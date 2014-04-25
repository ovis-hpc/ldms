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

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <getopt.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <zap/zap.h>

#include "../core/me.h"

/**
 * This is a program to read a file or generate inputs to ME.
 * The program won't reconnect to ME if the connection is rejected,
 * disconnected or error. Also, it will exit if 'send' fails.
 */

const char *hostname = NULL;
const char *port = NULL;
const char *xprt = NULL;
uint64_t metric_id = 1;
FILE *f = NULL;
FILE *log_fp;
int interval = 2; /* 2 seconds */
int is_connected = 0;

void __log(const char *fmt, ...)
{
	va_list ap;
	time_t t;
	struct tm *tm;
	char dtsz[200];

	t = time(NULL);
	tm = localtime(&t);
	if (strftime(dtsz, sizeof(dtsz), "%a %b %d %H:%M%S %Y", tm))
		fprintf(log_fp, "%s: ", dtsz);
	va_start(ap, fmt);
	vfprintf(log_fp, fmt, ap);
	fflush(log_fp);
}


#define FMT "h:p:x:m:f:i:"
void usage(char *argv[])
{
	printf("%s: [%s]\n", argv[0], FMT);
	printf("	-f	file path	path to the file containing the metric \n"
	       " 				values to be sent to ME\n");
	printf("	-h	hostname	hostname of ME\n");
	printf("	-i	interval	Interval (seconds) that each metric will \n"
	       "				be sent to ME\n");
	printf("	-m	metric_id	an unsigned int 64 number\n");
	printf("	-p	port		ME listener port number\n");
	printf("	-x	transport	ME transport[sock,rdma,ugni]\n");
}

zap_mem_info_t get_zap_mem_info()
{
	return NULL;
}

static void zap_cb(zap_ep_t zep, zap_event_t ev)
{
	switch (ev->type) {
	case ZAP_EVENT_DISCONNECTED:
		is_connected = 0;
		zap_close(zep);
		printf("Disconnected.\n");
		break;
	case ZAP_EVENT_CONNECT_ERROR:
		is_connected = 0;
		zap_close(zep);
		printf("Connection Error.\n");
		break;
	case ZAP_EVENT_REJECTED:
		is_connected = 0;
		zap_close(zep);
		printf("Rejected.\n");
		break;
	case ZAP_EVENT_CONNECTED:
		is_connected = 1;
		printf("Connected.\n");
		break;
	case ZAP_EVENT_RENDEZVOUS:
	case ZAP_EVENT_READ_COMPLETE:
	case ZAP_EVENT_WRITE_COMPLETE:
	case ZAP_EVENT_CONNECT_REQUEST:
	case ZAP_EVENT_RECV_COMPLETE:
	default:
		assert(0);
		break;
	}
}

static void connect_me(zap_t zap, zap_ep_t *zep)
{
	zap_err_t zerr;
	int rc;

	struct addrinfo hints, *servinfo, *p;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	rc = getaddrinfo(hostname, port, &hints, &servinfo);
	if (rc) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
		exit(rc);
	}

	zerr = zap_new(zap, zep, zap_cb);
	if (zerr) {
		fprintf(stderr, "zap_new: %s\n", zap_err_str(zerr));
		exit(zerr);
	}

	struct sockaddr_in sa;
	socklen_t sa_len = servinfo->ai_addrlen;
	memcpy(&sa, servinfo->ai_addr, servinfo->ai_addrlen);
	freeaddrinfo(servinfo);

	zerr = zap_connect(*zep, (void *)&sa, sa_len);
	if (zerr) {
		fprintf(stderr, "zap_connect: %s\n", zap_err_str(zerr));
		exit(zerr);
	}
}

void *send_input_from_file(void *arg)
{
	char *s;
	char buff[1024];
	int rc;
	zap_err_t zerr;

	zap_t zap = arg;
	zap_ep_t zep;

	static int count = 0;

	struct me_msg msg;
	msg.metric_id = htobe64(metric_id);
	struct timeval tv;

	fseek(f, 0, SEEK_SET);

	while (1) {
		if (!is_connected) {
			connect_me(zap, &zep);
			sleep(interval);
			continue;
		}

		count++;
		if (count % 10 == 0) {
			msg.value = 0;
			msg.tag = htonl(ME_NO_DATA);
		} else {
			s = fgets(buff, sizeof(buff), f);

			if (!s) {
				count--;
				fseek(f, 0, SEEK_SET);
				continue;
			}

			msg.value = atof(buff);
			msg.tag = htonl(ME_INPUT_DATA);
		}

		rc = gettimeofday(&tv, NULL);
		if (rc) {
			fprintf(stderr, "gettimeofday: error '%d'\n", rc);
			exit(rc);
		}

		msg.timestamp.tv_sec = htonl(tv.tv_sec);
		msg.timestamp.tv_usec = htonl(tv.tv_usec);

		zerr = zap_send(zep, (void *)&msg, sizeof(msg));
		if (zerr) {
			fprintf(stderr, "zap_send: %s\n", zap_err_str(zerr));
			exit(zerr);
		}

		sleep(interval);
	}

	return NULL;
}

void *send_input(void *arg)
{
	int rc;
	zap_err_t zerr;

	static int count = 0;

	zap_t zap = arg;
	zap_ep_t zep;

	struct me_msg msg;

	double range = 10000;
	double div = RAND_MAX / range;
	struct timeval tv;

	msg.metric_id = htobe64(metric_id);
	while (1) {
		if (!is_connected) {
			connect_me(zap, &zep);
			sleep(interval);
			continue;
		}

		count++;
		if (count % 10 == 0)
			msg.tag = htonl(ME_NO_DATA);
		else
			msg.tag = htonl(ME_INPUT_DATA);

		msg.value = rand() / div;
		printf("send: %f\n", msg.value);
		rc = gettimeofday(&tv, NULL);
		if (rc) {
			printf("gettimeofday: '%d'\n", rc);
			exit(rc);
		}
		msg.timestamp.tv_sec = htonl(tv.tv_sec);
		msg.timestamp.tv_usec = htonl(tv.tv_usec);

		zerr = zap_send(zep, (void *)&msg, sizeof(msg));
		if (zerr) {
			printf("zap_send: %s\n", zap_err_str(zerr));
			exit(zerr);
		}

		sleep(interval);
	}

	return NULL;
}

int process_opt(int argc, char **argv)
{
	int rc, op;
	char *end;
	while (-1 != (op = getopt(argc, argv, FMT))) {
		switch (op) {
		case 'h':
			hostname = strdup(optarg);
			break;
		case 'p':
			port = strdup(optarg);
			break;
		case 'x':
			xprt = strdup(optarg);
			break;
		case 'm':
			metric_id = strtoull(optarg, &end, 10);
			if (*optarg == '\0' || *end != '\0') {
				fprintf(stderr, "Invalid metric ID "
						"'%s'\n", optarg);
				usage(argv);
				exit(EINVAL);
			}
			break;
		case 'f':
			f = fopen(optarg, "r");
			if (!f) {
				fprintf(stderr, "Could not open the file "
						"'%s'\n", optarg);
				usage(argv);
				exit(EINVAL);
			}
			break;
		case 'i':
			interval = atoi(optarg);
			break;
		default:
			fprintf(stderr, "Invalid argument '%c'\n", op);
			usage(argv);
			exit(EINVAL);
		}
	}

	if (!hostname)
		fprintf(stderr, "hostname is needed.\n");
	else if (!xprt)
		fprintf(stderr, "transport is needed.\n");
	else if (port == 0)
		fprintf(stderr, "port is needed.\n");
	else
		return 0;

	usage(argv);
	exit(EINVAL);
}

int main(int argc, char **argv)
{
	int rc;
	process_opt(argc, argv);

	log_fp = stderr;

	zap_t zap;
	zap_err_t zerr;
	zerr = zap_get(xprt, &zap, __log, get_zap_mem_info);
	if (zerr) {
		fprintf(stderr, "Failed to create a Zap handle. "
				"Error: %s.\n", zap_err_str(zerr));
		exit(zerr);
	}

	pthread_t t;
	if (!f)
		rc = pthread_create(&t, NULL, send_input, zap);
	else
		rc = pthread_create(&t, NULL, send_input_from_file, zap);


	pthread_join(t, NULL);
	return 0;
}
