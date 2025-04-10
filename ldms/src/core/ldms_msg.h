/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2022-2025 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2022-2025 Open Grid Computing, Inc. All rights reserved.
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
 * \file ldms_msg.h
 */

#ifndef __LDMS_MSG_H__
#define __LDMS_MSG_H__

#include <sys/types.h>
#include <regex.h>
#include <sys/queue.h>
#include <pthread.h>

#include "ovis_ref/ref.h"
#include "coll/rbt.h"
#include "ldms.h"
#include "ldms_rail.h"

/* private structures / functions for LDMS Message Service */

struct ldms_msg_ch_s {
	struct rbn rbn;
	pthread_rwlock_t rwlock; /* protects cli_tq */
	TAILQ_HEAD(, ldms_msg_ch_cli_entry_s) cli_tq;
	int name_len;
	struct ldms_msg_counters_s rx; /* total rx regardless of src */
	struct rbt src_stats_rbt; /* tree of statistics by src; the nodes are `struct ldms_msg_src_stats_s` */
	char name[OVIS_FLEX];
};

/* channel-client relation */
struct ldms_msg_ch_cli_entry_s {
	struct ref_s ref;
	struct ldms_msg_ch_s *ch;
	struct ldms_msg_client_s *cli;

	/* For client->ch_tq */
	TAILQ_ENTRY(ldms_msg_ch_cli_entry_s) cli_ch_entry;

	/* For ch->cli_tq */
	TAILQ_ENTRY(ldms_msg_ch_cli_entry_s) ch_cli_entry;

	/* transmission-to-client counters for this channel */
	struct ldms_msg_counters_s tx;
	/* client drops counters for this channel */
	struct ldms_msg_counters_s drops;
};

struct ldms_msg_client_s {
	TAILQ_ENTRY(ldms_msg_client_s) entry; /* for __regex_client_tq */

	struct rbn rbn; /* for rail->ch_cli_rbt */

	struct ldms_msg_client_coll_s *coll;

	/* transmission-to-client counters regradless of channel */
	struct ldms_msg_counters_s tx;
	/* drops counters regradless of channel */
	struct ldms_msg_counters_s drops;

	pthread_rwlock_t rwlock;
	/* channels that this client subscribed for */
	TAILQ_HEAD(, ldms_msg_ch_cli_entry_s) ch_tq;

	struct ldms_addr dest;

	ldms_t x;
	ldms_msg_event_cb_t cb_fn;
	void *cb_arg;
	int is_regex;
	regex_t regex;
	struct ref_s ref;

	struct ldms_rail_rate_quota_s rate_quota;

	int desc_len;
	char *desc; /* a short description at &match[match_len] */
	int match_len; /* length of c->match[], including '\0' */
	char match[OVIS_FLEX]; /* exact name match or regex */
};

/* the full message service message */
struct ldms_msg_full_s {
	struct ldms_addr src;
	uint32_t pad0;
	uint64_t reserve[4];
	uint64_t msg_gn;
	uint32_t msg_len;
	uint32_t msg_type;
	struct ldms_cred cred; /* credential of the originator */
	uint32_t perm; /* 0777 style permission */
	uint32_t name_hash;
	/* Allocate space to collect profile data for 8 hops */
	uint32_t hop_cnt;
	uint32_t pad1;
	struct ldms_msg_hop hops[LDMS_MSG_MAX_PROFILE_HOPS+1];
	char     msg[OVIS_FLEX];
	/* `msg` format:
	 * .----------------------.
	 * | name (char[])        |
	 * |----------------------|
	 * | data (char[]) |
	 * '----------------------'
	 */
};

struct __sbuf_key_s {
	struct ldms_addr src;
	uint64_t msg_gn;
};

struct __msg_buf_s {
	struct rbn rbn;
	struct __sbuf_key_s key;
	struct ref_s ref;
	struct ldms_rail_ep_s *rep;
	size_t full_msg_len;
	off_t  off;
	const char *name;
	const char *data;
	uint32_t name_len;
	uint32_t data_len;
	union {
		struct ldms_msg_full_s msg[0];
		char buf[0];
	};
};

/* for internal use */
int __rep_publish(struct ldms_rail_ep_s *rep, const char *name,
			uint32_t hash, ldms_msg_type_t msg_type,
			struct ldms_addr *src, uint64_t msg_gn,
			ldms_cred_t cred, int perm,
			uint32_t hop_cnt,
			struct ldms_msg_hop *hops,
			const char *data, size_t data_len,
			struct strm_publish_profile_s *pts);
#endif /* __LDMS_MSG_H__ */
