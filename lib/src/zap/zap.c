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
#include "zap.h"
#include "zap_priv.h"

#ifdef DEBUG
#define DLOG(ep, fmt, ...) do { \
	if (ep && ep->z && ep->z->log_fn) \
		ep->z->log_fn(fmt, ##__VA_ARGS__); \
} while(0)
#else
#define DLOG(ep, fmt, ...)
#endif

#define ZLOG(ep, fmt, ...) do { \
	if (ep && ep->z && ep->z->log_fn) \
		ep->z->log_fn(fmt, ##__VA_ARGS__); \
} while(0)

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

#define ZAP_LIBPATH_DEFAULT PLUGINDIR
#define _SO_EXT ".so"
static char _libdir[PATH_MAX];

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
	if (e < 0 || e > ZAP_EVENT_LAST)
		return "ZAP_EVENT_UNKNOWN";
	return __zap_event_str[e];
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

zap_t zap_get(const char *name, zap_log_fn_t log_fn, zap_mem_info_fn_t mem_info_fn)
{
	char *libdir;
	zap_t z = NULL;
	char *errstr;
	int len;
	int ret;

	if (!log_fn)
		log_fn = default_log;
	if (!mem_info_fn)
		mem_info_fn = default_zap_mem_info;

	libdir = getenv("ZAP_LIBPATH");
	if (!libdir || libdir[0] == '\0')
		strcpy(_libdir, ZAP_LIBPATH_DEFAULT);
	else
		strcpy(_libdir, libdir);

	/* Add a trailing / if one is not present in the path */
	len = strlen(_libdir);
	if (_libdir[len-1] != '/')
		strcat(_libdir, "/");

	strcat(_libdir, "libzap_");
	strcat(_libdir, name);
	strcat(_libdir, _SO_EXT);
	void *d = dlopen(_libdir, RTLD_NOW);
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

	strcpy(z->name, name);
	z->log_fn = log_fn;
	z->mem_info_fn = mem_info_fn;

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
	}
}

zap_ep_t zap_new(zap_t z, zap_cb_fn_t cb)
{
	zap_ep_t zep = NULL;
	if (!cb)
		cb = blocking_zap_cb;
	zep = z->new(z, cb);
	if (zep) {
		zep->z = z;
		zep->cb = cb;
		zep->ref_count = 1;
		zep->state = ZAP_EP_INIT;
		pthread_mutex_init(&zep->lock, NULL);
		sem_init(&zep->block_sem, 0, 0);
	}
	return zep;
}

zap_err_t zap_accept(zap_ep_t ep, zap_cb_fn_t cb, char *data, size_t data_len)
{
	return ep->z->accept(ep, cb, data, data_len);
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
	if (0 == __sync_sub_and_fetch(&ep->ref_count, 1))
		ep->z->destroy(ep);
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

zap_err_t zap_map(zap_ep_t ep, zap_map_t *pm,
		  void *addr, size_t len, zap_access_t acc)
{
	zap_map_t map;
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

static void __attribute__ ((constructor)) cs_init(void)
{
	pthread_mutex_init(&zap_list_lock, 0);
}

static void __attribute__ ((destructor)) cs_term(void)
{
}
