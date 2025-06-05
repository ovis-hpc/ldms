/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2018,2023 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2018,2023 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2025 Lawrence Livermore National Security, LLC
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
#ifndef __LDMSD_H__
#define __LDMSD_H__
#include <limits.h>
#include <regex.h>
#include <sys/queue.h>
#include <time.h>
#include <pthread.h>

#include <sys/time.h>
#include <jansson.h>

#include <ovis_event/ovis_event.h>
#include <ovis_util/util.h>
#include "ovis_log/ovis_log.h"
#include "ovis_ref/ref.h"
#include "ldms.h"
#include "ldmsd_plug_api.h"

typedef void *ldmsd_plug_handle_t;

#define LDMSD_PLUGIN_LIBPATH_DEFAULT PLUGINDIR

/*
 * LDMSD plugin interface version
 *
 * A plugin implemented with a plugin interface version that
 * its major and/or minor numbers different from the version numbers below
 * may not be loaded or started.
 */
#define LDMSD_VERSION_MAJOR	0x03
#define LDMSD_VERSION_MINOR	0x02
#define LDMSD_VERSION_PATCH	0x02
#define LDMSD_VERSION_FLAGS	0x00

#define LDMSD_DEFAULT_FILE_PERM 0600

#define LDMSD_FAILOVER_NAME_PREFIX "#"

struct ldmsd_version {
	uint8_t major;
	uint8_t minor;
	uint8_t patch;
	uint8_t flags;
};

/** Get the ldmsd version  */
void ldmsd_version_get(struct ldmsd_version *v);

#define LDMSD_STR_WRAP(NAME) #NAME

/** Update hint */
#define LDMSD_SET_INFO_UPDATE_HINT_KEY "updt_hint_us"
#define LDMSD_UPDT_HINT_OFFSET_NONE LONG_MIN

typedef struct ldmsd_plugin_set {
	ldms_set_t set;
	char *cfg_name;	/* The config instance name */
	LIST_ENTRY(ldmsd_plugin_set) entry;
} *ldmsd_plugin_set_t;
typedef struct ldmsd_plugin_set_list {
	struct rbn rbn;
	LIST_HEAD(, ldmsd_plugin_set) list;
} *ldmsd_plugin_set_list_t;

/** Set information */
#define LDMSD_SET_INFO_INTERVAL_KEY "interval_us"
#define LDMSD_SET_INFO_OFFSET_KEY "offset_us"

/** Request that the task stop */
#define LDMSD_TASK_F_STOP		0x01
/** Use 'synchronous' scheduling. This is set when offset_us is !0 */
#define LDMSD_TASK_F_SYNCHRONOUS	0x02
/** Ignore the schedule interval for the initial call to task_fn */
#define LDMSD_TASK_F_IMMEDIATE		0x04

struct ldmsd_task;
typedef void (*ldmsd_task_fn_t)(struct ldmsd_task *, void *arg);
typedef struct ldmsd_task {
	int thread_id;
	int flags;
	long sched_us;
	long offset_us;
	pthread_mutex_t lock;
	pthread_cond_t join_cv;
	struct timeval timeout;
	enum ldmsd_task_state {
		LDMSD_TASK_STATE_STOPPED,
		LDMSD_TASK_STATE_STARTED,
		LDMSD_TASK_STATE_RUNNING
	} state;
	ldmsd_task_fn_t fn;
	void *fn_arg;
	ovis_scheduler_t os;
	struct ovis_event_s oev;
} *ldmsd_task_t;

typedef struct ldmsd_sec_ctxt {
	struct ldms_cred crd;
} *ldmsd_sec_ctxt_t;

typedef enum ldmsd_cfgobj_type {
	LDMSD_CFGOBJ_FIRST = 1,
	LDMSD_CFGOBJ_PRDCR = LDMSD_CFGOBJ_FIRST,
	LDMSD_CFGOBJ_UPDTR,
	LDMSD_CFGOBJ_STRGP,
	LDMSD_CFGOBJ_LISTEN,
	LDMSD_CFGOBJ_AUTH,
	LDMSD_CFGOBJ_PRDCR_LISTEN,
	LDMSD_CFGOBJ_SAMPLER,
	LDMSD_CFGOBJ_STORE,
	LDMSD_CFGOBJ_LAST = LDMSD_CFGOBJ_STORE,
} ldmsd_cfgobj_type_t;

struct ldmsd_cfgobj;
typedef void (*ldmsd_cfgobj_del_fn_t)(struct ldmsd_cfgobj *);

#define LDMSD_PERM_UEX 0100
#define LDMSD_PERM_UWR 0200
#define LDMSD_PERM_URD 0400
#define LDMSD_PERM_GEX 0010
#define LDMSD_PERM_GWR 0020
#define LDMSD_PERM_GRD 0040
#define LDMSD_PERM_OEX 0001
#define LDMSD_PERM_OWR 0002
#define LDMSD_PERM_ORD 0004

/* for deferred start */
#define LDMSD_PERM_DSTART 01000

/* for failover internal requests */
#define LDMSD_PERM_FAILOVER_INTERNAL 02000

/* can execute even if the failover is turned on */
#define LDMSD_PERM_FAILOVER_ALLOWED 04000

struct attr_value_list;
struct avl_q_item {
	struct attr_value_list *av_list;
	TAILQ_ENTRY(avl_q_item) entry;
};
TAILQ_HEAD(avl_q, avl_q_item);

typedef struct ldmsd_cfgobj {
	char *name;		/* Unique cfgobj name */
	struct ref_s ref;
	ldmsd_cfgobj_type_t type;
	ldmsd_cfgobj_del_fn_t __del;
	struct rbn rbn;
	pthread_mutex_t lock;
	uid_t uid;
	gid_t gid;
	int perm;
	char *avl_str;
	char *kvl_str;
} *ldmsd_cfgobj_t;

typedef struct ldmsd_prdcr_stream_s {
	char *name;
	char *msg;
	int64_t rate;
	LIST_ENTRY(ldmsd_prdcr_stream_s) entry;
} *ldmsd_prdcr_stream_t;

/**
 * Producer: Named instance of a remote LDMSD
 *
 * The Producer name, by policy, equals the name of this configuration object.
 */
typedef struct ldmsd_prdcr {
	struct ldmsd_cfgobj obj;

	/* Controls hostname resolution caching behavior (user configurable)
	 * 1 = (default) Cache hostname after first successfull resolution
	 * 0 = Resolve hostname on every connection
	 */
	uint8_t cache_ip;
	struct sockaddr_storage ss;	/* Host address */
	socklen_t ss_len;
	char *host_name;	/* Host name */
	unsigned short port_no;		/* Port number */
	char *xprt_name;	/* Transport name */
	ldms_t xprt;
	long conn_intrvl_us;	/* connect interval */
	char *conn_auth_dom_name;		/* auth domain name */
	char *conn_auth;			/* auth plugin for the connection */
	struct attr_value_list *conn_auth_args;  /* auth options of the connection auth */

	enum ldmsd_prdcr_state {
		/** Producer task has stopped & no outstanding xprt */
		LDMSD_PRDCR_STATE_STOPPED,
		/** Ready for connect attempts (no outstanding xprt) */
		LDMSD_PRDCR_STATE_DISCONNECTED,
		/** Connection request is outstanding */
		LDMSD_PRDCR_STATE_CONNECTING,
		/** Connect complete, and ready to send a dir request */
		LDMSD_PRDCR_STATE_CONNECTED,
		/** Waiting for task join and xprt cleanup */
		LDMSD_PRDCR_STATE_STOPPING,
		/** The STANDBY state is valid only for 'GENERATED' producers.
		 *
		 *  Producer task has been stopped but there is an outstanding xprt.
		 *
		 *  Once the aggregator receives an advertisement notification
		 *  and verifies that the hostname or IP address matches
		 *  a listen producer, it creates a generated producer,
		 *  maps the producer to the request's transport, moves
		 *  the producer state to STANDBY, and then starts the producer.
		 *
		 *  The producer synchronously moves to 'CONNECTED' when it starts.
		 *
		 *  prdcr_stop does not tear down the connection.
		 *  The producer's transport is reset to NULL only when
		 *  the aggregator receives a 'disconnected' event either initiated by
		 *  the sampler daemon or the aggregator.
		 */
		LDMSD_PRDCR_STATE_STANDBY,
	} conn_state;

	enum ldmsd_prdcr_type {
		/** Connection initiated at this side */
		LDMSD_PRDCR_TYPE_ACTIVE,
		/** Connection initiated by peer */
		LDMSD_PRDCR_TYPE_PASSIVE,
		/** Producer is local to this daemon */
		LDMSD_PRDCR_TYPE_LOCAL,
		/** Connection initiated at this side but the peer will initiate the dir request. */
		/**
		 * Connection initiated at this side and the peer is aware of its existence.
		 * The peer will initiate the dir request after the connection is established.
		 */
		LDMSD_PRDCR_TYPE_BRIDGE,
		/**
		 * Connection initiated at this side to advertise itself to the peer.
		 * The peer does not know about its existence until it sends
		 * an advertise_notification request. The peer will initiate the dir request
		 * after the peer verifies its hostname.
		 */
		LDMSD_PRDCR_TYPE_ADVERTISER,
		/**
		 * The producer is generated by LDMSD upon receiving an
		 * advertise_notification request from a peer whose hostname
		 * or IP address matches the regular expression or the IP mask of
		 * a listening producer. LDMSD also starts the producer
		 * automatically after its creation, unless configured not
		 * to automatically start in the prdcr_listen line.
		 *
		 * Similarly to passive producers, the connection is initiated
		 * by an advertising peer. This side initiates the dir request.
		 */
		LDMSD_PRDCR_TYPE_ADVERTISED_PASSIVE,
		/**
		 * The producer is generated by LDMSD upon receiving an
		 * advertise_notification request from a peer whose hostname
		 * or IP address matches the regular expression or the IP mask of
		 * a listening producer. LDMSD also starts the producer
		 * automatically after its creation, unless configured not
		 * to automatically start in the prdcr_listen line.
		 *
		 * This advertised producer initiates a connection to the advertising peer,
		 * according to parameters specified in the matched listening producer.
		 * The connection that has delivered the advertise_notification request
		 * remains connected, and the advertisting peer uses it to
		 * determine when it to re-advertise once it is disconnected.
		 */
		LDMSD_PRDCR_TYPE_ADVERTISED_ACTIVE,
	} type;

	struct ldmsd_task task;

	/**
	 * list of subscribed streams from this producer
	 */
	LIST_HEAD(,ldmsd_prdcr_stream_s) stream_list;

	/**
	 * Maintains a tree of all metric sets available from this
	 * producer. It is a tree to allow quick lookup by the logic
	 * that handles dir_add and dir_del directory updates from the
	 * producer.
	 */
	struct rbt set_tree;
	/**
	 * Maintains a free of all metric sets with update hint
	 * available from this producer. It is a tree to allow
	 * quick lookup by the logic that handles update schedule.
	 */
	struct rbt hint_set_tree;

	int rail; /* the number of xprt in the rail */
	int64_t quota;
	int64_t rx_rate;
} *ldmsd_prdcr_t;

struct ldmsd_strgp;
typedef struct ldmsd_strgp *ldmsd_strgp_t;

typedef struct ldmsd_strgp_ref {
	ldmsd_strgp_t strgp;
	void *decomp_ctxt;
	LIST_ENTRY(ldmsd_strgp_ref) entry;
} *ldmsd_strgp_ref_t;

#define LDMSD_PRDCR_SET_F_PUSH_REG	1

typedef struct ldmsd_updt_hint_set_list {
	struct rbn rbn;
	LIST_HEAD(, ldmsd_prdcr_set) list;
} *ldmsd_updt_hint_set_list_t;

struct ldmsd_updtr_schedule {
	long intrvl_us;
	long offset_us;
};
typedef struct ldmsd_updtr *ldmsd_updtr_ptr;

struct ldmsd_stat {
	struct timespec start;
	struct timespec end;
	double min;
	struct timespec min_ts;
	double max;
	struct timespec max_ts;
	double avg;
	int count;
};

#define LDMSD_PRDSET_STATS_F_UPD 1
#define LDMSD_PRDSET_STATS_F_STORE 2
typedef struct ldmsd_prdcr_set {
	char *inst_name;
	char *schema_name;
	ldmsd_prdcr_t prdcr;
	ldms_set_t set;
	int push_flags;
	enum ldmsd_prdcr_set_state {
		LDMSD_PRDCR_SET_STATE_START,
		LDMSD_PRDCR_SET_STATE_LOOKUP,
		LDMSD_PRDCR_SET_STATE_READY,
		LDMSD_PRDCR_SET_STATE_UPDATING,
		LDMSD_PRDCR_SET_STATE_DELETED
	} state;
	uint64_t last_gn;
	pthread_mutex_t lock;
	LIST_HEAD(ldmsd_strgp_ref_list, ldmsd_strgp_ref) strgp_list;
	struct rbn rbn;

	LIST_ENTRY(ldmsd_prdcr_set) updt_hint_entry;

	struct ldmsd_updtr_schedule updt_hint;

	int updt_interval;
	int updt_offset;
	uint8_t updt_sync;

	struct ldmsd_stat updt_stat;
	struct ldmsd_stat store_stat;
	int skipped_upd_cnt;
	int oversampled_cnt;
	uint64_t zap_thread_id; /* A thread handling the update completion event. */

	int ref_count;
	struct timespec lookup_complete_ts;
} *ldmsd_prdcr_set_t;

typedef struct ldmsd_prdcr_ref {
	ldmsd_prdcr_t prdcr;
	struct rbn rbn;
} *ldmsd_prdcr_ref_t;

/**
 * Listening Producer: Named set of conditions of LDMS metric set providers
 */
typedef struct ldmsd_prdcr_listen {
	struct ldmsd_cfgobj obj;
	enum ldmsd_listen_prdcr_state_e {
		/** Initial listen producer state */
		LDMSD_PRDCR_LISTEN_STATE_STOPPED = 0,
		/** Ready for handling advertise_notification and generating producer */
		LDMSD_PRDCR_LISTEN_STATE_RUNNING,
	} state;
	const char *hostname_regex_s;
	regex_t regex;
	int auto_start; /* default is 1, i.e., auto start producers */

	/* Network Address & prefix_len from a given CIDR IP address string */
	const char *cidr_str; /* IP Range */
	struct ldms_addr net_addr;
	int prefix_len;

	/* Advertised Producer Properties */
	uint64_t quota;
	uint64_t rx_rate;

	enum ldmsd_prdcr_type prdcr_type; /* Advertised producer type */

	/* -- Active Mode Properties -- */
	int rail; /* Rail size of advertised producers */
	long reconnect; /* Reconnect interval of the advertised producers */
	const char *advtr_xprt; /* Transport for advertised producers to connect to advertiser */
	/*
	 * For the 'active' mode, `advtr_port` is the listening port on the advertiser
	 * for the advertised producers to connect to. If this is 0, the aggregator gets
	 * the listening port information from the advertisement message from the advertiser.
	 */
	unsigned short advtr_port; /* The listening port for advertised producers to connect to */
	/*
	 * The authentication domain to be used by advertised producers to connect to advertisers.
	 *
	 * If it is NULL, the default authentication domain is used. Otherwise,
	 * the authentication domain must be added using 'auth_add'.
	 */
	const char *auth;

	/*
	 * For query the prdcr_listen information, ldmsd could report which
	 * producers were added because their hostnames match the regex of
	 * this prdcr_listen.
	 */
	struct rbt prdcr_tree;
} *ldmsd_prdcr_listen_t;

/**
 * Updater: Named set of rules for updating remote metric sets
 *
 * The prdcr_list specifies the set of LDMS from which to gather
 * metric sets. The match_list specifies which metric sets from each
 * producer will be updated. If the match_list is empty, all metric
 * sets on each producer will be updated.
 *
 */
#define LDMSD_UPDTR_F_PUSH		1
#define LDMSD_UPDTR_F_PUSH_CHANGE	2
#define LDMSD_UPDTR_OFFSET_INCR_DEFAULT	100000
#define LDMSD_UPDTR_OFFSET_INCR_VAR	"LDMSD_UPDTR_OFFSET_INCR"

struct ldmsd_updtr;
typedef struct ldmsd_updtr_task {
	struct ldmsd_updtr *updtr;
	int is_default;
	struct ldmsd_task task;
	int task_flags;
	struct ldmsd_updtr_schedule hint; /* Hint from producer set */
	struct ldmsd_updtr_schedule sched; /* actual schedule */
	int set_count;
	struct rbn rbn;
	LIST_ENTRY(ldmsd_updtr_task) entry; /* Entry in the list of to-be-deleted tasks */
} *ldmsd_updtr_task_t;
LIST_HEAD(ldmsd_updtr_task_list, ldmsd_updtr_task);

struct ldmsd_name_match;
typedef struct ldmsd_updtr {
	struct ldmsd_cfgobj obj;

	int push_flags;

	enum ldmsd_updtr_state {
		/** Initial updater state */
		LDMSD_UPDTR_STATE_STOPPED = 0,
		/** Ready for update attempts */
		LDMSD_UPDTR_STATE_RUNNING,
		/** Stopping, waiting for callback tasks to finish */
		LDMSD_UPDTR_STATE_STOPPING,
	} state;

	/* The list of regular expressions to match producer names. */
	LIST_HEAD(updtr_prdcr_filter, ldmsd_name_match) prdcr_filter;

	/*
	 * flag to enable or disable the functionality
	 * that automatically schedules set updates according to
	 * the update hint.
	 *
	 * 0 is disabled. Otherwise, it is enabled.
	 *
	 * If this value is 0, \c task_tree must contain
	 * only the default task.
	 */
	uint8_t is_auto_task;

	/* The default schedule specified from configuration */
	struct ldmsd_updtr_task default_task;
	/*
	 * All tasks here don't have the same schedule as the root task.
	 * The key is interval and offset hint.
	 */
	struct rbt task_tree;
	/* Task to cleanup useless tasks from the task tree */
	struct ldmsd_updtr_task tree_mgmt_task;

	/*
	 * For quick search when query for updater that updates a prdcr_set.
	 */
	struct rbt prdcr_tree;
	LIST_HEAD(updtr_match_list, ldmsd_name_match) match_list;
} *ldmsd_updtr_t;

typedef struct ldmsd_name_match {
	/** Regular expresion matching schema or instance name */
	char *regex_str;
	regex_t regex;

	/** see man recomp */
	int regex_flags;

	enum ldmsd_name_match_sel {
		LDMSD_NAME_MATCH_INST_NAME,
		LDMSD_NAME_MATCH_SCHEMA_NAME,
	} selector;

	LIST_ENTRY(ldmsd_name_match) entry;
} *ldmsd_name_match_t;

/** Storage Policy: Defines which producers and metrics are
 * saved when an update completes. Must include meta vs data metric flags.
 */
typedef void *ldmsd_store_handle_t;
typedef struct ldmsd_strgp_metric {
	char *name;
	enum ldms_value_type type;
	int flags;
	TAILQ_ENTRY(ldmsd_strgp_metric) entry;
} *ldmsd_strgp_metric_t;

typedef struct ldmsd_row_group_s {
	ldmsd_strgp_t strgp;
	int row_key_count;
	struct rbt row_tree;	/* Tree of ldmsd_row_cache_entry_t */
	struct rbn rbn;
	LIST_ENTRY( ldmsd_row_group_s ) bucket_entry;
	struct timespec last_update; /* informational */
} *ldmsd_row_group_t;

typedef struct ldmsd_row_cache_s {
	ldmsd_strgp_t strgp;
	int row_limit;
	struct rbt group_tree;	/* Tree of ldmsd_row_group_t */
	pthread_mutex_t lock;
	LIST_HEAD(, ldmsd_row_group_s) group_bucket[3];
	int gb_idx; /* current group bucket index: 0, 1, or 2 */
	struct timespec bucket_ts; /* timestamp to trigger the bucket change */
	struct timespec cfg_timeout; /* timeout for each bucket */
} *ldmsd_row_cache_t;

typedef struct ldmsd_row_s *ldmsd_row_t;
typedef struct ldmsd_row_cache_idx_s *ldmsd_row_cache_idx_t;
typedef struct ldmsd_row_cache_entry_s {
	ldmsd_row_t row;
	ldmsd_row_cache_idx_t idx;
	struct rbn rbn;
} *ldmsd_row_cache_entry_t;

typedef struct ldmsd_row_cache_key_s {
	enum ldms_value_type type;
	size_t count;			/* The element count if an array */
	size_t mval_size;
	ldms_mval_t mval;
} *ldmsd_row_cache_key_t;

struct ldmsd_row_cache_idx_s {
	int key_count;
	ldmsd_row_cache_key_t *keys;	/* Array of ldmsd_row_cache_key_t */
};

typedef struct ldmsd_row_s *ldmsd_row_t;
typedef struct ldmsd_row_list_s *ldmsd_row_list_t;

ldmsd_row_cache_t ldmsd_row_cache_create(ldmsd_strgp_t strgp, int row_count,
					 struct timespec *timeout);
ldmsd_row_cache_key_t ldmsd_row_cache_key_create(enum ldms_value_type type, size_t len);
ldmsd_row_cache_idx_t ldmsd_row_cache_idx_create(int key_count, ldmsd_row_cache_key_t *keys);
void ldmsd_row_cache_idx_free(ldmsd_row_cache_idx_t idx);
int ldmsd_row_cache(ldmsd_row_cache_t rcache,
		ldmsd_row_cache_idx_t group_key,
		ldmsd_row_cache_idx_t row_key,
		ldmsd_row_t row);
ldmsd_row_t ldmsd_row_dup(ldmsd_row_t);
int ldmsd_row_cache_make_list(ldmsd_row_list_t row_list, int row_count,
	ldmsd_row_cache_t cache, ldmsd_row_cache_idx_t group_key);

typedef void (*strgp_update_fn_t)(ldmsd_strgp_t strgp, ldmsd_prdcr_set_t prd_set, void **ctxt);
typedef struct ldmsd_cfgobj_store *ldmsd_cfgobj_store_t;

struct ldmsd_strgp {
	 struct ldmsd_cfgobj obj;

	/** A set of match strings to select a subset of all producers */
	LIST_HEAD(ldmsd_strgp_prdcr_list, ldmsd_name_match) prdcr_list;

	/** A list of the names of the metrics in the set specified by schema */
	TAILQ_HEAD(ldmsd_strgp_metric_list, ldmsd_strgp_metric) metric_list;
	int metric_count;
	int *metric_arry;	/* Array of metric ids */

	/** Schema name of the metric set on the producer */
	char *schema;

	/** The digest of the first set used in opening the store */
	ldms_digest_t digest;

	/** The container name in which the storage backend will place data */
	char *container;

	/** The storage backend plugin */
	ldmsd_cfgobj_store_t store;
	LIST_ENTRY(ldmsd_strgp) store_entry;

	/** The open instance of the container */
	ldmsd_store_handle_t store_handle;
	ldmsd_store_handle_t next_store_handle;

	enum ldmsd_strgp_state {
		LDMSD_STRGP_STATE_STOPPED,
		LDMSD_STRGP_STATE_RUNNING
	} state;

	struct ldmsd_task task;	/* rotate open task */

	/** Flush interval */
	struct timespec flush_interval;
	struct timespec last_flush;

	/** Update function */
	strgp_update_fn_t update_fn;

	/** Decomposer resource handle */
	struct ldmsd_decomp_s *decomp;
	char *decomp_path;	/* path to decomposition configuration */

	/** Regular expression for the schema */
	regex_t schema_regex;
	char *regex_s;

	int prdset_cnt; /* Number of producer sets strgp stores */

	int row_cache_init;
	ldmsd_row_cache_t row_cache;
};


/* === Decompositions === */

typedef struct ldmsd_req_ctxt *ldmsd_req_ctxt_t;
typedef struct ldmsd_decomp_s *ldmsd_decomp_t;
typedef struct json_entity_s *json_entity_t;

/** Decomposition interface. */
struct ldmsd_decomp_s {

	/**
	 * Configuring the decomposition according to \c strgp and \c json_path.
	 *
	 * \c reqc is given so that the confiıg function can describe the errors
	 * back to the user (not just log file) in details.
	 *
	 * \param strgp
	 * \param json_path
	 * \param reqc
	 *
	 * \retval decomp The decomposition resource handle.
	 * \retval NULL   If there is an error. In this case, \c errno must also
	 *                be set to describe the error.
	 */
	ldmsd_decomp_t (*config)(ldmsd_strgp_t strgp, json_t *jcfg, ldmsd_req_ctxt_t reqc);

	/**
	 * Decompose method.
	 *
	 * The \c ctxt_ptr is an in/out parameter where \c *ctxt_ptr was
	 * initially set to \c NULL. The \c ctxt_ptr is a per-set context for
	 * \c strgp. If \c *ctxt_ptr is set, the value will be supplied in the
	 * next \c decompose() call of the same \c set for this \c strgp.
	 *
	 * When the set is deleted, \c decomp_ctxt_release() is called with
	 * \c *ctxt_ptr to let the decomposition clean up its context associated
	 * with the \c set.
	 *
	 * \param      strgp     The storage policy.
	 * \param      set       The LDMS set to be decomposed.
	 * \param      row_list  The list head to which the output rows are
	 *                       appended.
	 * \param[out] row_count The number of rows appended to the \c row_list.
	 * \param[in,out] ctxt_ptr The pointer to a context for the \c set for
	 *                         this \c strgp.
	 *
	 * \retval 0     If configure successfully.
	 * \retval errno If there is an error.
	 */
	int (*decompose)(ldmsd_strgp_t strgp, ldms_set_t set,
			 ldmsd_row_list_t row_list, int *row_count,
			 void **ctxt_ptr);

	/**
	 * Release resources of rows from \c decompose().
	 *
	 * When \c ldmsd is done with the rows in the storage routine, it calls
	 * \c strgp->release_rows() to let the decomposer releases the rows and
	 * their resources.
	 */
	void (*release_rows)(ldmsd_strgp_t strgp, ldmsd_row_list_t row_list);

	/**
	 * Release decomposer resources (\c strgp->decomp).
	 *
	 * This will be called in the \c strgp_del call chain.
	 */
	void (*release_decomp)(ldmsd_strgp_t strgp);

	/**
	 * Release the \c *ctxt_ptr.
	 *
	 * \param[in,out] ctxt_ptr The ctxt pointer.
	 */
	void (*decomp_ctxt_release)(ldmsd_strgp_t strgp, void **ctxt_ptr);
};

/*
 * Phony metric IDs are used in `struct ldmsd_col_s` construction when the
 * value of the column refers to a non-metric set data such as:
 *   - timestamp
 *   - producer
 *   - instance
 */
typedef enum ldmsd_phony_metric_id {
	LDMSD_PHONY_METRIC_ID_FIRST = 0x10000,
	LDMSD_PHONY_METRIC_ID_TIMESTAMP = LDMSD_PHONY_METRIC_ID_FIRST, /* "M_timestamp" */
	LDMSD_PHONY_METRIC_ID_PRODUCER, /* "M_producer" */
	LDMSD_PHONY_METRIC_ID_INSTANCE, /* "M_instance" */
	LDMSD_PHONY_METRIC_ID_UID,      /* "M_uid" */
	LDMSD_PHONY_METRIC_ID_GID,      /* "M_gid" */
	LDMSD_PHONY_METRIC_ID_PERM,     /* "M_perm" */
	LDMSD_PHONY_METRIC_ID_DURATION, /* "M_duration" */
	LDMSD_PHONY_METRIC_ID_DIGEST,   /* "M_digest" */
	LDMSD_PHONY_METRIC_ID_SCHEMA,   /* "M_schema" */
	LDMSD_PHONY_METRIC_ID_CARD,     /* "M_card" */

	LDMSD_PHONY_METRIC_ID_FILL = 0x20000, /* Special `FILL` */

	LDMSD_PHONY_METRIC_ID_UNKNOWN = -1,
} ldmsd_phony_metric_id_t;

/**
 * \brief Resolve \c str to phony metric ID.
 *
 * \retval id The corresponding phony metric ID.
 * \retval -1 If \c str does not resolve to any known phony metric ID.
 */
ldmsd_phony_metric_id_t ldmsd_phony_metric_resolve(const char *str);

__attribute__((unused)) /* compiler hush */
static int is_phony_metric_id(int metric_id)
{
	return metric_id >= LDMSD_PHONY_METRIC_ID_FIRST;
}

enum ldmsd_decomp_op {
	 LDMSD_DECOMP_OP_NONE = 0,
	 LDMSD_DECOMP_OP_DIFF = 1,
	 LDMSD_DECOMP_OP_MEAN = 2,
	 LDMSD_DECOMP_OP_MIN = 3,
	 LDMSD_DECOMP_OP_MAX = 4,
};

struct ldmsd_col_s {
	const char *name;          /* The column name */
	ldms_mval_t mval;          /* The metric value for this column */
	void *column;              /* The plugin’s column handle */
	enum ldms_value_type type; /* The LDMS metric type */
	int array_len;             /* if mval is an array */
	int metric_id;             /* metric ID */

	int rec_metric_id;         /* If this is in a record,
				      rec_metric_id >= 0.
				      metric_id in this case refers to
				      the list head containing the record. */
};
typedef struct ldmsd_col_s *ldmsd_col_t;

struct ldmsd_row_index_s {
	const char *name;    /* The name of the index */
	int col_count;       /* number of columns comprising the index */
	int pad;
	ldmsd_col_t cols[OVIS_FLEX]; /* reference to the columns comprising the index */
};
typedef struct ldmsd_row_index_s *ldmsd_row_index_t;

struct ldmsd_row_s {
	TAILQ_ENTRY(ldmsd_row_s) entry;
	void *schema;		 /* The storage plugin’s schema handle */
	const char *schema_name; /* The name of the schema from the
				    configuration. */
	const struct ldms_digest_s *schema_digest; /* LDMSD metric set digest */
	int idx_count; /* the number of indices */
	int col_count; /* The number of columns */
	ldmsd_row_index_t *indices; /* pointer to array of indices */
	uint8_t *mvals;	/* Ptr to memory that contains the mvals for each metric */
	struct ldmsd_col_s cols[OVIS_FLEX];
};

TAILQ_HEAD(ldmsd_row_list_s, ldmsd_row_s);

typedef struct ldmsd_req_ctxt *ldmsd_req_ctxt_t;

struct ldmsd_worker_thrstat_result {
	int count; /* Number of worker threads */
	struct ovis_scheduler_thrstats *entries[0];
};

/**
 * A utility to convert \c row to JSON array.
 *
 * The output format is in the form of JSON array as follows
 * \code
 *   [ COL_1_VAL, COL_2_VAL, COL_3_VAL, ..., COL_N_VAL ]
 * \endcode
 *
 * \param       row The row handle.
 * \param [out] str The output C string containing JSON array for the \c row.
 * \param [out] len The strlen() of \c *str.
 *
 * \retval 0     If succeded.
 * \retval errno If there is an error.
 */
int ldmsd_row_to_json_array(ldmsd_row_t row, char **str, int *len);

/**
 * Create a JSON text object from an ldmsd_row_t
 *
 * The user is responsible for freeing the allocated memory
 * returned in \c str.
 *
 * The output format is in the form of JSON object as follows
 * \code
 *   { "COL_1_NAME":COL_1_VAL, "COL_2_NAME":COL_2_VAL, ...,
 *     "COL_N_NAME":COL_N_VAL }
 * \endcode
 *
 * \param       row The row handle.
 * \param [out] str The output C string containing JSON object for the \c row.
 * \param [out] len The strlen() of \c *str.
 *
 * \retval 0     If succeded.
 * \retval errno If there is an error.
 */
int ldmsd_row_to_json_object(ldmsd_row_t row, char **str, int *len);

/**
 * Create an Avro schema definition from an ldmsd_row_t
 *
 * The user is responsible for freeing the allocated memory
 * returned in \c str.
 *
 * The JSON format schema object is defined to be used with
 * the following API:
 * \code
 * int avro_schema_from_json(const char *jsontext,
 *			     int32_t unused1,
 *			     avro_schema_t *schema);
 * \endcode
 *
 * See https://avro.apache.org/docs/1.11.1/specification/ for
 * a detailed specification of this format.
 *
 * \param       row The row handle.
 * \param [out] str The output C string containing JSON object for the \c row.
 * \param [out] len The strlen() of \c *str.
 *
 * \retval 0     If succeded.
 * \retval errno If there is an error.
 */
int ldmsd_row_to_json_avro_schema(ldmsd_row_t row, char **str, size_t *len);

/**
 * \brief ldmsd_avro_name_get
 *
 * Avro names may only contain the characters [A-Za-z0-9\\_\\-]. LDMS metric
 * names by contrast may characters outside this set. When creating Avro
 * schema, these LDMS names must be mapped a valid Avro name. The function
 * returns malloc'd memory that should be freed by the caller when no
 * long longer needed.
 *
 * \param ldms_name The LDMS metric name to be mapped to a valid Avro name
 * \return char* Pointer to the allocated buffer or NULL if ENOMEM
 */
char *ldmsd_avro_name_get(const char *ldms_name);

/**
 * Configure strgp decomposer.
 *
 * The decomposer shall use the \c strgp->decomp generic pointer
 * for the decomposer's resources associated with the \c strgp.
 *
 * \retval 0     If configure successfully.
 * \retval errno If there is an error.
 */
int ldmsd_decomp_config(ldmsd_strgp_t strgp, const char *json_path, ldmsd_req_ctxt_t reqc);

typedef struct ldmsd_xprt_ctxt {
	char *name;
} *ldmsd_xprt_ctxt_t;

int process_config_file(const char *path, int *lineno, int trust);

int process_config_str(char *config_str, int *lno, int trust);

char *process_yaml_config_file(const char *path, const char *dname);

#define LDMSD_PLUGIN_MULTI_INSTANCE 1 /* Set in flags to indicate MI capability */
#define LDMSD_MAX_PLUGIN_NAME_LEN 64
#define LDMSD_CFG_FILE_XPRT_MAX_REC 8192
/* This struct is owned by the plugin. ldmsd should never modify the contents. */
typedef struct ldmsd_plugin {
	enum ldmsd_plugin_type {
		LDMSD_PLUGIN_SAMPLER = 1,
		LDMSD_PLUGIN_STORE,
	} type;
	uint64_t flags;
	int (*config)(ldmsd_plug_handle_t handle,
		      struct attr_value_list *kwl,
		      struct attr_value_list *avl);
	const char *(*usage)(ldmsd_plug_handle_t handle);

	int (*constructor)(ldmsd_plug_handle_t handle);
	void (*destructor)(ldmsd_plug_handle_t handle);

	void *reserved[8]; /* reserved for future use */
} *ldmsd_plugin_t;

/* This struct is owned by the plugin. ldmsd should never modify the contents. */
struct ldmsd_store {
	struct ldmsd_plugin base;
	ldmsd_store_handle_t (*open)(ldmsd_plug_handle_t handle,
				     const char *container, const char *schema,
				     struct ldmsd_strgp_metric_list *metric_list);
	void (*close)(ldmsd_plug_handle_t handle, ldmsd_store_handle_t sh);
	int (*flush)(ldmsd_plug_handle_t handle, ldmsd_store_handle_t sh);
	int (*store)(ldmsd_plug_handle_t handle, ldmsd_store_handle_t sh,
		     ldms_set_t set, int *, size_t count);
	int (*commit)(ldmsd_plug_handle_t handle, ldmsd_strgp_t strgp, ldms_set_t set,
		      ldmsd_row_list_t row_list, int row_count);
};


/**
 * struct ldmsd_plugin_generic is used to track generic information common to
 * all plugins that implement the ldmsd_plugin_interface symbol.
 * NOTE: This is an ldmsd internals data structure.
 *       Not to be used from inside plugins.
 */
struct ldmsd_plugin_generic {
        struct ldmsd_plugin *api;
        char *name;
        char *libpath;
        void *dl_handle; /* handle that was returned by dlopen() */
};

typedef struct ldmsd_cfgobj_sampler *ldmsd_cfgobj_sampler_t;
typedef struct ldmsd_sampler {
	struct ldmsd_plugin base;
	int (*sample)(ldmsd_plug_handle_t handle);
} *ldmsd_sampler_plugin_t;

struct ldmsd_cfgobj_store {
	struct ldmsd_cfgobj cfg;
	struct ldmsd_plugin_generic *plugin;
	struct ldmsd_store *api;

	/* Set to 1 if the plugin has been configured */
	int configured;

	/* List of strgp that are using this store */
	LIST_HEAD(, ldmsd_strgp) strgp_list;

	/* Private context pointer, managed by plugin */
	void *context;
	/* ovis_log handle to use when logging plugin messages */
	ovis_log_t log;
};

typedef struct ldmsd_sampler_set {
	ldms_set_t set;
	ldmsd_cfgobj_sampler_t sampler;
	LIST_ENTRY(ldmsd_sampler_set) entry;
} *ldmsd_sampler_set_t;

struct ldmsd_cfgobj_sampler {
	struct ldmsd_cfgobj cfg;
	struct ldmsd_plugin_generic *plugin;
	struct ldmsd_sampler *api;

	/* Set to 1 if the plugin has been configured */
	int configured;

	/* Private context pointer, managed by plugin */
	void *context;
	/* ovis_log handle to use when logging plugin messages */
	ovis_log_t log;
	unsigned long sample_interval_us;
	long sample_offset_us;
	int thread_id;
	ovis_scheduler_t os;
	struct ovis_event_s oev;

	/* List of all sets associated with this configuration. See
	 * ldmsd_set_register
	 */
	LIST_HEAD(, ldmsd_sampler_set) set_list;
	int use_xthread; /* !0 if use exclusitve thread */
	pthread_t xthread; /* the exclusive thread */
};

#define LDMSD_DEFAULT_SAMPLE_INTERVAL 1000000
/** Metric name for component ids (u64). */
#define LDMSD_COMPID "component_id"
/** Metric name for job id number */
#define LDMSD_JOBID "job_id"

ldmsd_cfgobj_sampler_t ldmsd_sampler_add(const char *name,
					struct ldmsd_plugin_generic *plugin,
					ldmsd_cfgobj_del_fn_t __del,
					uid_t uid, gid_t gid, int perm);

/**
 * \brief ldmsd_set_register
 *
 * Register the metric set \c set with the ldmsd and associate the set
 * with the plugin \c plugin_name. After registration, the plugin_sets
 * configuration request will report the set as being provided by \c
 * plugin_name.
 *
 * This function is typically called by a plugin after creating a
 * metric set.
 *
 * \param set The set to register for the plugin
 * \param plugin_name The name of the plugin to associate with the set
 * \returns 0 on success
 */
int ldmsd_set_register(ldms_set_t set, const char *plugin_name);

/**
 * \brief ldmsd_set_deregister
 *
 * Stop associating the metric set \c set with the sampler
 * configuration \c cfg_name.  After de-registration, the
 * configuration command \c plugin_sets will no longer report \c set with
 * the sampler.
 *
 * This function is typically called by a sampler prior to calling
 * ldms_set_delete. This function is not thread safe and the caller
 * should ensure the sampler's set list isn't concurrently updated in
 * another thread.
 *
 * \param inst_name The set-name to de-register for the plugin configuration
 * \param cfg_name The name of the plugin configuration
 * \returns 0 on success
 */
void ldmsd_set_deregister(const char *inst_name, const char *cfg_name);

/**
 * \brief ldms_store
 *
 * A \c ldms_store encapsulates a storage strategy. For example,
 * MySQL, or SOS. Each strategy provides a library that exports the \c
 * ldms_store structure. This structure contains strategy routines for
 * initializing, configuring and storing metric set data to the
 * persistent storage used by the strategy. When a metric set is
 * sampled, if metrics in the set are associated with a storage
 * strategy, the sample is saved automatically by \c ldmsd.
 *
 * An \c ldms_store manages Metric Series. A Metric Series is a named,
 * grouped, and time ordered series of metric samples. A Metric Series
 * is indexed by Component ID, and Time.
 */

ldmsd_cfgobj_store_t ldmsd_store_add(const char *name,
				struct ldmsd_plugin_generic *plugin,
				ldmsd_cfgobj_del_fn_t __del,
				uid_t uid, gid_t gid, int perm);

/**
 * \brief Get the security context (uid, gid) of the daemon.
 *
 * \param [out] sctxt the security context output buffer.
 */
void ldmsd_sec_ctxt_get(ldmsd_sec_ctxt_t sctxt);

static inline ldmsd_store_handle_t
ldmsd_store_open(ldmsd_cfgobj_store_t store,
		 const char *container, const char *schema,
		 struct ldmsd_strgp_metric_list *metric_list)
{
	if (store->api->open)
		return store->api->open(store, container, schema, metric_list);
	return NULL;
}

static inline void
ldmsd_store_flush(ldmsd_cfgobj_store_t store, ldmsd_store_handle_t sh)
{
        if (store->api->flush)
                store->api->flush(store, sh);
}

static inline void
ldmsd_store_close(ldmsd_cfgobj_store_t store, ldmsd_store_handle_t sh)
{
	if (store->api->close)
		store->api->close(store, sh);
}

/* ldmsctl command callback function definition */
typedef int (*ldmsctl_cmd_fn_t)(char *, struct attr_value_list*, struct attr_value_list *);

#define LDMSCTL_LIST_PLUGINS	0    /* List Plugins */
#define LDMSCTL_LOAD_PLUGIN	1    /* Load Plugin */
#define LDMSCTL_TERM_PLUGIN	2    /* Term Plugin */
#define LDMSCTL_CFG_PLUGIN	3    /* Configure Plugin */
#define LDMSCTL_START_SAMPLER	4    /* Start Sampler */
#define LDMSCTL_STOP_SAMPLER	5    /* Stop Sampler */
#define LDMSCTL_INFO_DAEMON	9   /* Query daemon status */
#define LDMSCTL_SET_UDATA	10   /* Set user data of a metric */
#define LDMSCTL_EXIT_DAEMON	11   /* Shut down ldmsd */
#define LDMSCTL_ONESHOT_SAMPLE	13   /* Sample a set at a specific timestamp once */
#define LDMSCTL_SET_UDATA_REGEX 14   /* Set user data of metrics using regex and increment */
#define LDMSCTL_VERSION		15   /* Get LDMS version */
#define LDMSCTL_VERBOSE	16   /* Change the log level */

#define LDMSCTL_INCLUDE		17  /* Include another configuration file */
#define LDMSCTL_ENV		18  /* Set environment variable */
#define LDMSCTL_LOGROTATE	19  /* Rotate the log file */

#define LDMSCTL_PRDCR_ADD	20   /* Add a producer specification */
#define LDMSCTL_PRDCR_DEL	21   /* Disable a producer specification */
#define LDMSCTL_PRDCR_START	22
#define LDMSCTL_PRDCR_STOP	23
#define LDMSCTL_PRDCR_START_REGEX	24
#define LDMSCTL_PRDCR_STOP_REGEX	25

#define LDMSCTL_UPDTR_ADD	30   /* Add an updater specification */
#define LDMSCTL_UPDTR_DEL	31   /* Delete an updater specification */
#define LDMSCTL_UPDTR_MATCH_ADD 32
#define LDMSCTL_UPDTR_MATCH_DEL 33
#define LDMSCTL_UPDTR_PRDCR_ADD 34
#define LDMSCTL_UPDTR_PRDCR_DEL 35
#define LDMSCTL_UPDTR_START	38
#define LDMSCTL_UPDTR_STOP	39

#define LDMSCTL_STRGP_ADD		40
#define LDMSCTL_STRGP_DEL		41
#define LDMSCTL_STRGP_PRDCR_ADD		42
#define LDMSCTL_STRGP_PRDCR_DEL		43
#define LDMSCTL_STRGP_METRIC_ADD	44
#define LDMSCTL_STRGP_METRIC_DEL	45
#define LDMSCTL_STRGP_START		48
#define LDMSCTL_STRGP_STOP		49

#define LDMSCTL_LAST_COMMAND	LDMSCTL_STRGP_STOP

extern ldmsctl_cmd_fn_t cmd_table[LDMSCTL_LAST_COMMAND + 1];

#define LDMSD_CONTROL_SOCKNAME "ldmsd/control"

#define LDMSD_CONNECT_TIMEOUT		20000000 /* 20 Seconds */
#define LDMSD_INITIAL_CONNECT_TIMEOUT	500000  /* 1/2 second */

/*
 * Max length of error strings while ldmsd is being configured.
 */
#define LEN_ERRSTR 256
#define LDMSD_ENOMEM_MSG "Memory allocation failure\n"

int ldmsd_logrotate();
int ldmsd_plugins_usage(const char *plugin_name);
void ldmsd_mm_status(int level, const char *prefix);

char *ldmsd_get_max_mem_sz_str();

/** Configuration object management */
void ldmsd_cfgobj___del(ldmsd_cfgobj_t obj);
void ldmsd_cfg_lock(ldmsd_cfgobj_type_t type);
void ldmsd_cfg_unlock(ldmsd_cfgobj_type_t type);
void ldmsd_cfgobj_lock(ldmsd_cfgobj_t obj);
void ldmsd_cfgobj_unlock(ldmsd_cfgobj_t obj);
/**
 * Allocate a configuration object of the requested size. A
 * configuration object with the same name and type must not already
 * exist.
 *
 * On success, the object is returned locked.
 *
 * NOTE: The caller must use ldmsd_cfgobj_unlock() to unlock the
 * object once configured.
 */
ldmsd_cfgobj_t ldmsd_cfgobj_new_with_auth(const char *name,
					  ldmsd_cfgobj_type_t type,
					  size_t obj_size,
					  ldmsd_cfgobj_del_fn_t __del,
					  uid_t uid,
					  gid_t gid,
					  int perm);
#define ldmsd_cfgobj_get(o, name) ({ \
		if (o) \
			ref_get(&(o)->ref, name); \
		(o); \
	})
int ldmsd_cfgobj_refcount(ldmsd_cfgobj_t obj);
void ldmsd_cfgobj_put(ldmsd_cfgobj_t obj, const char *ref_name);
/* On success, the returned object will have a reference set.
   NOTE: The caller must call ldmsd_cfgobj_find_put() to drop the
   reference from ldmsd_cfgobj_find_get(). */
ldmsd_cfgobj_t ldmsd_cfgobj_find_get(const char *name, ldmsd_cfgobj_type_t type);
/* Drop the reference aquired by ldmsd_cfg_obj_find_get() */
void ldmsd_cfgobj_find_put(ldmsd_cfgobj_t obj);
void ldmsd_cfgobj_del(ldmsd_cfgobj_t obj);
ldmsd_cfgobj_t ldmsd_cfgobj_first(ldmsd_cfgobj_type_t type);
ldmsd_cfgobj_t ldmsd_cfgobj_next(ldmsd_cfgobj_t obj);
int ldmsd_cfgobj_access_check(ldmsd_cfgobj_t obj, int acc, ldmsd_sec_ctxt_t ctxt);
int ldmsd_cfgobj_add(ldmsd_cfgobj_t obj);
void ldmsd_cfgobj_rm(ldmsd_cfgobj_t obj);

/* NOTE: Caller must call ldms_sampler_find_put() to drop reference in returned object. */
ldmsd_cfgobj_sampler_t ldmsd_sampler_find_get(const char *name);
/* Drop the reference aquired by ldmsd_sampler_find_get() */
void ldmsd_sampler_find_put(ldmsd_cfgobj_sampler_t obj);

#define ldmsd_sampler_get(_s_, _r_) (ldmsd_cfgobj_sampler_t)ldmsd_cfgobj_get(&(_s_)->cfg, _r_)
#define ldmsd_sampler_put(_s_, _r_) ldmsd_cfgobj_put(&(_s_)->cfg, _r_)
#define ldmsd_store_get(_s_, _r_) (ldmsd_cfgobj_store_t)ldmsd_cfgobj_get(&(_s_)->cfg, _r_)
#define ldmsd_store_put(_s_, _r_) ldmsd_cfgobj_put(&(_s_)->cfg, _r_)
ldmsd_cfgobj_sampler_t ldmsd_sampler_first();
ldmsd_cfgobj_sampler_t ldmsd_sampler_next(ldmsd_cfgobj_sampler_t);
void ldmsd_sampler_lock(ldmsd_cfgobj_sampler_t samp);
void ldmsd_sampler_unlock(ldmsd_cfgobj_sampler_t samp);
extern int ldmsd_sampler_start(char *cfg_name, char *interval, char *offset,
			char *exclusive_thread);
extern int ldmsd_sampler_stop(char *name);

/* NOTE: Caller must call ldms_store_find_put() to drop reference in returned object. */
ldmsd_cfgobj_store_t ldmsd_store_find_get(const char *name);
/* Drop the reference aquired by ldmsd_store_find_get() */
void ldmsd_store_find_put(ldmsd_cfgobj_store_t obj);
ldmsd_cfgobj_store_t ldmsd_store_first();
ldmsd_cfgobj_store_t ldmsd_store_next(ldmsd_cfgobj_store_t store);
void ldmsd_store_lock(ldmsd_cfgobj_store_t store);
void ldmsd_store_unlock(ldmsd_cfgobj_store_t store);

#define LDMSD_CFGOBJ_FOREACH(obj, type) \
	for ((obj) = ldmsd_cfgobj_first(type); (obj);  \
			(obj) = ldmsd_cfgobj_next(obj))

/** Producer configuration object management */
int ldmsd_prdcr_str2type(const char *type);
const char *ldmsd_prdcr_type2str(enum ldmsd_prdcr_type type);
ldmsd_prdcr_t
ldmsd_prdcr_new(const char *name, const char *xprt_name,
		const char *host_name, const unsigned short port_no,
		enum ldmsd_prdcr_type type,
		int conn_intrvl_us, int rail, int64_t quota, int64_t rx_rate);
ldmsd_prdcr_t
ldmsd_prdcr_new_with_auth(const char *name, const char *xprt_name,
		const char *host_name, const unsigned short port_no,
		enum ldmsd_prdcr_type type,
		int conn_intrvl_us,
		const char *auth, uid_t uid, gid_t gid, int perm, int rail,
		int64_t quota, int64_t rx_rate, int cache_ip);
int ldmsd_prdcr_del(const char *prdcr_name, ldmsd_sec_ctxt_t ctxt);
ldmsd_prdcr_t ldmsd_prdcr_first();
ldmsd_prdcr_t ldmsd_prdcr_next(struct ldmsd_prdcr *prdcr);
ldmsd_prdcr_set_t ldmsd_prdcr_set_first(ldmsd_prdcr_t prdcr);
ldmsd_prdcr_set_t ldmsd_prdcr_set_next(ldmsd_prdcr_set_t prv_set);
ldmsd_prdcr_set_t ldmsd_prdcr_set_find(ldmsd_prdcr_t prdcr, const char *setname);
ldmsd_prdcr_set_t ldmsd_prdcr_set_first_by_hint(ldmsd_prdcr_t prdcr,
					struct ldmsd_updtr_schedule *hint);
ldmsd_prdcr_set_t ldmsd_prdcr_set_next_by_hint(ldmsd_prdcr_set_t prd_set);
static inline void ldmsd_prdcr_lock(ldmsd_prdcr_t prdcr) {
	ldmsd_cfgobj_lock(&prdcr->obj);
}
static inline void ldmsd_prdcr_unlock(ldmsd_prdcr_t prdcr) {
	ldmsd_cfgobj_unlock(&prdcr->obj);
}
static inline ldmsd_prdcr_t ldmsd_prdcr_get(ldmsd_prdcr_t prdcr, char *name) {
	ldmsd_cfgobj_get(&prdcr->obj, name);
	return prdcr;
}
static inline void ldmsd_prdcr_put(ldmsd_prdcr_t prdcr, char *name) {
	ldmsd_cfgobj_put(&prdcr->obj, name);
}
static inline ldmsd_prdcr_t ldmsd_prdcr_find(const char *name)
{
	return (ldmsd_prdcr_t)ldmsd_cfgobj_find_get(name, LDMSD_CFGOBJ_PRDCR);
}
static inline const char *ldmsd_prdcr_set_state_str(enum ldmsd_prdcr_set_state state) {
	switch (state) {
	case LDMSD_PRDCR_SET_STATE_START:
		return "START";
	case LDMSD_PRDCR_SET_STATE_LOOKUP:
		return "LOOKUP";
	case LDMSD_PRDCR_SET_STATE_READY:
		return "READY";
	case LDMSD_PRDCR_SET_STATE_UPDATING:
		return "UPDATING";
	case LDMSD_PRDCR_SET_STATE_DELETED:
		return "DELETED";
	}
	return "BAD STATE";
}
void ldmsd_prdcr_set_ref_get(ldmsd_prdcr_set_t set);
void ldmsd_prdcr_set_ref_put(ldmsd_prdcr_set_t set);
void ldmsd_prd_set_updtr_task_update(ldmsd_prdcr_set_t prd_set);
int ldmsd_prdcr_start(const char *name, const char *interval_str,
		      ldmsd_sec_ctxt_t ctxt);
int ldmsd_prdcr_start_regex(const char *prdcr_regex, const char *interval_str,
			    char *rep_buf, size_t rep_len,
			    ldmsd_sec_ctxt_t ctxt);
int ldmsd_prdcr_stop(const char *name, ldmsd_sec_ctxt_t ctxt);
int ldmsd_prdcr_stop_regex(const char *prdcr_regex,
			char *rep_buf, size_t rep_len, ldmsd_sec_ctxt_t ctxt);
int ldmsd_prdcr_subscribe_regex(const char *prdcr_regex, const char *stream,
				const char *msg,
				char *rep_buf, size_t rep_len,
				ldmsd_sec_ctxt_t ctxt, int64_t rate);
int ldmsd_prdcr_unsubscribe_regex(const char *prdcr_regex,
				const char *stream_name,
				const char *msg,
				char *rep_buf, size_t rep_len,
				ldmsd_sec_ctxt_t ctxt);

int __ldmsd_prdcr_start(ldmsd_prdcr_t prdcr, ldmsd_sec_ctxt_t ctxt);
int __ldmsd_prdcr_stop(ldmsd_prdcr_t prdcr, ldmsd_sec_ctxt_t ctxt);

/* updtr */
ldmsd_updtr_t
ldmsd_updtr_new(const char *name, char *interval_str,
		char *offset_str, int push_flags,
				int is_auto_interval);
ldmsd_updtr_t
ldmsd_updtr_new_with_auth(const char *name, char *interval_str, char *offset_str,
					int push_flags, int is_auto_task,
					uid_t uid, gid_t gid, int perm);
int ldmsd_updtr_del(const char *updtr_name, ldmsd_sec_ctxt_t ctxt);
ldmsd_updtr_t ldmsd_updtr_first();
ldmsd_updtr_t ldmsd_updtr_next(struct ldmsd_updtr *updtr);
ldmsd_name_match_t ldmsd_updtr_match_first(ldmsd_updtr_t updtr);
ldmsd_name_match_t ldmsd_updtr_match_next(ldmsd_name_match_t match);
ldmsd_prdcr_ref_t ldmsd_updtr_prdcr_first(ldmsd_updtr_t updtr);
ldmsd_prdcr_ref_t ldmsd_updtr_prdcr_next(ldmsd_prdcr_ref_t ref);
static inline ldmsd_updtr_t ldmsd_updtr_get(ldmsd_updtr_t updtr, const char *name) {
	ldmsd_cfgobj_get(&updtr->obj, name);
	return updtr;
}
static inline void ldmsd_updtr_put(ldmsd_updtr_t updtr, const char *name) {
	ldmsd_cfgobj_put(&updtr->obj, name);
}
static inline void ldmsd_updtr_lock(ldmsd_updtr_t updtr) {
	ldmsd_cfgobj_lock(&updtr->obj);
}
static inline void ldmsd_updtr_unlock(ldmsd_updtr_t updtr) {
	ldmsd_cfgobj_unlock(&updtr->obj);
}
static inline ldmsd_updtr_t ldmsd_updtr_find(const char *name) {
	return (ldmsd_updtr_t)ldmsd_cfgobj_find_get(name, LDMSD_CFGOBJ_UPDTR);
}

static inline const char *ldmsd_updtr_state_str(enum ldmsd_updtr_state state) {
	switch (state) {
	case LDMSD_UPDTR_STATE_STOPPING:
		return "STOPPING";
	case LDMSD_UPDTR_STATE_STOPPED:
		return "STOPPED";
	case LDMSD_UPDTR_STATE_RUNNING:
		return "RUNNING";
	}
	return "BAD STATE";
}
int ldmsd_updtr_start(const char *updtr_name, const char *interval_str,
		      const char *offset_str, const char *auto_interval,
		      ldmsd_sec_ctxt_t ctxt);
int ldmsd_updtr_stop(const char *updtr_name, ldmsd_sec_ctxt_t ctxt);
int ldmsd_updtr_match_add(const char *updtr_name, const char *regex_str,
		const char *selector_str, char *rep_buf, size_t rep_len,
		ldmsd_sec_ctxt_t ctxt);
int ldmsd_updtr_match_del(const char *updtr_name, const char *regex_str,
			  const char *selector_str, ldmsd_sec_ctxt_t ctxt);

int __ldmsd_updtr_start(ldmsd_updtr_t updtr, ldmsd_sec_ctxt_t ctxt);
int __ldmsd_updtr_stop(ldmsd_updtr_t updtr, ldmsd_sec_ctxt_t ctxt);

/* strgp */
ldmsd_strgp_t ldmsd_strgp_new(const char *name);
ldmsd_strgp_t ldmsd_strgp_new_with_auth(const char *name,
					uid_t uid, gid_t gid, int perm);
int ldmsd_strgp_del(const char *strgp_name, ldmsd_sec_ctxt_t ctxt);
ldmsd_strgp_t ldmsd_strgp_first();
ldmsd_strgp_t ldmsd_strgp_next(struct ldmsd_strgp *strgp);
ldmsd_name_match_t ldmsd_strgp_prdcr_first(ldmsd_strgp_t strgp);
ldmsd_name_match_t ldmsd_strgp_prdcr_next(ldmsd_name_match_t match);
ldmsd_strgp_metric_t ldmsd_strgp_metric_first(ldmsd_strgp_t strgp);
ldmsd_strgp_metric_t ldmsd_strgp_metric_next(ldmsd_strgp_metric_t metric);
static inline ldmsd_strgp_t ldmsd_strgp_get(ldmsd_strgp_t strgp, char *name) {
	ldmsd_cfgobj_get(&strgp->obj, name);
	return strgp;
}
static inline void ldmsd_strgp_put(ldmsd_strgp_t strgp, char* name) {
	ldmsd_cfgobj_put(&strgp->obj, name);
}
static inline void ldmsd_strgp_lock(ldmsd_strgp_t strgp) {
	ldmsd_cfgobj_lock(&strgp->obj);
}
static inline void ldmsd_strgp_unlock(ldmsd_strgp_t strgp) {
	ldmsd_cfgobj_unlock(&strgp->obj);
}
static inline ldmsd_strgp_t ldmsd_strgp_find(const char *name) {
	return (ldmsd_strgp_t)ldmsd_cfgobj_find_get(name, LDMSD_CFGOBJ_STRGP);
}
static inline const char *ldmsd_strgp_state_str(enum ldmsd_strgp_state state) {
	switch (state) {
	case LDMSD_STRGP_STATE_STOPPED:
		return "STOPPED";
	case LDMSD_STRGP_STATE_RUNNING:
		return "RUNNING";
	}
	return "BAD STATE";
}
int ldmsd_strgp_stop(const char *strgp_name, ldmsd_sec_ctxt_t ctxt);
int ldmsd_strgp_start(const char *name, ldmsd_sec_ctxt_t ctxt);

int __ldmsd_strgp_start(ldmsd_strgp_t strgp, ldmsd_sec_ctxt_t ctxt);
int __ldmsd_strgp_stop(ldmsd_strgp_t strgp, ldmsd_sec_ctxt_t ctxt);


/* Function to update inter-dependent configuration objects */
void ldmsd_prdcr_update(ldmsd_strgp_t strgp);
void ldmsd_strgp_update(ldmsd_prdcr_set_t prd_set);
int ldmsd_strgp_update_prdcr_set(ldmsd_strgp_t strgp, ldmsd_prdcr_set_t prd_set);
int ldmsd_strgp_prdcr_add(const char *strgp_name, const char *regex_str,
			  char *rep_buf, size_t rep_len, ldmsd_sec_ctxt_t ctxt);
int ldmsd_strgp_prdcr_del(const char *strgp_name, const char *regex_str,
			ldmsd_sec_ctxt_t ctxt);
int ldmsd_strgp_metric_del(const char *strgp_name, const char *metric_name,
			   ldmsd_sec_ctxt_t ctxt);
int ldmsd_strgp_metric_add(const char *strgp_name, const char *metric_name,
			   ldmsd_sec_ctxt_t ctxt);
int ldmsd_updtr_prdcr_add(const char *updtr_name, const char *prdcr_regex,
			  char *rep_buf, size_t rep_len, ldmsd_sec_ctxt_t ctxt);
int ldmsd_updtr_prdcr_del(const char *updtr_name, const char *prdcr_regex,
			  char *rep_buf, size_t rep_len, ldmsd_sec_ctxt_t ctxt);
ldmsd_prdcr_ref_t ldmsd_updtr_prdcr_find(ldmsd_updtr_t updtr,
					const char *prdcr_name);
int ldmsd_updtr_schedule_cmp(void *a, const void *b);
int ldmsd_updtr_tasks_update(ldmsd_updtr_t updtr, ldmsd_prdcr_set_t prd_set);

/* Failover routines */
extern int ldmsd_use_failover;
int ldmsd_failover_config(const char *host, const char *port, const char *xprt,
			  int auto_switch, uint64_t interval_us);
int ldmsd_failover_start();
int cfgobj_is_failover(ldmsd_cfgobj_t obj);
int ldmsd_cfgobjs_start(int (*filter)(ldmsd_cfgobj_t));

int ldmsd_ourcfg_start_proc();

/** Task scheduling */
void ldmsd_task_init(ldmsd_task_t task);
int ldmsd_task_start(ldmsd_task_t task,
		     ldmsd_task_fn_t task_fn, void *task_arg,
		     int flags, long sched_us, long offset_us);
int ldmsd_task_resched(ldmsd_task_t task,
		     int flags, long sched_us, long offset_us);
void ldmsd_task_stop(ldmsd_task_t task);
void ldmsd_task_join(ldmsd_task_t task);

int ldmsd_set_update_hint_set(ldms_set_t set, long interval_us, long offset_us);
int ldmsd_set_update_hint_get(ldms_set_t set, long *interva_us, long *offset_us);

/** Regular expressions */
int ldmsd_compile_regex(regex_t *regex, const char *ex, char *errbuf, size_t errsz);

/* Receive a message from an ldms endpoint */
void ldmsd_recv_msg(ldms_t x, char *data, size_t data_len);

/* Get the hostname of this ldmsd */
extern const char *ldmsd_myhostname_get();

/* Get the name of this ldmsd */
const char *ldmsd_myname_get();

/* Increment config counter */
void ldmsd_inc_cfg_cntr();
int ldmsd_cfg_cntr_get();

mode_t ldmsd_inband_cfg_mask_get();
void ldmsd_inband_cfg_mask_set(mode_t mask);
void ldmsd_inband_cfg_mask_add(mode_t mask);
void ldmsd_inband_cfg_mask_rm(mode_t mask);

/* Listen for a connection either on Unix domain socket or Socket. A dedicated thread is assigned to a new connection. */
extern int listen_on_cfg_xprt(char *xprt_str, char *port_str, char *secretword);

/**
 * \brief Create a new group of sets.
 *
 * To destroy the group, simply call \c ldms_set_delete().
 *
 * \param grp_name The name of the group.
 *
 * \retval grp  If success, the LDMS set handle that represents the group.
 * \retval NULL If failed.
 */
ldms_set_t ldmsd_group_new(const char *grp_name);

/**
 * \brief Add a set into the group.
 *
 * \param grp      The group handle (from \c ldmsd_group_new()).
 * \param set_name The name of the set to be added.
 */
int ldmsd_group_set_add(ldms_set_t grp, const char *set_name);

/**
 * \brief Remove a set from the group.
 *
 * \param grp      The group handle (from \c ldmsd_group_new()).
 * \param set_name The name of the set to be removed.
 */
int ldmsd_group_set_rm(ldms_set_t grp, const char *set_name);

enum ldmsd_group_check_flag {
	LDMSD_GROUP_IS_GROUP = 0x00000001,
	LDMSD_GROUP_MODIFIED = 0x00000002,
	LDMSD_GROUP_ERROR    = 0xF0000000,
};

/**
 * \brief Check ldmsd group status.
 *
 * \retval flags LDMSD_GROUP check flags. The caller should check the returned
 *               flags against ::ldmsd_group_check_flag enumeration.
 */
int ldmsd_group_check(ldms_set_t set);

/**
 * \brief Group member iteration callback signature.
 *
 * The callback function will be called for each member of the group.
 *
 *
 * \param grp  The group handle.
 * \param name The member name.
 * \param arg  The application-supplied generic argument.
 *
 * \retval 0     If there is no error.
 * \retval errno If an error occurred. In this case, the iteration will be
 *               stopped.
 */
typedef int (*ldmsd_group_iter_cb_t)(ldms_set_t grp, const char *name, void *arg);

/**
 * \brief Iterate over the members of the group.
 *
 * Iterate over the members of the group, calling the \c cb function for each
 * of them.
 *
 * \param grp The group handle.
 * \param cb  The callback function.
 * \param arg The argument to be supplied to the callback function.
 *
 * \retval 0     If there is no error.
 * \retval errno If failed.
 */
int ldmsd_group_iter(ldms_set_t grp, ldmsd_group_iter_cb_t cb, void *arg);

/**
 * \brief Get the member name from a set info key.
 *
 * \retval NULL If \c info_key is NOT for set member entry.
 * \retval name If \c info_key is for set member entry.
 */
const char *ldmsd_group_member_name(const char *info_key);

/*
 * The maximum number of authentication options
 */
#define LDMSD_AUTH_OPT_MAX 128

/**
 * LDMSD object of the listener transport/port
 */
typedef struct ldmsd_listen {
	struct ldmsd_cfgobj obj;
	char *xprt;
	unsigned short port_no;
	char *host;
	char *auth_name;
	char *auth_dom_name;
	struct attr_value_list *auth_attrs;
	int quota;
	int rx_limit;
	ldms_t x;
} *ldmsd_listen_t;

/* Listen for a connection request on an ldms xprt */
extern int listen_on_ldms_xprt(ldmsd_listen_t listen);

uint8_t ldmsd_is_initialized();

/**
 * \brief Create a listening endpoint
 *
 * \param xprt   transport name
 * \param port   port
 * \param host   hostname
 * \param auth   authentication domain name
 * \param quota    receive quota
 * \param rx_limit   receive rate limit
 *
 * To use the default receive quota or receive rate limit, provide NULL.
 *
 * \return a listen cfgobj
 */
ldmsd_listen_t ldmsd_listen_new(char *xprt, char *port, char *host, char *auth,
				char *quota, char *rx_limit);

/**
 * LDMSD Authentication Domain Configuration Object
 */
typedef struct ldmsd_auth {
	struct ldmsd_cfgobj obj; /* this contains the `name` */
	char *plugin; /* auth plugin name */
	struct attr_value_list *attrs; /* attributes for the plugin */
} *ldmsd_auth_t;


/* Key (name) of the default auth -- intentionally including SPACE as it is not
 * allowed in user-defined names */
#define DEFAULT_AUTH "DEFAULT"

ldmsd_auth_t
ldmsd_auth_new_with_auth(const char *name, const char *plugin,
			 struct attr_value_list *attrs,
			 uid_t uid, gid_t gid, int perm);
int ldmsd_auth_del(const char *name, ldmsd_sec_ctxt_t ctxt);
ldmsd_auth_t ldmsd_auth_default_get();
int ldmsd_auth_default_set(const char *plugin, struct attr_value_list *attrs);

static inline
ldmsd_auth_t ldmsd_auth_find(const char *name)
{
	return (ldmsd_auth_t)ldmsd_cfgobj_find_get(name, LDMSD_CFGOBJ_AUTH);
}
void ldmsd_xprt_term(ldms_t x);
int ldmsd_timespec_from_str(struct timespec *result, const char *str);
void ldmsd_timespec_add(struct timespec *a, struct timespec *b, struct timespec *result);
int ldmsd_timespec_cmp(struct timespec *a, struct timespec *b);
void ldmsd_timespec_diff(struct timespec *a, struct timespec *b, struct timespec *result);

void ldmsd_log_flush_interval_set(unsigned long interval);
void ldmsd_flush_log();

struct ldmsd_str_ent {
	char *str;
	TAILQ_ENTRY(ldmsd_str_ent) entry;
};
TAILQ_HEAD(ldmsd_str_list, ldmsd_str_ent);
struct ldmsd_str_ent *ldmsd_str_ent_new(char *s);
void ldmsd_str_ent_free(struct ldmsd_str_ent *ent);
void ldmsd_str_list_destroy(struct ldmsd_str_list *list);

__attribute__((format(printf, 3, 4)))
size_t Snprintf(char **dst, size_t *len, char *fmt, ...);

__attribute__((format(printf, 2, 3)))
int linebuf_printf(struct ldmsd_req_ctxt *reqc, char *fmt, ...);

void ldmsd_stat_update(struct ldmsd_stat *stat, struct timespec *start, struct timespec *end);
void ldmsd_stat_reset(struct ldmsd_stat *stats, struct timespec *now);
#endif
