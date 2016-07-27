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
/**
 * \file bin_metric.c
 * \author Narate Taerat (narate at ogc dot us)
 */
/**
 * \page bin_metric.config metric input plugin configuration
 *
 *
 * \section synopsis SYNOPSIS
 *
 * <b>plugin name=bin_metric bin_file=PATH port=PORT</b>
 *
 *
 * \section description DESCRIPTION
 *
 * The plugin listens on a port and receives metric value stream. Upon metric
 * data arrival, the plugin generates an input entry for balerd to process
 * according to metric binning definition in the \b bin_file. In other words,
 * this input plugin converts metric values into the form of "MetricX in
 * range[A, B)" events.
 *
 *
 * \section bin_file_format BIN_FILE FORMAT
 *
 * \b bin_file contains only lines of metric binning definition. A metric
 * binning definition has the following format:
 *
 * <pre>
 * <METRIC_NAME>:<BIN1_LOWERBOUND>,<BIN2_LOWERBOUND>,...,<BINX_LOWERBOUND>
 * </pre>
 *
 * The ranges of metric produced by the definition are:
 *   (-Inf, BIN1_LOWERBOUND), [BIN1_LOWERBOUND, BIN2_LOWERBOUND), ...
 *   [BINX-1_LOWERBOUND, BINX_LOWERBOUND), [BINX_LOWERBOUND, +Inf)
 *
 * \section bin_file_example BIN_FILE EXAMPLE
 * <pre>
 * percentage_of_MemFree:0,10,20,30,40,50,60,70,80,90
 * cpu_user:0,10,20,30,40,50,60,70,80,90
 * </pre>
 */

/**
 *
 * \defgroup bin_metric_dev Metric input plugin development documentation.
 * \{
 */
#include "baler/binput.h"
#include "baler/butils.h"
#include <pthread.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/thread.h>

#include "bin_metric.h"

#include "baler/bhash.h"

#define BIN_METRIC_DEFAULT_PORT 30003u

struct bhash *binhash;

typedef enum {
	PSTATUS_STOPPED=0,
	PSTATUS_RUNNING
} plugin_status_t;

/**
 * This structure stores context of this input plugin.
 */
struct bin_metric_ctxt {
	pthread_t thread; /**< Thread. */
	uint16_t port; /**< Port number to listen to. */
	int status; /**< Status of the plugin. */
};

/**
 * Context for a bufferevent socket connection.
 * Now only contain plugin, but might have more stuffs
 * later ...
 */
struct bin_metric_conn_ctxt {
	struct bplugin *plugin; /**< Plugin instance. */
};

/**
 * Function for freeing connection context.
 * \param ctxt The context to be freed.
 */
static
void bin_metric_conn_ctxt_free(struct bin_metric_conn_ctxt *ctxt)
{
	free(ctxt);
}

/**
 * This function prepare ::bwq_entry from the given ::bstr \a s.
 * \param s The entire raw log message in ::bstr structure.
 * \return NULL on error.
 * \return A pointer to ::bwq_entry on success.
 */
static
struct bwq_entry* prepare_bwq_entry(struct bin_metric_msg *m)
{
	struct bhash_entry *hent;
	struct bmetricbin *bin;
	struct bstr_list_entry *lent;
	int idx;
	struct bwq_entry *qent = calloc(1, sizeof(*qent));
	if (!qent) {
		berror("Out of memory");
		return NULL;
	}
	struct binq_data *in_data = &qent->data.in;
	struct bstr_list_head *tok_head = &in_data->tokens;
	struct bstr_list_entry *tok_tail = NULL;
	in_data->type = BINQ_DATA_METRIC;
	in_data->hostname = bstr_alloc(sizeof(uint32_t));
	if (!in_data->hostname) {
		berror("Out of memory");
		goto err0;
	}
	in_data->hostname->blen = sizeof(uint32_t);
	in_data->hostname->u32str[0] = m->comp_id;
	in_data->tv.tv_sec = m->sec;
	in_data->tv.tv_usec = 0;
	LIST_INIT(tok_head);

	hent = bhash_entry_get(binhash, m->name.cstr, m->name.blen);

	if (!hent) {
		berr("bin_metric: cannot find metric '%.*s'",
					m->name.blen, m->name.cstr);
		goto err0;
	}

	bin = (void*)hent->value;
	idx = bmetricbin_getbinidx(bin, m->value);

	/* LEAD METRIC TOKEN */
	lent = bstr_list_entry_alloci(strlen(BMETRIC_LEAD_TKN), BMETRIC_LEAD_TKN);
	if (!lent) {
		goto err0;
	}
	LIST_INSERT_HEAD(tok_head, lent, link);
	tok_tail = lent;

	/* MetricName */
	lent = bstr_list_entry_alloc(m->name.blen);
	if (!lent) {
		goto err1;
	}
	lent->str.blen = m->name.blen;
	memcpy(lent->str.cstr, m->name.cstr, m->name.blen);
	LIST_INSERT_AFTER(tok_tail, lent, link);
	tok_tail = lent;

	/* [value1, value2) */
	lent = bstr_list_entry_alloc(128);
	if (!lent) {
		goto err1;
	}
	LIST_INSERT_AFTER(tok_tail, lent, link);
	tok_tail = lent;

	/* NOTE: If I snprintf directly to lent->str.cstr, gcc will somehow
	 * think that the buffer is of size 0, and complain: "error: call to
	 * __builtin___snprintf_chk will always overflow destination buffer
	 * [-Werror]"
	 */
	struct bstr *str = &lent->str;
	lent->str.blen = snprintf(str->cstr, 127, "[%e,%e)",
						bin->bin[idx].lower_bound,
						bin->bin[idx+1].lower_bound);
	if (lent->str.blen >= 128) {
		errno = ENAMETOOLONG;
		goto err1;
	}

	return qent;

err1:
	bstr_list_free_entries(tok_head);

err0:
	free(qent);

	return NULL;
}

/**
 * Read callback for bufferevent.
 * \note 1 \a bev per connection.
 * \param bev The bufferevent object.
 * \param arg Pointer to ::conn_ctxt.
 */
static
void read_cb(struct bufferevent *bev, void *arg)
{
	struct bin_metric_conn_ctxt *cctxt = arg;
	struct bplugin *p = cctxt->plugin;
	struct plugin_ctxt *pctxt = p->context;
	struct evbuffer_ptr evbptr;
	struct evbuffer *input = bufferevent_get_input(bev);
	struct bin_metric_msg mhdr;
	int len;
loop:
	/* wait for a full messgae */
	len = evbuffer_get_length(input);
	if (len < sizeof(mhdr))
		return;
	len = evbuffer_copyout(input, &mhdr, sizeof(mhdr));
	if (len != sizeof(mhdr))
		return;
	ntoh_bin_metric_msg(&mhdr);
	len = evbuffer_get_length(input);
	if (len < bin_metric_msg_get_len(&mhdr))
		return;

	struct bin_metric_msg *m = bin_metric_msg_alloc(mhdr.name.blen);
	if (!m) {
		berror("bin_metric: cannot allocate message buffer");
		return;
	}

	len = evbuffer_remove(input, m, bin_metric_msg_get_len(&mhdr));
	if (len != bin_metric_msg_get_len(&mhdr)) {
		berr("bin_metric: evbuffer_remove() error, len: %d", len);
		return;
	}
	ntoh_bin_metric_msg(m);

	struct bwq_entry *ent = prepare_bwq_entry(m);
	if (ent)
		binq_post(ent);
	bin_metric_msg_free(m);
	goto loop;
}

/**
 * Event handler on bufferevent (in libevent).
 * \param bev The buffer event instance.
 * \param events The events.
 * \param arg The pointer to connection context ::conn_ctxt.
 */
static
void event_cb(struct bufferevent *bev, short events, void *arg)
{
	if (events & BEV_EVENT_ERROR) {
		berror("BEV_EVENT_ERROR");
	}
	if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF)) {
		bufferevent_free(bev);
		bin_metric_conn_ctxt_free(arg);
	}
}

/**
 * Connect callback. This function will be called when there is a connection
 * request coming in. This is a call back function for
 * evconnlistener_new_bind().
 * \param listener Listener
 * \param sock Socket file descriptor
 * \param addr Socket address ( sockaddr_in )
 * \param len Length of \a addr
 * \param arg Pointer to plugin instance.
 */
static
void conn_cb(struct evconnlistener *listener, evutil_socket_t sock,
		struct sockaddr *addr, int len, void *arg)
{
	struct event_base *evbase = evconnlistener_get_base(listener);
	struct bufferevent *bev = bufferevent_socket_new(evbase, sock,
			BEV_OPT_CLOSE_ON_FREE);
	struct bin_metric_conn_ctxt *cctxt = malloc(sizeof(*cctxt));
	cctxt->plugin = arg;
	bufferevent_setcb(bev, read_cb, NULL, event_cb, cctxt);
	bufferevent_enable(bev, EV_READ);
}

/**
 * This is a pthread routine function for listening for connections from
 * rsyslog over TCP. pthread_create() function in ::plugin_start() will call
 * this function.
 * \param arg A pointer to the ::bplugin of this thread.
 * \return (abuse) 0 if there is no error.
 * \return (abuse) errno if there are some error.
 * \note This is a thread routine.
 */
static
void* bin_metric_tcp_listen(void *arg)
{
	int64_t rc = 0;
	struct bplugin *p = arg;
	struct bin_metric_ctxt *ctxt = p->context;
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_addr = { .s_addr = INADDR_ANY },
		.sin_port = htons(ctxt->port)
	};
	struct event_base *evbase = event_base_new();
	if (!evbase) {
		rc = ENOMEM;
		goto err0;
	}
	int evconn_flag = LEV_OPT_REUSEABLE | LEV_OPT_THREADSAFE;
	struct evconnlistener *evconn = evconnlistener_new_bind( evbase,
			conn_cb,
			p,
			evconn_flag,
			SOMAXCONN,
			(void*) &addr,
			sizeof(addr));
	if (!evconn) {
		rc = ENOMEM;
		goto err1;
	}

	event_base_dispatch(evbase); /* This will loop + listen for new
					connection. */
	binfo("bin_metric: listening thread exiting ...");
	/* Clear resources on exit */
	evconnlistener_free(evconn);
err1:
	event_base_free(evbase);
err0:
	return (void*)rc;
}

static
int process_bin_line(char *line, void *ctxt)
{
	struct bhash_entry *hent;
	struct bmetricbin *bin = bmetricbin_create(line);
	if (!bin)
		return errno;
	int len = strlen(bin->metric_name);
	hent = bhash_entry_set(binhash, bin->metric_name, len, (uint64_t)bin);
	if (!hent)
		return errno;
	return 0;
}

static
int load_metric_bin(struct bplugin *this, const char *path)
{
	int rc = 0;

	rc = bprocess_file_by_line(path, process_bin_line, NULL);
	return rc;
}

/**
 * This is called from the main daemon (through ::bplugin::config) to configure
 * the plugin before the main daemon calls ::plugin_start()
 * (through ::bplugin::start).
 * \param this The plugin.
 * \param arg_head The head of the list of arguments.
 * \return 0 on success.
 * \return errno on error.
 * \note Now only accept 'port' for rsyslog_tcp plugin.
 */
static
int plugin_config(struct bplugin *this, struct bpair_str_head *arg_head)
{
	struct bpair_str *bpstr;
	struct bin_metric_ctxt *ctxt = this->context;
	int rc = 0;
	bpstr = bpair_str_search(arg_head, "bin_file", NULL);
	if (!bpstr) {
		berr("bin_metric plugin: bin_file parameter is needed");
		return EINVAL;
	}

	rc = load_metric_bin(this, bpstr->s1);
	if (rc)
		return rc;

	bpstr = bpair_str_search(arg_head, "port", NULL);
	if (bpstr) {
		uint16_t port = atoi(bpstr->s1);
		ctxt->port = port;
	}
	return rc;
}

/**
 * This will be called from the main baler daemon to start the plugin
 * (through ::bplugin::start).
 * \param this The plugin instance.
 * \return 0 on success.
 * \return errno on error.
 */
static
int plugin_start(struct bplugin *this)
{
	struct bin_metric_ctxt *ctxt = this->context;
	int rc = pthread_create(&ctxt->thread, NULL, bin_metric_tcp_listen, this);
	if (rc)
		return rc;
	return 0;
}

static
int plugin_stop(struct bplugin *this)
{
	struct bin_metric_ctxt *ctxt = this->context;
	return pthread_cancel(ctxt->thread);
}

/**
 * Free the plugin instance.
 * \param this The plugin to be freed.
 * \return 0 on success.
 * \note Now only returns 0, but the errors will be logged.
 */
static
int plugin_free(struct bplugin *this)
{
	/* If context is not null, meaning that the plugin is running,
	 * as it is set to null in ::plugin_stop(). */
	struct bin_metric_ctxt *ctxt = (typeof(ctxt)) this->context;
	int rc = 0;
	if (ctxt && ctxt->status == PSTATUS_RUNNING) {
		rc = plugin_stop(this);
		if (rc) {
			errno  = rc;
			berror("plugin_stop");
		}
	}
	bplugin_free(this);
	return 0;
}

/**
 * Global variable flag for ::init_once() function.
 */
static
int __once = 0;

/**
 * This function will initialize global variable (but local to the plugin),
 * and should be called only once. This function is also thread-safe.
 * \return -1 on error.
 * \return 0 on success.
 */
static
int init_once()
{
	int rc = 0;
	static pthread_mutex_t __once_mutex = PTHREAD_MUTEX_INITIALIZER;

	pthread_mutex_lock(&__once_mutex);
	if (__once)
		goto out;
	__once = 1;
	rc = evthread_use_pthreads();
	if (rc)
		goto err0;

	binhash = bhash_new(65521, 11, NULL);
	if (!binhash) {
		rc = errno;
		goto err0;
	}

	goto out;
err0:
	__once = 0;
out:
	pthread_mutex_unlock(&__once_mutex);
	return rc;
}

struct bplugin* create_plugin_instance()
{
	if (init_once())
		return NULL;
	struct bplugin *p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;
	p->name = strdup("rsyslog_tcp");
	p->version = strdup("0.1a");
	p->config = plugin_config;
	p->start = plugin_start;
	p->stop = plugin_stop;
	p->free = plugin_free;
	struct bin_metric_ctxt *ctxt = calloc(1, sizeof(*ctxt));
	ctxt->status = PSTATUS_STOPPED;
	ctxt->port = BIN_METRIC_DEFAULT_PORT;
	p->context = ctxt;
	return p;
}

static
char* ver()
{
	binfo("%s", "1.1.1.1");
	return "1.1.1.1";
}

/**\}*/
