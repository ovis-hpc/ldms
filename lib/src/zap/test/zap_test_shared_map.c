/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2020 Open Grid Computing, Inc. All rights reserved.
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
/**
 * \file zap_test_shared_map.c
 *
 * \brief Zap shared map functionality test.
 *
 * This program run in three modes: server, writer, and reader.
 *
 * The server allocates a block of memory and register it with `zap_map()`.
 * Then, it will `zap_share()` that map to anybody connecting to it. The server
 * listen on multiple endpoints (specified multiple times by `-x xprt:addr:port`
 * option). The memory block is initialized with all zeroes. The server process
 * stays up for the entire life of the test.
 *
 * The writer connects to the server (-x xprt:addr:port) and wait for the
 * RENDEZVOUS event. Then, it zap_write() data to the server, close
 * connection and exit.
 *
 * The reader connects to the server (-x xprt:addr:port) using different
 * transport and wait for the RENDEZVOUS event. Then, it zap_read() the data
 * from the server and verifies that the data is the same as the writer wrote it
 * then exit with success or failure.
 *
 * Example:
 * $ zap_test_shared_map -m server -x sock:*:10000 -x rdma:*:10001
 *
 * # in another terminal / or on other node
 * $ zap_test_shared_map -m writer -x sock:1.2.3.4:10000
 *
 * # on another node
 * $ zap_test_shared_map -m reader -x rdma:5.6.7.8:10001
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <stdarg.h>

#include "zap.h"

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof(a)/sizeof((a)[0]))
#endif

/* data for verification */
char data[1024] = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG.";

/* the memory block for read/write */
char mem[1024] = {0};
zap_map_t map; /* describe mem */
zap_map_t peer_map; /* describe peer's mem */
struct zap_mem_info meminfo = {.start = mem, .len = sizeof(mem)};

int completed = 0;

const char *shortopts = "x:m:?";

struct xprt_spec_s {
	const char *xprt;
	const char *fabric_provider;
	const char *fabric_domain;
	struct sockaddr_in addr;
};

enum proc_mode {
	MODE_NONE,
	MODE_SERVER,
	MODE_WRITER,
	MODE_READER,
	MODE_LAST,
};
typedef enum proc_mode proc_mode_t;

struct xprt_spec_s xprt_spec[16] = {{0}}; /* should be enough */
int xprt_spec_n = 0;
proc_mode_t proc_mode = MODE_NONE;


struct option longopts[] = {
	{ "xprt" , 1 , 0 , 'x' },
	{ "mode" , 1 , 0 , 'm' },
	{ "help" , 1 , 0 , 'h' },
	{ 0      , 0 , 0 ,  0  }
};

const char *_usage = "\
SYNOPSIS\n\
	zap_test_shared_map -m MODE -x XPRT:ADDR:PORT [-x ...]\n\
\n\
DESCRIPTION\n\
\n\
This program run in three modes: server, writer, and reader.\n\
\n\
NOTE: The server must be connected by a writer before a reader so that the server\n\
would write to the to-be-read memory by the reader.\n\
\n\
The server allocates a block of memory and register it with `zap_map()`.\n\
Then, it will `zap_share()` that map to anybody connecting to it. The server\n\
listen on multiple endpoints (specified multiple times by `-x xprt:addr:port`\n\
option). The memory block is initialized with all zeroes. The server process\n\
stays up for the entire life of the test.\n\
\n\
The writer connects to the server (-x xprt:addr:port) and wait for the\n\
RENDEZVOUS event. Then, it zap_write() data to the server, close\n\
connection and exit.\n\
\n\
The reader connects to the server (-x xprt:addr:port) using different\n\
transport and wait for the RENDEZVOUS event. Then, it zap_read() the data\n\
from the server and verifies that the data is the same as the writer wrote it\n\
then exit with success or failure.\n\
\n\
Example:\n\
$ zap_test_shared_map -m server -x sock:*:10000 -x rdma:*:10001\n\
\n\
# in another terminal / or on other node\n\
$ zap_test_shared_map -m writer -x sock:1.2.3.4:10000\n\
\n\
# on another node\n\
$ zap_test_shared_map -m reader -x rdma:5.6.7.8:10001\n\
\n\
";

void usage()
{
	printf("%s", _usage);
}

zap_mem_info_t __meminfo(void)
{
	return &meminfo;
}


void server_cb(zap_ep_t ep, zap_event_t ev)
{
	zap_err_t zerr;
	switch (ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		printf("Info: conn request %p.\n", ep);
		zap_accept(ep, server_cb, NULL, 0);
		break;
	case ZAP_EVENT_CONNECTED:
		/* ep is the new endpoint */
		printf("Info: connected %p.\n", ep);
		zerr = zap_share(ep, map, NULL, 0);
		assert(zerr == ZAP_ERR_OK);
		break;
	case ZAP_EVENT_DISCONNECTED:
		printf("Info: disconnected %p.\n", ep);
		/* no-op */
		break;
	case ZAP_EVENT_READ_COMPLETE:
	case ZAP_EVENT_WRITE_COMPLETE:
	case ZAP_EVENT_RECV_COMPLETE:
	case ZAP_EVENT_REJECTED:
	case ZAP_EVENT_RENDEZVOUS:
	case ZAP_EVENT_CONNECT_ERROR:
	case ZAP_EVENT_LAST:
	default:
		printf("Error: unexpected %s\n", zap_event_str(ev->type));
		break;
	}
}

void server_proc()
{
	int i;
	zap_err_t zerr;
	zap_t zap;
	zap_ep_t zep;

	zerr = zap_map(&map, mem, sizeof(mem), ZAP_ACCESS_READ|ZAP_ACCESS_WRITE);
	if (zerr) {
		printf("Error: zap_map() error: %d\n", zerr);
		assert(0);
		exit(-1);
	}

	for (i = 0; i < xprt_spec_n; i++) {
		zap = zap_get(xprt_spec[i].xprt, __meminfo);
		assert(zap);
		zep = zap_new(zap, server_cb);
		if (!zep) {
			printf("Error: zap_new() error: %d\n", errno);
			assert(0);
			exit(-1);
		}
		zerr = zap_listen(zep, (void*)&xprt_spec[i].addr,
					sizeof(xprt_spec[i].addr));
		if (zerr) {
			printf("Error: zap_listen() error: %d\n", errno);
			assert(0);
			exit(-1);
		}
	}
	while (!completed) {
		sleep(1);
	}
	zap_unmap(map);
}

void writer_cb(zap_ep_t ep, zap_event_t ev)
{
	zap_err_t zerr;
	switch (ev->type) {
	case ZAP_EVENT_CONNECTED:
		/* no-op */
		printf("Info: connected.\n");
		break;
	case ZAP_EVENT_RENDEZVOUS:
		peer_map = ev->map;
		printf("Info: writing data to the server\n");
		zerr = zap_write(ep, map, data,
				 peer_map, zap_map_addr(peer_map),
				 64, NULL);
		if (zerr) {
			printf("Error: zap_write() error: %d\n", zerr);
			assert(0);
			exit(-1);
		}
		break;
	case ZAP_EVENT_WRITE_COMPLETE:
		printf("Info: write completed, status: %d.\n", ev->status);
		if (ev->status)
			printf("Error: write completed with error: %d\n", ev->status);
		completed = 1;
		break;
	case ZAP_EVENT_DISCONNECTED:
		printf("Info: disconnected.\n");
		/* no-op */
		break;
	case ZAP_EVENT_REJECTED:
		printf("Error: connection rejected");
		assert(0);
		break;
	case ZAP_EVENT_CONNECT_ERROR:
		printf("Error: connect error");
		assert(0);
		break;
	case ZAP_EVENT_CONNECT_REQUEST:
	case ZAP_EVENT_READ_COMPLETE:
	case ZAP_EVENT_RECV_COMPLETE:
	case ZAP_EVENT_LAST:
	default:
		printf("Error: unexpected %s\n", zap_event_str(ev->type));
		break;
	}
}

void writer_proc()
{
	zap_err_t zerr;
	zap_ep_t zep;
	zap_t zap;
	struct xprt_spec_s *spec = &xprt_spec[0];

	zerr = zap_map(&map, data, sizeof(data), ZAP_ACCESS_READ);
	if (zerr) {
		printf("Error: zap_map() error: %d\n", zerr);
		assert(0);
		exit(-1);
	}
	zap = zap_get(spec->xprt, __meminfo);
	assert(zap);
	zep = zap_new(zap, writer_cb);
	if (!zep) {
		printf("Error: zap_new() error: %d\n", errno);
		assert(0);
		exit(-1);
	}
	zerr = zap_connect(zep, (void*)&spec->addr, sizeof(spec->addr), NULL, 0);
	if (zerr) {
		printf("Error: zap_connect() error: %d\n", zerr);
		assert(0);
		exit(-1);
	}
	while (!completed) {
		sleep(1);
	}
	zap_unmap(peer_map);
	zap_unmap(map);
	zap_close(zep);
}

void reader_cb(zap_ep_t ep, zap_event_t ev)
{
	zap_err_t zerr;
	switch (ev->type) {
	case ZAP_EVENT_CONNECTED:
		printf("Info: connected.\n");
		/* no-op */
		break;
	case ZAP_EVENT_RENDEZVOUS:
		printf("Info: map rendezvous.\n");
		peer_map = ev->map;
		zerr = zap_read(ep, peer_map, zap_map_addr(peer_map),
				 map, mem,
				 sizeof(mem), NULL);
		if (zerr) {
			printf("Error: zap_write() error: %d\n", zerr);
			assert(0);
			exit(-1);
		}
		break;
	case ZAP_EVENT_READ_COMPLETE:
		printf("Info: read completed, status: %d\n", ev->status);
		if (ev->status)
			printf("Error: read completed with error: %d\n", ev->status);
		if (0 != memcmp(data, mem, sizeof(data))) {
			/* bad */
			printf("Error: bad read, data: %s\n", mem);
			assert(0);
			exit(-1);
		} else {
			/* good */
			printf("Info: data verified\n");
		}
		completed = 1;
		break;
	case ZAP_EVENT_DISCONNECTED:
		printf("Info: disconnected.\n");
		/* no-op */
		break;
	case ZAP_EVENT_REJECTED:
		printf("Error: connection rejected");
		assert(0);
		exit(-1);
		break;
	case ZAP_EVENT_CONNECT_ERROR:
		printf("Error: connect error");
		assert(0);
		exit(-1);
		break;
	case ZAP_EVENT_CONNECT_REQUEST:
	case ZAP_EVENT_RECV_COMPLETE:
	case ZAP_EVENT_WRITE_COMPLETE:
	case ZAP_EVENT_LAST:
	default:
		printf("Error: unexpected %s\n", zap_event_str(ev->type));
		break;
	}
}

void reader_proc()
{
	zap_err_t zerr;
	zap_ep_t zep;
	zap_t zap;
	struct xprt_spec_s *spec = &xprt_spec[0];

	zerr = zap_map(&map, mem, sizeof(mem), ZAP_ACCESS_READ|ZAP_ACCESS_WRITE);
	if (zerr) {
		printf("Error: zap_map() error: %d\n", zerr);
		assert(0);
		exit(-1);
	}
	zap = zap_get(spec->xprt, __meminfo);
	assert(zap);
	zep = zap_new(zap, reader_cb);
	if (!zep) {
		printf("Error: zap_new() error: %d\n", errno);
		assert(0);
		exit(-1);
	}
	zerr = zap_connect(zep, (void*)&spec->addr, sizeof(spec->addr), NULL, 0);
	if (zerr) {
		printf("Error: zap_connect() error: %d\n", zerr);
		assert(0);
		exit(-1);
	}
	while (!completed) {
		sleep(1);
	}
	zap_unmap(peer_map);
	zap_unmap(map);
	zap_close(zep);
}

int parse_spec(char *text, struct xprt_spec_s *s, int ai_hint_flags)
{
	char *host, *port;
	char *ptr;
	struct addrinfo *ai;
	struct addrinfo hint = {.ai_family = AF_INET, .ai_flags = ai_hint_flags};
	int rc;
	s->xprt = strtok_r(text, ":", &ptr);
	host = strtok_r(NULL, ":", &ptr);
	port = strtok_r(NULL, ":", &ptr);
	if (!s->xprt || !port) {
		printf( "Error: spec parse error, expecting XPRT:PORT[:ADDR], "
			"but got: %s\n", text);
		return EINVAL;
	}
	rc = getaddrinfo(host, port, &hint, &ai);
	if (rc) {
		printf("Error: getaddrinfo() error %d\n", rc);
		return EINVAL;
	}
	memcpy(&s->addr, ai->ai_addr, ai->ai_addrlen);
	freeaddrinfo(ai);
	return 0;
}

int main(int argc, char **argv)
{
	int opt;
	int rc;
	int ai_hint_flags = 0;
	while ((opt=getopt_long(argc, argv, shortopts, longopts, NULL))>=0) {
		switch (opt) {
		case 'm':
			if (proc_mode) {
				printf("Error: mode has already been set\n");
				assert(0);
				exit(-1);
			}
			if (0 == strcasecmp("server", optarg)) {
				proc_mode = MODE_SERVER;
				ai_hint_flags = AI_PASSIVE;
			} else if (0 == strcasecmp("writer", optarg)) {
				proc_mode = MODE_WRITER;
			} else if (0 == strcasecmp("reader", optarg)) {
				proc_mode = MODE_READER;
			} else {
				printf("Error: unknown mode '%s'\n", optarg);
				assert(0);
				exit(-1);
			}
			break;
		case 'x':
			if (xprt_spec_n >= ARRAY_LEN(xprt_spec)) {
				printf("Error: too many xprts\n");
				assert(0);
				exit(-1);
			}
			rc = parse_spec(optarg, &xprt_spec[xprt_spec_n++], ai_hint_flags);
			if (rc) {
				assert(0);
				exit(-1);
			}
			break;
		default:
			usage();
			exit(0);
		}
	}

	if (!xprt_spec_n) {
		printf("Error: xprt not specified.\n");
		exit(-1);
	}

	switch (proc_mode) {
	case MODE_SERVER:
		server_proc();
		break;
	case MODE_WRITER:
		writer_proc();
		break;
	case MODE_READER:
		reader_proc();
		break;
	case MODE_NONE:
		printf("Error: mode not set\n");
		exit(-1);
	default:
		printf("Error: bad mode: %d\n", proc_mode);
		exit(-1);
	}
	printf("Info: exiting...\n");

	return 0;
}
