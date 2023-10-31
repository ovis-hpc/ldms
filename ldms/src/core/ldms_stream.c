/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2022-2023 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2022-2023 Open Grid Computing, Inc. All rights reserved.
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
 * TODO List
 * [x] Spread stream based on src.
 * [x] Stream credentials.
 *     [x] drop stream messages based on cred/permission
 *     [x] send stream messages 'as' someone else
 * [.] Stream statistics and info
 *     [.] (from Nichamon's stream status code).
 *     [.] drop statistics.
 * [x] Python interface
 */

#define _GNU_SOURCE

#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include "ovis_json/ovis_json.h"
#include "coll/rbt.h"
#include "ovis_ref/ref.h"
#include "ovis_log/ovis_log.h"

#include "ldms.h"
#include "ldms_rail.h"
#include "ldms_stream.h"

static ovis_log_t __ldms_stream_log = NULL; /* see __ldms_stream_init() below */

#define __LOG(LVL, FMT, ...) ovis_log(__ldms_stream_log, LVL, FMT, ##__VA_ARGS__ );


#define __DEBUG(FMT, ...) __LOG(OVIS_LDEBUG, FMT, ##__VA_ARGS__)
#define __INFO(FMT, ...) __LOG(OVIS_LINFO, FMT, ##__VA_ARGS__)
#define __WARN(FMT, ...) __LOG(OVIS_LWARN, FMT, ##__VA_ARGS__)
#define __ERROR(FMT, ...) __LOG(OVIS_LERROR, FMT, ##__VA_ARGS__)

#define __TIMESPEC_MIN ( (struct timespec){0, 0} )
#define __TIMESPEC_MAX ( (struct timespec){INT64_MAX, 999999999} )
#define __TIMESPEC_LT(a, b) ( \
		(a)->tv_sec < (b)->tv_sec || \
		( (a)->tv_sec == (b)->tv_sec && (a)->tv_nsec < (b)->tv_nsec ) \
	)
#define __TIMESPEC_GT(a, b) __TIMESPEC_LT(b, a)
#define __TIMESPEC_LE(a, b) (!__TIMESPEC_LT(b, a))
#define __TIMESPEC_GE(a, b) __TIMESPEC_LE(b, a)

static int __stream_stats_level = 1;

/* see implementation in ldms_rail.c */
int  __credit_acquire(uint64_t *credit, uint64_t n);
void __credit_release(uint64_t *credit, uint64_t n);
int __rate_credit_acquire(struct ldms_rail_rate_credit_s *c, uint64_t n);
void __rate_credit_release(struct ldms_rail_rate_credit_s *c, uint64_t n);

int __str_rbn_cmp(void *tree_key, const void *key);
int __u64_rbn_cmp(void *tree_key, const void *key);
int __ldms_addr_rbn_cmp(void *tree_key, const void *key);

pthread_rwlock_t __stream_rwlock = PTHREAD_RWLOCK_INITIALIZER;
#define __STREAM_RDLOCK() pthread_rwlock_rdlock(&__stream_rwlock)
#define __STREAM_WRLOCK() pthread_rwlock_wrlock(&__stream_rwlock)
#define __STREAM_UNLOCK() pthread_rwlock_unlock(&__stream_rwlock)

static struct rbt __stream_rbt = RBT_INITIALIZER(__str_rbn_cmp);

TAILQ_HEAD(, ldms_stream_client_s) __regex_client_tq = TAILQ_HEAD_INITIALIZER(__regex_client_tq);

static uint64_t stream_gn = 0;

int __rail_rep_send_raw(struct ldms_rail_ep_s *rep, void *data, int len);

/*
 * __part_send(rep, src, msg_gn, data0, len0, data1, len1, ..., NULL)
 * dataX are `const char *`
 * lenX  are `int`.
 */
static int __part_send(struct ldms_rail_ep_s *rep,
			struct ldms_addr *src, uint64_t msg_gn,
			...)
{
	size_t zmax = rep->rail->max_msg;
	struct ldms_request *req; /* partial stream message */
	int rc = 0;
	va_list ap;
	const char *data;
	int data_len;
	size_t pmsg_len, a_len;
	size_t req_len;

	req = malloc(zmax);
	if (!req)
		return ENOMEM;

	req->hdr.cmd = htobe32(LDMS_CMD_STREAM_MSG);
	req->hdr.xid = 0;
	req->stream_part.src = *src; /* src is IP4/6 port+addr; already big endian */
	req->stream_part.msg_gn = htobe64(msg_gn);
	req->stream_part.more = 1;
	req->stream_part.first = 1;
	pmsg_len = 0;
	a_len = zmax - sizeof(req->hdr) - sizeof(req->stream_part);

	va_start(ap, msg_gn);
 next_param:
	data = va_arg(ap, const char *);
	if (!data) {
		va_end(ap);
		req->stream_part.more = 0;
		goto flush_req;
	}
	data_len = va_arg(ap, int);

	if (!a_len)
		goto flush_req;

 fill_req:
	if (data_len <= a_len) {
		memcpy(req->stream_part.part_msg + pmsg_len, data, data_len);
		pmsg_len += data_len;
		a_len -= data_len;
		goto next_param;
	} else {
		memcpy(req->stream_part.part_msg + pmsg_len, data, a_len);
		pmsg_len += a_len;
		a_len = 0;
		data += a_len;
		data_len -= a_len;
		goto flush_req;
	}

 flush_req:
	req_len = sizeof(req->hdr) + sizeof(req->stream_part) + pmsg_len;
	req->hdr.len = htobe32(req_len);
	rc = __rail_rep_send_raw(rep, req, req_len);
	if (rc)
		goto out;
	if (!data)
		/* no more data */
		goto out;
	/* reset req buffer */
	pmsg_len = 0;
	a_len = zmax - sizeof(req->hdr) - sizeof(req->stream_part);
	req->stream_part.first = 0;
	goto fill_req;

 out:
	if (req)
		free(req);
	return rc;
}

static int __rep_publish(struct ldms_rail_ep_s *rep, const char *stream_name,
                        ldms_stream_type_t stream_type,
			struct ldms_addr *src, uint64_t msg_gn,
			ldms_cred_t cred, int perm,
			const char *data, size_t data_len)
{
	int rc = 0;
	int name_len = strlen(stream_name) + 1;
	int credit_required = name_len + data_len; /* header stuff are not credited */
	struct ldms_stream_full_msg_s msg;

	rc = __credit_acquire(&rep->send_credit, credit_required);
	if (rc)
		return rc;
	rc = __rate_credit_acquire(&rep->rate_credit, credit_required);
	if (rc) {
		__credit_release(&rep->send_credit, credit_required);
		return rc;
	}

	/* credit acquired */
	if (src) {
		msg.src = *src;
		msg.src.sa_family = htons(msg.src.sa_family);
		/* the rest of msg.src are in network endian */
	} else {
		bzero(&msg.src, sizeof(msg.src));
	}
	msg.msg_gn = htobe64(msg_gn);
	msg.msg_len = htobe32(name_len + data_len);
	msg.stream_type = htobe32(stream_type);
	msg.cred.uid = htobe32(cred->uid);
	msg.cred.gid = htobe32(cred->gid);
	msg.perm = htobe32(perm);
	rc = __part_send(rep, &msg.src, msg_gn,
			 &msg, sizeof(msg), /* msg hdr */
			 stream_name, name_len, /* name */
			 data, data_len, /* data */
			 NULL /* term */);
	return rc;
}

static int primer = 1033;

/* callback function for remote client; republish data to c->x */
static int
__remote_client_cb(ldms_stream_event_t ev, void *cb_arg)
{
	ldms_rail_t r;
	int ep_idx;
	int rc;
	assert( ev->type == LDMS_STREAM_EVENT_RECV );
	if (!XTYPE_IS_RAIL(ev->recv.client->x->xtype))
		return ENOTSUP;
	r = (ldms_rail_t)ev->recv.client->x;
	switch (ev->recv.src.sa_family) {
	case 0:
		ep_idx = 0;
		break;
	case AF_INET:
		ep_idx = ( be32toh(*(int*)&ev->recv.src.addr[0]) % primer ) % r->n_eps;
		break;
	case AF_INET6:
		ep_idx = ( be32toh(*(int*)&ev->recv.src.addr[12]) % primer ) % r->n_eps;
		break;
	default:
		assert(0 == "Unexpected network family");
		ep_idx = 0;
	}

	rc = ldms_access_check(r->eps[ep_idx].ep, LDMS_ACCESS_READ,
			ev->recv.cred.uid, ev->recv.cred.gid, ev->recv.perm);
	if (0 != rc)
		return 0; /* remote has no access; do not forward */

	rc = __rate_credit_acquire(&ev->recv.client->rate_credit, ev->recv.data_len);
	if (rc)
		goto out;

	rc = __rep_publish(&r->eps[ep_idx], ev->recv.name, ev->recv.type,
			     &ev->recv.src, ev->recv.msg_gn,
			     &ev->recv.cred, ev->recv.perm,
			     ev->recv.data,
			     ev->recv.data_len);
	if (rc)
		__rate_credit_release(&ev->recv.client->rate_credit, ev->recv.data_len);
 out:
	return rc;
}

static int
__client_stream_bind(ldms_stream_client_t c, struct ldms_stream_s *s);

/* must NOT hold __stream_rwlock */
static struct ldms_stream_s *
__stream_get(const char *stream_name, int *is_new)
{
	struct ldms_stream_s *s;
	int name_len = strlen(stream_name) + 1;
	__STREAM_RDLOCK();
	s = (void*)rbt_find(&__stream_rbt, stream_name);
	__STREAM_UNLOCK();
	if (s)
		goto out_0;
	/* unlikely */
	__STREAM_WRLOCK();
	/* need to find the stream again in the case that the other thread
	 * won the write race */
	s = (void*)rbt_find(&__stream_rbt, stream_name);
	if (s)
		goto out_1;
	s = calloc(1, sizeof(*s) + name_len);
	if (!s)
		goto out_1;
	pthread_rwlock_init(&s->rwlock, NULL);
	rbn_init(&s->rbn, s->name);
	TAILQ_INIT(&s->client_tq);
	s->name_len = name_len;
	memcpy(s->name, stream_name, name_len);
	rbt_ins(&__stream_rbt, &s->rbn);
	if (is_new)
		*is_new = 1;

	rbt_init(&s->src_stats_rbt, __ldms_addr_rbn_cmp);
	s->rx.first_ts = __TIMESPEC_MAX;
	s->rx.last_ts  = __TIMESPEC_MIN;

	/* We need to go through the _regex_ clients to see if we match
	 * any. */
	struct ldms_stream_client_s *c;
	int rc;
	TAILQ_FOREACH(c, &__regex_client_tq, entry) {
		rc = regexec(&c->regex, stream_name, 0, NULL, 0);
		if (rc) /* does not match */
			continue;
		/* matched; add the client into the stream client list */
		rc = __client_stream_bind(c, s);
		if (rc)
			goto out_1;
	}

	/* Don't have to go through the NON _regex_ clients b/c the
	 * non-regex clients already create the stream structure and
	 * register themselves before reaching here. */
 out_1:
	__STREAM_UNLOCK();
 out_0:
	return s;
}

static void __sce_ref_free(void *arg)
{
	__DEBUG("sce %p: free\n", arg);
	free(arg);
}

static int
__client_stream_bind(ldms_stream_client_t c, struct ldms_stream_s *s)
{
	struct ldms_stream_client_entry_s *sce;
	sce = calloc(1, sizeof(*sce));
	if (!sce)
		return ENOMEM;
	ref_get(&c->ref, "client_entry");
	sce->client = c;
	sce->stream = s;

	ref_init(&sce->ref, "client_stream_entry", __sce_ref_free, sce);

	pthread_rwlock_wrlock(&c->rwlock);
	TAILQ_INSERT_TAIL(&c->stream_tq, sce, client_stream_entry);
	pthread_rwlock_unlock(&c->rwlock);

	pthread_rwlock_wrlock(&s->rwlock);
	TAILQ_INSERT_TAIL(&s->client_tq, sce, stream_client_entry);
	pthread_rwlock_unlock(&s->rwlock);
	ref_get(&sce->ref, "stream_client_entry");

	LDMS_STREAM_COUNTERS_INIT(&sce->tx);
	LDMS_STREAM_COUNTERS_INIT(&sce->drops);

	__DEBUG("sce %p: bind stream '%s' - client '%s' match '%s'\n",
			sce, s->name, c->desc, c->match);

	return 0;
}

static void
__client_stream_unbind(struct ldms_stream_client_entry_s *sce)
{
	struct ldms_stream_client_s *c;
	struct ldms_stream_s *s;

	c = sce->client;
	s = sce->stream;

	if (!c || !s)
		return; /* no-op */

	/* Unbind the client and stream from the entry.
	 * The sce list entry is not removed from either list */
	__DEBUG("sce %p: unbind stream '%s' - client '%s' match '%s'\n",
			sce, s->name, c->desc, c->match);

	pthread_rwlock_wrlock(&c->rwlock);
	sce->stream = NULL;
	pthread_rwlock_unlock(&c->rwlock);

	pthread_rwlock_wrlock(&s->rwlock);
	sce->client = NULL;
	pthread_rwlock_unlock(&s->rwlock);

	ref_put(&c->ref, "client_entry");
}

void  __counters_update(struct ldms_stream_counters_s *ctr,
			struct timespec *now, size_t bytes)
{
	if (__TIMESPEC_LT(now, &ctr->first_ts))
		ctr->first_ts = *now;
	if (__TIMESPEC_GT(now, &ctr->last_ts))
		ctr->last_ts = *now;
	ctr->bytes += bytes;
	ctr->count += 1;
}

/* deliver stream data to all clients */
/* must NOT hold __stream_mutex */
static int
__stream_deliver(struct ldms_addr *src, uint64_t msg_gn,
		 const char *stream_name, int name_len,
		 ldms_stream_type_t stream_type,
		 ldms_cred_t cred, uint32_t perm,
		 const char *data, size_t data_len)
{
	int rc = 0, gc;
	struct ldms_stream_s *s;
	struct ldms_stream_client_entry_s *sce, *next_sce;
	struct ldms_stream_client_s *c;
	struct timespec now;

	s = __stream_get(stream_name, NULL);
	if (!s) {
		rc = errno;
		goto out;
	}

	struct ldms_stream_event_s _ev = {
		.type = LDMS_STREAM_EVENT_RECV,
		.recv = {
			.src = {0},
			.msg_gn = msg_gn,
			.type = stream_type,
			.name_len = name_len,
			.data_len = data_len,
			.name = stream_name,
			.data = data,
			.cred = *cred,
			.perm = perm,
			.json = NULL,
		}
	};
	json_entity_t json = NULL;

	if (src)
		_ev.recv.src = *src;

	/* update stats */
	if (__stream_stats_level <= 0)
		goto skip_stats;
	pthread_rwlock_wrlock(&s->rwlock);
	clock_gettime(CLOCK_REALTIME, &now);
	__counters_update(&s->rx, &now, data_len);
	if (__stream_stats_level > 1) {
		/* stats by src */
		struct rbn *rbn = rbt_find(&s->src_stats_rbt, &_ev.recv.src);
		struct ldms_stream_src_stats_s *ss;
		if (rbn) {
			ss = container_of(rbn, struct ldms_stream_src_stats_s, rbn);
		} else {
			ss = malloc(sizeof(*ss));
			if (!ss) {
				/* error in stats shall not break the normal
				 * operations */
				pthread_rwlock_unlock(&s->rwlock);
				goto skip_stats;
			}
			ss->src = _ev.recv.src;
			rbn_init(&ss->rbn, &ss->src);
			ss->rx = LDMS_STREAM_COUNTERS_INITIALIZER;
			rbt_ins(&s->src_stats_rbt, &ss->rbn);
		}
		__counters_update(&ss->rx, &now, data_len);
	}
	pthread_rwlock_unlock(&s->rwlock);
 skip_stats:

	gc = 0;
	pthread_rwlock_rdlock(&s->rwlock);
	TAILQ_FOREACH(sce, &s->client_tq, stream_client_entry) {
		c = sce->client;
		if (!c) {
			gc = 1;
			continue;
		}
		if (!json && stream_type == LDMS_STREAM_JSON && !c->x) {
			/* json object is only required to parse once for
			 * the local client */
			struct json_parser_s *jp = json_parser_new(0);
			if (!jp) {
				rc = ENOMEM;
				goto cleanup;
			}
			rc = json_parse_buffer(jp, (void*)data, data_len, &json);
			_ev.recv.json = json;
			json_parser_free(jp);
			if (rc) {
				goto cleanup;
			}
		}
		ref_get(&c->ref, "callback");
		pthread_rwlock_unlock(&s->rwlock);
		_ev.recv.client = c;
		rc = c->cb_fn(&_ev, c->cb_arg);
		if (__stream_stats_level > 0) {
			pthread_rwlock_wrlock(&c->rwlock);
			if (rc) {
				__counters_update( &sce->drops, &now, data_len);
				__counters_update(&c->drops, &now, data_len);
			} else {
				__counters_update(&sce->tx, &now, data_len);
				__counters_update(&c->tx, &now, data_len);
			}
			pthread_rwlock_unlock(&c->rwlock);
		}
		ref_put(&c->ref, "callback");
		pthread_rwlock_rdlock(&s->rwlock);
	}

 cleanup:
	if (json)
		json_entity_free(json);
	pthread_rwlock_unlock(&s->rwlock);
	if (gc) {
		/* remove unbound sce from s->client_tq */
		pthread_rwlock_wrlock(&s->rwlock);
		sce = TAILQ_FIRST(&s->client_tq);
		while (sce) {
			next_sce = TAILQ_NEXT(sce, stream_client_entry);
			if (sce->client)
				goto next;
			TAILQ_REMOVE(&s->client_tq, sce, stream_client_entry);
			ref_put(&sce->ref, "stream_client_entry");
		next:
			sce = next_sce;
		}
		pthread_rwlock_unlock(&s->rwlock);
	}
 out:
	return rc;
}

typedef struct gid_array_s {
	int n;
	gid_t gids[OVIS_FLEX];
} *gid_array_t;

gid_array_t __get_gids()
{
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	static gid_array_t gids = NULL;

	int n, rc;
	gid_array_t _gids;

	if (gids)
		goto out;

	pthread_mutex_lock(&mutex);
	if (gids) /* our thread lost the create race */
		goto unlock_out;
	n = getgroups(0, NULL);
	if (n < 0)
		goto unlock_out;
 again:
	_gids = malloc(sizeof(*_gids) + n*sizeof(gid_t));
	if (!_gids)
		goto unlock_out;
	_gids->n = n;
	rc = getgroups(n, _gids->gids);
	if (rc < 0) {
		free(_gids);
		goto unlock_out;
	}
	if (rc > n) {
		free(_gids);
		n = rc;
		goto again;
	}
	gids = _gids;

 unlock_out:
	pthread_mutex_unlock(&mutex);
 out:
	return gids;
}

#if 0
/* NOTE: This code section requires "libcap-dev" package */
#include <sys/capability.h>
int __check_cap_setuidgid(int *can_set_uid, int *can_set_gid)
{
	cap_t cap = cap_get_proc();
	cap_flag_value_t cap_fv;
	int rc = 0;
	int x = _LINUX_CAPABILITY_VERSION_3;
	*can_set_uid = 0;
	*can_set_gid = 0;
	if (!cap) {
		rc = errno;
		goto out;
	}
	rc = cap_get_flag(cap, CAP_SETUID, CAP_PERMITTED, &cap_fv);
	if (rc) {
		rc = errno;
		goto out;
	}
	*can_set_uid = (cap_fv == CAP_SET);
	rc = cap_get_flag(cap, CAP_SETGID, CAP_PERMITTED, &cap_fv);
	if (rc) {
		rc = errno;
		goto out;
	}
	*can_set_gid = (cap_fv == CAP_SET);
 out:
	if (cap)
		cap_free(cap);
	return rc;
}
#else
#include <sys/syscall.h>
int __check_cap_setuidgid(int *can_set_uid, int *can_set_gid)
{
	/*
	 * NOTE_0: <sys/capability.h> is a part of "libcap-dev" package, which
	 *         may not be installed. We can make a syscall instead of using
	 *         libcap.
	 *
	 * NOTE_1: Definitions from <linux/capability.h>:
	 *
	 * #define CAP_SETGID           6
	 * #define CAP_SETUID           7
	 *
	 * #define _LINUX_CAPABILITY_VERSION_1  0x19980330
	 * #define _LINUX_CAPABILITY_U32S_1     1
	 *
	 * #define _LINUX_CAPABILITY_VERSION_2  0x20071026  // deprecated - use v3
	 * #define _LINUX_CAPABILITY_U32S_2     2
	 *
	 * #define _LINUX_CAPABILITY_VERSION_3  0x20080522
	 * #define _LINUX_CAPABILITY_U32S_3     2
	 *
	 * typedef struct __user_cap_header_struct {
	 *         __u32 version;
	 *         int pid;
	 * } *cap_user_header_t;
	 *
	 * typedef struct __user_cap_data_struct {
	 *         __u32 effective;
	 *         __u32 permitted;
	 *         __u32 inheritable;
	 * } *cap_user_data_t;
	 */
	struct {
		uint32_t version;
		int pid;
	} cap_hdr;

	struct {
		uint32_t effective;
		uint32_t permitted;
		uint32_t inheritable;
	} cap_data[2];

	int rc;
	const int ver = 0x20080522;
	uint32_t cap_setgid_mask = 1<<6;
	uint32_t cap_setuid_mask = 1<<7;
	cap_hdr.version = ver;
	cap_hdr.pid = (pid_t) syscall (SYS_gettid);
	rc = syscall(SYS_capget, &cap_hdr, &cap_data);
	if (rc) {
		if (errno == EINVAL && cap_hdr.version != ver) {
			/* different preferred version; retry */
			rc = syscall(SYS_capget, &cap_hdr, &cap_data);
			if (rc) {
				return errno;
			}
		} else {
			return errno;
		}
	}
	/* regardless of version; the data that we care is in cap_data[0] */
	*can_set_uid = !!(cap_data[0].permitted & cap_setuid_mask);
	*can_set_gid = !!(cap_data[0].permitted & cap_setgid_mask);
	return 0;
}
#endif

int __publish_cred_check(ldms_cred_t cred)
{
	int rc = 0;
	uid_t ruid, euid, suid;
	gid_t rgid, egid, sgid;
	int can_set_uid = 0, can_set_gid = 0;
	int cap_checked = 0, i;
	gid_array_t gids;

	/* UID */
	rc = getresuid(&ruid, &euid, &suid);
	if (rc) {
		rc = errno;
		return rc;
	}
	if (cred->uid == ruid || cred->uid == euid || cred->uid == suid)
		goto check_gid; /* uid OK */
	rc = __check_cap_setuidgid(&can_set_uid, &can_set_gid);
	if (rc)
		goto out;
	cap_checked = 1;
	if (can_set_uid)
		goto check_gid; /* can set uid */
	rc = EPERM;
	goto out;

 check_gid:
	/* GID */
	rc = getresgid(&rgid, &egid, &sgid);
	if (rc) {
		rc = errno;
		return rc;
	}
	if (cred->gid == rgid || cred->gid == egid || cred->gid == sgid)
		goto out; /* gid OK */
	gids = __get_gids();
	if (gids) {
		for (i = 0; i < gids->n; i++) {
			if (cred->gid == gids->gids[i])
				goto out;
		}
	}
	if (!cap_checked) {
		rc = __check_cap_setuidgid(&can_set_uid, &can_set_gid);
		if (rc)
			goto out;
	}
	if (can_set_gid)
		goto out;
	rc = EPERM;
 out:
	return rc;
}

int ldms_stream_publish(ldms_t x, const char *stream_name,
                        ldms_stream_type_t stream_type,
			ldms_cred_t cred,
			uint32_t perm,
                        const char *data, size_t data_len)
{
	ldms_rail_t r;
	uint64_t msg_gn;
	int name_len = strlen(stream_name) + 1;
	struct ldms_cred _cred;
	int rc;

	msg_gn = __atomic_fetch_add(&stream_gn, 1, __ATOMIC_SEQ_CST);

	if (cred) {
		/* verify credential; check if we can publish with the given
		 * credential */
		rc = __publish_cred_check(cred);
		if (rc)
			return rc;
	} else {
		/* else; use process's credential */
		_cred.uid = geteuid();
		_cred.gid = getegid();
		cred = &_cred;
	}

	/* publish directly to remote peer */
	if (x) {
		if (!XTYPE_IS_RAIL(x->xtype))
			return ENOTSUP;
		r = (ldms_rail_t)x;
		return __rep_publish(&r->eps[0], stream_name, stream_type, 0,
				     msg_gn, cred, perm, data, data_len);
	}

	/* else publish locally */
	return __stream_deliver(0, msg_gn, stream_name, name_len, stream_type,
				   cred, perm, data, data_len);
}

static void __client_ref_free(void *arg)
{
	struct ldms_stream_client_s *c = arg;
	struct ldms_stream_client_entry_s *sce;
	while ((sce = TAILQ_FIRST(&c->stream_tq))) {
		assert(sce->stream == NULL);
		TAILQ_REMOVE(&c->stream_tq, sce, client_stream_entry);
		ref_put(&sce->ref, "client_stream_entry");
	}
	if (c->is_regex) {
		regfree(&c->regex);
	}
	free(c);
}

/* subscribe the client to the streams */
static int
__client_subscribe(struct ldms_stream_client_s *c)
{
	int rc = 0;
	struct rbn *rbn;
	struct ldms_stream_client_entry_s *sce;
	struct ldms_stream_s *s;

	if (c->is_regex) {
		__STREAM_WRLOCK();
		TAILQ_INSERT_TAIL(&__regex_client_tq, c, entry);
		ref_get(&c->ref, "__regex_client_tq");
		RBT_FOREACH(rbn, &__stream_rbt) {
			s = container_of(rbn, struct ldms_stream_s, rbn);
			if (regexec(&c->regex, s->name, 0, NULL, 0))
				continue; /* not matched */
			/* matched; bind the client */
			rc = __client_stream_bind(c, s);
			if (rc)
				goto err_1;
		}
		__STREAM_UNLOCK();
	} else {
		/* bind client to the stream */
		s = __stream_get(c->match, NULL);
		if (!s) {
			rc = errno;
			goto out;
		}
		rc = __client_stream_bind(c, s);
		if (rc)
			goto out;
	}
	goto out;

 err_1:
	/* unbind client from streams */
	TAILQ_FOREACH(sce, &c->stream_tq, client_stream_entry) {
		__client_stream_unbind(sce);
	}
	if (c->is_regex) {
		TAILQ_REMOVE(&__regex_client_tq, c, entry);
		ref_put(&c->ref, "__regex_client_tq");
	}
	__STREAM_UNLOCK();
 out:
	return rc;
}

static ldms_stream_client_t
__client_alloc(const char *stream, int is_regex,
	       ldms_stream_event_cb_t cb_fn, void *cb_arg,
	       const char *desc)
{
	ldms_stream_client_t c;
	int rc, slen = strlen(stream) + 1;
	int dlen = (desc?strlen(desc):0) + 1;
	c = calloc(1, sizeof(*c) + slen + dlen);
	if (!c)
		goto out;
	pthread_rwlock_init(&c->rwlock, NULL);
	ref_init(&c->ref, "init", __client_ref_free, c);
	c->match_len = slen;
	memcpy(c->match, stream, slen);
	c->desc = &c->match[c->match_len]; /* c->desc next to c->match */
	c->desc_len = dlen;
	if (desc)
		memcpy(c->desc, desc, dlen);
	rbn_init(&c->rbn, c->match);
	c->cb_fn = cb_fn;
	c->cb_arg = cb_arg;
	TAILQ_INIT(&c->stream_tq);
	c->x = NULL;
	if (is_regex) {
		c->is_regex = 1;
		rc = regcomp(&c->regex, c->match, REG_EXTENDED|REG_NOSUB);
		if (rc)
			goto err_0;
	}

	LDMS_STREAM_COUNTERS_INIT(&c->tx);
	LDMS_STREAM_COUNTERS_INIT(&c->drops);

	c->rate_credit.credit = __RAIL_UNLIMITED;
	c->rate_credit.rate   = __RAIL_UNLIMITED;
	c->rate_credit.ts.tv_sec  = 0;
	c->rate_credit.ts.tv_nsec = 0;

	goto out;
 err_0:
	free(c);
 out:
	return c;
}

static void
__client_free(ldms_stream_client_t c)
{
	ref_put(&c->ref, "init");
}

ldms_stream_client_t
ldms_stream_subscribe(const char *stream, int is_regex,
		      ldms_stream_event_cb_t cb_fn, void *cb_arg,
		      const char *desc)
{
	ldms_stream_client_t c = NULL;
	int rc;

	if (!cb_fn) {
		errno = EINVAL;
		goto out;
	}

	c = __client_alloc(stream, is_regex, cb_fn, cb_arg, desc);
	if (!c)
		goto out;
	rc = __client_subscribe(c);
	if (rc) {
		__client_free(c);
		c = NULL;
		errno = rc;
	}

 out:
	return c;
}

void ldms_stream_close(ldms_stream_client_t c)
{
	struct ldms_stream_client_entry_s *sce;

	__STREAM_WRLOCK();
	/* unbind from all streams it subscried to */
	TAILQ_FOREACH(sce, &c->stream_tq, client_stream_entry) {
		__client_stream_unbind(sce);
	}
	if (c->is_regex) {
		TAILQ_REMOVE(&__regex_client_tq, c, entry);
		ref_put(&c->ref, "__regex_client_tq");
	}
	__STREAM_UNLOCK();
	ref_put(&c->ref, "init");
}

struct __sub_req_ctxt_s {
	ldms_stream_event_cb_t cb_fn;
	void *cb_arg;
	struct ldms_request req[0];
};

static int
__remote_sub(ldms_t x, enum ldms_request_cmd cmd,
	     const char *match, int is_regex,
	     ldms_stream_event_cb_t cb_fn, void *cb_arg,
	     int64_t rate)
{
	ldms_rail_t r;
	struct ldms_request *req;
	struct __sub_req_ctxt_s *ctxt;
	int match_len;
	int msg_len;
	zap_err_t zerr;

	if (!XTYPE_IS_RAIL(x->xtype))
		return ENOTSUP;

	r = (void*)x;
	match_len = strlen(match) + 1;
	msg_len = sizeof(req->hdr) + sizeof(req->stream_sub) + match_len;
	if (msg_len > r->max_msg)
		return ENAMETOOLONG;
	ctxt = malloc( sizeof(*ctxt) + msg_len );
	/* freed in __process_stream_sub_reply() */
	if (!ctxt)
		return ENOMEM;
	ctxt->cb_fn  = cb_fn;
	ctxt->cb_arg = cb_arg;
	req = ctxt->req;
	req->hdr.cmd = htobe32(cmd);
	req->hdr.len = htobe32(msg_len);
	req->hdr.xid = (uint64_t)ctxt;
	req->stream_sub.is_regex = is_regex; /* 1 bit */
	req->stream_sub.match_len = htobe32(match_len);
	req->stream_sub.rate = htobe64(rate);
	memcpy(req->stream_sub.match, match, match_len);

	zerr = __rail_rep_send_raw(&r->eps[0], req, msg_len);
	if (zerr) {
		free(ctxt);
		return EIO;
	}

	return 0;
}

int ldms_stream_remote_subscribe(ldms_t x, const char *match, int is_regex,
		      ldms_stream_event_cb_t cb_fn, void *cb_arg, int64_t rate)
{
	return __remote_sub(x, LDMS_CMD_STREAM_SUB, match, is_regex, cb_fn, cb_arg, rate);
}

int ldms_stream_remote_unsubscribe(ldms_t x, const char *match, int is_regex,
		      ldms_stream_event_cb_t cb_fn, void *cb_arg)
{
	return __remote_sub(x, LDMS_CMD_STREAM_UNSUB, match, is_regex, cb_fn, cb_arg, -1);
}

struct __sbuf_key_s {
	struct ldms_addr src;
	uint64_t msg_gn;
};

int __stream_buf_cmp(void *tree_key, const void *key)
{
	return memcmp(tree_key, key, sizeof(struct __sbuf_key_s));
}

int __str_rbn_cmp(void *tree_key, const void *key)
{
	return strcmp(tree_key, key);
}

int __u64_rbn_cmp(void *tree_key, const void *key)
{
	uint64_t a = *(uint64_t*)tree_key;
	uint64_t b = *(uint64_t*)key;
	if (a < b)
		return -1;
	if (a > b)
		return 1;
	return 0;
}

int __ldms_addr_rbn_cmp(void *tree_key, const void *key)
{
	return memcmp(tree_key, key, sizeof(struct ldms_addr));
}

struct __stream_buf_s {
	struct rbn rbn;
	struct __sbuf_key_s key;
	size_t full_msg_len;
	off_t  off;
	union {
		struct ldms_stream_full_msg_s msg[0];
		char buf[0];
	};
};

/* implementation in ldms_rail.c */
void __rail_ep_credit_return(struct ldms_rail_ep_s *rep, int credit);

static void
__process_stream_msg(ldms_t x, struct ldms_request *req)
{
	struct ldms_rail_ep_s *rep = ldms_xprt_ctxt_get(x);
	/* no need to take lock ; only one zap thread working on this tree */
	struct rbn *rbn;
	struct __stream_buf_s *sbuf;
	struct ldms_stream_full_msg_s *fmsg;
	int plen, flen;
	union ldms_sockaddr lsa, rsa;
	socklen_t slen = sizeof(lsa);
	int rc;
	const char *name;
	int name_len;
	const char *data;
	int data_len;

	/* src is always big endian */
	req->stream_part.msg_gn = be64toh(req->stream_part.msg_gn);
	plen = be32toh(req->hdr.len) - sizeof(req->hdr) - sizeof(req->stream_part);
	if (req->stream_part.src.sa_family == 0) {
		/* resolve source */
		rc = ldms_xprt_sockaddr(x, &lsa.sa, &rsa.sa, &slen);
		if (rc)
			return;
		switch (rsa.sa.sa_family) {
		case AF_INET:
			/* Exclude 127.0.0.0/8 loopbacks.
			 * In the case of the loopback, the 'src' stays 0 and the
			 * next level will resolve the src address.
			 */
			if (*((char*)&rsa.sin.sin_addr.s_addr) != 127) {
				req->stream_part.src.sa_family = htons(AF_INET);
				memcpy(req->stream_part.src.addr,
					&rsa.sin.sin_addr,
					sizeof(rsa.sin.sin_addr));
				req->stream_part.src.sin_port = rsa.sin.sin_port;
			}
			break;
		case AF_INET6:
			/* Exclude loopbacks */
			if (0 != memcmp(&rsa.sin6.sin6_addr, &in6addr_loopback,
						sizeof(struct in6_addr))) {
				req->stream_part.src.sa_family = htons(AF_INET6);
				memcpy(req->stream_part.src.addr,
				       &rsa.sin6.sin6_addr,
				       sizeof(struct in6_addr));
				req->stream_part.src.sin_port = rsa.sin6.sin6_port;
			}
			break;
		default:
			break;
		}
	}

	/* stream_part starts with 'src; msg_gn' */
	rbn = rbt_find(&rep->sbuf_rbt, &req->stream_part);
	if (rbn) {
		sbuf = container_of(rbn, struct __stream_buf_s, rbn);
		goto collect;
	}

	/* else; expecting first message */
	if (!req->stream_part.first) {
		assert(0 == "Bad message");
		return;
	}

	fmsg = (void*)req->stream_part.part_msg;
	flen = be32toh(fmsg->msg_len);
	sbuf = malloc(sizeof(*sbuf) + sizeof(*fmsg) + flen);
	if (!sbuf)
		return;
	sbuf->full_msg_len = flen + sizeof(*fmsg);
	sbuf->key.src = req->stream_part.src;
	sbuf->key.msg_gn = req->stream_part.msg_gn;
	rbn_init(&sbuf->rbn, &sbuf->key);
	sbuf->off = 0;
	rbt_ins(&rep->sbuf_rbt, &sbuf->rbn);
 collect:
	if (plen + sbuf->off > sbuf->full_msg_len) {
		assert(0 == "Bad message length");
		return;
	}
	memcpy(sbuf->buf + sbuf->off, req->stream_part.part_msg, plen);
	sbuf->off += plen;

	if (req->stream_part.more)
		return; /* need more partial messages */

	/* full message collected completely */
	if (sbuf->off != sbuf->full_msg_len) {
		assert(0 == "Bad message / message length");
		goto cleanup;
	}
	sbuf->msg->src = req->stream_part.src;
	sbuf->msg->src.sa_family = ntohs(sbuf->msg->src.sa_family);
	sbuf->msg->msg_gn = be64toh(sbuf->msg->msg_gn);
	sbuf->msg->msg_len = be32toh(sbuf->msg->msg_len);
	sbuf->msg->stream_type = be32toh(sbuf->msg->stream_type);
	sbuf->msg->cred.uid = be32toh(sbuf->msg->cred.uid);
	sbuf->msg->cred.gid = be32toh(sbuf->msg->cred.gid);
	sbuf->msg->perm = be32toh(sbuf->msg->perm);

	name = sbuf->msg->msg;
	name_len = strlen(name)+1;
	data = sbuf->msg->msg + name_len;
	data_len = sbuf->msg->msg_len - name_len;

	__stream_deliver(&sbuf->msg->src, sbuf->msg->msg_gn,
			 name, name_len, sbuf->msg->stream_type,
			 &sbuf->msg->cred, sbuf->msg->perm,
			 data, data_len);
	__rail_ep_credit_return(rep, name_len + data_len);

 cleanup:
	rbt_del(&rep->sbuf_rbt, &sbuf->rbn);
	free(sbuf);
}

static void
__process_stream_sub(ldms_t x, struct ldms_request *req)
{
	struct ldms_rail_ep_s *rep = ldms_xprt_ctxt_get(x);
	ldms_rail_t r = rep->rail;
	struct rbn *rbn;
	struct ldms_stream_client_s *c;
	int rc;
	const char *err_msg;
	int msg_len, reply_len;
	union ldms_sockaddr lsin, rsin;
	socklen_t sin_len = sizeof(lsin);
	struct {
		struct ldms_reply r;
		char _[512];
	} buf;

	pthread_mutex_lock(&r->mutex);
	rbn = rbt_find(&r->stream_client_rbt, req->stream_sub.match);
	if (rbn) {
		pthread_mutex_unlock(&r->mutex);
		err_msg = "Stream existed";
		rc = EEXIST;
		goto reply;
	}
	/* network to host */
	req->stream_sub.match_len = be32toh(req->stream_sub.match_len);
	c = __client_alloc(req->stream_sub.match, req->stream_sub.is_regex,
			   __remote_client_cb, x, "remote_client");
	if (!c) {
		pthread_mutex_unlock(&r->mutex);
		err_msg = "Client allocation failure";
		rc = errno;
		goto reply;
	}

	c->rate_credit.rate = be64toh(req->stream_sub.rate);
	c->rate_credit.credit = be64toh(req->stream_sub.rate);

	rc = ldms_xprt_sockaddr(x, &lsin.sa, &rsin.sa, &sin_len);
	if (!rc) {
		rc = sockaddr2ldms_addr(&rsin.sa, &c->dest);
		if (rc) {
			bzero(&c->dest, sizeof(c->dest));
		}
	} else {
		c->dest.sa_family = 0;
	}

	c->x = (ldms_t)rep->rail;
	rbt_ins(&r->stream_client_rbt, &c->rbn);
	ref_get(&r->ref, "r->stream_client_rbt");
	pthread_mutex_unlock(&r->mutex);

	rc = __client_subscribe(c);
	if (rc) {
		err_msg = "Client subscription failed";
		goto reply;
	}

	err_msg = "OK";

	/* let through */
 reply:
	buf.r.hdr.cmd = htobe32(LDMS_CMD_STREAM_SUB_REPLY);
	buf.r.hdr.rc = htobe32(rc);
	buf.r.hdr.xid = req->hdr.xid;
	msg_len = strlen(err_msg) + 1;
	reply_len = sizeof(buf.r.hdr) + sizeof(buf.r.sub) + msg_len;
	buf.r.hdr.len = htobe32( reply_len );
	buf.r.sub.msg_len = htobe32(msg_len);
	memcpy(buf.r.sub.msg, err_msg, msg_len);
	__rail_rep_send_raw(rep, &buf, reply_len);
	return;
}

static void
__process_stream_unsub(ldms_t x, struct ldms_request *req)
{
	struct ldms_rail_ep_s *rep = ldms_xprt_ctxt_get(x);
	ldms_rail_t r = rep->rail;
	struct rbn *rbn;
	struct ldms_stream_client_s *c;
	int rc;
	const char *err_msg;
	int msg_len, reply_len;
	struct {
		struct ldms_reply r;
		char _[512];
	} buf;

	pthread_mutex_lock(&r->mutex);
	rbn = rbt_find(&r->stream_client_rbt, req->stream_sub.match);
	if (!rbn) {
		pthread_mutex_unlock(&r->mutex);
		err_msg = "Stream not found";
		rc = ENOENT;
		goto reply;
	}
	c = container_of(rbn, struct ldms_stream_client_s, rbn);
	rbt_del(&r->stream_client_rbt, rbn);
	pthread_mutex_unlock(&r->mutex);
	ldms_stream_close(c);
	ref_put(&r->ref, "r->stream_client_rbt");
	rc = 0;
	err_msg = "OK";

	/* let through */
 reply:
	buf.r.hdr.cmd = htobe32(LDMS_CMD_STREAM_UNSUB_REPLY);
	buf.r.hdr.rc = htobe32(rc);
	buf.r.hdr.xid = req->hdr.xid;
	msg_len = strlen(err_msg) + 1;
	reply_len = sizeof(buf.r.hdr) + sizeof(buf.r.sub) + msg_len;
	buf.r.hdr.len = htobe32( reply_len );
	buf.r.sub.msg_len = htobe32(msg_len);
	memcpy(buf.r.sub.msg, err_msg, msg_len);
	__rail_rep_send_raw(rep, &buf, reply_len);
	return;
}

void __rail_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg);

void __stream_req_recv(ldms_t x, int cmd, struct ldms_request *req)
{
	assert(0 == XTYPE_IS_RAIL(x->xtype)); /* x is NOT a rail */
	assert(x->event_cb == __rail_cb);
	switch (cmd) {
	case LDMS_CMD_STREAM_MSG:
		__process_stream_msg(x, req);
		break;
	case LDMS_CMD_STREAM_SUB:
		__process_stream_sub(x, req);
		break;
	case LDMS_CMD_STREAM_UNSUB:
		__process_stream_unsub(x, req);
		break;
	default:
		assert(0 == "Unexpected request");
	}
}

static void
__process_stream_subunsub_reply(ldms_t x, struct ldms_reply *reply,
				enum ldms_stream_event_type sev_type)
{
	struct ldms_rail_ep_s *rep = ldms_xprt_ctxt_get(x);
	ldms_rail_t r = rep->rail;
	struct __sub_req_ctxt_s *ctxt = (void*)reply->hdr.xid;
	struct ldms_stream_event_s sev;
	if (!ctxt->cb_fn)
		goto out;
	sev.r = (ldms_t)r;
	sev.type = sev_type;
	sev.status.is_regex = ctxt->req->stream_sub.is_regex;
	sev.status.match = ctxt->req->stream_sub.match;
	sev.status.status = be32toh(reply->hdr.rc);
	ctxt->cb_fn(&sev, ctxt->cb_arg);
 out:
	free(ctxt);
}

void __stream_reply_recv(ldms_t x, int cmd, struct ldms_reply *reply)
{
	assert(0 == XTYPE_IS_RAIL(x->xtype)); /* x is NOT a rail */
	assert(x->event_cb == __rail_cb);
	switch (cmd) {
	case LDMS_CMD_STREAM_SUB_REPLY:
		__process_stream_subunsub_reply(x, reply, LDMS_STREAM_EVENT_SUBSCRIBE_STATUS);
		break;
	case LDMS_CMD_STREAM_UNSUB_REPLY:
		__process_stream_subunsub_reply(x, reply, LDMS_STREAM_EVENT_SUBSCRIBE_STATUS);
		break;
	default:
		assert(0 == "Unexpected reply");
	}
}

void __stream_on_rail_disconnected(struct ldms_rail_s *r)
{
	struct ldms_stream_client_s *c;
	struct rbn *rbn;
	pthread_mutex_lock(&r->mutex);
	while ((rbn = rbt_min(&r->stream_client_rbt))) {
		c = container_of(rbn, struct ldms_stream_client_s, rbn);
		rbt_del(&r->stream_client_rbt, rbn);
		assert(c->x == (void*)r);
		pthread_mutex_unlock(&r->mutex);
		ldms_stream_close(c);
		ref_put(&r->ref, "r->stream_client_rbt");
		pthread_mutex_lock(&r->mutex);
	}
	pthread_mutex_unlock(&r->mutex);
}

void __ldms_stream_stats_init()
{
	char *var = getenv("LDMS_STREAM_STATS_LEVEL");
	int lvl = 1;
	if (var) {
		lvl = atoi(var);
	}
	ldms_stream_stats_level_set(lvl);
}

int ldms_stream_stats_level_set(int level)
{
	__atomic_store_n(&__stream_stats_level, level, __ATOMIC_SEQ_CST);
	return 0;
}

int ldms_stream_stats_level_get()
{
	return __atomic_load_n(&__stream_stats_level, __ATOMIC_SEQ_CST);
}

void __src_stats_rbt_purge(struct rbt *rbt)
{
	struct rbn *rbn;
	struct ldms_stream_src_stats_s *sss;
	while ((rbn = rbt_min(rbt))) {
		rbt_del(rbt, rbn);
		sss = container_of(rbn, struct ldms_stream_src_stats_s, rbn);
		free(sss);
	}
}

/* copy entries from t0 into t1 */
int __src_stats_rbt_copy(struct rbt *t0, struct rbt *t1)
{
	struct rbn *rbn;
	struct ldms_stream_src_stats_s *s0, *s1;
	int rc;
	for (rbn = rbt_min(t0); rbn; rbn = rbn_succ(rbn)) {
		s0 = container_of(rbn, struct ldms_stream_src_stats_s, rbn);
		s1 = malloc(sizeof(*s1));
		if (!s1) {
			rc = ENOMEM;
			goto err_0;
		}
		*s1 = *s0;
		rbn_init(&s1->rbn, &s1->src);
		rbt_ins(t1, &s1->rbn);
	}

	return 0;
 err_0:
	__src_stats_rbt_purge(t1);
	return rc;
}

void __stream_stats_free(struct ldms_stream_stats_s *ss)
{
	struct ldms_stream_client_pair_stats_s *ps;
	__src_stats_rbt_purge(&ss->src_stats_rbt);
	while ((ps = TAILQ_FIRST(&ss->pair_tq))) {
		TAILQ_REMOVE(&ss->pair_tq, ps, entry);
		free(ps);
	}
	free(ss);
}

/* readlock already taken */
struct ldms_stream_stats_s * __stream_get_stats(struct ldms_stream_s *s)
{
	/* s->name_len already includes '\0' */
	struct ldms_stream_stats_s *ss;
	struct ldms_stream_client_entry_s *sce;
	struct ldms_stream_client_pair_stats_s *ps;
	int rc;

	ss = malloc(sizeof(*ss) + s->name_len);
	if (!ss)
		goto err_0;
	ss->name = (char*)&ss[1];
	memcpy((char*)ss->name, s->name, s->name_len);
	TAILQ_INIT(&ss->pair_tq);
	rbt_init(&ss->src_stats_rbt, __ldms_addr_rbn_cmp);
	LDMS_STREAM_COUNTERS_INIT(&ss->rx);
	ss->rx = s->rx;

	rc = __src_stats_rbt_copy(&s->src_stats_rbt, &ss->src_stats_rbt);
	if (rc)
		goto err_1;

	TAILQ_FOREACH(sce, &s->client_tq, stream_client_entry) {
		/* match_len already includes '\0' */
		ps = malloc(sizeof(*ps) + sce->client->match_len + sce->client->desc_len);
		if (!ps)
			goto err_2;
		ps->stream_name = ss->name;
		ps->client_match = (char*)&ps[1];
		ps->client_desc = ps->client_match + sce->client->match_len;
		memcpy((char*)ps->client_match, sce->client->match,
				sce->client->match_len + sce->client->desc_len);
		ps->is_regex = sce->client->is_regex;
		ps->tx = sce->tx;
		ps->drops = sce->drops;
		TAILQ_INSERT_TAIL(&ss->pair_tq, ps, entry);
	}

	return ss;

 err_2:
	while ((ps = TAILQ_FIRST(&ss->pair_tq))) {
		TAILQ_REMOVE(&ss->pair_tq, ps, entry);
		free(ps);
	}
	__src_stats_rbt_purge(&ss->src_stats_rbt);
 err_1:
	free(ss);
 err_0:
	return NULL;
}

struct ldms_stream_stats_tq_s *ldms_stream_stats_tq_get(const char *match, int is_regex)
{
	regex_t r = {0};
	int free_reg = 0;
	struct rbn *rbn = NULL;
	struct ldms_stream_s *stream;
	struct ldms_stream_stats_s *stats;
	struct ldms_stream_stats_tq_s *tq = NULL;
	int rc;
	if (is_regex) {
		if (!match) {
			errno = EINVAL;
			goto out;
		}
		rc = regcomp(&r, match, REG_EXTENDED|REG_NOSUB);
		if (rc) {
			/* rc is REG_XXX, not errno */
			errno = EINVAL;
			goto out;
		}
		free_reg = 1;
	}

	tq = malloc(sizeof(*tq));
	if (!tq)
		goto out;
	TAILQ_INIT(tq);
	__STREAM_RDLOCK();
	if (match && !is_regex) {
		/* single stream matching */
		rbn = rbt_find(&__stream_rbt, match);
		if (!rbn)
			goto done;
		stream = container_of(rbn, struct ldms_stream_s, rbn);
		pthread_rwlock_rdlock(&stream->rwlock);
		stats = __stream_get_stats(stream);
		pthread_rwlock_unlock(&stream->rwlock);
		if (!stats)
			goto err_0;
		TAILQ_INSERT_TAIL(tq, stats, entry);
		goto done;
	}
	for (rbn = rbt_min(&__stream_rbt); rbn; rbn = rbn_succ(rbn)) {
		stream = container_of(rbn, struct ldms_stream_s, rbn);
		if (is_regex) {
			rc = regexec(&r, stream->name, 0, NULL, 0);
			if (rc)
				continue;
		}
		pthread_rwlock_rdlock(&stream->rwlock);
		stats = __stream_get_stats(stream);
		pthread_rwlock_unlock(&stream->rwlock);
		if (!stats)
			goto err_0;
		TAILQ_INSERT_TAIL(tq, stats, entry);
	}
 done:
	if (TAILQ_EMPTY(tq)) {
		errno = ENOENT;
		goto err_0;
	}
	__STREAM_UNLOCK();
	goto out;

 err_0:
	__STREAM_UNLOCK();
	ldms_stream_stats_tq_free(tq);
	tq = NULL;
	/* let through */
 out:
	if (free_reg)
		regfree(&r);
	return tq;
}

void ldms_stream_stats_tq_free(struct ldms_stream_stats_tq_s *tq)
{
	struct ldms_stream_stats_s *stats;
	if (!tq)
		return;
	while ((stats = TAILQ_FIRST(tq))) {
		TAILQ_REMOVE(tq, stats, entry);
		__stream_stats_free(stats);
	}
	free(tq);
}

int __counters_buff_append(struct ldms_stream_counters_s *ctr,
			   struct ovis_buff_s *buff)
{
	int rc = 0;
	rc = ovis_buff_appendf(buff, "{") ||
	     ovis_buff_appendf(buff, "\"bytes\": %lu", ctr->bytes) ||
	     ovis_buff_appendf(buff, ",\"count\": %lu", ctr->count) ||
	     ovis_buff_appendf(buff, ",\"first_ts\": %lu.%09lu", ctr->first_ts.tv_sec, ctr->first_ts.tv_nsec) ||
	     ovis_buff_appendf(buff, ",\"last_ts\": %lu.%09lu", ctr->last_ts.tv_sec, ctr->last_ts.tv_nsec) ||
	     ovis_buff_appendf(buff, "}");
	return rc;
}

int __src_stats_buff_append(struct ldms_stream_src_stats_s *src,
			      struct ovis_buff_s *buff)
{
	return __counters_buff_append(&src->rx, buff);
}

int __stream_stats_sources_buff_append(struct ldms_stream_stats_s *stats,
			  struct ovis_buff_s *buff)
{
	int rc;
	struct rbn *rbn;
	struct ldms_stream_src_stats_s *src;
	struct ldms_addr addr;
	char addr_buff[128] = "";
	const char *sep = "";
	rc = ovis_buff_appendf(buff, "{");
	if (rc)
		goto out;
	for (rbn = rbt_min(&stats->src_stats_rbt); rbn; rbn = rbn_succ(rbn)) {
		src = container_of(rbn, struct ldms_stream_src_stats_s, rbn);
		addr = src->src;
		ldms_addr_ntop(&addr, addr_buff, sizeof(addr_buff));
		rc = ovis_buff_appendf(buff, "%s\"%s\":",sep, addr_buff);
		if (rc)
			goto out;
		rc = __src_stats_buff_append(src, buff);
		if (rc)
			goto out;
		sep = ",";
	}
	rc = ovis_buff_appendf(buff, "}");
 out:
	return rc;
}

int __client_pair_stats_buff_append(struct ldms_stream_client_pair_stats_s *ent,
				    struct ovis_buff_s *buff)
{
	int rc;
	rc = ovis_buff_appendf(buff, "{") ||
	     ovis_buff_appendf(buff, "\"stream_name\":\"%s\"", ent->stream_name) ||
	     ovis_buff_appendf(buff, ",\"client_match\":\"%s\"", ent->client_match) ||
	     ovis_buff_appendf(buff, ",\"client_desc\":\"%s\"", ent->client_desc) ||
	     ovis_buff_appendf(buff, ",\"is_regex\": %d", ent->is_regex) ||
	     ovis_buff_appendf(buff, ",\"drops\":") ||
	     __counters_buff_append(&ent->drops, buff) ||
	     ovis_buff_appendf(buff, ",\"tx\":") ||
	     __counters_buff_append(&ent->tx, buff) ||
	     ovis_buff_appendf(buff, "}");
	return rc;
}

int __pair_tq_buff_append(struct ldms_stream_client_pair_stats_tq_s *tq,
			  struct ovis_buff_s *buff)
{
	int rc;
	const char *sep = "";
	struct ldms_stream_client_pair_stats_s *ent;
	rc = ovis_buff_appendf(buff, "[");
	if (rc)
		goto out;
	TAILQ_FOREACH(ent, tq, entry) {
		rc = ovis_buff_appendf(buff, "%s", sep) ||
		     __client_pair_stats_buff_append(ent, buff);
		if (rc)
			goto out;
		sep = ",";
	}
	rc = ovis_buff_appendf(buff, "]");
 out:
	return rc;
}

int __stream_stats_buff_append(struct ldms_stream_stats_s *stats,
			       struct ovis_buff_s *buff)
{
	int rc = 0;
	rc = ovis_buff_appendf(buff, "{") ||
	     ovis_buff_appendf(buff, "\"name\":\"%s\"", stats->name) ||
	     ovis_buff_appendf(buff, ",\"rx\":") ||
	     __counters_buff_append(&stats->rx, buff) ||
	     ovis_buff_appendf(buff, ",\"sources\":") ||
	     __stream_stats_sources_buff_append(stats, buff) ||
	     ovis_buff_appendf(buff, ",\"clients\":") ||
	     __pair_tq_buff_append(&stats->pair_tq, buff) ||
	     ovis_buff_appendf(buff, "}");
	return rc;
}

char *ldms_stream_stats_tq_to_str(struct ldms_stream_stats_tq_s *tq)
{
	ovis_buff_t buff = ovis_buff_new(4096);
	struct ldms_stream_stats_s *stats = NULL;
	char *ret = NULL;
	const char *sep = "";
	int rc;
	if (!buff)
		return NULL;

	rc = ovis_buff_appendf(buff, "[");
	if (rc)
		goto out;
	TAILQ_FOREACH(stats, tq, entry) {
		/* "stream_name": { ... } */
		rc = ovis_buff_appendf(buff, "%s", sep) ||
		     __stream_stats_buff_append(stats, buff);
		if (rc)
			goto out;
		sep = ",";
	}
	rc = ovis_buff_appendf(buff, "]");
	if (rc)
		goto out;

	ret = ovis_buff_str(buff);
	/* let through */
 out:
	if (buff)
		ovis_buff_free(buff);
	return ret;
}

char *ldms_stream_stats_str(const char *match, int is_regex)
{
	struct ldms_stream_stats_tq_s *tq = NULL;
	char *ret = NULL;

	tq = ldms_stream_stats_tq_get(match, is_regex);
	if (!tq) {
		if (errno == ENOENT) {
			ret = malloc(3);
			if (ret)
				memcpy(ret, "[]", 3);
			return ret;
		}
		return NULL;
	}
	ret = ldms_stream_stats_tq_to_str(tq);
	ldms_stream_stats_tq_free(tq);
	return ret;
}

struct ldms_stream_client_stats_s *
ldms_stream_client_get_stats(ldms_stream_client_t cli)
{
	struct ldms_stream_client_stats_s *cs = NULL;
	struct ldms_stream_client_entry_s *sce;
	struct ldms_stream_client_pair_stats_s *cps;
	cs = malloc(sizeof(*cs) + cli->match_len + cli->desc_len); /* included '\0' */
	if (!cs)
		goto out;
	cs->match = (char*)&cs[1];
	cs->desc = cs->match + cli->match_len;
	memcpy((char*)cs->match, cli->match, cli->match_len + cli->desc_len);
	TAILQ_INIT(&cs->pair_tq);

	pthread_rwlock_rdlock(&cli->rwlock);
	cs->dest = cli->dest;
	cs->drops = cli->drops;
	cs->tx = cli->tx;
	cs->is_regex = cli->is_regex;

	TAILQ_FOREACH(sce, &cli->stream_tq, client_stream_entry) {
		/* name_len included '\0' */
		cps = malloc(sizeof(*cps) + sce->stream->name_len);
		if (!cps)
			goto err_0;
		cps->client_match = cs->match;
		cps->client_desc = cs->desc;
		cps->stream_name = (char*)&cps[1];
		memcpy((char*)cps->stream_name, sce->stream->name,
				sce->stream->name_len);
		cps->is_regex = sce->client->is_regex;
		cps->tx = sce->tx;
		cps->drops = sce->drops;
		TAILQ_INSERT_TAIL(&cs->pair_tq, cps, entry);
	}

	pthread_rwlock_unlock(&cli->rwlock);
	goto out;

 err_0:
	pthread_rwlock_unlock(&cli->rwlock);
	ldms_stream_client_stats_free(cs);
	cs = NULL;
 out:
	return cs;
}

void ldms_stream_client_stats_free(struct ldms_stream_client_stats_s *cs)
{
	struct ldms_stream_client_pair_stats_s *cps;
	if (!cs)
		return;
	while ((cps = TAILQ_FIRST(&cs->pair_tq))) {
		TAILQ_REMOVE(&cs->pair_tq, cps, entry);
		free(cps);
	}
	free(cs);
}

struct ldms_stream_client_stats_tq_s *ldms_stream_client_stats_tq_get()
{
	struct ldms_stream_client_stats_tq_s *tq;
	struct ldms_stream_client_stats_s *cs;
	struct rbn *rbn;
	struct ldms_stream_s *s;
	ldms_stream_client_t cli;
	struct ldms_stream_client_entry_s *sce;

	tq = malloc(sizeof(*tq));
	if (!tq)
		goto out;
	TAILQ_INIT(tq);

	__STREAM_RDLOCK();

	/* go through regex clients first */
	TAILQ_FOREACH(cli, &__regex_client_tq, entry) {
		cs = ldms_stream_client_get_stats(cli);
		if (!cs)
			goto err_0;
		TAILQ_INSERT_TAIL(tq, cs, entry);
	}

	/* then go through stream-specific clients */
	for (rbn = rbt_min(&__stream_rbt); rbn; rbn = rbn_succ(rbn)) {
		s = container_of(rbn, struct ldms_stream_s, rbn);
		pthread_rwlock_rdlock(&s->rwlock);
		TAILQ_FOREACH(sce, &s->client_tq, stream_client_entry) {
			cli = sce->client;
			if (!cli)
				continue;
			if (cli->is_regex)
				continue; /* already handled above */
			cs = ldms_stream_client_get_stats(cli);
			if (!cs) {
				pthread_rwlock_unlock(&s->rwlock);
				goto err_0;
			}
			TAILQ_INSERT_TAIL(tq, cs, entry);
		}
		pthread_rwlock_unlock(&s->rwlock);
	}

	if (TAILQ_EMPTY(tq)) {
		errno = ENOENT;
		goto err_0;
	}

	__STREAM_UNLOCK();
	goto out;

 err_0:
	__STREAM_UNLOCK();
	ldms_stream_client_stats_tq_free(tq);
	tq = NULL;
 out:
	return tq;
}

void ldms_stream_client_stats_tq_free(struct ldms_stream_client_stats_tq_s *tq)
{
	struct ldms_stream_client_stats_s *cs;
	if (!tq)
		return;
	while ((cs = TAILQ_FIRST(tq))) {
		TAILQ_REMOVE(tq, cs, entry);
		ldms_stream_client_stats_free(cs);
	}
	free(tq);
}

int __client_stats_buff_append(struct ldms_stream_client_stats_s *cs,
			       ovis_buff_t buff)
{
	int rc;
	char addr_buff[128];
	ldms_addr_ntop(&cs->dest, addr_buff, sizeof(addr_buff));
	rc = ovis_buff_appendf(buff, "{"
		"\"match\":\"%s\""
		",\"desc\":\"%s\""
		",\"dest\":\"%s\"",
		cs->match,
		cs->desc,
		addr_buff) ||
	     ovis_buff_appendf(buff, ",\"tx\":") ||
	     __counters_buff_append(&cs->tx, buff) ||
	     ovis_buff_appendf(buff, ",\"drops\":") ||
	     __counters_buff_append(&cs->drops, buff) ||
	     ovis_buff_appendf(buff, ",\"streams\":") ||
	     __pair_tq_buff_append(&cs->pair_tq, buff);
	if (rc)
		goto out;
	rc = ovis_buff_appendf(buff, "}");
 out:
	return rc;
}

char *ldms_stream_client_stats_tq_to_str(struct ldms_stream_client_stats_tq_s *tq)
{
	struct ldms_stream_client_stats_s *cs;
	ovis_buff_t buff = ovis_buff_new(4096);
	char *ret = NULL;
	const char *sep = "";
	int rc;

	if (!buff)
		goto out;
	rc = ovis_buff_appendf(buff, "[");
	if (rc)
		goto out;
	TAILQ_FOREACH(cs, tq, entry) {
		rc = ovis_buff_appendf(buff, "%s", sep) ||
		     __client_stats_buff_append(cs, buff);
		if (rc)
			goto out;
		sep = ",";
	}
	rc = ovis_buff_appendf(buff, "]");
	if (rc)
		goto out;
	ret = ovis_buff_str(buff);
 out:
	if (buff)
		ovis_buff_free(buff);
	return ret;
}

char *ldms_stream_client_stats_str()
{
	struct ldms_stream_client_stats_tq_s *tq;
	char *ret = NULL;
	tq = ldms_stream_client_stats_tq_get();
	if (!tq) {
		if (errno == ENOENT) {
			ret = strdup("[]");
			return ret;
		}
		return NULL;
	}
	ret = ldms_stream_client_stats_tq_to_str(tq);
	ldms_stream_client_stats_tq_free(tq);
	return ret;
}

int ldms_stream_publish_file(ldms_t x, const char *stream_name,
                        ldms_stream_type_t stream_type,
			ldms_cred_t cred, uint32_t perm,
			FILE *f)
{
	int fd;
	int rc = 0;
	struct stat st;
	size_t sz;
	char *buff = NULL;
	struct ovis_buff_s *obuff = NULL;
	char lbuf[4096];

	if (!f) {
		rc = EINVAL;
		goto out;
	}

	fd = fileno(f);
	rc = fstat(fd, &st);
	if (rc) {
		rc = errno;
		goto out;
	}

	switch (st.st_mode & S_IFMT) {
	case S_IFCHR:
	case S_IFIFO:
		goto fstream;
	case S_IFREG:
		goto regular_file;
	default:
		rc = ENOTSUP;
		goto out;
	}

 fstream: /* e.g. stdin tty or PIPE */
	obuff = ovis_buff_new(4096);
	if (!obuff) {
		rc = errno;
		goto out;
	}
	while (fgets(lbuf, sizeof(lbuf), f)) {
		rc = ovis_buff_appendf(obuff, "%s", lbuf);
		if (rc)
			goto out;
	}
	buff = ovis_buff_str(obuff);
	sz = strlen(buff) + 1;
	goto publish;

 regular_file: /* regular file */
	buff = malloc(st.st_size+1);
	if (!buff) {
		rc = errno;
		goto out;
	}
	sz = fread(buff, 1, st.st_size, f);
	if (sz != st.st_size) {
		rc = errno;
		goto out;
	}
	buff[st.st_size] = 0; /* '\0' */
	sz = st.st_size + 1;
	/* let through */
 publish:
	rc = ldms_stream_publish(x, stream_name, stream_type, cred, perm,
			buff, sz);
 out:
	if (obuff)
		ovis_buff_free(obuff);
	if (buff)
		free(buff);
	return rc;
}

__attribute__((constructor))
static void __ldms_stream_init()
{
	static int once = 0;
	if (once)
		return ;
	__ldms_stream_log = ovis_log_register("ldms.stream", "LDMS Stream Library");
	once = 1;
}
