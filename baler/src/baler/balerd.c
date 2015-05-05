/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2015 Sandia Corporation. All rights reserved.
 *
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
 * \file balerd.c
 * \author Narate Taerat (narate@ogc.us)
 * \date Mar 19, 2013
 *
 * \page balerd Baler Daemon
 *
 * \section synopsis SYNOPSIS
 *   balerd [OPTIONS]
 *
 * \section description DESCRIPTION
 * TODO More about balerd here.
 *
 * In master mode, balerd manages its own token and pattern maps.  It won't ask
 * other balerd when it found a new token or pattern. Master-mode balerd also
 * serves token/pattern operations requested by client-mode balerd.
 *
 * Client-mode balerd always ask its master when it finds a new token or
 * pattern. The information received from the master will also be stored locally
 * to reduce further network traffic.
 *
 * \section options OPTIONS
 *
 * \par -l LOG_PATH
 * Log file (default: None, and log to stdout)
 *
 * \par -s STORE_PATH
 * Path to a baler store (default: ./store)
 *
 * \par -C CONFIG_FILE
 * Path to the configuration (Baler commands) file. This is optional as users
 * may use ocmd to configure baler.
 *
 * \par -F
 * Run in foreground mode (default: daemon mode)
 *
 * \par -m SM_MODE
 * Either 'master' or 'slave' mode (default: master).
 *
 * \par -x SM_XPRT
 * Zap transport to be used in slave-master communication (default: sock).
 *
 * \par -h M_HOST
 * For slave-mode balerd, this option specifies master hostname or IP address to
 * connect to. This option is ignored in master-mode. This option is required in
 * slave-mode balerd and has no default value.
 *
 * \par -p M_PORT
 * For slave-mode balerd, this specifies the port of the master to connect to.
 * For master-mode balerd, this specifies the port number to listen to.
 * (default: ':30003').
 *
 * \par -z OCM_PORT
 * Specifying a port for receiving OCM connection and configuration (default:
 * 20005).
 *
 * \par -?
 * Display help message.
 *
 * \section configuration CONFIGURATION
 * Baler configuration file
 *
 * \section conf_example CONFIGURATION_EXAMPLE
 * TODO Configuration example here
 *
 * \defgroup balerd_dev Baler Daemon Development Documentation
 * \{
 * \brief Baler daemon implementation.
 */
#include <stdio.h>
#include <pthread.h>
#include <limits.h>
#include <ctype.h>
#include <dlfcn.h>
#include <unistd.h>
#include <signal.h>
#include <stdarg.h>
#include <assert.h>
#include <semaphore.h>

#include <event2/event.h>
#include <event2/thread.h>
#include <zap/zap.h>

#ifdef ENABLE_OCM
#include "ocm/ocm.h"
ocm_t ocm; /**< ocm handle */
char ocm_key[512]; /**< $HOSTNAME/balerd */
uint16_t ocm_port = 20005;
int ocm_cb(struct ocm_event *e);
#endif

#include "bcommon.h"
#include "btypes.h"
#include "butils.h"
#include "bmapper.h"
#include "binput_private.h"
#include "boutput.h"
#include "btkn.h"
#include "bptn.h"
#include "bwqueue.h"

/***** Definitions *****/
typedef enum bmap_idx_enum {
	BMAP_IDX_TKN,
	BMAP_IDX_HST,
	BMAP_IDX_PTN,
} bmap_idx_e;

typedef enum bzmsg_type_enum {
	BZMSG_TYPE_FIRST=0,
	BZMSG_ID_REQ = BZMSG_TYPE_FIRST,
	BZMSG_ID_REP,
	BZMSG_BSTR_REQ,
	BZMSG_BSTR_REP,
	BZMSG_INSERT_REQ,
	BZMSG_INSERT_REP,
	BZMSG_TYPE_LAST,
} bzmsg_type_e;

struct bzmsg {
	uint32_t type;
	void *ctxt;
	void *ctxt_bstr;
	int rc;
	uint32_t mapidx;
	int tkn_idx;

	uint32_t id;
	struct btkn_attr attr;
	struct bstr bstr;
};

struct bzmsg *bzmsg_alloc(size_t bstr_len)
{
	return malloc(sizeof(struct bzmsg) + bstr_len);
}

struct bzap_ctxt {
	pthread_mutex_t mutex;
	int is_ready;
};

/***** Command line arguments ****/
#ifdef ENABLE_OCM
const char *optstring = "FC:l:s:z:m:x:h:p:v:?";
#else
const char *optstring = "FC:l:s:m:x:h:p:v:?";
#endif
const char *config_path = NULL;
const char *log_path = NULL;

void display_help_msg()
{
	const char *help_msg =
"Usage: balerd [options]\n"
"\n"
"Options: \n"
"	-l <path>	Log file (log to stdout by default)\n"
"	-s <path>	Store path (default: ./store)\n"
"	-C <path>	Configuration (Baler commands) file\n"
"	-F		Foreground mode (default: daemon mode)\n"
"	-m <mode>	'master' or 'slave' (default: master)\n"
"	-h <host>	master host name or IP address.\n"
"	-x <xprt>	transport (default: sock).\n"
"	-p <port>	port of master balerd (default: 30003).\n"
#ifdef ENABLE_OCM
"	-z <port>	ocm port for balerd (default: 20005).\n"
#endif
"	-v <LEVEL>	Set verbosity level (DEBUG, INFO, WARN, ERROR).\n"
"			The default value is WARN.\n"
"	-?		Show help message\n"
"\n"
"For more information see balerd(3) manpage.\n"
"\n";
	printf("%s\n", help_msg);
}

/********** Configuration Variables **********/
/**
 * \defgroup bconf_vars Configuration Variables
 * \{
 */
int binqwkrN = 1; /**< Input Worker Thread Number */
int boutqwkrN = 1; /**< Output Worker Thread Number */
int is_foreground = 0; /**< Run as foreground? */
int is_master = 1; /**< Run in master mode */

uint16_t m_port = 30003;
char *m_host = NULL;
char *sm_xprt = "sock";
struct timeval reconnect_interval = {.tv_sec = 2};

/**\}*/

/********** Configuration Commands **********/
char* str_to_lower(char *s)
{
	while (s) {
		*s = tolower(*s);
		s++;
	}
	return s;
}

/* define cfg commands here.
 * an extra '_' infront of LIST is to prevent name collision with possible
 * command 'list' */
#define BCFG_CMD__LIST(PREFIX, CMD) \
	CMD(PREFIX, PLUGIN), \
	CMD(PREFIX, TOKENS), \
	CMD(PREFIX, HOSTS)

/* automatically generated enum */
enum BCFG_CMD {
	BCFG_CMD__LIST(BCFG_CMD_, BENUM),
	BCFG_CMD_LAST
};

/* automatically generated command strings */
#define BCFG_CMD_STR(X) #X,
const char *bcfg_str[] = {
	BCFG_CMD__LIST(, BENUM_STR)
};

enum BCFG_CMD bcfg_cmd_str2enum(const char *s)
{
	return bget_str_idx(bcfg_str, BCFG_CMD_LAST, s);
}

/********** Global Variable Section **********/
zap_t zap;
zap_ep_t zap_ep;
struct event_base *evbase;

int master_connected = 0;
struct bzap_ctxt slave_zap_ctxt = {.mutex = PTHREAD_MUTEX_INITIALIZER};

extern uint64_t *metric_ids;

/**
 * Input queue workers
 */
pthread_t *binqwkr;

/**
 * Head of the ::bconfig_list.
 */
LIST_HEAD(bconfig_head, bconfig_list);

/**
 * List of plugin configurations.
 */
struct bconfig_list {
	char *command; /**< Configuration command */
	struct bpair_str_head arg_head_s; /**< Argument list head */
	LIST_ENTRY(bconfig_list) link; /**< The link to next/prev item */
};

/**
 * The input plugin configuration list head.
 */
struct bconfig_head bipconf_head_s = {NULL};

/**
 * The output plugin configuration list head.
 */
struct bconfig_head bopconf_head_s = {NULL};

/**
 * Plugin instance list head.
 */
LIST_HEAD(bplugin_head, bplugin);

/**
 * Input plugin instance head.
 */
struct bplugin_head bip_head_s = {NULL};

/**
 * Output plugin instance head.
 */
struct bplugin_head bop_head_s = {NULL};

/**
 * Output queue workers
 */
pthread_t *boutqwkr;

/**
 * Pending input queue.
 *
 * Input entry that requires master-mode balerd consultant will have to wait in
 * this pending input queue. When the data is ready, the input entry will be put
 * into normal input queue again.
 */
struct bwq *binq_pending;

/**
 * Baler Input Worker's Context.
 * Data in this structure will be reused repeatedly by the worker thread
 * that owns it. Repeatedly allocate and deallocate is much slower than
 * using stack or global variables. I do not put this in stack because
 * this is relatively huge.
 */
struct bin_wkr_ctxt {
	int worker_id; /**< Worker ID */
	uint32_t comp_id;
	uint32_t unresolved_count;
	int rc;
	int (*next_fn)(struct bwq_entry *);
	union {
		/**
		 * Space for ptn_str.
		 */
		char _ptn_str[sizeof(struct bstr)+sizeof(uint32_t)*4096];
		/**
		 * Pattern string.
		 */
		struct bstr ptn_str;
	};
	union {
		/**
		 * Space for message structure.
		 */
		char _msg[sizeof(struct bmsg)+sizeof(uint32_t)*4096];
		/**
		 * Message.
		 */
		struct bmsg msg;
	};
	union {
		/**
		 * Space for zap message.
		 */
		char _zmsg[sizeof(struct bzmsg) + sizeof(uint32_t)*4096];
		/**
		 * zap message buffer.
		 */
		struct bzmsg zmsg;
	};
};

/**
 * Context for Output Worker.
 */
struct bout_wkr_ctxt {
	int worker_id; /**< Worker ID */
};

struct btkn_store *token_store; /**< Token store */
struct bptn_store *pattern_store; /**< Pattern store */
struct btkn_store *comp_store; /**< Token store for comp_id */

/*********************************************/

const char *bzmsg_type_str(bzmsg_type_e e)
{
	static const char *_str[] = {
		[BZMSG_ID_REQ]      =  "BZMSG_ID_REQ",
		[BZMSG_ID_REP]      =  "BZMSG_ID_REP",
		[BZMSG_BSTR_REQ]    =  "BZMSG_BSTR_REQ",
		[BZMSG_BSTR_REP]    =  "BZMSG_BSTR_REP",
		[BZMSG_INSERT_REQ]  =  "BZMSG_INSERT_REQ",
		[BZMSG_INSERT_REP]  =  "BZMSG_INSERT_REP",
	};

	if (BZMSG_TYPE_FIRST <= e && e < BZMSG_TYPE_LAST)
		return _str[e];
	return "UNKNOWN";
}

size_t bzmsg_len(struct bzmsg *m)
{
	size_t len = sizeof(*m);
	switch (m->type) {
	case BZMSG_ID_REQ:
	case BZMSG_BSTR_REP:
	case BZMSG_INSERT_REQ:
		len += m->bstr.blen;
		break;
	}
	return len;
}

void hton_bzmsg(struct bzmsg *m)
{
	switch (m->type) {
	case BZMSG_BSTR_REP:
	case BZMSG_INSERT_REQ:
		m->attr.type = htobe32(m->attr.type);
	case BZMSG_ID_REQ:
		m->bstr.blen = htobe32(m->bstr.blen);
		break;
	case BZMSG_ID_REP:
	case BZMSG_INSERT_REP:
		m->attr.type = htobe32(m->attr.type);
	case BZMSG_BSTR_REQ:
		m->id = htobe32(m->id);
		break;
	}
	m->mapidx = htobe32(m->mapidx);
	m->rc = htobe32(m->rc);
	m->type = htobe32(m->type);
}

void ntoh_bzmsg(struct bzmsg *m)
{
	m->type = be32toh(m->type);
	m->rc = be32toh(m->rc);
	m->mapidx = be32toh(m->mapidx);
	switch (m->type) {
	case BZMSG_BSTR_REP:
	case BZMSG_INSERT_REQ:
		m->attr.type = be32toh(m->attr.type);
	case BZMSG_ID_REQ:
		m->bstr.blen = be32toh(m->bstr.blen);
		break;
	case BZMSG_ID_REP:
	case BZMSG_INSERT_REP:
		m->attr.type = be32toh(m->attr.type);
	case BZMSG_BSTR_REQ:
		m->id = be32toh(m->id);
		break;
	}
}

void* binqwkr_routine(void *arg);
void* boutqwkr_routine(void *arg);

void bconfig_list_free(struct bconfig_list *bl) {
	struct bpair_str *bp;
	while ((bp = LIST_FIRST(&bl->arg_head_s))) {
		LIST_REMOVE(bp, link);
		bpair_str_free(bp);
	}
	free(bl);
}

static
zap_mem_info_t bzap_mem_info()
{
	return NULL;
}

void snprint_sockaddr(char *buff, size_t len, struct sockaddr *sa)
{
	uint64_t x, y;
	switch (sa->sa_family) {
	case AF_INET:
		x = ((struct sockaddr_in*)sa)->sin_addr.s_addr;
		snprintf(buff, len, "%d.%d.%d.%d",
				(int)(0xff & (x)),
				(int)(0xff & (x>>8)),
				(int)(0xff & (x>>16)),
				(int)(0xff & (x>>32))
			);
		break;
	default:
		snprintf(buff, len, "UNKNOWN");
	}
}

inline
bzmsg_type_e bzmsg_type_inverse(bzmsg_type_e type)
{
	return (type ^ 1);
}

void master_handle_recv(zap_ep_t ep, zap_event_t ev)
{
	struct bzmsg *m = (void*)ev->data;
	struct bzmsg simple_rep;
	struct bzmsg *rep;
	struct bmap *map = NULL;
	int has_attr = 0;
	void *store = NULL;
	const struct bstr *bstr;
	size_t len;
	uint32_t (*ins_fn)(void*, void*);
	zap_err_t zerr;

	ntoh_bzmsg(m);

	simple_rep.ctxt = m->ctxt;
	simple_rep.ctxt_bstr = m->ctxt_bstr;
	simple_rep.tkn_idx = m->tkn_idx;
	simple_rep.mapidx = m->mapidx;

	rep = &simple_rep;

	switch (m->mapidx) {
	case BMAP_IDX_TKN:
		has_attr = 1;
		map = token_store->map;
		ins_fn = (void*)btkn_store_insert;
		store = token_store;
		break;
	case BMAP_IDX_HST:
		has_attr = 1;
		map = comp_store->map;
		ins_fn = (void*)btkn_store_insert;
		store = comp_store;
		break;
	case BMAP_IDX_PTN:
		map = pattern_store->map;
		ins_fn = (void*)bptn_store_addptn;
		store = pattern_store;
		break;
	default:
		berr("Unknown mapid: %u", m->mapidx);
		rep->rc = EINVAL;
		rep->type = bzmsg_type_inverse(m->type);
		goto out;
	}

	switch (m->type) {
	case BZMSG_ID_REQ:
		rep->type = BZMSG_ID_REP;
		rep->rc = 0;
		rep->id = bmap_get_id(map, &m->bstr);
		if (has_attr) {
			rep->attr = btkn_store_get_attr(store, rep->id);
		}
		break;
	case BZMSG_BSTR_REQ:
		bstr = bmap_get_bstr(map, m->id);
		if (!bstr) {
			rep = &simple_rep;
			rep->type = BZMSG_BSTR_REP;
			rep->rc = ENOENT;
			goto out;
		}
		rep = bzmsg_alloc(bstr->blen);
		if (!rep) {
			rep = &simple_rep;
			rep->type = BZMSG_BSTR_REP;
			rep->rc = ENOMEM;
			goto out;
		}
		*rep = simple_rep;
		rep->type = BZMSG_BSTR_REP;
		rep->rc = 0;
		if (has_attr)
			rep->attr = btkn_store_get_attr(store, m->id);
		bstr_set_cstr(&rep->bstr, bstr->cstr, bstr->blen);
		break;
	case BZMSG_INSERT_REQ:
		rep->type = BZMSG_INSERT_REP;
		rep->id = ins_fn(store, &m->bstr);
		if (rep->id < BMAP_ID_BEGIN) {
			rep->rc = ENOMEM;
			goto out;
		}
		if (has_attr) {
			if (m->attr.type != BTKN_TYPE_STAR) {
				btkn_store_set_attr(store, rep->id, m->attr);
			}
			rep->attr = btkn_store_get_attr(store, rep->id);
		}
		rep->rc = 0;
		break;
	default:
		berr("Unexpected balerd zap message type: %s",
						bzmsg_type_str(m->type));
	}
out:
	len = bzmsg_len(rep);
	hton_bzmsg(rep);
	zerr = zap_send(ep, rep, len);
	if (zerr != ZAP_ERR_OK) {
		berr("%s: zap_send() error: %s", __func__, zap_err_str(zerr));
	}
cleanup:
	if (rep != &simple_rep)
		free(rep);
}

/**
 * zap call back function for master-mode balerd
 */
void master_zap_cb(zap_ep_t ep, zap_event_t ev)
{
	struct sockaddr lsock, rsock;
	char tmp[64];
	socklen_t slen;
	switch (ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		zap_accept(ep, master_zap_cb, NULL, 0);
		break;
	case ZAP_EVENT_CONNECTED:
		zap_get_name(ep, &lsock, &rsock, &slen);
		snprint_sockaddr(tmp, sizeof(tmp), &rsock);
		binfo("connected from slave-mode balerd: %s", tmp);
		break;
	case ZAP_EVENT_DISCONNECTED:
		zap_get_name(ep, &lsock, &rsock, &slen);
		snprint_sockaddr(tmp, sizeof(tmp), &rsock);
		binfo("disconnected from slave-mode balerd: %s", tmp);
		zap_free(ep);
		break;
	case ZAP_EVENT_RECV_COMPLETE:
		master_handle_recv(ep, ev);
		break;
	default:
		bwarn("%s: Unexpected zap event: %s", __func__,
						zap_event_str(ev->type));
		break;
	}
}

void slave_zap_cb(zap_ep_t ep, zap_event_t ev);
void slave_connect(int sock, short which, void *arg);

void slave_schedule_reconnect()
{
	struct event *event;
	bdebug("Scheduling reconnect...");
	event = event_new(evbase, -1, 0, 0, 0);
	event_assign(event, evbase, -1, 0, slave_connect, event);
	event_add(event, &reconnect_interval);
}

void slave_connect(int sock, short which, void *arg)
{
	zap_err_t zerr;
	struct event *ev = arg;
	struct sockaddr *sa;
	struct addrinfo hint = {.ai_family = AF_INET};
	struct addrinfo *ai = NULL;
	char tmp[8];
	int rc;
	pthread_mutex_lock(&slave_zap_ctxt.mutex);
	bdebug("connecting to master ...");
	zap_ep = zap_new(zap, slave_zap_cb);
	if (!zap_ep) {
		berror("zap_new()");
		slave_schedule_reconnect();
		goto out;
	}
	zap_set_ucontext(zap_ep, &slave_zap_ctxt);
	snprintf(tmp, sizeof(tmp), "%hu", m_port);
	rc = getaddrinfo(m_host, tmp, &hint, &ai);
	if (rc) {
		berr("getaddrinfo() error: %d, %s\n", rc, gai_strerror(rc));
		goto out;
	}
	uint32_t addr = ((struct sockaddr_in*)ai->ai_addr)->sin_addr.s_addr;
	uint16_t prt = ((struct sockaddr_in*)ai->ai_addr)->sin_port;
	bdebug("connecting to master: %hhu.%hhu.%hhu.%hhu:%hu",
			addr & 0xFF,
			(addr>>8) & 0xFF,
			(addr>>16) & 0xFF,
			(addr>>24) & 0xFF,
			be16toh(prt));

	zerr = zap_connect(zap_ep, ai->ai_addr, ai->ai_addrlen, NULL, 0);
	if (zerr != ZAP_ERR_OK) {
		zap_free(zap_ep);
		berr("zap_connect() error: %s", zap_err_str(zerr));
		slave_schedule_reconnect();
		goto out;
	}
out:
	if (ev)
		event_free(ev);
	if (ai)
		freeaddrinfo(ai);
	pthread_mutex_unlock(&slave_zap_ctxt.mutex);
}

void slave_handle_insert_rep(struct bzmsg *bzmsg)
{
	uint32_t id;
	struct bwq_entry *ent = bzmsg->ctxt;
	struct bin_wkr_ctxt *ctxt = ent->ctxt;
	struct bstr *bstr = bzmsg->ctxt_bstr;
	ctxt->unresolved_count--;

	if (bzmsg->rc) {
		ctxt->rc = bzmsg->rc;
	}

	uint32_t (*ins_fn)(void *, void*, uint32_t);
	void *store = NULL;
	const char *store_name = "Unknown";
	int has_attr = 0;

	/* Handle by map type */
	switch (bzmsg->mapidx) {
	case BMAP_IDX_HST:
		ctxt->comp_id = bmapid2compid(bzmsg->id);
		ins_fn = (void*)btkn_store_insert_with_id;
		has_attr = 1;
		store = comp_store;
		store_name = "comp_store";
		break;
	case BMAP_IDX_TKN:
		ctxt->ptn_str.u32str[bzmsg->tkn_idx] = bzmsg->id;
		ins_fn = (void*)btkn_store_insert_with_id;
		has_attr = 1;
		store = token_store;
		store_name = "token_store";
		break;
	case BMAP_IDX_PTN:
		ctxt->msg.ptn_id = bzmsg->id;
		ins_fn = (void*)bptn_store_addptn_with_id;
		has_attr = 0;
		store = pattern_store;
		store_name = "pattern_store";
		break;
	}

	/* Update local map with the replied information */
	id = ins_fn(store, bzmsg->ctxt_bstr, bzmsg->id);
	if (id < BMAP_ID_BEGIN) {
		ctxt->rc = ENOMEM;
		berr("Error inserting <str, id>: <%.*s, %u>, into store: %s, "
				"ret: %u",
				bstr->blen, bstr->cstr,
				bzmsg->id,
				store_name,
				id);
		return;
	}

	if (has_attr) {
		btkn_store_set_attr(store, id, bzmsg->attr);
	}

	if (ctxt->unresolved_count)
		return;

	/* Ready for next processing */
	if (ctxt->rc) {
		binq_entry_free(ent);
		free(ctxt);
		return;
	}

	ctxt->next_fn(ent);
}

void slave_handle_recv(zap_ep_t ep, zap_event_t ev)
{
	struct bzmsg *bzmsg = (void*)ev->data;
	ntoh_bzmsg(bzmsg);
	switch (bzmsg->type) {
	case BZMSG_INSERT_REP:
		slave_handle_insert_rep(bzmsg);
		break;
	default:
		berr("Unexpected bzmsg type: %s", bzmsg_type_str(bzmsg->type));
	}
}

/**
 * zap call back function for slave-mode balerd
 */
void slave_zap_cb(zap_ep_t ep, zap_event_t ev)
{
	struct event *event;
	struct bzap_ctxt *ctxt = zap_get_ucontext(ep);
	switch (ev->type) {
	case ZAP_EVENT_CONNECTED:
		pthread_mutex_lock(&ctxt->mutex);
		ctxt->is_ready = 1;
		pthread_mutex_unlock(&ctxt->mutex);
		break;
	case ZAP_EVENT_DISCONNECTED:
	case ZAP_EVENT_CONNECT_ERROR:
		pthread_mutex_lock(&ctxt->mutex);
		ctxt->is_ready = 0;
		bdebug("master disconnected!!!");
		pthread_mutex_unlock(&ctxt->mutex);
		zap_free(ep);
		slave_schedule_reconnect();
		break;
	case ZAP_EVENT_RECV_COMPLETE:
		slave_handle_recv(ep, ev);
		break;
	default:
		berr("Unexpected event: %s", zap_event_str(ev->type));
		break;
	}
}

void zap_log_fn(const char *fmt, ...)
{
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	static char buff[4096];
	va_list ap;
	pthread_mutex_lock(&mutex);
	va_start(ap, fmt);
	vsnprintf(buff, sizeof(buff), fmt, ap);
	va_end(ap);
	pthread_mutex_unlock(&mutex);
}

void master_pattern_bmap_ev_cb(struct bmap *map, bmap_event_t ev, void *_arg)
{
	struct bmap_event_new_insert_arg *arg = _arg;
	/* XXX TODO IMPLEMENT ME IN THE FUTURE */
	/* Do nohting for now ... */
}

void master_init()
{
	zap_err_t zerr;
	struct sockaddr_in sin = {
		.sin_family = AF_INET,
		.sin_port = htons(m_port),
	};

	bmap_set_event_cb(pattern_store->map, master_pattern_bmap_ev_cb);

	zap_ep = zap_new(zap, master_zap_cb);
	if (!zap_ep) {
		berror("zap_new()");
		exit(-1);
	}

	zerr = zap_listen(zap_ep, (void*)&sin, sizeof(sin));
	if (zerr) {
		berr("zap_listen() error: %s", zap_err_str(zerr));
		exit(-1);
	}
}

void slave_init()
{
	slave_schedule_reconnect();
}

/**
 * Baler Daemon Initialization.
 */
void initialize_daemon()
{
	/* Daemonize? */
	if (!is_foreground) {
		binfo("Daemonizing...");
		if (daemon(1, 1) == -1) {
			berror("daemon");
			exit(-1);
		}
		binfo("Daemonized");
	}
	umask(0);
	evthread_use_pthreads();

	/* Event base for internal balerd event */
	evbase = event_base_new();
	if (!evbase) {
		berr("event_base_new() error, errno: %d, %m", errno);
		exit(-1);
	}

	/* Input/Output Work Queue */
	binq = bwq_alloci(1024);
	if (!binq) {
		berror("(binq) bwq_alloci");
		exit(-1);
	}

	boutq = bwq_alloci(1024);
	if (!boutq) {
		berror("(boutq) bwq_alloci");
		exit(-1);
	}

	binq_pending = bwq_alloci(1024);
	if (!binq_pending) {
		berror("(binq_pending) bwq_alloci");
		exit(-1);
	}

	/* Token/Pattern stores */
	binfo("Opening Token Store.");
	char tmp[PATH_MAX];
	const char *store_path = bget_store_path();
	sprintf(tmp, "%s/tkn_store", store_path);
	token_store = btkn_store_open(tmp, O_CREAT|O_RDWR);
	if (!token_store) {
		berror("btkn_store_open");
		berr("Cannot open token store: %s", tmp);
		exit(-1);
	}
	/* First add the spaces into the store */
	btkn_store_char_insert(token_store, " \t\r\n", BTKN_TYPE_SPC);
	btkn_store_char_insert(token_store, BTKN_SYMBOL_STR, BTKN_TYPE_SYM);

	sprintf(tmp, "%s/ptn_store", store_path);
	pattern_store = bptn_store_open(tmp, O_CREAT|O_RDWR);
	if (!pattern_store) {
		berror("bptn_store_open");
		berr("Cannot open pattern store: %s", tmp);
		exit(-1);
	}

	/* Comp store for comp<->comp_id */
	binfo("Opening Component Store.");
	sprintf(tmp, "%s/comp_store", store_path);
	comp_store = btkn_store_open(tmp, O_CREAT|O_RDWR);
	if (!comp_store) {
		berror("btkn_store_open");
		berr("Cannot open token store: %s", tmp);
		exit(-1);
	}

	/* Input worker threads */
	binqwkr = malloc(sizeof(*binqwkr)*binqwkrN);
	if (!binqwkr) {
		berror("malloc for binqwkr");
		exit(-1);
	}
	int i, rc;
	struct bin_wkr_ctxt *ictxt;
	for (i=0; i<binqwkrN; i++) {
		ictxt = calloc(1, sizeof(*ictxt));
		if (!ictxt) {
			berror("calloc for ictxt");
			exit(-1);
		}
		ictxt->worker_id = i;
		if ((rc = pthread_create(binqwkr+i, NULL,binqwkr_routine,
						ictxt)) != 0) {
			berr("pthread_create error code: %d\n", rc);
			exit(-1);
		}
	}

	/* Output worker threads */
	boutqwkr = malloc(sizeof(*boutqwkr)*boutqwkrN);
	if (!boutqwkr) {
		berror("malloc for boutqwkr");
		exit(-1);
	}
	struct bout_wkr_ctxt *octxt;
	for (i=0; i<boutqwkrN; i++) {
		octxt = calloc(1, sizeof(*octxt));
		if (!octxt) {
			berror("calloc for octxt");
			exit(-1);
		}
		octxt->worker_id = i;
		if ((rc = pthread_create(boutqwkr+i, NULL, boutqwkr_routine,
						octxt)) != 0) {
			berr("pthread_create error, code: %d\n", rc);
			exit(-1);
		}
	}

	metric_ids = calloc(1024*1024, sizeof(*metric_ids));
	if (!metric_ids) {
		berr("cannot allocate metric_ids.\n");
		exit(-1);
	}

	/* OCM */
#ifdef ENABLE_OCM
	ocm = ocm_create("sock", ocm_port, ocm_cb, __blog);
	if (!ocm) {
		berr("cannot create ocm: error %d\n", errno);
		exit(-1);
	}
	rc = gethostname(ocm_key, 512);
	if (rc) {
		berr("cannot get hostname, error %d: %m\n", errno);
		exit(-1);
	}
	sprintf(ocm_key + strlen(ocm_key), "/%s", "balerd");
	ocm_register(ocm, ocm_key, ocm_cb);
	rc = ocm_enable(ocm);
	if (rc) {
		berr("ocm_enable failed, rc: %d", rc);
		berr("Please check if port %d is occupied.", ocm_port);
		exit(-1);
	}
#endif
	/* slave/master network init */
	zap_err_t zerr;
	zap = zap_get(sm_xprt, zap_log_fn, bzap_mem_info);
	if (!zap) {
		berror("zap_get()");
		exit(-1);
	}
	zap_cb_fn_t cb;
	if (is_master)
		master_init();
	else
		slave_init();
	binfo("Baler Initialization Complete.");
}

/**
 * Load a single plugin and put it into the given plugin instance list.
 * \param pcl The load plugin configuration.
 * \param inst_head The head of the plugin instance list.
 * \return 0 on success.
 * \return Error code on error.
 */
int load_plugin(struct bconfig_list *pcl, struct bplugin_head *inst_head)
{
	int rc = 0;
	char plibso[PATH_MAX];
	struct bpair_str *bname = bpair_str_search(&pcl->arg_head_s,
			"name", NULL);
	if (!bname) {
		berr("Cannot load plugin without argument 'name', "
				"command: %s", pcl->command);
		rc = EINVAL;
		goto out;
	}
	sprintf(plibso, "lib%s.so", bname->s1);
	void *h = dlopen(plibso, RTLD_NOW);
	if (!h) {
		rc = ELIBACC;
		berr("dlopen: %s\n", dlerror());
		goto out;
	}
	struct bplugin* (*pcreate)();
	dlerror(); /* Clear current dlerror */
	*(void**) (&pcreate) = dlsym(h, "create_plugin_instance");
	char *err = dlerror();
	if (err) {
		rc = ELIBBAD;
		berr("dlsym error: %s\n", err);
		goto out;
	}
	struct bplugin *p = pcreate();
	if (!p) {
		rc = errno;
		berr("Cannot create plugin %s\n", bname->s1);
		goto out;
	}
	LIST_INSERT_HEAD(inst_head, p, link);

	/* Configure the plugin. */
	if ((rc = p->config(p, &pcl->arg_head_s))) {
		berr("Config error, code: %d\n", rc);
		goto out;
	}

	/* And start the plugin. Plugin should not block this though. */
	if ((rc = p->start(p))) {
		berr("Plugin %s start error, code: %d\n", bname->s1,
				rc);
		goto out;
	}
out:
	return rc;
}

int load_plugins(struct bconfig_head *conf_head,
		struct bplugin_head *inst_head)
{
	struct bconfig_list *pcl;
	char plibso[PATH_MAX];
	int rc = 0;
	LIST_FOREACH(pcl, conf_head, link) {
		rc = load_plugin(pcl, inst_head);
		if (rc)
			goto out;
	}
out:
	return rc;
}

/**
 * A simple macro to skip the delimiters in \c d.
 * \param x A pointer to the string.
 * \param d A pointer to the string containing delimiters.
 */
#define SKIP_DELIMS(x, d) while (*(x) && strchr((d), *(x))) {(x)++;}

#define WHITE_SPACES " \t"

/**
 * Get the first token from \c *s. The returned string is newly created, and \c
 * *s will point to the next token. On error, \c *s will not be changed and the
 * function returns NULL.
 * \param[in,out] s \c *s is the string.
 * \param delims The delimiters.
 * \return NULL on error.
 * \return A pointer to the extracted token. Caller is responsible for freeing
 * 	it.
 */
char* get_token(const char **s, char* delims)
{
	const char *str = *s;
	while (*str && !strchr(delims, *str)) {
		str++;
	}
	int len = str - *s;
	char *tok = malloc(len + 1);
	if (!tok)
		return NULL;
	memcpy(tok, *s, len);
	tok[len] = 0;
	*s = str;
	return tok;
}

/**
 * Parse the configuration string \c cstr.
 * \param cstr The configuration string (e.g. "rsyslog port=9999").
 * \return NULL on error.
 * \return The pointer to ::bconfig_list, containing the parsed
 * 	information.
 */
struct bconfig_list* parse_config_str(const char *cstr)
{
	struct bconfig_list *l = calloc(1, sizeof(*l));
	if (!l)
		goto err0;
	const char *s = cstr;
	SKIP_DELIMS(s, WHITE_SPACES);

	/* The first token is the name of the plugin. */
	l->command = get_token(&s, WHITE_SPACES);
	if (!l->command) {
		errno = EINVAL;
		goto err1;
	}
	SKIP_DELIMS(s, WHITE_SPACES);

	/* The rest are <key>=<value> configuration arguments. */
	char *value;
	char *key;
	struct bpair_str *pstr;
	struct bpair_str tail; /* dummy tail */
	LIST_INSERT_HEAD(&l->arg_head_s, &tail, link);
	while (*s && (key = get_token(&s, "="WHITE_SPACES))) {
		SKIP_DELIMS(s, "="WHITE_SPACES);
		value = get_token(&s, WHITE_SPACES);
		SKIP_DELIMS(s, WHITE_SPACES);
		if (!value) {
			errno = EINVAL;
			goto err2;
		}
		pstr = malloc(sizeof(*pstr));
		if (!pstr)
			goto err3;
		pstr->s0 = key;
		pstr->s1 = value;
		LIST_INSERT_BEFORE(&tail, pstr, link);
	}
	LIST_REMOVE(&tail, link);

	/* Parse done, return the config list node. */
	return l;
err3:
	free(value);
err2:
	free(key);
	/* Reuse pstr */
	while ((pstr = LIST_FIRST(&l->arg_head_s))) {
		LIST_REMOVE(pstr, link);
		bpair_str_free(pstr);
	}
err1:
	free(l);
err0:
	return NULL;
}

int process_cmd_plugin(struct bconfig_list *cfg)
{
	struct bpair_str *bp = bpair_str_search(&cfg->arg_head_s, "name", NULL);
	if (!bp)
		return EINVAL;
	if (strncmp(bp->s1, "bin", 3) == 0)
		return load_plugin(cfg, &bip_head_s);
	else if (strncmp(bp->s1, "bout", 4) == 0)
		return load_plugin(cfg, &bop_head_s);
	return EINVAL;
}

/**
 * \returns 0 on success.
 * \returns Error code on error.
 */
int process_cmd_tokens(struct bconfig_list *cfg)
{
	struct btkn_store *store;
	struct bpair_str *bp_path = bpair_str_search(&cfg->arg_head_s, "path",
									NULL);
	struct bpair_str *bp_type = bpair_str_search(&cfg->arg_head_s, "type",
									NULL);
	if (!bp_path || !bp_type)
		return EINVAL;

	const char *path = bp_path->s1;
	btkn_type_t tkn_type = btkn_type(bp_type->s1);
	switch (tkn_type) {
	case BTKN_TYPE_HOST:
		store = comp_store;
		break;
	case BTKN_TYPE_ENG:
		store = token_store;
		break;
	default:
		berr("Unknown token type: %s\n", bp_type->s1);
		return EINVAL;
	}
	FILE *fi = fopen(path, "rt");
	if (!fi)
		return errno;
	char buff[1024 + sizeof(struct bstr)];
	struct bstr *bstr = (void*)buff;
	char *c;
	uint32_t tkn_id;
	while (fgets(bstr->cstr, 1024, fi)) {
		c = bstr->cstr + strlen(bstr->cstr) - 1;
		while (isspace(*c))
			*c-- = '\0';
		bstr->blen = strlen(bstr->cstr);
		if (!bstr->blen)
			continue; /* skip empty line */
		tkn_id = btkn_store_insert(store, bstr);
		if (tkn_id == BMAP_ID_ERR) {
			berr("cannot insert '%s' into token store", bstr->cstr);
			return errno;
		}
		struct btkn_attr attr = {tkn_type};
		btkn_store_set_attr(token_store, tkn_id, attr);
	}
	return 0;
}

int process_cmd_hosts(struct bconfig_list *bl)
{
	/*
	 * NOTE 1: comp_store should be filled at this point.
	 *         Each hostname should be able to resolved into an internal
	 *         component id.
	 *
	 * NOTE 2: attribute-value list of 'hosts' command is
	 * hostname1:metric_id1, hostnam2:metric_id2, ...
	 */
	struct btkn_store *store = comp_store;
	struct bpair_str *av;
	char buff[512];
	struct bstr *bs = (void*)buff;
	LIST_FOREACH(av, &bl->arg_head_s, link) {
		uint64_t mid = strtoull(av->s1, NULL, 0);
		bstr_set_cstr(bs, av->s0, 0);
		uint32_t cid = bmap_get_id(store->map, bs);
		if (cid < BMAP_ID_BEGIN) {
			cid = btkn_store_insert(comp_store, bs);
			if (cid < BMAP_ID_BEGIN) {
				berr("cannot insert host: %s\n", av->s0);
				return EINVAL;
			}
		}
		if (cid >= 1024*1024) {
			berr("component_id for '%s' out of range: %d\n", av->s0,
					cid);
			return EINVAL;
		}
		cid = bmapid2compid(cid);
		metric_ids[cid] = mid;
	}
	return 0;
}

/**
 * Process the given command \c cmd.
 * \param cmd The command to process.
 * \return 0 on success.
 * \return Error code on error.
 */
int process_command(const char *cmd)
{
	struct bconfig_list *cfg = parse_config_str(cmd);
	int rc = 0;

	switch (bcfg_cmd_str2enum(cfg->command)) {
	case BCFG_CMD_PLUGIN:
		rc = process_cmd_plugin(cfg);
		break;
	case BCFG_CMD_TOKENS:
		rc = process_cmd_tokens(cfg);
		break;
	case BCFG_CMD_HOSTS:
		rc = process_cmd_hosts(cfg);
		break;
	default:
		/* Unknown command */
		berr("Unknown command: %s", cfg->command);
		rc = EINVAL;
	}

	bconfig_list_free(cfg);

	return rc;
}

/**
 * Configuration file handling function.
 */
void config_file_handling(const char *path)
{
	FILE *fin = fopen(path, "rt");
	if (!fin) {
		perror("fopen");
		exit(-1);
	}
	char buff[1024];
	int lno = 0;
	int rc = 0;
	char *s;
	while (fgets(buff, 1024, fin)) {
		lno++;
		/* eliminate trailing spaces */
		s = buff + strlen(buff) - 1;
		while (isspace(*s))
			*s = '\0';
		/* eliminate preceding white spaces */
		s = buff;
		while (isspace(*s))
			s++;
		if (*s == '#')
			continue; /* comment line */
		if ((rc = process_command(s))) {
			berr("process_command error %d: %s at %s:%d\n",
					rc, strerror(rc), path, lno);
			exit(rc);
		}
	}
}

#ifdef ENABLE_OCM
/* This is an interface to the existing baler configuration */
void process_ocm_cmd(ocm_cfg_cmd_t cmd)
{
	struct bconfig_list *bl;
	struct bpair_str *bp;
	struct bpair_str *blast;
	struct ocm_av_iter iter;
	const char *key;
	const struct ocm_value *v;

	bl = calloc(1, sizeof(*bl));
	if (!bl) {
		berr("%m at %s:%d in %s\n", __FILE__, __LINE__, __func__);
		return;
	}

	/* Create baler command from ocm command */
	ocm_av_iter_init(&iter, cmd);
	bl->command = (char*)ocm_cfg_cmd_verb(cmd);
	blast = NULL;
	while (ocm_av_iter_next(&iter, &key, &v) == 0) {
		bp = calloc(1, sizeof(*bp));
		bp->s0 = (char*)key;
		bp->s1 = (char*)v->s.str;
		if (!blast)
			LIST_INSERT_HEAD(&bl->arg_head_s, bp, link);
		else
			LIST_INSERT_AFTER(blast, bp, link);
		blast = bp;
	}
	/* end create baler command */

	/* Process the command */
	switch (bcfg_cmd_str2enum(bl->command)) {
	case BCFG_CMD_PLUGIN:
		process_cmd_plugin(bl);
		break;
	case BCFG_CMD_TOKENS:
		process_cmd_tokens(bl);
		break;
	case BCFG_CMD_HOSTS:
		process_cmd_hosts(bl);
		break;
	default:
		/* Unknown command */
		berr("Unknown command: %s", bl->command);
	}

cleanup:
	while ((bp = LIST_FIRST(&bl->arg_head_s))) {
		LIST_REMOVE(bp, link);
		free(bp);
	}
	free(bl);
}

void process_ocm_cfg(ocm_cfg_t cfg)
{
	ocm_cfg_cmd_t cmd;
	struct ocm_cfg_cmd_iter iter;
	ocm_cfg_cmd_iter_init(&iter, cfg);
	while (ocm_cfg_cmd_iter_next(&iter, &cmd) == 0) {
		process_ocm_cmd(cmd);
	}
}

int ocm_cb(struct ocm_event *e)
{
	switch (e->type) {
	case OCM_EVENT_CFG_REQUESTED:
		ocm_event_resp_err(e, ENOSYS, ocm_cfg_req_key(e->req),
							"Not implemented.");
		break;
	case OCM_EVENT_CFG_RECEIVED:
		process_ocm_cfg(e->cfg);
		break;
	case OCM_EVENT_ERROR:
		berr("ocm event error, key: %s, code: %d, msg: %s",
				ocm_err_key(e->err),
				ocm_err_code(e->err),
				ocm_err_msg(e->err));
		break;
	default:
		/* do nothing, but suppressing compilation warning */
		break;
	}
	return 0;
}
#endif

void handle_set_log_file(const char *path)
{
	log_path = strdup(path);
	if (!log_path) {
		perror(path);
		exit(-1);
	}

	int rc = blog_open_file(log_path);
	if (rc) {
		fprintf(stderr, "Failed to open the log file '%s'\n", path);
		exit(rc);
	}
}

/**
 * Configuration handling.
 * Currently, just do stuffs from the command line. This function needs to be
 * changed later to receive the real configuration from the configuration
 * manager.
 */
void args_handling(int argc, char **argv)
{
	bset_store_path("./store");
	int c;
	int len;
	int rc;

	blog_set_level(BLOG_LV_WARN);

next_arg:
	c = getopt(argc, argv, optstring);
	switch (c) {
	case 'l':
		handle_set_log_file(optarg);
		break;
	case 's':
		bset_store_path(optarg);
		break;
	case 'F':
		is_foreground = 1;
		break;
	case 'C':
		config_path = strdup(optarg);
		break;
#ifdef ENABLE_OCM
	case 'z':
		ocm_port = atoi(optarg);
		break;
#endif
	case 'm':
		len = strlen(optarg);
		if (strncasecmp("master", optarg, len) == 0) {
			is_master = 1;
		} else if (strncasecmp("slave", optarg, len) == 0) {
			is_master = 0;
		} else {
			berr("Unknown mode: %s", optarg);
			exit(-1);
		}
		break;
	case 'x':
		sm_xprt = optarg;
		break;
	case 'h':
		m_host = optarg;
		break;
	case 'p':
		m_port = atoi(optarg);
		break;
	case 'v':
		rc = blog_set_level_str(optarg);
		if (rc) {
			berr("Invalid verbosity level: %s", optarg);
			exit(-1);
		}
		break;
	case '?':
		display_help_msg();
		exit(0);
	case -1:
		goto arg_done;
	}
	goto next_arg;

arg_done:
	binfo("Baler Daemon Started.\n");
}

void binq_data_print(struct binq_data *d)
{
	printf("binq: %.*s %ld (%d): ", d->hostname->blen, d->hostname->cstr,
			d->tv.tv_sec, d->tok_count);
	struct bstr_list_entry *e;
	LIST_FOREACH(e, &d->tokens, link) {
		printf(" '%.*s'", e->str.blen, e->str.cstr);
	}
	printf("\n");
}

int slave_bzmsg_send(struct bzmsg *bzmsg)
{
	zap_err_t zerr;
	int rc = 0;
	size_t len;
	pthread_mutex_lock(&slave_zap_ctxt.mutex);
	if (!slave_zap_ctxt.is_ready) {
		rc = EIO;
		goto out;
	}

	len = bzmsg_len(bzmsg);
	hton_bzmsg(bzmsg);
	zerr = zap_send(zap_ep, bzmsg, len);
	if (zerr != ZAP_ERR_OK)
		rc = EIO;

out:
	pthread_mutex_unlock(&slave_zap_ctxt.mutex);
	return rc;
}

int slave_process_input_entry_step3(struct bwq_entry *ent)
{
	struct bin_wkr_ctxt *ctxt = ent->ctxt;
	struct binq_data *in_data = &ent->data.in;
	struct bmsg *msg = &ctxt->msg;
	int rc;
	rc = bptn_store_addmsg(pattern_store, msg);
	if (rc)
		goto cleanup;
	/* Copy msg to omsg for future usage in output queue. */
	struct bmsg *omsg = bmsg_alloc(msg->argc);
	if (!omsg) {
		rc = ENOMEM;
		goto cleanup;
	}
	memcpy(omsg, msg, BMSG_SZ(msg));
	/* Prepare output queue entry. */
	struct bwq_entry *oent = (typeof(oent))malloc(sizeof(*oent));
	struct boutq_data *odata = &oent->data.out;
	odata->comp_id = ctxt->comp_id;
	odata->tv = in_data->tv;
	odata->msg = omsg;
	/* Put the processed message into output queue */
	bwq_nq(boutq, oent);
cleanup:
	binq_entry_free(ent);
	free(ent->ctxt);
	return rc;
}

void process_input_extract_ptn(struct binq_data *in_data, struct bmsg *msg, struct bstr *str)
{
	int tkn_idx = 0;
	int attr_count = 0;

	if (in_data->type == BINQ_DATA_METRIC) {
		msg->argc = 0;
		return;
	}

	/* Otherwise, we need to mark some of the token in 'str' as '*' */
	attr_count = 0;
	for (tkn_idx = 0; tkn_idx < in_data->tok_count; tkn_idx++) {
		int tid = str->u32str[tkn_idx];
		struct btkn_attr attr = btkn_store_get_attr(token_store, tid);
		/* REMARK: The default type of a token is '*' */
		if (attr.type == BTKN_TYPE_STAR) {
			/* This will be marked as '*' and put into arg list. */
			str->u32str[tkn_idx] = BMAP_ID_STAR;
			msg->argv[attr_count++] = tid;
		}
		/* else, do nothing */
	}
	msg->argc = attr_count;
}

int slave_process_input_entry_step2(struct bwq_entry *ent)
{
	int rc = 0;
	struct bin_wkr_ctxt *ctxt = ent->ctxt;
	struct binq_data *in_data = &ent->data.in;

	struct bmsg *msg = &ctxt->msg;
	struct bstr *str = &ctxt->ptn_str;
	int attr_count = 0;
	int tkn_idx = 0;
	struct bstr_list_entry *str_ent;

	ctxt->next_fn = slave_process_input_entry_step3;

	process_input_extract_ptn(in_data, msg, str);

	/* Now str is the pattern string, with arguments in msg->argv */

	msg->ptn_id = bptn_store_get_id(pattern_store, str);
	if (msg->ptn_id >= BMAP_ID_BEGIN)
		return slave_process_input_entry_step3(ent);

	zap_err_t zerr;
	struct bzmsg *bzmsg = &ctxt->zmsg;
	bzmsg->type = BZMSG_INSERT_REQ;
	bzmsg->mapidx = BMAP_IDX_PTN;
	bzmsg->attr.type = -1;
	bzmsg->ctxt = ent;
	bzmsg->ctxt_bstr = str;
	bstr_cpy(&bzmsg->bstr, str);
	ctxt->unresolved_count = 1;
	rc = slave_bzmsg_send(bzmsg);
	if (rc)
		goto err;

	return 0;

err:
	binq_entry_free(ent);
	free(ent->ctxt);
	return rc;
}

/*
 * TODO make sure that ctxt owns by ent. (see callers)
 */
int slave_process_input_entry_step1(struct bwq_entry *ent, struct bin_wkr_ctxt *_ignore)
{
	int rc = 0;
	struct binq_data *in_data = &ent->data.in;
	uint32_t comp_id;
	int unresolved_count = 0;

	if (in_data->type == BINQ_DATA_MSG) {
		comp_id = bmap_get_id(comp_store->map, in_data->hostname);
		if (comp_id < BMAP_ID_BEGIN) {
			comp_id = -1; /* all 0xFF */
			unresolved_count++;
		} else {
			comp_id = bmapid2compid(comp_id);
		}
	} else {
		comp_id = in_data->hostname->u32str[0];
	}

	struct bin_wkr_ctxt *ctxt = malloc(sizeof(*ctxt));
	if (!ctxt) {
		berr("Cannot allocate input context in %s:%s",
							__FILE__, __func__);
		return ENOMEM;
	}

	ent->ctxt = ctxt;
	ctxt->comp_id = comp_id;
	ctxt->next_fn = slave_process_input_entry_step2;
	ctxt->rc = 0;
	ctxt->worker_id = -1;

	struct bstr_list_entry *str_ent;
	/* NOTE: ptn and msg is meant to be a uint32_t string */
	struct bstr *str = &ctxt->ptn_str;
	/* msg->agrv will hold the pattern arguments. */
	uint32_t attr_count = 0;
	uint32_t tkn_idx = 0;
	/* resolving token IDs, use ctxt->ptn_str to temporarily hold tok IDs */
	LIST_FOREACH(str_ent, &in_data->tokens, link) {
		struct btkn_attr attr;
		int tid = btkn_store_get_id(token_store, &str_ent->str);
		if (tid == BMAP_ID_NOTFOUND)
			unresolved_count++;
		str->u32str[tkn_idx] = tid;
		tkn_idx++;
	}
	str->blen = in_data->tok_count * sizeof(uint32_t);
	ctxt->unresolved_count = unresolved_count;

	if (!unresolved_count)
		return slave_process_input_entry_step2(ent);

	/* Reaching here means there are some unresolved */
	/* Make requests for the unresolved tokens */
	zap_err_t zerr;
	struct bzmsg *bzmsg = &ctxt->zmsg;

	if (ctxt->comp_id == -1) {
		bzmsg->ctxt = (void*)ent;
		bzmsg->ctxt_bstr = in_data->hostname;
		bzmsg->type = BZMSG_INSERT_REQ;
		bzmsg->attr.type = BTKN_TYPE_HOST;
		bzmsg->tkn_idx = -1;
		bzmsg->mapidx = BMAP_IDX_HST;
		bstr_cpy(&bzmsg->bstr, in_data->hostname);
		rc = slave_bzmsg_send(bzmsg);
		if (rc)
			goto err;
	}

	tkn_idx = 0;
	LIST_FOREACH(str_ent, &in_data->tokens, link) {
		if (str->u32str[tkn_idx] != BMAP_ID_NOTFOUND)
			goto next;
		bzmsg->ctxt = ent;
		bzmsg->ctxt_bstr = &str_ent->str;
		bzmsg->type = BZMSG_INSERT_REQ;
		bzmsg->attr.type = BTKN_TYPE_LAST; /* master will set it */
		bzmsg->tkn_idx = tkn_idx;
		bzmsg->mapidx = BMAP_IDX_TKN;
		bstr_cpy(&bzmsg->bstr, &str_ent->str);
		rc = slave_bzmsg_send(bzmsg);
		if (rc)
			goto err;
	next:
		tkn_idx++;
	}

	return rc;

err:
	binq_entry_free(ent);
	free(ent->ctxt);
	return rc;
}

/**
 * Core processing of an input entry.
 * \param ent Input entry.
 * \param ctxt The context of the worker.
 * \return 0 on success.
 * \return errno on error.
 */
int process_input_entry(struct bwq_entry *ent, struct bin_wkr_ctxt *ctxt)
{
	int rc = 0;
	struct binq_data *in_data = &ent->data.in;
	uint32_t comp_id;
	if (in_data->type == BINQ_DATA_MSG) {
		comp_id = bmap_get_id(comp_store->map, in_data->hostname);
		if (comp_id < BMAP_ID_BEGIN) {
			/* Error, cannot find the comp_id */
			berr("host not found: %.*s", in_data->hostname->blen,
					in_data->hostname->cstr);
			rc = ENOENT;
			goto cleanup;
		}
		comp_id = bmapid2compid(comp_id);
	} else {
		comp_id = in_data->hostname->u32str[0];
	}
	struct bstr_list_entry *str_ent;
	/* NOTE: ptn and msg is meant to be a uint32_t string */
	struct bstr *str = &ctxt->ptn_str;
	/* msg->agrv will hold the pattern arguments. */
	struct bmsg *msg = &ctxt->msg;
	uint32_t tkn_idx = 0;
	LIST_FOREACH(str_ent, &in_data->tokens, link) {
		int tid = btkn_store_insert(token_store, &str_ent->str);
		if (tid == BMAP_ID_ERR) {
			rc = errno;
			goto cleanup;
		}
		struct btkn_attr attr = btkn_store_get_attr(token_store, tid);
		str->u32str[tkn_idx] = tid;
		tkn_idx++;
	}
	str->blen = tkn_idx * sizeof(uint32_t);

	process_input_extract_ptn(in_data, msg, str);

	/* Now str is the pattern string, with arguments in msg->argv */
	/* pid stands for pattern id */
	uint32_t pid = bptn_store_addptn(pattern_store, str);
	if (pid == BMAP_ID_ERR) {
		rc = errno;
		goto cleanup;
	}
	msg->ptn_id = pid;
	rc = bptn_store_addmsg(pattern_store, msg);
	if (rc)
		goto cleanup;
	/* Copy msg to omsg for future usage in output queue. */
	struct bmsg *omsg = bmsg_alloc(msg->argc);
	if (!omsg) {
		rc = ENOMEM;
		goto cleanup;
	}
	memcpy(omsg, msg, BMSG_SZ(msg));
	/* Prepare output queue entry. */
	struct bwq_entry *oent = (typeof(oent))malloc(sizeof(*oent));
	struct boutq_data *odata = &oent->data.out;
	odata->comp_id = comp_id;
	odata->tv = in_data->tv;
	odata->msg = omsg;
	/* Put the processed message into output queue */
	bwq_nq(boutq, oent);

cleanup:
	binq_entry_free(ent);
	return rc;
}

/**
 * Routine for Input Queue Worker. When data are avialable in the input queue,
 * one of the input queue workers will get an access to the input queue.
 * It then consume the input entry and release the input queue lock so that
 * the other workers can work on the next entry.
 * \param arg A pointer to ::bin_wkr_ctxt.
 * \return NULL (should be ignored)
 */
void* binqwkr_routine(void *arg)
{
	struct bwq_entry *ent;
	int (*fn)(struct bwq_entry *, struct bin_wkr_ctxt *);
	if (is_master)
		fn = process_input_entry;
	else
		fn = slave_process_input_entry_step1;
loop:
	/* bwq_dq will block the execution if the queue is empty. */
	ent = bwq_dq(binq);
	if (!ent) {
		/* This is not supposed to happen. */
		berr("Error, ent == NULL\n");
	}
	if (fn(ent, (struct bin_wkr_ctxt*) arg) == -1) {
		/* XXX Do better error handling ... */
		berr("process input error ...");
	}
	goto loop;
}

int process_output_entry(struct bwq_entry *ent, struct bout_wkr_ctxt *ctxt)
{
	struct boutq_data *d = &ent->data.out;
	struct bplugin *p;
	struct boutplugin *op;
	int rc = 0;
	LIST_FOREACH(p, &bop_head_s, link) {
		op = (typeof(op))p;
		rc = op->process_output(op, d);
		/** TODO Better error handling here */
		if (rc) {
			bwarn("Output plugin %s->process_output error, code:"
				       " %d\n", p->name, rc);
		}
	}
	return 0;
}

/**
 * Work routine for Output Queue Worker Thread.
 * \param arg A pointer to ::bout_wkr_ctxt.
 * \return Nothing as the worker thread(s) run forever.
 */
void* boutqwkr_routine(void *arg)
{
	struct bwq_entry *ent;
loop:
	/* bwq_dq will block the execution if the queue is empty. */
	ent = bwq_dq(boutq);
	if (!ent) {
		/* This is not supposed to happen. */
		berr("Error, ent == NULL\n");
	}
	if (process_output_entry(ent, (struct bout_wkr_ctxt *) arg) == -1) {
		/* XXX Do better error handling ... */
		berr("process input error, code %d\n", errno);
	}
	boutq_entry_free(ent);
	goto loop;
}

/**
 * Input Worker Thread & Output Worker Thread join point.
 */
void thread_join()
{
	int i;
	/* Joining the input queue workers */
	for (i=0; i<binqwkrN; i++){
		pthread_join(binqwkr[i], NULL);
	}

	/* Joining the output queue workers */
	for (i=0; i<boutqwkrN; i++){
		pthread_join(boutqwkr[i], NULL);
	}
}

/**
 * Daemon clean-up routine.
 */
void cleanup_daemon(int x)
{
	binfo("Baler Daemon exiting ... code %d\n", x);
	exit(0);
}

/**
 * Action when receives SIGUSR1 from logrotate
 */
void handle_logrotate(int x)
{
	int rc = 0;
	rc = blog_rotate(log_path);
	if (rc) {
		berr("Failed to open a new log file. '%s'\n",
						strerror(errno));
		cleanup_daemon(x);
	}
}

void keepalive_cb(int fd, short what, void *arg)
{
	berr("keepalive_cb() should not be invoked!!!");
	assert(0);
}

void main_event_routine()
{
	/* Instead of periodiaclly getting invoked, this keepalive event is
	 * looking for an impossible read event on fd -1. Hence, it will forever
	 * be pending in the evbase and keep evbase from exiting. */
	struct event *keepalive = event_new(evbase, -1, EV_READ | EV_PERSIST,
							keepalive_cb, 0);
	event_add(keepalive, NULL);
	event_base_dispatch(evbase);
	bwarn("Exiting main_event_routine()");
}

/**
 * \brief The main function.
 */
int main(int argc, char **argv)
{
	struct sigaction cleanup_act, logrotate_act;
	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGUSR1);
	cleanup_act.sa_handler = cleanup_daemon;
	cleanup_act.sa_flags = 0;
	cleanup_act.sa_mask = sigset;

	sigaction(SIGHUP, &cleanup_act, NULL);
	sigaction(SIGINT, &cleanup_act, NULL);
	sigaction(SIGTERM, &cleanup_act, NULL);

	sigaddset(&sigset, SIGHUP);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGTERM);
	logrotate_act.sa_handler = handle_logrotate;
	logrotate_act.sa_flags = 0;
	logrotate_act.sa_mask = sigset;
	sigaction(SIGUSR1, &logrotate_act, NULL);

	args_handling(argc, argv);

	/* Initializing daemon and worker threads according to the
	 * configuration. */
	initialize_daemon();

	if (config_path)
		config_file_handling(config_path);

	binfo("Baler is ready.");

	/* Main thread handle events pertaining to balerd */
	main_event_routine();

	/* Then join the worker threads. */
	thread_join();

	/* On exit, clean up the daemon. */
	cleanup_daemon(0);
	return 0;
}
/**\}*/
