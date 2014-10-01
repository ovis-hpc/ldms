/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012 Sandia Corporation. All rights reserved.
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

/*
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/epoll.h>
#include "gni_pub.h"
#include "rca_lib.h"
#include "ldms.h"
#include "ldms_xprt.h"
#include "ldms_ugni.h"
#include "mmalloc/mmalloc.h"

#define VERSION_FILE "/proc/version"
/* Convenience macro for logging errors */
#define LOG_(x, level, ...) { if (x && x->xprt && x->xprt->log) x->xprt->log(level, __VA_ARGS__); }

/* 100000 because the Cray node names have only 5 digits, e.g, nid00000  */
#define UGNI_MAX_NUM_NODE 100000

static char *verfile = VERSION_FILE;
static struct ldms_ugni_xprt ugni_gxp;
int first;
uint8_t ptag;
uint32_t cookie;
int modes = 0;
int device_id = 0;
int cq_entries = 128;
int IS_GEMINI=0;
int IS_ARIES=0;
static int reg_count;

#define UGNI_INTERVAL_DEFAULT 1000000 /* micro seconds */
static unsigned long state_interval_us;
static unsigned long state_offset_us;
static struct event_base *node_state_base;
static pthread_t node_state_thread;
static int state_ready = 0;
static int get_state_success = 0;

LIST_HEAD(mh_list, ugni_mh) mh_list;
pthread_mutex_t ugni_mh_lock;

static struct event_base *io_event_loop;
static pthread_t io_thread;
static pthread_t cq_thread;

static ldms_log_fn_t ugni_log;
pthread_mutex_t ugni_lock;
// pthread_mutex_t ugni_list_lock;
// LIST_HEAD(ugni_list, ldms_ugni_xprt) ugni_list;
pthread_mutex_t desc_list_lock;
LIST_HEAD(desc_list, ugni_desc) desc_list;

struct ugni_rbuf_desc {
	struct ldms_rbuf_desc desc;
	struct ugni_buf_local_data ldata;
	struct ugni_buf_remote_data rdata;
	LIST_ENTRY(ugni_rbuf_desc) link;
};
LIST_HEAD(ugni_rbuf_list, ugni_rbuf_desc) ugni_rbuf_list;
pthread_mutex_t ugni_rbuf_lock = PTHREAD_MUTEX_INITIALIZER;

#define UGNI_NODE_PREFIX "nid"
#define UGNI_NODE_GOOD 7

static int *node_state;
static int rca_get_failed = 0;


static int calculate_node_state_timeout(unsigned long interval_us,
			       long offset_us, struct timeval* tv){

	  struct timeval new_tv;
	  long int adj_interval;
	  long int epoch_us;

	  /* NOTE: this uses libevent's cached time for the callback.
	     By the time we add the event we will be at least off by
	     the amount of time the thread takes to do its other funcationality.
	     We deem this accepable. */
	  event_base_gettimeofday_cached(node_state_base, &new_tv);

	  epoch_us = (1000000 * (long int)new_tv.tv_sec) +
		  (long int)new_tv.tv_usec;
	  adj_interval = interval_us - (epoch_us % interval_us) + offset_us;
	  /* Could happen initially, and later depending on when the event
	   actually occurs. However the max negative this can be, based on
	   the restrictions put in is (-0.5*interval+ 1us). Skip this next
	   point and go on to the next one.
	  */
	  if (adj_interval <= 0)
		  adj_interval += interval_us; /* Guaranteed to be positive */

	  tv->tv_sec = adj_interval/1000000;
	  tv->tv_usec = adj_interval % 1000000;

	  return 0;
}


int get_node_state()
{
	int i, node_id;
	rs_node_array_t nodelist;
	if (rca_get_sysnodes(&nodelist)) {
		rca_get_failed++;
		if ((rca_get_failed % 100) == 0) {
			ugni_log(LDMS_LERROR, "ugni: rca_get_sysnodes"
							" failed.\n");
		}

		for (i = 0; i < UGNI_MAX_NUM_NODE; i++)
			node_state[i] = UGNI_NODE_GOOD;

		state_ready = -1;
		return -1;
	}
	rca_get_failed = 0;
	for (i = 0; i < nodelist.na_len; i++) {
		assert(i < UGNI_MAX_NUM_NODE);
		node_id = nodelist.na_ids[i].rs_node_s._node_id;
		node_state[node_id] =
				nodelist.na_ids[i].rs_node_s._node_state;
	}
	free(nodelist.na_ids);
	state_ready = 1;
	return 0;
}

int get_nodeid(struct sockaddr *sa, socklen_t sa_len,
				struct ldms_ugni_xprt *gxp)
{
	int rc = 0;
	char host[HOST_NAME_MAX];
	rc = getnameinfo(sa, sa_len, host, HOST_NAME_MAX,
					NULL, 0, NI_NAMEREQD);
	if (rc) {
		ugni_log(LDMS_LERROR, "ugni: %s\n", gai_strerror(rc));
		return rc;
	}
	char *ptr = strstr(host, UGNI_NODE_PREFIX);
	if (!ptr) {
		ugni_log(LDMS_LINFO, "ugni: '%s', unexpected "
				"node name format\n", host);
		return -1;
	}
	ptr = 0;
	int id = strtol(host + strlen(UGNI_NODE_PREFIX), &ptr, 10);
	if (ptr[0] != '\0') {
		ugni_log(LDMS_LINFO, "ugni: '%s', unexpected "
				"node name format\n", host);
		return -1;
	}
	gxp->node_id = id;
	return 0;
}

int check_node_state(struct ldms_ugni_xprt *gxp)
{
	while (state_ready == 0) {
		/* wait for the state to be populated. */
		/* FIXME: what if cant read rca? */
	}

	if (node_state[gxp->node_id] != UGNI_NODE_GOOD)
		return 1; /* not good */

	return 0; /* good */
}

void node_state_cb(int fd, short sig, void *arg)
{
	struct timeval tv;
	struct event *ns = arg;
	get_node_state(); /* FIXME: what if this fails? */
	calculate_node_state_timeout(state_interval_us, state_offset_us, &tv);
	evtimer_add(ns, &tv);
}

void *node_state_proc(void *v)
{
	struct timeval tv;
	struct event *ns;

	ns = evtimer_new(node_state_base, node_state_cb, NULL);
	get_node_state(); /* FIXME: what if this fails? */
	evtimer_assign(ns, node_state_base, node_state_cb, ns);
	calculate_node_state_timeout(state_interval_us, state_offset_us, &tv);
	(void)evtimer_add(ns, &tv);
	event_base_loop(node_state_base, 0);
	ldms_log(LDMS_LINFO, "Exiting the node state thread\n");
	return NULL;
}

int node_state_thread_init()
{
	node_state = malloc(UGNI_MAX_NUM_NODE * sizeof(int));
	if (!node_state) {
		ugni_log(LDMS_LERROR, "Out of memory\n");
		errno = ENOMEM;
		return -1;
	}
	memset(node_state, UGNI_NODE_GOOD, UGNI_MAX_NUM_NODE);

	if (evthread_use_pthreads()) {
		ugni_log(LDMS_LERROR, "evthread_use_pthreads failed\n");
		return -1;
	}

	node_state_base = event_base_new();
	if (!node_state_base) {
		ugni_log(LDMS_LERROR, "Failed to init node_state_base\n");
		return -1;
	}

	int rc = 0;
	rc = pthread_create(&node_state_thread, NULL, node_state_proc, NULL);
	if (rc) {
		event_base_free(node_state_base);
		ugni_log(LDMS_LERROR, "%s\n", strerror(rc));
		return -1;
	}
	return 0;
}

static void *io_thread_proc(void *arg);
static void *cq_thread_proc(void *arg);

static void ugni_event(struct bufferevent *buf_event, short error, void *arg);
static void ugni_read(struct bufferevent *buf_event, void *arg);
static void ugni_write(struct bufferevent *buf_event, void *arg);

static void timeout_cb(int fd , short events, void *arg);
static struct ldms_ugni_xprt * setup_connection(struct ldms_ugni_xprt *x,
						int sockfd,
						struct sockaddr*remote_addr,
						socklen_t sa_len);
static int _setup_connection(struct ldms_ugni_xprt *r,
			      struct sockaddr *remote_addr, socklen_t sa_len);

static void ugni_xprt_error_handling(struct ldms_ugni_xprt *r);

#define UGNI_MAX_OUTSTANDING_BTE 8192
static gni_return_t ugni_job_setup(uint8_t *ptag, uint32_t cookie)
{
	gni_job_limits_t limits;
	gni_return_t grc;

	/* ptag=0 will be passed if XC30. Call GNI_GetPtag(0, cookie, &ptag) to return ptag associated with cookie */
	if (IS_ARIES) {
		if (*ptag == 0) {
			#ifdef GNI_FIND_ALLOC_PTAG
				grc = GNI_FIND_ALLOC_PTAG;
				if (grc)
					goto err;
			#else
				goto err;
			#endif
		}
	}

	/* Do not apply any resource limits */
	limits.mdd_limit = GNI_JOB_INVALID_LIMIT;
	limits.fma_limit = GNI_JOB_INVALID_LIMIT;
	limits.cq_limit = GNI_JOB_INVALID_LIMIT;

	/* This limits the fan-out of the aggregator */
	limits.bte_limit = UGNI_MAX_OUTSTANDING_BTE;

	/* Do not use an NTT */
	limits.ntt_size = 0;

	/* GNI_ConfigureJob():
	 * -device_id should always be 0 for XE
	 * -job_id should always be 0 (meaning "no job container created")
	 */
	pthread_mutex_lock(&ugni_lock);
	grc = GNI_ConfigureJob(0, 0, *ptag, cookie, &limits);
	pthread_mutex_unlock(&ugni_lock);
	return grc;
 err:
	return grc;
}

/*
 * We have to keep a dense array of CQ. So as CQ come and go, this array needs
 * to get rebuilt
 */
#define CQ_BUMP_COUNT 1024
static gni_cq_handle_t *cq_table = NULL; /* table of all CQ being monitored...on per host */
static int cq_table_count = 0;		 /* size of table */
static int cq_use_count = 0;		 /* elements consumed in the table */

/*
 * add_cq_table is an array of CQ that need to be added to the CQ
 * table. Entries are added to this table when the connection is made.
 */
static int add_cq_used = 0;
static gni_cq_handle_t add_cq_table[1024];
/*
 * rem_cq_table is an array of CQ that need to be removed from the CQ table.
 * Entries are added to this table when a connection goes away.
 */

static int rem_cq_used = 0;
static gni_cq_handle_t rem_cq_table[1024];

/*
 * The cq_table_lock serializes access to the add_cq_table and rem_cq_table.
 */
static pthread_mutex_t cq_table_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t cq_empty_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cq_empty_cv = PTHREAD_COND_INITIALIZER;

int desc_count = 0;
/*
 * Allocate an I/O request context. These descriptors contain the context for
 * RDMA submitted to the transport.
 */
static struct ugni_desc *alloc_desc(struct ldms_ugni_xprt *gxp)
{
	struct ugni_desc *desc = NULL;
	pthread_mutex_lock(&desc_list_lock);
	desc_count++;
	if (!LIST_EMPTY(&desc_list)) {
		desc = LIST_FIRST(&desc_list);
		LIST_REMOVE(desc, link);
	}
	pthread_mutex_unlock(&desc_list_lock);
	if (!desc)
		desc = calloc(1, sizeof *desc);
	// desc = desc ? desc : calloc(1, sizeof *desc);
	if (desc)
		desc->gxp = gxp;
	return desc;
}

/*
 * Free an I/O request context.
 */
static void free_desc(struct ugni_desc *desc)
{
	pthread_mutex_lock(&desc_list_lock);
	desc_count--;
	LIST_INSERT_HEAD(&desc_list, desc, link);
	pthread_mutex_unlock(&desc_list_lock);
}

static gni_return_t ugni_dom_init(uint8_t *ptag, uint32_t cookie, uint32_t inst_id,
				  struct ldms_ugni_xprt *gxp)
{
	gni_return_t grc;

	if (IS_GEMINI) {
		if (*ptag == 0)
			return GNI_RC_INVALID_PARAM;
	}

	pthread_mutex_lock(&ugni_lock);
	if (IS_GEMINI)
		grc = GNI_CdmCreate(inst_id, *ptag, cookie, GNI_CDM_MODE_FMA_SHARED,
			    &gxp->dom.cdm);
	else if (IS_ARIES) {
		#ifdef GNI_FIND_ALLOC_PTAG
			grc = GNI_CdmCreate(inst_id, GNI_FIND_ALLOC_PTAG, cookie, GNI_CDM_MODE_FMA_SHARED,
				&gxp->dom.cdm);
		#else
			goto err;
		#endif
	}
	else
		goto err;

	if (grc)
		goto err;

	grc = GNI_CdmAttach(gxp->dom.cdm, 0, &gxp->dom.info.pe_addr, &gxp->dom.nic);
	if (grc != GNI_RC_SUCCESS)
		goto err;

	if (IS_GEMINI)
		gxp->dom.info.ptag = *ptag;
	gxp->dom.info.cookie = cookie;
	gxp->dom.info.inst_id = inst_id;

	pthread_mutex_unlock(&ugni_lock);
	return GNI_RC_SUCCESS;
 err:
	pthread_mutex_unlock(&ugni_lock);
	return grc;
}

static struct ldms_ugni_xprt *ugni_from_xprt(ldms_t d)
{
	return ((struct ldms_xprt *)d)->private;
}

void ugni_xprt_cleanup(void)
{
	void *dontcare;

	if (io_event_loop)
		event_base_loopbreak(io_event_loop);
	if (io_thread) {
		pthread_cancel(io_thread);
		pthread_join(io_thread, &dontcare);
	}
	if (cq_thread) {
		pthread_cancel(cq_thread);
		pthread_join(cq_thread, &dontcare);
	}
	if (io_event_loop)
		event_base_free(io_event_loop);

	if (node_state_base)
		event_base_loopbreak(node_state_base);
	if (node_state_thread) {
		pthread_cancel(node_state_thread);
		pthread_join(node_state_thread, &dontcare);
	}
	if (node_state_base)
		event_base_free(node_state_base);

	if (node_state)
		free(node_state);
}

static void ugni_xprt_close(struct ldms_xprt *x)
{
	struct ldms_ugni_xprt *gxp = ugni_from_xprt(x);
	gni_return_t grc;
	struct bufferevent *buf_event;
	struct evconnlistener *listen_ev;

	pthread_mutex_lock(&ugni_lock);
	gxp->conn_status = CONN_IDLE;
	buf_event = gxp->buf_event;
	gxp->buf_event = NULL;
	listen_ev = gxp->listen_ev;
	gxp->listen_ev = NULL;
	assert(gxp->xprt);
	if (gxp->sock >= 0)
		close(gxp->sock);
	gxp->sock = -1;
	if (gxp->ugni_ep) {
		grc = GNI_EpDestroy(gxp->ugni_ep);
		if (grc != GNI_RC_SUCCESS)
			gxp->xprt->log(LDMS_LDEBUG,"Error %d destroying Ep %p.\n",
				grc, gxp->ugni_ep);
		gxp->ugni_ep = NULL;
	}
	pthread_mutex_unlock(&ugni_lock);
	if (buf_event)
		bufferevent_free(buf_event);
	if (listen_ev)
		evconnlistener_free(listen_ev);
}

static int set_nonblock(struct ldms_xprt *x, int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL);
	if (flags == -1) {
		x->log(LDMS_LDEBUG,"Error getting flags on fd %d", fd);
		return -1;
	}
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags)) {
		x->log(LDMS_LDEBUG,"Error setting non-blocking I/O on fd %d", fd);
		return -1;
	}
	return 0;
}

#define UGNI_CQ_DEPTH 2048
static int ugni_xprt_connect(struct ldms_xprt *x,
			     struct sockaddr *sa, socklen_t sa_len)
{
	struct ldms_ugni_xprt *gxp = ugni_from_xprt(x);
	struct sockaddr_storage ss;
	int cq_depth;
	char *cq_depth_s;
	int rc;
	gni_return_t grc;
	int epfd;
	int epcount;
	int fdcnt;
	struct epoll_event event;

	if (get_state_success) {
		node_state_thread_init(); /* FIXME - is this for every individual conn? */
		if (gxp->node_id == -1)
			if (get_nodeid(sa, sa_len, gxp))
				return -1;

		if (check_node_state(gxp)) {
			x->log(LDMS_LERROR, "node %d is in a bad state.\n",
							gxp->node_id);
			return -1;
		}
	}

	gxp->sock = socket(AF_INET, SOCK_STREAM, 0);
	if (gxp->sock < 0)
		return -1;
	gxp->type = LDMS_UGNI_ACTIVE;

	if (!ugni_gxp.dom.src_cq) {
		cq_depth_s = getenv("LDMS_UGNI_CQ_DEPTH");
		if (!cq_depth_s) {
			cq_depth = UGNI_CQ_DEPTH;
		} else {
			cq_depth = atoi(cq_depth_s);
			if (cq_depth == 0)
				cq_depth = UGNI_CQ_DEPTH;
		}

		pthread_mutex_lock(&ugni_lock);
		grc = GNI_CqCreate(gxp->dom.nic, cq_depth, 0,
				   GNI_CQ_BLOCKING, NULL, NULL, &ugni_gxp.dom.src_cq);
		pthread_mutex_unlock(&ugni_lock);
		if (grc != GNI_RC_SUCCESS) {
			gxp->xprt->log(LDMS_LDEBUG,"The CQ could not be created.\n");
			goto err;
		}
		gxp->dom.src_cq = ugni_gxp.dom.src_cq;
		cq_use_count = 1;
		pthread_mutex_lock(&cq_empty_lock);
		pthread_cond_signal(&cq_empty_cv);
		pthread_mutex_unlock(&cq_empty_lock);
	}

	pthread_mutex_lock(&ugni_lock);
	grc = GNI_EpCreate(gxp->dom.nic, gxp->dom.src_cq, &gxp->ugni_ep);
	pthread_mutex_unlock(&ugni_lock);
	if (grc != GNI_RC_SUCCESS)
		goto err;

	fcntl(gxp->sock, F_SETFL, O_NONBLOCK);
	epfd = epoll_create(1);
	if (epfd < 0)
		goto err1;
	rc = connect(gxp->sock, sa, sa_len);
	if (errno != EINPROGRESS) {
		close(epfd);
		goto err1;
	}
	event.events = EPOLLIN | EPOLLOUT | EPOLLHUP;
	event.data.fd = gxp->sock;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, gxp->sock, &event)) {
		close(epfd);
		goto err1;
	}
	epcount = epoll_wait(epfd, &event, 1, 5000 /* 5s */);
	close(epfd);
	if (!epcount || (event.events & (EPOLLERR | EPOLLHUP)))
		goto err1;

	fcntl(gxp->sock, F_SETFL, ~O_NONBLOCK);
	sa_len = sizeof(ss);
	rc = getsockname(gxp->sock, (struct sockaddr *)&ss, &sa_len);
	if (rc)
		goto err1;
	if (_setup_connection(gxp, (struct sockaddr *)&ss, sa_len))
		goto err1;

	/*
	 * When we receive the peer's hello request, we will bind the endpoint
	 * to his PE and move to connected.
	 */
	return 0;

err1:
	pthread_mutex_lock(&ugni_lock);
	grc = GNI_EpDestroy(gxp->ugni_ep);
	close(gxp->sock);
	gxp->sock = -1;
	pthread_mutex_unlock(&ugni_lock);
	if (grc != GNI_RC_SUCCESS)
		gxp->xprt->log(LDMS_LDEBUG,"Error %d destroying Ep %p.\n",
				grc, gxp->ugni_ep);
	gxp->ugni_ep = NULL;
err:
	return -1;
}

/*
 * Received by the active side (aggregator/ldms_ls node).
 */
int process_ugni_hello_req(struct ldms_ugni_xprt *x, struct ugni_hello_req *req)
{
	int rc;
	x->rem_pe_addr = ntohl(req->pe_addr);
	x->rem_inst_id = ntohl(req->inst_id);
	pthread_mutex_lock(&ugni_lock);
	rc = GNI_EpBind(x->ugni_ep, x->rem_pe_addr, x->rem_inst_id);
	pthread_mutex_unlock(&ugni_lock);
	if (rc == GNI_RC_SUCCESS)
		x->xprt->connected = 1;
	return rc;
}

int process_ugni_msg(struct ldms_ugni_xprt *x, struct ldms_request *req)
{
	switch (ntohl(req->hdr.cmd)) {
	case UGNI_HELLO_REQ_CMD:
		return process_ugni_hello_req(x, (struct ugni_hello_req *)req);
	default:
		x->xprt->log(LDMS_LDEBUG,"Invalid request on uGNI transport %d\n",
			     ntohl(req->hdr.cmd));
	}
	return EINVAL;
}

static int process_xprt_io(struct ldms_ugni_xprt *s, struct ldms_request *req)
{
	int cmd;

	cmd = ntohl(req->hdr.cmd);

	/* The sockets transport must handle solicited read */
	if (cmd & LDMS_CMD_XPRT_PRIVATE) {
		int ret = process_ugni_msg(s, req);
		if (ret) {
			s->xprt->log(LDMS_LDEBUG,"Error %d processing transport request.\n",
				     ret);
			goto close_out;
		}
	} else
		s->xprt->recv_cb(s->xprt, req);
	return 0;
 close_out:
	ugni_xprt_error_handling(s);
	return -1;
}

static void ugni_write(struct bufferevent *buf_event, void *arg)
{
}

#define min_t(t, x, y) (t)((t)x < (t)y?(t)x:(t)y)
static void ugni_read(struct bufferevent *buf_event, void *arg)
{

	struct ldms_ugni_xprt *r = (struct ldms_ugni_xprt *)arg;
	struct evbuffer *evb;
	struct ldms_request_hdr hdr;
	struct ldms_request *req;
	size_t len;
	size_t reqlen;
	size_t buflen;
	do {
		evb = bufferevent_get_input(buf_event);
		buflen = evbuffer_get_length(evb);
		if (buflen < sizeof(hdr))
			break;
		evbuffer_copyout(evb, &hdr, sizeof(hdr));
		reqlen = ntohl(hdr.len);
		if (buflen < reqlen)
			break;
		req = malloc(reqlen);
		if (!req) {
			r->xprt->log(LDMS_LDEBUG,"%s Memory allocation failure reqlen %zu\n",
				     __FUNCTION__, reqlen);
			ugni_xprt_error_handling(r);
			break;
		}
		len = evbuffer_remove(evb, req, reqlen);
		assert(len == reqlen);
		if (r->conn_status == CONN_CONNECTED)
			process_xprt_io(r, req);
		free(req);
	} while (1);
}

static void *io_thread_proc(void *arg)
{
	int oldtype;
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);
	event_base_dispatch(io_event_loop);
	return NULL;
}

static gni_return_t process_cq(gni_cq_handle_t cq, gni_cq_entry_t cqe)
{
	gni_return_t grc;
	gni_post_descriptor_t *post;
	do {
		if (GNI_CQ_GET_TYPE(cqe) != GNI_CQ_EVENT_TYPE_POST) {
			ugni_log(LDMS_LDEBUG,"Unexepcted cqe type %d cqe %08x on CQ %p\n",
				 GNI_CQ_GET_TYPE(cqe), cqe, cq);
			continue;
		}
		pthread_mutex_lock(&ugni_lock);
		post = NULL;
		grc = GNI_GetCompleted(cq, cqe, &post);
		pthread_mutex_unlock(&ugni_lock);
		if (grc) {
			/* ugni_log(LDMS_LDEBUG,"Error %d from CQ %p\n", grc, cq); Removed by Brandt 6-14-2014 */
			if (!(grc == GNI_RC_SUCCESS ||
			      grc == GNI_RC_TRANSACTION_ERROR))
				continue;
		}
		struct ugni_desc *desc = (struct ugni_desc *)post;
		if (!desc) {
			ugni_log(LDMS_LDEBUG,"Post descriptor is Null!\n");
			continue;
		}
#if 0
		if (grc) {
			/* ugni_log(LDMS_LDEBUG,"%s GNI_GetCompleted failed with %d, desc = %p.\n", desc); Removed by Brandt 6-14-2014 */
			/* The request failed, tear down the transport */
			if (desc->gxp->xprt)
				ugni_xprt_error_handling(desc->gxp);
			goto skip;
		}
#endif
		switch (desc->post.type) {
		case GNI_POST_RDMA_GET:
			if (grc)
				ugni_log(LDMS_LDEBUG,"%s update completing with error %d.\n", __func__, grc);
			if (desc->gxp->xprt && desc->gxp->xprt->read_complete_cb)
				desc->gxp->xprt->
					read_complete_cb(desc->gxp->xprt,
							 desc->context, grc);
			break;
		default:
			if (desc->gxp)
				desc->gxp->xprt->log(LDMS_LDEBUG,"Unknown completion "
						     "type %d on transport "
						     "%p.\n",
						     desc->post.type, desc->gxp);
		}
	skip:
		free_desc(desc);
		pthread_mutex_lock(&ugni_lock);
		grc = GNI_CqGetEvent(cq, &cqe);
		pthread_mutex_unlock(&ugni_lock);
	} while (grc == GNI_RC_SUCCESS);

	return GNI_RC_SUCCESS;
}

#define WAIT_20SECS 20000
static void *cq_thread_proc(void *arg)
{
	gni_return_t grc;
	gni_cq_entry_t event_data;
	gni_cq_entry_t cqe;
	uint32_t which;
	int oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);
	while (1) {
		uint64_t timeout = WAIT_20SECS;
		pthread_mutex_lock(&cq_empty_lock);
		while (!cq_use_count)
			pthread_cond_wait(&cq_empty_cv, &cq_empty_lock);
		pthread_mutex_unlock(&cq_empty_lock);

		grc = GNI_CqWaitEvent(ugni_gxp.dom.src_cq, timeout, &cqe);
		if (grc == GNI_RC_TIMEOUT)
			continue;
		if (grc = process_cq(ugni_gxp.dom.src_cq, cqe))
			ugni_log(LDMS_LDEBUG,"Error %d processing CQ %p.\n",
				 grc, ugni_gxp.dom.src_cq);
	}
	return NULL;
}

static void ugni_event(struct bufferevent *buf_event, short events, void *arg)
{
	struct ldms_ugni_xprt *r = arg;

	if (events & ~BEV_EVENT_CONNECTED) {
		/* Peer disconnect or other error */
		if (events & (BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT))
			LOG_(r, LDMS_LDEBUG, "Socket errors %x\n", events);
		r->xprt->connected = 0;
		r->conn_status = CONN_IDLE;
		if (r->type == LDMS_UGNI_PASSIVE)
			ldms_xprt_close(r->xprt);
	} else
		LOG_(r, LDMS_LDEBUG, "Peer connect complete %x\n", events);
}

static int _setup_connection(struct ldms_ugni_xprt *gxp,
			      struct sockaddr *remote_addr, socklen_t sa_len)
{
	int rc;

	gxp->conn_status = CONN_CONNECTED;
	memcpy((char *)&gxp->xprt->remote_ss, (char *)remote_addr, sa_len);
	gxp->xprt->ss_len = sa_len;

	if (set_nonblock(gxp->xprt, gxp->sock))
		LOG_(gxp, LDMS_LDEBUG,"Warning: error setting non-blocking I/O on an "
			     "incoming connection.\n");

	/* Initialize send and recv I/O events */
	gxp->buf_event = bufferevent_socket_new(io_event_loop, gxp->sock, BEV_OPT_THREADSAFE);
	if(!gxp->buf_event) {
		LOG_(gxp, LDMS_LDEBUG, "Error initializing buffered I/O event for "
		     "fd %d.\n", gxp->sock);
		goto err_0;
	}

	bufferevent_setcb(gxp->buf_event, ugni_read, ugni_write, ugni_event, gxp);
	if (bufferevent_enable(gxp->buf_event, EV_READ | EV_WRITE)) {
		LOG_(gxp, LDMS_LDEBUG, "Error enabling buffered I/O event for fd %d.\n",
		     gxp->sock);
		goto err_0;
	}

	return 0;

 err_0:
	return -1;
}

static struct ldms_ugni_xprt *
setup_connection(struct ldms_ugni_xprt *p, int sockfd,
		 struct sockaddr *remote_addr, socklen_t sa_len)
{
	struct ldms_ugni_xprt *r;
	ldms_t _x;

	/* Create a transport instance for this new connection */
	_x = ldms_create_xprt("ugni", p->xprt->log);
	if (!_x) {
		p->xprt->log(LDMS_LDEBUG,"Could not create a new transport.\n");
		close(sockfd);
		return NULL;
	}

	r = ugni_from_xprt(_x);
	r->type = LDMS_UGNI_PASSIVE;
	r->sock = sockfd;
	r->xprt->local_ss = p->xprt->local_ss;
	if (_setup_connection(r, remote_addr, sa_len)) {
		ugni_xprt_error_handling(r);
		return NULL;
	}
	return r;
}

struct timeval listen_tv;
struct timeval report_tv;

static void ugni_connect_req(struct evconnlistener *listener,
			     evutil_socket_t sockfd,
			     struct sockaddr *address, int socklen, void *arg)
{
	struct ldms_ugni_xprt *gxp = arg;
	struct ldms_ugni_xprt *new_gxp = NULL;
	static int conns;

	new_gxp = setup_connection(gxp, sockfd,
				   (struct sockaddr *)&address, socklen);
	if (new_gxp)
		conns ++;
	else
		goto setup_conn_err;

	struct timeval connect_tv;
	gettimeofday(&connect_tv, NULL);

	if ((connect_tv.tv_sec - report_tv.tv_sec) >= 10) {
		double rate;
		rate = (double)conns / (double)(connect_tv.tv_sec - report_tv.tv_sec);
		/* gxp->xprt->log(LDMS_LDEBUG,"Connection rate is %.2f/second\n", rate); Removed by Brandt 7-1-2014 */
		report_tv = connect_tv;
		conns = 0;
	}
	/*
	 * Send peer (aggregator/ldms_ls) our PE so he can bind to us.
	 */
	struct ugni_hello_req req;
	req.hdr.cmd = htonl(UGNI_HELLO_REQ_CMD);
	req.hdr.len = htonl(sizeof(req));
	req.hdr.xid = (uint64_t)(unsigned long)0; /* no response to hello */
	req.pe_addr = htonl(new_gxp->dom.info.pe_addr);
	req.inst_id = htonl(new_gxp->dom.info.inst_id);
	(void)bufferevent_write(new_gxp->buf_event, &req, sizeof(req));

	return;

setup_conn_err:
	gxp->xprt->log(LDMS_LDEBUG,"Cannot setup connection.\n");
}

static int ugni_xprt_listen(struct ldms_xprt *x, struct sockaddr *sa, socklen_t sa_len)
{
	int rc;
	struct ldms_ugni_xprt *gxp = ugni_from_xprt(x);
	int optval = 1;

	gettimeofday(&listen_tv, NULL);
	report_tv = listen_tv;

	gxp->sock = socket(PF_INET, SOCK_STREAM, 0);
	if (gxp->sock < 0) {
		rc = errno;
		goto err_0;
	}

	setsockopt(gxp->sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);

	if (set_nonblock(x, gxp->sock))
		x->log(LDMS_LDEBUG,"Warning: Could not set listening socket to non-blocking\n");

	rc = ENOMEM;
	gxp->listen_ev = evconnlistener_new_bind(io_event_loop, ugni_connect_req, gxp,
						 LEV_OPT_THREADSAFE | LEV_OPT_REUSEABLE,
						 1024, sa, sa_len);
	if (!gxp->listen_ev)
		goto err_0;

	gxp->sock = evconnlistener_get_fd(gxp->listen_ev);
	return 0;
 err_0:
	ldms_xprt_close(gxp->xprt);
	return rc;
}

static void ugni_xprt_destroy(struct ldms_xprt *x)
{
	struct ldms_ugni_xprt *gxp = ugni_from_xprt(x);
	assert(NULL == gxp->listen_ev);
	assert(NULL == gxp->buf_event);
	free(gxp);
	free(x);
}

static int ugni_xprt_send(struct ldms_xprt *x, void *buf, size_t len)
{
	struct ldms_ugni_xprt *r = ugni_from_xprt(x);
	if (get_state_success && (check_node_state(r))) {
		x->log(LDMS_LERROR, "node %d is in a bad state.\n",
						r->node_id);
		return -1;
	}


	int rc;

	if (r->conn_status != CONN_CONNECTED)
		return -ENOTCONN;

	rc = bufferevent_write(r->buf_event, buf, len);
	return rc;
}

gni_return_t ugni_get_mh(struct ldms_ugni_xprt *gxp,
			 void *addr, size_t size, gni_mem_handle_t *mh)
{
	gni_return_t grc = GNI_RC_SUCCESS;
	struct ugni_mh *umh;
	int need_mh = 0;
	unsigned long start;
	unsigned long end;

	pthread_mutex_lock(&ugni_mh_lock);
	umh = LIST_FIRST(&mh_list);
	if (!umh) {
		struct mm_info mmi;
		mm_get_info(&mmi);
		start = (unsigned long)mmi.start;
		end = start + mmi.size;
		need_mh = 1;
	}
	if (!need_mh)
		goto out;

	umh = malloc(sizeof *umh);
	umh->start = start;
	umh->end = end;
	umh->ref_count = 0;

	grc = GNI_MemRegister(gxp->dom.nic, umh->start, end - start,
			      NULL,
			      GNI_MEM_READWRITE | GNI_MEM_RELAXED_PI_ORDERING,
			      -1, &umh->mh);
	if (grc != GNI_RC_SUCCESS) {
		free(umh);
		goto out;
	}
	LIST_INSERT_HEAD(&mh_list, umh, link);
	reg_count++;
out:
	*mh = umh->mh;
	umh->ref_count++;
	pthread_mutex_unlock(&ugni_mh_lock);
	return grc;
}

void ugni_rbuf_free(struct ldms_xprt *x, struct ldms_rbuf_desc *desc)
{
	struct ugni_rbuf_desc *udesc = container_of(desc, struct ugni_rbuf_desc, desc);
	pthread_mutex_lock(&ugni_rbuf_lock);
	if (desc->lcl_data)
		assert(desc->lcl_data == &udesc->ldata);
	if (desc->xprt_data)
		assert(desc->xprt_data == &udesc->rdata);
	assert(udesc->link.le_prev == NULL);
	assert(udesc->link.le_next == NULL);
	LIST_INSERT_HEAD(&ugni_rbuf_list, udesc, link);
	pthread_mutex_unlock(&ugni_rbuf_lock);
}

/*
 * Allocate a remote buffer. If we are the producer, the xprt_data
 * will be NULL. In this case, we fill in the local side
 * information.
 */
struct ldms_rbuf_desc *ugni_rbuf_alloc(struct ldms_xprt *x,
				       struct ldms_set *set,
				       void *xprt_data,
				       size_t xprt_data_len)
{
	gni_return_t grc;
	struct ldms_ugni_xprt *gxp = ugni_from_xprt(x);
	struct ugni_buf_local_data *lcl_data;
	struct ldms_rbuf_desc *desc;
	struct ugni_rbuf_desc *udesc;

	pthread_mutex_lock(&ugni_rbuf_lock);
	if (!LIST_EMPTY(&ugni_rbuf_list)) {
		udesc = LIST_FIRST(&ugni_rbuf_list);
		LIST_REMOVE(udesc, link);
		memset(udesc, 0, sizeof(*udesc));
	} else
		udesc = calloc(1, sizeof *udesc);
	pthread_mutex_unlock(&ugni_rbuf_lock);
	if (!udesc)
		return NULL;
	desc = &udesc->desc;

	lcl_data = &udesc->ldata;
	lcl_data->meta = set->meta;
	lcl_data->meta_size = set->meta->meta_size;
	lcl_data->data = set->data;
	lcl_data->data_size = set->meta->data_size;
	grc = ugni_get_mh(gxp, set->meta, set->meta->meta_size, &lcl_data->meta_mh);
	if (grc)
		goto err;
	grc = ugni_get_mh(gxp, set->data, set->meta->data_size, &lcl_data->data_mh);
	if (grc)
		goto err;

	desc->lcl_data = lcl_data;

	if (xprt_data) {
		assert(xprt_data_len == sizeof(udesc->rdata));
		desc->xprt_data_len = xprt_data_len;
		desc->xprt_data = &udesc->rdata;
		memcpy(desc->xprt_data, xprt_data, xprt_data_len);
	} else {
		struct ugni_buf_remote_data *rem_data = &udesc->rdata;
		rem_data->meta_buf = (uint64_t)(unsigned long)lcl_data->meta;
		rem_data->meta_size = htonl(lcl_data->meta_size);
		rem_data->meta_mh = lcl_data->meta_mh;
		rem_data->data_buf = (uint64_t)(unsigned long)lcl_data->data;
		rem_data->data_size = htonl(lcl_data->data_size);
		rem_data->data_mh = lcl_data->data_mh;
		desc->xprt_data = rem_data;
		desc->xprt_data_len = sizeof(*rem_data);
	}
	return desc;
 err:
	ugni_rbuf_free(x, desc);
	gxp->xprt->log(LDMS_LDEBUG,"RBUF allocation failed. Registration count is %d\n", reg_count);
	return NULL;
}

static int ugni_read_start(struct ldms_ugni_xprt *gxp,
			   uint64_t laddr, gni_mem_handle_t local_mh,
			   uint64_t raddr, gni_mem_handle_t remote_mh,
			   uint32_t len, void *context)
{
	if (get_state_success && (check_node_state(gxp))) {
		ugni_log(LDMS_LERROR, "node %d is in a bad state.\n",
							gxp->node_id);
		return -1;
	}

	gni_return_t grc;
	struct ugni_desc *desc = alloc_desc(gxp);
	if (!desc)
		return ENOMEM;

	desc->post.type = GNI_POST_RDMA_GET;
	desc->post.cq_mode = GNI_CQMODE_GLOBAL_EVENT;
	desc->post.dlvr_mode = GNI_DLVMODE_PERFORMANCE;
	desc->post.local_addr = laddr;
	desc->post.local_mem_hndl = local_mh;
	desc->post.remote_addr = raddr;
	desc->post.remote_mem_hndl = remote_mh;
	desc->post.length = (len + 3) & ~3;
	desc->post.post_id = (uint64_t)(unsigned long)desc;
	desc->context = context;
	pthread_mutex_lock(&ugni_lock);
	grc = GNI_PostRdma(gxp->ugni_ep, &desc->post);
	pthread_mutex_unlock(&ugni_lock);
	if (grc != GNI_RC_SUCCESS)
		return -1;
	return 0;
}

static int ugni_read_meta_start(struct ldms_xprt *x, ldms_set_t s, size_t len, void *context)
{
	struct ldms_ugni_xprt *gxp = ugni_from_xprt(x);
	struct ldms_set_desc *sd = s;
	struct ugni_buf_remote_data* rbuf = sd->rbd->xprt_data;
	struct ugni_buf_local_data* lbuf = sd->rbd->lcl_data;

	int rc = ugni_read_start(gxp,
				 (uint64_t)(unsigned long)lbuf->meta,
				 lbuf->meta_mh,
				 rbuf->meta_buf,
				 rbuf->meta_mh,
				 (len?len:ntohl(rbuf->meta_size)),
				 context);
	return rc;
}

static int ugni_read_data_start(struct ldms_xprt *x, ldms_set_t s, size_t len, void *context)
{
	struct ldms_ugni_xprt *gxp = ugni_from_xprt(x);
	struct ldms_set_desc *sd = s;
	struct ugni_buf_remote_data* rbuf = sd->rbd->xprt_data;
	struct ugni_buf_local_data* lbuf = sd->rbd->lcl_data;

	int rc = ugni_read_start(gxp,
				 (uint64_t)(unsigned long)lbuf->data,
				 lbuf->data_mh,
				 rbuf->data_buf,
				 rbuf->data_mh,
				 (len?len:ntohl(rbuf->data_size)),
				 context);
	return rc;
}

static void ugni_xprt_error_handling(struct ldms_ugni_xprt *r)
{
	r->xprt->log(LDMS_LDEBUG,"%s error on transport %p.\n", __func__, r);
	ldms_t x = r->xprt;
	if (x) {
		ldms_xprt_get(x);
		ldms_xprt_close(x);
		ldms_release_xprt(x);
	}
}

static struct timeval to;
static struct event *keepalive;
static void timeout_cb(int s, short events, void *arg)
{
	to.tv_sec = 10;
	to.tv_usec = 0;
	evtimer_add(keepalive, &to);
}

int get_state_interval()
{
	char *thr = getenv("LDMS_UGNI_STATE_INTERVAL");
	if (!thr) {
		state_interval_us = 0;
		state_offset_us = 0;
		return 0;
	}
	char *ptr;
	int tmp = strtol(thr, &ptr, 10);
	if (ptr[0] != '\0') {
		ugni_log(LDMS_LERROR, "Invalid "
			"LDMS_UGNI_STATE_INTERVAL value\n");
		return -1;
	}
	if (tmp < 100000) {
		state_interval_us = 100000;
		ugni_log(LDMS_LERROR, "Invalid "
			"LDMS_UGNI_STATE_INTERVAL value. Using 100ms.\n");
	} else {
		state_interval_us = tmp;
	}

	thr = getenv("LDMS_UGNI_STATE_OFFSET");
	if (!thr) {
		state_offset_us = 0;
		return 0;
	}

	tmp = strtol(thr, &ptr, 10);
	if (ptr[0] != '\0') {
		ugni_log(LDMS_LERROR, "Invalid "
			"LDMS_UGNI_STATE_OFFSET value\n");
		return -1;
	}
	if ( !(state_interval_us >= labs(state_offset_us)*2) ){ /* FIXME: What should this check be ? */
		ugni_log(LDMS_LERROR, "Invalid "
			"LDMS_UGNI_STATE_OFFSET value. Using 0ms.\n");
		state_offset_us = 0;
	} else {
		state_offset_us = tmp;
	}

	get_state_success = 1;
	return 0;
}

static int once = 0;
static int init_once(ldms_log_fn_t log_fn)
{
	int rc = ENOMEM;
	uint8_t ptag = 0;
	uint32_t cookie = 0;
	char* ptag_str = NULL;
	char* cookie_str = NULL;
	uid_t euid = 0;
	char *errpath = NULL;
	char *rc_setup_errpath = NULL;
	char *rc_init_errpath = NULL;
	FILE *rcsfp = NULL;
	FILE *rcifp = NULL;
	FILE *vfp = NULL;
	char lbuf[256];
	char *s;

	vfp = fopen(verfile, "r");
	s = fgets(lbuf, sizeof(lbuf), vfp);
	fclose (vfp);
	if (strstr(lbuf, "cray_gem") != NULL)
		IS_GEMINI = 1;
	if (strstr(lbuf, "cray_ari") != NULL)
		IS_ARIES = 1;
	vfp = NULL;
	evthread_use_pthreads();
	// pthread_mutex_init(&ugni_list_lock, 0);
	pthread_mutex_init(&desc_list_lock, 0);
	pthread_mutex_init(&ugni_mh_lock, 0);
	io_event_loop = event_base_new();
	if (!io_event_loop)
		return errno;

	keepalive = evtimer_new(io_event_loop, timeout_cb, NULL);
	if (!keepalive)
		goto err_1;

	to.tv_sec = 1;
	to.tv_usec = 0;
	evtimer_add(keepalive, &to);

	rc = pthread_create(&io_thread, NULL, io_thread_proc, 0);
	if (rc)
		goto err_1;

	rc = pthread_create(&cq_thread, NULL, cq_thread_proc, 0);
	if (rc)
		goto err_2;

	if (IS_GEMINI) {
		ptag_str = getenv("LDMS_UGNI_PTAG");
		if (!ptag_str) {
			rc = EINVAL;
			log_fn(LDMS_LERROR, "Missing LDMS_UGNI_PTAG\n");
			goto err_3;
		}
		ptag = atoi(ptag_str);
	}

	cookie_str = getenv("LDMS_UGNI_COOKIE");
	if (!cookie_str) {
		rc = EINVAL;
		log_fn(LDMS_LERROR, "Missing LDMS_UGNI_COOKIE\n");
		goto err_3;
	}
	cookie = strtol(cookie_str, NULL, 0);

	errpath = getenv("LDMSD_ERRPATH");
	if (errpath) {
		rc_setup_errpath = malloc(strlen(errpath) + 10);
		if (!rc_setup_errpath) {
			rc = ENOMEM;
			goto err_3;
		}
		rc = sprintf(rc_setup_errpath, "%s/%s", errpath, "rc_setup");
		if (!rc) {
			rc = EINVAL;
			goto err_3;
		}
		rcsfp = fopen(rc_setup_errpath, "w");
		if (!rcsfp) {
			rc = EPERM;
			goto err_3;
		}

		rc_init_errpath = malloc(strlen(errpath) + 10);
		if (!rc_init_errpath) {
			rc = ENOMEM;
			goto err_3;
		}
		rc = sprintf(rc_init_errpath, "%s/%s", errpath, "rc_init");
		if (!rc) {
			rc = EINVAL;
			goto err_3;
		}
		rcifp = fopen(rc_init_errpath, "w");
		if (!rcifp) {
			rc = EPERM;
			goto err_3;
		}
	}

	rc = 0;
	if (IS_GEMINI) {
		euid = geteuid();
		if ((int) euid == 0){
			rc = ugni_job_setup(&ptag, cookie);
			if (rc != GNI_RC_SUCCESS)
				log_fn(LDMS_LERROR, "ugni_job_setup failed %d\n",rc);
			if (rcsfp) {
				fprintf(rcsfp, "%d\n", rc);
				fflush(rcsfp);
			}
		}
		if (rcsfp)
			fclose(rcsfp);
		rcsfp = NULL;
	}

	if (((int) euid != 0) || rc == GNI_RC_SUCCESS || IS_ARIES ) {
		rc = ugni_dom_init(&ptag, cookie, getpid(), &ugni_gxp);
		if (rc != GNI_RC_SUCCESS) {
			log_fn(LDMS_LERROR, "ugni_dom_init failed %d\n",rc);
		}
		if (rcifp) {
			fprintf(rcifp, "%d\n", rc);
			fflush(rcifp);
		}
	}
	if (rcifp)
		fclose(rcifp);
	rcifp = NULL;

	if (rc)
		goto err_3;

	/* node state */
	node_state = NULL;
	get_state_interval();

	atexit(ugni_xprt_cleanup);
	return 0;
 err_3:
	pthread_cancel(cq_thread);
 err_2:
	pthread_cancel(io_thread);
 err_1:
	if (rcsfp)
		fclose(rcsfp);
	if (rcifp)
		fclose(rcifp);
	rcsfp = NULL;
	rcifp = NULL;
	event_base_free(io_event_loop);
	io_event_loop = NULL;
	return rc;
}

struct ldms_xprt *xprt_get(recv_cb_t recv_cb,
			   read_complete_cb_t read_complete_cb,
			   ldms_log_fn_t log_fn)
{
	struct ldms_xprt *x;
	struct ldms_ugni_xprt *gxp;

	if (!ugni_log)
		ugni_log = log_fn;
	if (!once) {
		int rc = init_once(log_fn);
		if (rc) {
			errno = rc;
			goto err_0;
		}
		once = 1;
	}

	x = calloc(1, sizeof (*x));
	if (!x) {
		errno = ENOMEM;
		goto err_0;
	}

	gxp = calloc(1, sizeof(struct ldms_ugni_xprt));
	*gxp = ugni_gxp;
	// LIST_INSERT_HEAD(&ugni_list, gxp, client_link);

	gxp->conn_status = CONN_IDLE;
	gxp->node_id = -1;
	x->max_msg = (1024*1024);
	x->log = log_fn;
	x->connect = ugni_xprt_connect;
	x->listen = ugni_xprt_listen;
	x->destroy = ugni_xprt_destroy;
	x->close = ugni_xprt_close;
	x->send = ugni_xprt_send;
	x->read_meta_start = ugni_read_meta_start;
	x->read_data_start = ugni_read_data_start;
	x->read_complete_cb = read_complete_cb;
	x->recv_cb = recv_cb;
	x->alloc = ugni_rbuf_alloc;
	x->free = ugni_rbuf_free;
	x->private = gxp;
	gxp->xprt = x;
	return x;

 err_0:
	return NULL;
}

static void __attribute__ ((constructor)) ugni_init();
static void ugni_init()
{
}

static void __attribute__ ((destructor)) ugni_fini(void);
static void ugni_fini()
{
	gni_return_t grc;
	struct ugni_mh *mh;
	while (!LIST_EMPTY(&mh_list)) {
		mh = LIST_FIRST(&mh_list);
		LIST_REMOVE(mh, link);
		(void)GNI_MemDeregister(ugni_gxp.dom.nic, &mh->mh);
		free(mh);
	}
}
