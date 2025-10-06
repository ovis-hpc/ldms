/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2017,2019,2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2017,2019,2020 Open Grid Computing, Inc. All rights reserved.
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
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <pthread.h>
#include <dlfcn.h>
#include <assert.h>
#include <time.h>
#include <limits.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include "ovis_log/ovis_log.h"
#include "ovis_thrstats/ovis_thrstats.h"
#include "ovis-ldms-config.h"
#include "zap.h"
#include "zap_priv.h"

static ovis_log_t zlog;

#ifdef DEBUG
int __zap_assert = 1;
#else
int __zap_assert = 0;
#endif

/* the busy threshold by thread utilization (0.0 - 1.0) */
#define ZAP_IO_BUSY 0.8 /* default value */
static double zap_io_busy = ZAP_IO_BUSY;
static int zap_io_max;

LIST_HEAD(zap_list, zap) zap_list;

pthread_mutex_t zap_list_lock = PTHREAD_MUTEX_INITIALIZER;

#ifndef PLUGINDIR
#define PLUGINDIR "/usr/local/lib/ovis-lib"
#endif
#define ZAP_LIBPATH_DEFAULT PLUGINDIR
#define _SO_EXT ".so"
#define MAX_ZAP_LIBPATH	1024

static void zap_init(void);

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
	"ZAP_EVENT_SEND_MAPPED_COMPLETE",
	"ZAP_EVENT_LAST"
};

int zap_zerr2errno(enum zap_err_e e)
{
	switch (e)  {
	case ZAP_ERR_OK:
		return 0;
	case ZAP_ERR_PARAMETER:
		return EINVAL;
	case ZAP_ERR_TRANSPORT:
		return EIO;
	case ZAP_ERR_ENDPOINT:
		return ENOTCONN;
	case ZAP_ERR_ADDRESS:
		return EADDRNOTAVAIL;
	case ZAP_ERR_ROUTE:
		return ENETUNREACH;
	case ZAP_ERR_MAPPING:
		return EFAULT;
	case ZAP_ERR_RESOURCE:
		return ENOBUFS;
	case ZAP_ERR_BUSY:
		return EBUSY;
	case ZAP_ERR_NO_SPACE:
		return ENOSPC;
	case ZAP_ERR_INVALID_MAP_TYPE:
		return EPROTOTYPE;
	case ZAP_ERR_CONNECT:
		return ECONNREFUSED;
	case ZAP_ERR_NOT_CONNECTED:
		return ENOTCONN;
	case ZAP_ERR_HOST_UNREACHABLE:
		return EHOSTUNREACH;
	case ZAP_ERR_LOCAL_LEN:
		return E2BIG;
	case ZAP_ERR_LOCAL_OPERATION:
		return EOPNOTSUPP;
	case ZAP_ERR_LOCAL_PERMISSION:
		return EPERM;
	case ZAP_ERR_REMOTE_MAP:
		return EFAULT;
	case ZAP_ERR_REMOTE_LEN:
		return EFAULT;
	case ZAP_ERR_REMOTE_PERMISSION:
		return EPERM;
	case ZAP_ERR_REMOTE_OPERATION:
		return EOPNOTSUPP;
	case ZAP_ERR_RETRY_EXCEEDED:
		return ETIMEDOUT;
	case ZAP_ERR_TIMEOUT:
		return ETIMEDOUT;
	case ZAP_ERR_FLUSH:
		return EPIPE;
	default:
		assert(NULL == "Invalid Zapp error value");
	}
}

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
	if ((int)e < 1 || e >= ZAP_EVENT_LAST)
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

struct zap_tbl_entry {
	enum zap_type type;
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

zap_t zap_get(const char *name, zap_mem_info_fn_t mem_info_fn)
{
	char _libdir[MAX_ZAP_LIBPATH];
	char _libpath[MAX_ZAP_LIBPATH];
	char *libdir;
	char *libpath;
	char *lib = _libpath;
	zap_t z = NULL;
	char *errstr;
	int ret, len, i;
	void *d = NULL;
	char *saveptr = NULL;
	struct zap_tbl_entry *zent;

	pthread_mutex_lock(&zap_list_lock);
	for (zent = __zap_tbl; zent->name; zent++) {
		if (0 != strcmp(zent->name, name))
			continue;
		/* found the entry */
		if (zent->zap) { /* already loaded */
			pthread_mutex_unlock(&zap_list_lock);
			return zent->zap;
		}
		break;
	}
	pthread_mutex_unlock(&zap_list_lock);
	if (!zent->name) {
		/* unknown zap name */
		errno = ENOENT;
		return NULL;
	}

	/* otherwise, it is a known zap name but has not been loaded yet */
	if (!mem_info_fn)
		mem_info_fn = default_zap_mem_info;

	if (strlen(name) >= ZAP_MAX_TRANSPORT_NAME_LEN) {
		errno = ENAMETOOLONG;
		goto err;
	}

	zap_init();

	libdir = getenv("ZAP_LIBPATH");
	if (!libdir || libdir[0] == '\0')
		libdir = ZAP_LIBPATH_DEFAULT;
	len = strlen(libdir);
	if (len >= MAX_ZAP_LIBPATH) {
		errno = ENAMETOOLONG;
		goto err;
	}
	memcpy(_libdir, libdir, len + 1);
	libdir = _libdir;

	while ((libpath = strtok_r(libdir, ":", &saveptr)) != NULL) {
		libdir = NULL;
		snprintf(lib, sizeof(_libpath) - 1, "%s/libzap_%s%s",
			 libpath, name, _SO_EXT);

		d = dlopen(lib, RTLD_NOW);
		if (d != NULL) {
			break;
		}

		struct stat buf;
		if (stat(lib, &buf) == 0) {
			char *dlerr = dlerror();
			ovis_log(zlog, OVIS_LERROR, "Bad zap plugin " \
				 "'%s': dlerror %s\n", name, dlerr);
			goto err;
		}
	}

	if (!d) {
		/* The library doesn't exist */
		ovis_log(zlog, OVIS_LERROR, "dlopen: %s\n", dlerror());
		goto err;
	}

	dlerror();
	zap_get_fn_t get = dlsym(d, "zap_transport_get");
	errstr = dlerror();
	if (errstr || !get) {
		ovis_log(zlog, OVIS_LERROR, "dlsym: %s\n", errstr);
		/* The library exists but doesn't export the correct
		 * symbol and is therefore likely the wrong library type */
		goto err1;
	}
	ret = get(&z, mem_info_fn);
	if (ret)
		goto err1;
	memcpy(z->name, name, strlen(name)+1);
	int default_pools = sysconf(_SC_NPROCESSORS_CONF);
	if (default_pools < 0)
		default_pools = 8;
	z->_n_pools = zap_env_int("ZAP_POOLS", default_pools);
	for (i = 0; i < z->_n_pools; i++) {
		z->_thr_pool[i].n = 0;
		z->_thr_pool[i].idx = i;
		LIST_INIT(&z->_thr_pool[i]._io_threads);
	}
	z->mem_info_fn = mem_info_fn;
	pthread_mutex_init(&z->_io_mutex, NULL);

	pthread_mutex_lock(&zap_list_lock);
	zent->zap = z;
	LIST_INSERT_HEAD(&zap_list, z, zap_link);
	pthread_mutex_unlock(&zap_list_lock);

	return z;
err1:
	dlerror();
	ret = dlclose(d);
	if (ret) {
		errstr = dlerror();
		ovis_log(zlog, OVIS_LERROR, "dlclose: %s\n", errstr);
	}
err:
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
	case ZAP_EVENT_SEND_MAPPED_COMPLETE:
		/* Do nothing */
		break;
	default:
		assert(0);
	}
}

#ifdef _ZAP_EP_TRACK_
TAILQ_HEAD(zap_ep_list, zap_ep) zap_ep_list;
pthread_mutex_t zap_ep_list_lock;
#endif /* _ZAP_EP_TRACK_ */

static void __destroy_ep(void *zep)
{
	zap_ep_t ep = (zap_ep_t)zep;
#ifdef _ZAP_EP_TRACK_
	pthread_mutex_lock(&zap_ep_list_lock);
	TAILQ_REMOVE(&zap_ep_list, ep, ep_link);
	pthread_mutex_unlock(&zap_ep_list_lock);
#endif /* _ZAP_EP_TRACK_ */
	ep->z->destroy(ep);
}

zap_ep_t zap_new(zap_t z, zap_cb_fn_t cb)
{
	zap_ep_t zep = NULL;
	zap_init();
	if (!cb)
		cb = blocking_zap_cb;
	zep = z->new(z, cb);
	if (!zep)
		return NULL;
	zep->z = z;
	zep->cb = cb;
	zep->ref_count = 1;
	ref_init(&zep->ref, __func__, __destroy_ep, zep);
	zep->state = ZAP_EP_INIT;
	pthread_mutex_init(&zep->lock, NULL);
	sem_init(&zep->block_sem, 0, 0);
#ifdef _ZAP_EP_TRACK_
	pthread_mutex_lock(&zap_ep_list_lock);
	TAILQ_INSERT_TAIL(&zap_ep_list, zep, ep_link);
	pthread_mutex_unlock(&zap_ep_list_lock);
#endif /* _ZAP_EP_TRACK_ */
	return zep;
}

void zap_set_priority(zap_ep_t ep, int prio)
{
	ep->prio = prio;
}

zap_err_t zap_accept(zap_ep_t ep, zap_cb_fn_t cb, char *data, size_t data_len)
{
	return zap_accept2(ep, cb, data, data_len, -1);
}

zap_err_t zap_accept2(zap_ep_t ep, zap_cb_fn_t cb, char *data,
		size_t data_len, int tpi)
{
	zap_err_t zerr;
	ep->cb = cb;
	zerr = ep->z->accept(ep, cb, data, data_len, tpi);
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
	return zap_connect2(ep, sa, sa_len, data, data_len, -1);
}

zap_err_t zap_connect2(zap_ep_t ep, struct sockaddr *sa, socklen_t sa_len,
		      char *data, size_t data_len, int tpi)
{
	return ep->z->connect(ep, sa, sa_len, data, data_len, tpi);
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
	ref_get(&ep->ref, "zap_send");
	zerr = ep->z->send(ep, buf, sz);
	ref_put(&ep->ref, "zap_send");
	return zerr;
}

zap_err_t zap_send2(zap_ep_t ep, void *buf, size_t sz, void *cb_arg)
{
	zap_err_t zerr;
//	zap_get_ep(ep);
	ref_get(&ep->ref, "zap_send");
	zerr = ep->z->send2(ep, buf, sz, cb_arg);
//	zap_put_ep(ep);
	ref_put(&ep->ref, "zap_send");
	return zerr;
}

zap_err_t zap_send_mapped(zap_ep_t ep, zap_map_t map, void *buf, size_t len,
			  void *context)
{
	if (!ep->z->send_mapped)
		return ZAP_ERR_NOT_SUPPORTED;
	return ep->z->send_mapped(ep, map, buf, len, context);
}

zap_err_t zap_write(zap_ep_t ep,
		    zap_map_t src_map, void *src,
		    zap_map_t dst_map, void *dst,
		    size_t sz,
		    void *context)
{
	zap_err_t zerr;
	ref_get(&ep->ref, "zap_write");
	zerr = ep->z->write(ep, src_map, src, dst_map, dst, sz, context);
	ref_put(&ep->ref, "zap_write");
	return zerr;
}

zap_err_t zap_get_name(zap_ep_t ep, struct sockaddr *local_sa,
		       struct sockaddr *remote_sa, socklen_t *sa_len)
{
	return ep->z->get_name(ep, local_sa, remote_sa, sa_len);
}

void zap_get_ep(zap_ep_t ep, const char *name, const char *fn_name, int line_no)
{
	_ref_get(&ep->ref, name, fn_name, line_no);
}

void zap_free(zap_ep_t ep)
{
	/* Drop the zap_new() reference */
	assert(ep->ref_count);
	ref_put(&ep->ref, "zap_new");
}

int zap_ep_closed(zap_ep_t ep)
{
	assert(ep->ref_count);
	return (ep->state == ZAP_EP_CLOSE);
}

int zap_ep_connected(zap_ep_t ep)
{
	assert(ep && ep->ref_count);
	return (ep != NULL && ep->state == ZAP_EP_CONNECTED);
}

zap_ep_state_t zap_ep_state(zap_ep_t ep)
{
	return ep->state;
}

void zap_put_ep(zap_ep_t ep, const char *name, const char *fn_name, int line_no)
{
	if (!ep)
		return;
	_ref_put(&ep->ref, name, fn_name, line_no);
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

	zerr = ZAP_ERR_OK;
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
	free(map);
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
	return ep->z->reject(ep, data, data_len);
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

double zap_env_dbl(char *name, double default_value)
{
	char *x = getenv(name);
	if (!x)
		return default_value;
	return strtod(x, NULL);

}

static zap_err_t __io_thread_cancel(zap_io_thread_t t);
int zap_term(zap_t z, int timeout_sec)
{
	int tmp, i, n, j;
	int rc = 0;
	struct timespec ts;
	zap_io_thread_t t;
	pthread_t *thr;

	pthread_mutex_lock(&z->_io_mutex);
	n = 0;
	for (i = 0; i < z->_n_pools; i++) {
		n += z->_thr_pool[i].n;
	}
	thr = malloc(sizeof(*thr) * n);
	if (!thr) {
		rc = errno;
		goto out;
	}
	i = 0;
	for (j = 0; j < z->_n_pools; j++) {
		while ((t = LIST_FIRST(&z->_thr_pool[j]._io_threads))) {
			thr[i++] = t->thread;
			__io_thread_cancel(t); /* t is removed and invalidated */
		}
	}
	/* join */
	for (i = 0; i < n; i++) {
		if (timeout_sec > 0) {
			tmp = pthread_timedjoin_np(thr[i], NULL, &ts);
			if (tmp)
				rc = tmp;
		} else {
			pthread_join(thr[i], NULL);
		}
	}
	free(thr);
 out:
	pthread_mutex_unlock(&z->_io_mutex);
	return rc;
}

int zap_io_thread_init(zap_io_thread_t t, zap_t z, const char *name)
{
	pthread_mutexattr_t mattr;
	t->stat = zap_thrstat_new(name);
	if (!t->stat)
		return errno;
	t->zap = z;
	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&t->mutex, &mattr);
	LIST_INIT(&t->_ep_list);
	t->_n_ep = 0;
	return 0;
}

int zap_io_thread_release(zap_io_thread_t t)
{
	if (t->stat)
		zap_thrstat_free(t->stat);
	pthread_mutex_destroy(&t->mutex);
	return 0;
}

void zap_io_thread_thread_id(zap_io_thread_t t)
{
	ovis_thrstats_thread_id_set(&t->stat->stats);
}

zap_err_t zap_event_deliver(zap_event_t ev)
{
	ev->ep->cb(ev->ep, ev);
	return ZAP_ERR_OK;
}

static inline uint64_t timespec2usec(struct timespec *ts)
{
	return ts->tv_sec*1000000 + ts->tv_nsec/1000;
}

static zap_io_thread_t __io_thread_create(zap_t z, struct zap_io_thread_pool_s *tp)
{
	/* z->_io_mutex is held by the caller */

	zap_io_thread_t t;
	if (tp->n >= zap_io_max) {
		errno = EBUSY;
		return NULL;
	}
	t = z->io_thread_create(z);
	if (!t)
		return NULL;
	pthread_mutex_lock(&t->mutex);
	t->stat->stats.thread_id = t->thread;
	t->stat->pool_idx = tp->idx;
	t->tp = tp;
	pthread_mutex_unlock(&t->mutex);
	tp->n++;
	LIST_INSERT_HEAD(&tp->_io_threads, t, _entry);
	return t;
}

static zap_err_t __io_thread_cancel(zap_io_thread_t t)
{
	/* z->_io_mutex is held by the caller */
	struct zap_io_thread_pool_s *tp = t->tp;
	LIST_REMOVE(t, _entry);
	tp->n--;
	return t->zap->io_thread_cancel(t);
}

static double zap_utilization(zap_thrstat_t t, struct timespec now);

static zap_io_thread_t __zap_least_busy_thread(zap_t z, zap_ep_t ep, struct zap_io_thread_pool_s *p)
{
	zap_io_thread_t t = NULL, _t;
	struct timespec now;
	int min_ep;

	clock_gettime(CLOCK_REALTIME, &now);
	pthread_mutex_lock(&z->_io_mutex);

	/* Reap idle threads */
	LIST_FOREACH(_t, &p->_io_threads, _entry)
	{
		if (0 == _t->_n_ep) {
			t = _t;
			goto out;
		}
	}

	/* Create a new thread to the limit zap_io_max */
	t = __io_thread_create(z, p);
	if (t)
		goto out;

	min_ep = 0x7FFFFFFF;
	LIST_FOREACH(_t, &p->_io_threads, _entry)
	{
		if (_t->_n_ep < min_ep) {
			t = _t;
			min_ep = _t->_n_ep;
		}
	}
 out:
	if (t) {
		/* Add ep to the thread before releasing io mutex */
		pthread_mutex_lock(&t->mutex);
		LIST_INSERT_HEAD(&t->_ep_list, ep, _entry);
		t->_n_ep++;
		t->stat->n_eps = t->_n_ep;
		pthread_mutex_unlock(&t->mutex);
		ep->thread = t;
	}
	pthread_mutex_unlock(&z->_io_mutex);
	return t;
}

static zap_io_thread_t __zap_passive_ep_thread(zap_t z, zap_ep_t ep)
{
	zap_io_thread_t t;
	char name[16];

	pthread_mutex_lock(&z->_io_mutex);
	t = z->_passive_ep_thread;
	if (t) {
		pthread_mutex_lock(&t->mutex);
		goto add;
	}
	t = z->io_thread_create(z);
	if (!t)
		goto out;
	pthread_mutex_lock(&t->mutex);
	t->stat->stats.thread_id = t->thread;
	t->stat->pool_idx = -1;
	z->_passive_ep_thread = t;
	/* append "_p" to the thread name for "passive" endpoint thread.
	 * The suffix is shorten due to pthread 16-char name limit. */
	snprintf(name, sizeof(name), "%s_p", t->stat->stats.name);
	pthread_setname_np(t->thread, name);
 add:
	/* also add ep to the thread before releasing io mutex */
	LIST_INSERT_HEAD(&t->_ep_list, ep, _entry);
	t->_n_ep++;
	t->stat->n_eps = t->_n_ep;
	ep->thread = t;
	pthread_mutex_unlock(&t->mutex);
 out:
	pthread_mutex_unlock(&z->_io_mutex);
	return t;
}

zap_err_t zap_io_thread_ep_assign(zap_ep_t ep, int tpi)
{
	zap_err_t zerr = ZAP_ERR_OK;
	zap_t z = ep->z;
	zap_io_thread_t t;
	struct zap_io_thread_pool_s *tp;

	if (ep->state == ZAP_EP_LISTENING) {
		/* pasive endpoint */
		t = __zap_passive_ep_thread(z, ep);
	} else {
		if (tpi < 0){
			/* TODO round robin? */
			tpi = 0;
		}
		tpi %= z->_n_pools;
		tp = &z->_thr_pool[tpi];
		t = __zap_least_busy_thread(z, ep, tp);
	}

	if (!t) {
		zerr = errno; /* expect zap_err_t in errno */
		goto out;
	}
	zerr = z->io_thread_ep_assign(t, ep);
	if (zerr) {
		ep->thread = NULL;
		pthread_mutex_lock(&t->mutex);
		LIST_REMOVE(ep, _entry);
		t->_n_ep--;
		t->stat->n_eps = t->_n_ep;
		pthread_mutex_unlock(&t->mutex);
		goto out;
	}
 out:
	return zerr;
}

zap_err_t zap_io_thread_ep_release(zap_ep_t ep)
{
	zap_err_t zerr;
	zerr = ep->z->io_thread_ep_release(ep->thread, ep);
	__atomic_fetch_sub(&ep->thread->stat->sq_sz, ep->sq_sz, __ATOMIC_SEQ_CST);
	return zerr;
}

zap_err_t zap_io_thread_ep_remove(zap_ep_t ep)
{
	zap_err_t zerr = 0;
	zap_io_thread_t t = ep->thread;

	pthread_mutex_lock(&t->mutex);
	LIST_REMOVE(ep, _entry);
	t->_n_ep--;
	t->stat->n_eps = t->_n_ep;
	pthread_mutex_unlock(&t->mutex);
	ep->thread = NULL;
	return zerr;
}

static pthread_mutex_t thrstat_list_lock = PTHREAD_MUTEX_INITIALIZER;
static LIST_HEAD(thrstat_list, zap_thrstat) thrstat_list;
int thrstat_count = 0;

void zap_thrstat_dump()
{
	zap_thrstat_t t;
	struct timespec now;
	double u;
	clock_gettime(CLOCK_REALTIME, &now);
	pthread_mutex_lock(&thrstat_list_lock);
	LIST_FOREACH(t, &thrstat_list, entry) {
		u = zap_utilization(t, now);
		printf("thrstat '%s', u: %lf\n", t->stats.name, u);
	}
	pthread_mutex_unlock(&thrstat_list_lock);
}

void zap_thrstat_reset(zap_thrstat_t stats, struct timespec *now)
{
	struct ovis_thrstats *t = &stats->stats;
	ovis_thrstats_reset(t, now);
	if (t->app_reset_fn)
		t->app_reset_fn(t->app_ctxt, now);
}

void zap_thrstat_reset_all(struct timespec *now)
{
	zap_thrstat_t t;
	pthread_mutex_lock(&thrstat_list_lock);
	LIST_FOREACH(t, &thrstat_list, entry) {
		zap_thrstat_reset(t, now);
	}
	pthread_mutex_unlock(&thrstat_list_lock);
}

zap_thrstat_t zap_thrstat_new(const char *name)
{
	int rc;
	zap_thrstat_t stats = calloc(1, sizeof(*stats));
	if (!stats)
		return NULL;
	rc = ovis_thrstats_init(&stats->stats, name);
	if (rc)
		goto err_1;

	pthread_mutex_lock(&thrstat_list_lock);
	LIST_INSERT_HEAD(&thrstat_list, stats, entry);
	thrstat_count++;
	pthread_mutex_unlock(&thrstat_list_lock);
	return stats;

err_1:
	free(stats);
	return NULL;
}

void zap_thrstat_free(zap_thrstat_t stats)
{
	if (!stats)
		return;
	pthread_mutex_lock(&thrstat_list_lock);
	LIST_REMOVE(stats, entry);
	thrstat_count--;
	pthread_mutex_unlock(&thrstat_list_lock);
	ovis_thrstats_cleanup(&stats->stats);
	free(stats);
}

void inline zap_thrstat_wait_start(void *arg)
{
	struct zap_io_thread *thr = arg;
	pthread_mutex_lock(&thr->mutex);
	ovis_thrstats_wait_start(&thr->stat->stats);
	pthread_mutex_unlock(&thr->mutex);
}

void inline zap_thrstat_wait_end(void *arg)
{
	struct zap_io_thread *thr = arg;
	pthread_mutex_lock(&thr->mutex);
	ovis_thrstats_wait_end(&thr->stat->stats);
	pthread_mutex_unlock(&thr->mutex);
}

static double zap_utilization(zap_thrstat_t t, struct timespec now)
{
	struct ovis_thrstats_result res;
	ovis_thrstats_result_get(&t->stats, 0, &res);
	return (double)res.active_us/(double)res.interval_us;
}

struct zap_thrstat_result *zap_thrstat_get_result(uint64_t interval_s)
{
	struct zap_thrstat_result *res = NULL;
	zap_thrstat_t t;
	int i;
	struct timespec now;

	pthread_mutex_lock(&thrstat_list_lock);
	res = malloc(sizeof(*res) +
			(thrstat_count * sizeof(struct zap_thrstat_result_entry)));
	if (!res)
		goto out;
	res->count = thrstat_count;
	i = 0;
	clock_gettime(CLOCK_REALTIME, &now);
	LIST_FOREACH(t, &thrstat_list, entry) {
		ovis_thrstats_result_get(&t->stats, interval_s, &res->entries[i].res);
		res->entries[i].n_eps = t->n_eps;
		res->entries[i].sq_sz = t->sq_sz;
		res->entries[i].pool_idx = t->pool_idx;
		i += 1;
	}
out:
	pthread_mutex_unlock(&thrstat_list_lock);
	return res;
}

void zap_thrstat_free_result(struct zap_thrstat_result *res)
{
	if (!res)
		return;
	int i;
	for (i = 0; i < res->count; i++) {
		ovis_thrstats_result_cleanup(&res->entries[i].res);
	}
	free(res);
}

struct timespec *zap_ep_thrstat_wait_end(zap_ep_t zep)
{
	if (!zep->thread)
		return NULL;
	return &zep->thread->stat->stats.wait_end;
}

int zap_thrstat_ctxt_set(zap_ep_t zep, void *ctxt, ovis_thrstats_app_reset_fn reset_fn)
{
	zap_thrstat_t thrstat = (zep->thread)?(zep->thread->stat):NULL;
	if (!thrstat)
		return EBUSY;
	return ovis_thrstats_app_ctxt_set(&thrstat->stats, ctxt, reset_fn);
}

void *zap_thrstat_ctxt_get(zap_ep_t zep)
{
	zap_thrstat_t thrstat = (zep->thread)?(zep->thread->stat):NULL;
	if (!thrstat) {
		errno = EBUSY;
		return NULL;
	}
	return ovis_thrstats_app_ctxt_get(&thrstat->stats);
}

pthread_t zap_ep_thread(zap_ep_t ep)
{
	return ep->thread?ep->thread->thread:0;
}

pid_t zap_ep_thread_id_get(zap_ep_t ep)
{
	return ep->thread?ep->thread->stat->stats.tid:-1;
}

void zap_ep_thread_id_set(zap_ep_t ep, pid_t tid)
{
	if (!ep->thread)
		return;

	ep->thread->stat->stats.tid = tid;
}

uint64_t zap_ep_sq_sz(zap_ep_t ep)
{
	return ep->sq_sz;
}

static int zap_initialized = 0;

static void zap_atfork()
{
	__atomic_store_n(&zap_initialized, 0, __ATOMIC_SEQ_CST);
	zap_t z;
	int i;
	zap_io_thread_t t;
	/* notify zap plugins to cleanup the lingering thread resources */
	pthread_mutex_lock(&zap_list_lock);
	LIST_FOREACH(z, &zap_list, zap_link) {
		pthread_mutex_lock(&z->_io_mutex);
		for (i = 0; i < z->_n_pools; i++) {
			while ((t = LIST_FIRST(&z->_thr_pool[i]._io_threads))) {
				__io_thread_cancel(t);
			}
		}
		pthread_mutex_unlock(&z->_io_mutex);
	}
	pthread_mutex_unlock(&zap_list_lock);
}

static void zap_init(void)
{
	int nprocs = get_nprocs() / 2;
	zap_io_max = zap_env_int("ZAP_IO_MAX", nprocs ? nprocs : 1);
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	if (__atomic_load_n(&zap_initialized, __ATOMIC_SEQ_CST))
		return;

	pthread_mutex_lock(&mutex);
	/* re-check, we can lose the race */
	if (__atomic_load_n(&zap_initialized, __ATOMIC_SEQ_CST)) {
		pthread_mutex_unlock(&mutex);
		return;
	}

#ifdef _ZAP_EP_TRACK_
	TAILQ_INIT(&zap_ep_list);
	pthread_mutex_init(&zap_ep_list_lock, 0);
#endif /* _ZAP_EP_TRACK_ */
	zlog = ovis_log_register("xprt.zap", "Messages for Zap");
	if (!zlog)
		ovis_log(zlog, OVIS_LWARN, "Failed to create Zap's log subsystem.\n");
	zap_io_busy = ZAP_ENV_DBL(ZAP_IO_BUSY);
	if (zap_io_busy < 0.0 || zap_io_busy > 1.0) {
		/* bad value, set to default */
		ovis_log(NULL, OVIS_LERROR, "bad ZAP_IO_BUSY value: %lf, "
				"the value must be in (0.0-1.0) range\n",
				zap_io_busy);
		zap_io_busy = ZAP_IO_BUSY;
	}
	__atomic_store_n(&zap_initialized, 1, __ATOMIC_SEQ_CST);
	pthread_mutex_unlock(&mutex);
}

#ifdef NDEBUG
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
static void __attribute__ ((constructor)) cs_init(void)
{
	pthread_atfork(NULL, NULL, zap_atfork);
	zap_init();
}

static void __attribute__ ((destructor)) cs_term(void)
{
}
