/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2017 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2017 Open Grid Computing, Inc. All rights reserved.
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
#define _GNU_SOURCE
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <pthread.h>
#include <dlfcn.h>
#include <assert.h>
#include <time.h>
#include <limits.h>
#include <fcntl.h>
#include <netdb.h>
#include "ovis-lib-config.h"
#include "zap.h"
#include "zap_priv.h"

#ifdef DEBUG
#define DLOG(ep, fmt, ...) do { \
	if (ep && ep->z && ep->z->log_fn) \
		ep->z->log_fn(fmt, ##__VA_ARGS__); \
} while(0)
#else /* DEBUG */
#define DLOG(ep, fmt, ...)
#endif /* DEBUG */

#ifdef DEBUG
int __zap_assert = 1;
#else
int __zap_assert = 0;
#endif

static void default_log(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	fflush(stdout);
}

#if 0
#define TF() default_log("%s:%d\n", __FUNCTION__, __LINE__)
#else
#define TF()
#endif

LIST_HEAD(zap_list, zap) zap_list;

pthread_mutex_t zap_list_lock;

static int zap_event_workers = ZAP_EVENT_WORKERS;
static int zap_event_qdepth = ZAP_EVENT_QDEPTH;

struct zap_event_queue *zev_queue;

struct ovis_heap *zev_queue_heap;

#ifndef PLUGINDIR
#define PLUGINDIR "/usr/local/lib/ovis-lib"
#endif
#define ZAP_LIBPATH_DEFAULT PLUGINDIR
#define _SO_EXT ".so"
#define MAX_ZAP_LIBPATH	1024

const char *__zap_ep_state_str(zap_ep_state_t state)
{
	if (state < ZAP_EP_INIT || ZAP_EP_ERROR < state)
		return "UNKNOWN_STATE";
	return zap_ep_state_str[state];
}

void __zap_assert_flag(int f) {
	__zap_assert = f;
}

static char *__zap_event_str[] = {
	"ZAP_EVENT_ILLEGAL",
	"ZAP_EVENT_CONNECT_REQUEST",
	"ZAP_EVENT_CONNECT_ERROR",
	"ZAP_EVENT_CONNECTED",
	"ZAP_EVENT_REJECTED",
	"ZAP_EVENT_DISCONNECTED",
	"ZAP_EVENT_RECV_COMPLETE",
	"ZAP_EVENT_READ_COMPLETE",
	"ZAP_EVENT_WRITE_COMPLETE",
	"ZAP_EVENT_RENDEZVOUS",
	"ZAP_EVENT_LAST"
};

enum zap_err_e zap_errno2zerr(int e)
{
	switch (e) {
	case ENOTCONN:
		return ZAP_ERR_NOT_CONNECTED;
	case ENOMEM:
	case ENOBUFS:
		return ZAP_ERR_RESOURCE;
	case ECONNREFUSED:
		return ZAP_ERR_CONNECT;
	case EISCONN:
		return ZAP_ERR_BUSY;
	case EFAULT:
	case EINVAL:
		return ZAP_ERR_PARAMETER;
	default:
		return ZAP_ERR_ENDPOINT;
	}
}

const char* zap_event_str(enum zap_event_type e)
{
	if ((int)e < 0 || e > ZAP_EVENT_LAST)
		return "ZAP_EVENT_UNKNOWN";
	return __zap_event_str[e];
}

static inline
void zap_event_queue_ep_get(struct zap_event_queue *q)
{
	pthread_mutex_lock(&q->mutex);
	q->ep_count++;
	pthread_mutex_unlock(&q->mutex);
}

static inline
void zap_event_queue_ep_put(struct zap_event_queue *q)
{
	pthread_mutex_lock(&q->mutex);
	q->ep_count--;
	pthread_mutex_unlock(&q->mutex);
}

void *zap_get_ucontext(zap_ep_t ep)
{
	return ep->ucontext;
}

void zap_set_ucontext(zap_ep_t ep, void *context)
{
	ep->ucontext = context;
}

zap_mem_info_t default_zap_mem_info(void)
{
	return NULL;
}

static
void zap_interpose_event(zap_ep_t ep, void *ctxt);

struct zap_tbl_entry {
	zap_type_t type;
	const char *name;
	zap_t zap;
};
struct zap_tbl_entry __zap_tbl[] = {
	[ ZAP_SOCK   ]  =  { ZAP_SOCK   , "sock"   , NULL },
	[ ZAP_RDMA   ]  =  { ZAP_RDMA   , "rdma"   , NULL },
	[ ZAP_UGNI   ]  =  { ZAP_UGNI   , "ugni"   , NULL },
	[ ZAP_FABRIC ]  =  { ZAP_FABRIC , "fabric" , NULL },
	[ ZAP_LAST   ]  =  { 0          , NULL     , NULL },
};

zap_t zap_get(const char *name, zap_log_fn_t log_fn, zap_mem_info_fn_t mem_info_fn)
{
	char _libdir[MAX_ZAP_LIBPATH];
	char _libpath[MAX_ZAP_LIBPATH];
	char *libdir;
	char *libpath;
	char *lib = _libpath;
	zap_t z = NULL;
	char *errstr;
	int ret;
	void *d = NULL;
	char *saveptr = NULL;
	struct zap_tbl_entry *zent;

	for (zent = __zap_tbl; zent->name; zent++) {
		if (0 != strcmp(zent->name, name))
			continue;
		/* found the entry */
		if (zent->zap) /* already loaded */
			return zent->zap;
		break;
	}
	if (!zent->name) {
		/* unknown zap name */
		errno = ENOENT;
		return NULL;
	}

	/* otherwise, it is a known zap name but has not been loaded yet */
	pthread_mutex_lock(&zap_list_lock);
	if (!log_fn)
		log_fn = default_log;
	if (!mem_info_fn)
		mem_info_fn = default_zap_mem_info;

	libdir = getenv("ZAP_LIBPATH");
	if (!libdir || libdir[0] == '\0')
		strncpy(_libdir, ZAP_LIBPATH_DEFAULT, sizeof(_libdir));
	else
		strncpy(_libdir, libdir, sizeof(_libdir));

	libdir = _libdir;

	while ((libpath = strtok_r(libdir, ":", &saveptr)) != NULL) {
		libdir = NULL;
		snprintf(lib, sizeof(_libpath) - 1, "%s/libzap_%s%s",
			 libpath, name, _SO_EXT);

		d = dlopen(lib, RTLD_NOW);
		if (d != NULL) {
			break;
		}
	}

	if (!d) {
		/* The library doesn't exist */
		log_fn("dlopen: %s\n", dlerror());
		goto err;
	}

	dlerror();
	zap_get_fn_t get = dlsym(d, "zap_transport_get");
	errstr = dlerror();
	if (errstr || !get) {
		log_fn("dlsym: %s\n", errstr);
		/* The library exists but doesn't export the correct
		 * symbol and is therefore likely the wrong library type */
		goto err1;
	}
	ret = get(&z, log_fn, mem_info_fn);
	if (ret)
		goto err1;

	strncpy(z->name, name, sizeof(z->name));
	z->log_fn = log_fn;
	z->mem_info_fn = mem_info_fn;
	z->event_interpose = zap_interpose_event;

	zent->zap = z;
	LIST_INSERT_HEAD(&zap_list, z, zap_link);
	pthread_mutex_unlock(&zap_list_lock);
	return z;

err1:
	dlerror();
	ret = dlclose(d);
	if (ret) {
		errstr = dlerror();
		log_fn("dlclose: %s\n", errstr);
	}
err:
	pthread_mutex_unlock(&zap_list_lock);
	return NULL;
}

size_t zap_max_msg(zap_t z)
{
	return z->max_msg;
}

void blocking_zap_cb(zap_ep_t zep, zap_event_t ev)
{
	switch (ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
	case ZAP_EVENT_READ_COMPLETE:
	case ZAP_EVENT_RECV_COMPLETE:
	case ZAP_EVENT_RENDEZVOUS:
		ZLOG(zep, "zap: Unhandling event %s\n", zap_event_str(ev->type));
		break;
	case ZAP_EVENT_CONNECTED:
	case ZAP_EVENT_CONNECT_ERROR:
	case ZAP_EVENT_REJECTED:
		sem_post(&zep->block_sem);
		break;
	case ZAP_EVENT_DISCONNECTED:
		/* Just do nothing */
		break;
	case ZAP_EVENT_WRITE_COMPLETE:
		/* Do nothing */
		break;
	default:
		assert(0);
	}
}

struct zap_interpose_ctxt {
	struct zap_event ev;
	unsigned char data[OVIS_FLEX];
};

/*
 * interposing a real callback, putting callback task into the queue.
 * Only read/write/recv completions are posted to the queue.
 */
static
void zap_interpose_cb(zap_ep_t ep, zap_event_t ev)
{
	struct zap_interpose_ctxt *ictxt;
	uint32_t data_len = 0;

	switch (ev->type) {
	/* these events need data copy */
	case ZAP_EVENT_RENDEZVOUS:
	case ZAP_EVENT_REJECTED:
	case ZAP_EVENT_CONNECTED:
	case ZAP_EVENT_RECV_COMPLETE:
	case ZAP_EVENT_CONNECT_REQUEST:
		data_len = ev->data_len;
		break;
	/* these do not need data copy */
	case ZAP_EVENT_CONNECT_ERROR:
	case ZAP_EVENT_DISCONNECTED:
	case ZAP_EVENT_READ_COMPLETE:
	case ZAP_EVENT_WRITE_COMPLETE:
		ev->data = NULL;
		ev->data_len = 0;
		/* do nothing */
		break;
	default:
		assert(0);
		break;
	}

	ictxt = calloc(1, sizeof(*ictxt) + data_len);
	if (!ictxt) {
		DLOG(ep, "zap_interpose_cb(): ENOMEM\n");
		return;
	}
	DLOG(ep, "%s: Vep %p: ictxt %p: ev type %s\n", __func__, ep,
				ictxt, zap_event_str(ev->type));
#ifdef TMP_DEBUG
	ep->z->log_fn("%s: Vep %p: ictxt %p: ev type %s. q->depth = %d\n",
				__func__, ep,
				ictxt, zap_event_str(ev->type),
				ep->event_queue->depth);
#endif /* TMP_DEBUG */
	ictxt->ev = *ev;
	ictxt->ev.data = ictxt->data;
	if (data_len)
		memcpy(ictxt->data, ev->data, data_len);
	zap_get_ep(ep);
	if (zap_event_add(ep->event_queue, ep, ictxt)) {
		ep->z->log_fn("%s[%d]: event could not be added.",
			      __func__, __LINE__);
		zap_put_ep(ep);
	}
}

static
void zap_interpose_event(zap_ep_t ep, void *ctxt)
{
	/* delivering real io event callback */
	struct zap_interpose_ctxt *ictxt = ctxt;
	DLOG(ep, "%s: ep %p: ictxt %p\n", __func__, ep, ictxt);
#if defined(ZAP_DEBUG) || defined(DEBUG)
	if (ep->state == ZAP_EP_CLOSE)
		default_log("Delivering event after close.\n");
#endif /* ZAP_DEBUG || DEBUG */
	ep->app_cb(ep, &ictxt->ev);
	free(ictxt);
	zap_put_ep(ep);
}

static
struct zap_event_queue *__get_least_busy_zap_event_queue();

static
zap_ep_t __common_ep_setup(zap_ep_t zep, zap_t z, zap_cb_fn_t cb)
{
	zep->z = z;
	zep->app_cb = cb;
	zep->cb = zap_interpose_cb;
	zep->ref_count = 1;
	zep->state = ZAP_EP_INIT;
	pthread_mutex_init(&zep->lock, NULL);
	sem_init(&zep->block_sem, 0, 0);
	zep->event_queue = __get_least_busy_zap_event_queue();
	return zep;
}

zap_ep_t zap_new(zap_t z, zap_cb_fn_t cb)
{
	zap_ep_t zep = NULL;
	if (!cb)
		cb = blocking_zap_cb;
	zep = z->new(z, cb);
	if (!zep)
		return NULL;
	return __common_ep_setup(zep, z, cb);
}

char **zap_get_env(zap_t z)
{
	errno = 0;
	return z->get_env();
}

void zap_set_priority(zap_ep_t ep, int prio)
{
	ep->prio = prio;
}

zap_err_t zap_accept(zap_ep_t ep, zap_cb_fn_t cb, char *data, size_t data_len)
{
	zap_err_t zerr;
	ep->app_cb = cb;
	ep->cb = zap_interpose_cb;
	zerr = ep->z->accept(ep, zap_interpose_cb, data, data_len);
	return zerr;
}

zap_err_t zap_connect_by_name(zap_ep_t ep, const char *host, const char *port,
			      char *data, size_t data_len)
{
	struct addrinfo *ai;
	int rc = getaddrinfo(host, port, NULL, &ai);
	if (rc)
		return ZAP_ERR_RESOURCE;
	zap_err_t zerr = zap_connect_sync(ep, ai->ai_addr, ai->ai_addrlen,
					  data, data_len);
	freeaddrinfo(ai);
	return zerr;
}

zap_err_t zap_connect_sync(zap_ep_t ep, struct sockaddr *sa, socklen_t sa_len,
			   char *data, size_t data_len)
{
	zap_err_t zerr = zap_connect(ep, sa, sa_len, data, data_len);
	if (zerr)
		return zerr;
	sem_wait(&ep->block_sem);
	if (ep->state != ZAP_EP_CONNECTED)
		return ZAP_ERR_CONNECT;
	return ZAP_ERR_OK;
}

zap_err_t zap_connect(zap_ep_t ep, struct sockaddr *sa, socklen_t sa_len,
		      char *data, size_t data_len)
{
	return ep->z->connect(ep, sa, sa_len, data, data_len);
}

zap_err_t zap_listen(zap_ep_t ep, struct sockaddr *sa, socklen_t sa_len)
{
	return ep->z->listen(ep, sa, sa_len);
}

zap_err_t zap_close(zap_ep_t ep)
{
	zap_err_t zerr = ep->z->close(ep);
	return zerr;
}

zap_err_t zap_send(zap_ep_t ep, void *buf, size_t sz)
{
	zap_err_t zerr;
	zap_get_ep(ep);
	zerr = ep->z->send(ep, buf, sz);
	zap_put_ep(ep);
	return zerr;
}

zap_err_t zap_write(zap_ep_t ep,
		    zap_map_t src_map, void *src,
		    zap_map_t dst_map, void *dst,
		    size_t sz,
		    void *context)
{
	zap_err_t zerr;
	zap_get_ep(ep);
	zerr = ep->z->write(ep, src_map, src, dst_map, dst, sz, context);
	zap_put_ep(ep);
	return zerr;
}

zap_err_t zap_get_name(zap_ep_t ep, struct sockaddr *local_sa,
		       struct sockaddr *remote_sa, socklen_t *sa_len)
{
	return ep->z->get_name(ep, local_sa, remote_sa, sa_len);
}

void zap_get_ep(zap_ep_t ep)
{
	(void)__sync_fetch_and_add(&ep->ref_count, 1);
}

void zap_free(zap_ep_t ep)
{
	/* Drop the zap_new() reference */
	assert(ep->ref_count);
	zap_put_ep(ep);
}

void zap_put_ep(zap_ep_t ep)
{
	assert(ep->ref_count);
	if (0 == __sync_sub_and_fetch(&ep->ref_count, 1)) {
		zap_event_queue_ep_put(ep->event_queue);
		ep->z->destroy(ep);
	}
}

zap_err_t zap_read(zap_ep_t ep,
		   zap_map_t src_map, char *src,
		   zap_map_t dst_map, char *dst,
		   size_t sz,
		   void *context)
{
	zap_err_t zerr;
	if (dst_map->type != ZAP_MAP_LOCAL)
		return ZAP_ERR_INVALID_MAP_TYPE;
	if (src_map->type != ZAP_MAP_REMOTE)
		return ZAP_ERR_INVALID_MAP_TYPE;

	zerr = ep->z->read(ep, src_map, src, dst_map, dst, sz, context);
	return zerr;
}


size_t zap_map_len(zap_map_t map)
{
	return map->len;
}

char *zap_map_addr(zap_map_t map)
{
	return map->addr;
}

zap_map_t zap_map_get(zap_map_t map)
{
	__sync_fetch_and_add(&map->ref_count, 1);
	return map;
}

zap_err_t zap_map(zap_map_t *pm, void *addr, size_t len, zap_access_t acc)
{
	zap_map_t map = calloc(1, sizeof(*map));
	zap_err_t err = ZAP_ERR_OK;

	if (!map) {
		err = ZAP_ERR_RESOURCE;
		goto out;
	}
	*pm = map;
	map->ref_count = 1;
	map->type = ZAP_MAP_LOCAL;
	map->addr = addr;
	map->len = len;
	map->acc = acc;
 out:
	return err;
}

zap_err_t zap_unmap(zap_map_t map)
{
	zap_err_t zerr, tmp;
	int i;

	assert(map->ref_count);
	if (__sync_sub_and_fetch(&map->ref_count, 1))
		return ZAP_ERR_OK;
	for (i = ZAP_SOCK; i < ZAP_LAST; i++) {
		if (!map->mr[i])
			continue;
		assert(__zap_tbl[i].zap);
		tmp = __zap_tbl[i].zap->unmap(map);
		zerr = tmp?tmp:zerr; /* remember last error */
	}
	return zerr;
}

zap_err_t zap_share(zap_ep_t ep, zap_map_t m, const char *msg, size_t msg_len)
{
	zap_err_t zerr;
	zerr = ep->z->share(ep, m, msg, msg_len);
	return zerr;
}

zap_err_t zap_unshare(zap_ep_t ep, zap_map_t m, const char *msg, size_t msg_len)
{
	zap_err_t zerr = 0;
	if (ep->z->unshare)
		zerr = ep->z->unshare(ep, m, msg, msg_len);
	return zerr;
}

zap_err_t zap_reject(zap_ep_t ep, char *data, size_t data_len)
{
	return ep->z->reject(ep, data, data_len);
}

int z_map_access_validate(zap_map_t map, char *p, size_t sz, zap_access_t acc)
{
	if (p < map->addr || (map->addr + map->len) < (p + sz))
		return ERANGE;
	if ((map->acc & acc) != acc)
		return EACCES;
	return 0;
}

void *zap_event_thread_proc(void *arg)
{
	struct zap_event_queue *q = arg;
	struct zap_event_entry *ent, *pent;
loop:
	pthread_mutex_lock(&q->mutex);
	while (NULL == (pent = TAILQ_FIRST(&q->prio_q))
	       &&
	       NULL == (ent = TAILQ_FIRST(&q->queue))) {
		pthread_cond_wait(&q->cond_nonempty, &q->mutex);
	}
	if (pent) {
		TAILQ_REMOVE(&q->prio_q, pent, entry);
		ent = pent;
	} else {
		TAILQ_REMOVE(&q->queue, ent, entry);
	}
	q->depth++;
	pthread_cond_broadcast(&q->cond_vacant);
	pthread_mutex_unlock(&q->mutex);
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	ent->ep->z->event_interpose(ent->ep, ent->ctxt);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	free(ent);
	goto loop;
	return NULL;
}

int zap_event_add(struct zap_event_queue *q, zap_ep_t ep, void *ctxt)
{
	struct zap_event_entry *ent = malloc(sizeof(*ent));
	if (!ent)
		return errno;
	ent->ep = ep;
	ent->ctxt = ctxt;
	pthread_mutex_lock(&q->mutex);
	while (q->depth == 0) {
		pthread_cond_wait(&q->cond_vacant, &q->mutex);
	}
	q->depth--;
	if (ep->prio)
		TAILQ_INSERT_TAIL(&q->prio_q, ent, entry);
	else
		TAILQ_INSERT_TAIL(&q->queue, ent, entry);

	pthread_cond_broadcast(&q->cond_nonempty);
	pthread_mutex_unlock(&q->mutex);
	return 0;
}

void zap_event_queue_init(struct zap_event_queue *q, int qdepth)
{
	q->depth = qdepth;
	q->ep_count = 0;
	pthread_mutex_init(&q->mutex, NULL);
	pthread_cond_init(&q->cond_nonempty, NULL);
	pthread_cond_init(&q->cond_vacant, NULL);
	TAILQ_INIT(&q->queue);
	TAILQ_INIT(&q->prio_q);
}

struct zap_event_queue *zap_event_queue_new(int qdepth)
{
	struct zap_event_queue *q = calloc(1, sizeof(*q));
	if (!q)
		return NULL;
	zap_event_queue_init(q, qdepth);
	return q;
}

void zap_event_queue_free(struct zap_event_queue *q)
{
	free(q);
}

int zap_env_int(char *name, int default_value)
{
	char *x = getenv(name);
	int v;
	if (!x)
		return default_value;
	v = atoi(x);
	if (!v)
		return default_value;
	return v;

}

static
struct zap_event_queue *__get_least_busy_zap_event_queue()
{
	int i;
	struct zap_event_queue *q;
	q = &zev_queue[0];
	for (i = 1; i < zap_event_workers; i++) {
		if (zev_queue[i].ep_count < q->ep_count) {
			q = &zev_queue[i];
		}
	}
	zap_event_queue_ep_get(q);
	return q;
}

int zap_term(int timeout_sec)
{
	int i, tmp;
	int rc = 0;
	struct timespec ts;
	for (i = 0; i < zap_event_workers; i++) {
		pthread_cancel(zev_queue[i].thread);
	}
	if (timeout_sec > 0) {
		ts.tv_sec = time(NULL) + timeout_sec;
		ts.tv_nsec = 0;
	}
	for (i = 0; i < zap_event_workers; i++) {
		if (timeout_sec > 0) {
			tmp = pthread_timedjoin_np(zev_queue[i].thread, NULL, &ts);
			if (tmp)
				rc = tmp;
		} else {
			pthread_join(zev_queue[i].thread, NULL);
		}
	}
	return rc;
}

zap_err_t zap_ep_setopt(zap_ep_t ep, const char *name, const char *value)
{
	if (ep->z->ep_setopt)
		return ep->z->ep_setopt(ep, name, value);
	return ZAP_ERR_PARAMETER;
}

zap_ep_t zap_new_from_str(const char *str, zap_cb_fn_t cb,
			  zap_log_fn_t log_fn, zap_mem_info_fn_t mem_fn)
{
	zap_t zap = NULL;
	zap_ep_t ep = NULL;
	char *xprt = NULL, *args = NULL;
	int n;
	n = sscanf(str, "%m[^.].%ms", &xprt, &args);
	if (n < 0)
		goto out;
	if (n == 0) {
		errno = EINVAL;
		goto out;
	}
	zap = zap_get(xprt, log_fn, mem_fn);
	if (!zap)
		goto out;
	ep = zap->new_from_str(zap, cb, args);
	if (!ep)
		goto out;
	__common_ep_setup(ep, zap, cb);
 out:
	if (xprt)
		free(xprt);
	if (args)
		free(args);
	return ep;
}

static void __attribute__ ((constructor)) cs_init(void)
{
	int i;
	int rc;
	pthread_atfork(NULL, NULL, cs_init);
	pthread_mutex_init(&zap_list_lock, 0);

	zap_event_workers = ZAP_ENV_INT(ZAP_EVENT_WORKERS);
	zap_event_qdepth = ZAP_ENV_INT(ZAP_EVENT_QDEPTH);

	zev_queue = malloc(zap_event_workers * sizeof(*zev_queue));
	assert(zev_queue);

	for (i = 0; i < zap_event_workers; i++) {
		zap_event_queue_init(&zev_queue[i], zap_event_qdepth);
		rc = pthread_create(&zev_queue[i].thread, NULL,
					zap_event_thread_proc, &zev_queue[i]);
		if (rc) {
			default_log("%s: Error %d creating an event thread\n",
				    __func__, rc);
		} else {
			pthread_setname_np(zev_queue[i].thread, "zap:event");
		}
	}
}

static void __attribute__ ((destructor)) cs_term(void)
{
}
