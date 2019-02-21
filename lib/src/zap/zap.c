 /* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2016 Sandia Corporation. All rights reserved.
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
	assert(q || NULL == "zap_event_queue_ep_get called with null q.");
	pthread_mutex_lock(&q->mutex);
	q->ep_count++;
	pthread_mutex_unlock(&q->mutex);
}

static inline
void zap_event_queue_ep_put(struct zap_event_queue *q)
{
	assert(q || NULL == "zap_event_queue_ep_put called with null q.");
	pthread_mutex_lock(&q->mutex);
	q->ep_count--;
	pthread_mutex_unlock(&q->mutex);
}

void *zap_get_ucontext(zap_ep_t ep)
{
	assert(ep || NULL == "zap_get_ucontext called with null ep");
	return ep->ucontext;
}

void zap_set_ucontext(zap_ep_t ep, void *context)
{
	assert(ep || NULL == "zap_get_ucontext called with null ep");
	ep->ucontext = context;
}

zap_mem_info_t default_zap_mem_info(void)
{
	return NULL;
}

static
void zap_interpose_event(zap_ep_t ep, void *ctxt);

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
	void *d;
	char *saveptr = NULL;

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

	pthread_mutex_lock(&zap_list_lock);
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
	return NULL;
}

size_t zap_max_msg(zap_t z)
{
	assert(z || NULL == "zap_max_msg called with null z");
	return z->max_msg;
}

void blocking_zap_cb(zap_ep_t zep, zap_event_t ev)
{
	assert((zep && ev) || NULL == "blocking_zap_cb called with null argument");
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
		assert(0 && NULL == "unhandled ev type in blocking_zap_cb");
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
	assert((ep && ev) || NULL == "zap_interpose_cb called with null argument");

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
		assert(0 && NULL == "unhandled ev type in zap_interpose_cb");
		break;
	}

	ictxt = calloc(1, sizeof(*ictxt) + data_len);
	if (!ictxt) {
		DLOG(ep, "zap_interpose_cb(): ENOMEM\n");
		return;
	}
	DLOG(ep, "%s: ep %p: ictxt %p: ev type %s\n", __func__, ep,
				ictxt, zap_event_str(ev->type));
	ictxt->ev = *ev;
	ictxt->ev.data = ictxt->data;
	if (data_len)
		memcpy(ictxt->data, ev->data, data_len);
	zap_get_ep(ep);
	zap_event_add(ep->event_queue, ep, ictxt);
}

static
void zap_interpose_event(zap_ep_t ep, void *ctxt)
{
	assert(ep || NULL == "zap_interpose_event called with null ep");
	/* delivering real io event callback */
	struct zap_interpose_ctxt *ictxt = ctxt;
	DLOG(ep, "%s: ep %p: ictxt %p\n", __func__, ep, ictxt);
	ep->app_cb(ep, &ictxt->ev);
	free(ictxt);
	zap_put_ep(ep);
}

static
struct zap_event_queue *__get_least_busy_zap_event_queue();

zap_ep_t zap_new(zap_t z, zap_cb_fn_t cb)
{
	assert(z || NULL == "zap_new called with null z");
	zap_ep_t zep = NULL;
	if (!cb)
		cb = blocking_zap_cb;
	zep = z->new(z, cb);
	if (!zep)
		return NULL;
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

zap_err_t zap_accept(zap_ep_t ep, zap_cb_fn_t cb, char *data, size_t data_len)
{
	zap_err_t zerr;
	assert(ep || NULL == "zap_accept called with null ep");
	ep->app_cb = cb;
	ep->cb = zap_interpose_cb;
	zerr = ep->z->accept(ep, zap_interpose_cb, data, data_len);
	return zerr;
}

zap_err_t zap_connect_by_name(zap_ep_t ep, const char *host, const char *port,
			      char *data, size_t data_len)
{
	struct addrinfo *ai;
	assert(ep || NULL == "zap_connect_by_name called with null ep");
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
	assert(ep || NULL == "zap_connect_sync called with null ep");
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
	assert(ep || NULL == "zap_connect called with null ep");
	return ep->z->connect(ep, sa, sa_len, data, data_len);
}

zap_err_t zap_listen(zap_ep_t ep, struct sockaddr *sa, socklen_t sa_len)
{
	assert(ep || NULL == "zap_listen called with null ep");
	return ep->z->listen(ep, sa, sa_len);
}

zap_err_t zap_close(zap_ep_t ep)
{
	assert(ep || NULL == "zap_close called with null ep");
	zap_err_t zerr = ep->z->close(ep);
	return zerr;
}

zap_err_t zap_send(zap_ep_t ep, void *buf, size_t sz)
{
	zap_err_t zerr;
	assert(ep || NULL == "zap_send called with null ep");
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
	assert(ep || NULL == "zap_write called with null ep");
	zap_get_ep(ep);
	zerr = ep->z->write(ep, src_map, src, dst_map, dst, sz, context);
	zap_put_ep(ep);
	return zerr;
}

zap_err_t zap_get_name(zap_ep_t ep, struct sockaddr *local_sa,
		       struct sockaddr *remote_sa, socklen_t *sa_len)
{
	assert(ep || NULL == "zap_get_name called with null ep");
	return ep->z->get_name(ep, local_sa, remote_sa, sa_len);
}

void zap_get_ep(zap_ep_t ep)
{
	assert(ep || NULL == "zap_get_ep called with null ep");
	(void)__sync_fetch_and_add(&ep->ref_count, 1);
}

void zap_free(zap_ep_t ep)
{
	assert(ep || NULL == "zap_free called with null ep");
	/* Drop the zap_new() reference */
	assert(ep->ref_count || NULL == "zap_free: bad ref_count");
	zap_put_ep(ep);
}

void zap_put_ep(zap_ep_t ep)
{
	assert(ep || NULL == "zap_free called with null ep");
	assert(ep->ref_count || NULL == "zap_put_ep: bad ref_count");
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
	assert(ep && src_map && dst_map && NULL != "zap_read called with null");
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
	assert(map || NULL == "zap_map_len called with null map");
	return map->len;
}

char *zap_map_addr(zap_map_t map)
{
	assert(map || NULL == "zap_map_addr called with null map");
	return map->addr;
}

zap_err_t zap_map(zap_ep_t ep, zap_map_t *pm,
		  void *addr, size_t len, zap_access_t acc)
{
	zap_map_t map;
	assert((ep && pm) || NULL == "zap_map called with null");
	zap_err_t err = ep->z->map(ep, pm, addr, len, acc);
	if (err)
		goto out;

	map = *pm;
	map->type = ZAP_MAP_LOCAL;
	zap_get_ep(ep);
	map->ep = ep;
	map->addr = addr;
	map->len = len;
	map->acc = acc;
	pthread_mutex_lock(&ep->lock);
	LIST_INSERT_HEAD(&ep->map_list, map, link);
	pthread_mutex_unlock(&ep->lock);
 out:
	return err;
}

zap_err_t zap_unmap(zap_ep_t ep, zap_map_t map)
{
	zap_err_t zerr;
	assert((ep && map) || NULL == "zap_unmap called with null");

	pthread_mutex_lock(&ep->lock);
	LIST_REMOVE(map, link);
	pthread_mutex_unlock(&ep->lock);

	zerr = ep->z->unmap(ep, map);
	zap_put_ep(ep);
	return zerr;
}

zap_err_t zap_share(zap_ep_t ep, zap_map_t m, const char *msg, size_t msg_len)
{
	zap_err_t zerr;
	zerr = ep->z->share(ep, m, msg, msg_len);
	return zerr;
}

zap_err_t zap_reject(zap_ep_t ep, char *data, size_t data_len)
{
	assert(ep || NULL == "zap_reject called with null ep");
	return ep->z->reject(ep, data, data_len);
}

int z_map_access_validate(zap_map_t map, char *p, size_t sz, zap_access_t acc)
{
	assert(map || NULL == "z_map_access_validate called with null map");
	if (p < map->addr || (map->addr + map->len) < (p + sz))
		return ERANGE;
	if ((map->acc & acc) != acc)
		return EACCES;
	return 0;
}

void *zap_event_thread_proc(void *arg)
{
	struct zap_event_queue *q = arg;
	struct zap_event_entry *ent;
	assert(arg || NULL == "zap_event_thread_proc called iwth null");
loop:
	pthread_mutex_lock(&q->mutex);
	while (NULL == (ent = TAILQ_FIRST(&q->queue))) {
		pthread_cond_wait(&q->cond_nonempty, &q->mutex);
	}
	TAILQ_REMOVE(&q->queue, ent, entry);
	q->depth++;
	pthread_cond_broadcast(&q->cond_vacant);
	pthread_mutex_unlock(&q->mutex);
	ent->ep->z->event_interpose(ent->ep, ent->ctxt);
	free(ent);
	goto loop;
	return NULL;
}

int zap_event_add(struct zap_event_queue *q, zap_ep_t ep, void *ctxt)
{
	struct zap_event_entry *ent = malloc(sizeof(*ent));
	if (!ent)
		return ENOMEM; /*  errno is not set by malloc */
	ent->ep = ep;
	ent->ctxt = ctxt;
	pthread_mutex_lock(&q->mutex);
	while (q->depth == 0) {
		pthread_cond_wait(&q->cond_vacant, &q->mutex);
	}
	q->depth--;
	TAILQ_INSERT_TAIL(&q->queue, ent, entry);
	pthread_cond_broadcast(&q->cond_nonempty);
	pthread_mutex_unlock(&q->mutex);
	return 0;
}

void zap_event_queue_init(struct zap_event_queue *q, int qdepth)
{
	assert(q || NULL == "zap_event_queue_init called with null q");
	q->depth = qdepth;
	q->ep_count = 0;
	pthread_mutex_init(&q->mutex, NULL);
	pthread_cond_init(&q->cond_nonempty, NULL);
	pthread_cond_init(&q->cond_vacant, NULL);
	TAILQ_INIT(&q->queue);
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
	assert(zev_queue || NULL == "__get_least_busy_zap_event_queue: zev_queue is null");
	q = &zev_queue[0];
	for (i = 1; i < zap_event_workers; i++) {
		if (zev_queue[i].ep_count < q->ep_count) {
			q = &zev_queue[i];
		}
	}
	zap_event_queue_ep_get(q);
	return q;
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
	assert(zev_queue || NULL == "zap: cs_init: malloc failed.");

	for (i = 0; i < zap_event_workers; i++) {
		zap_event_queue_init(&zev_queue[i], zap_event_qdepth);
		rc = pthread_create(&zev_queue[i].thread, NULL,
					zap_event_thread_proc, &zev_queue[i]);
		assert(rc == 0 || NULL == "zap: cs_init: pthread_create failed");
	}
}

static void __attribute__ ((destructor)) cs_term(void)
{
}
