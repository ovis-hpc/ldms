/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010-2015 Sandia Corporation. All rights reserved.
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

#include <sys/queue.h>
#include <ovis_util/util.h>
#include "ldms.h"

#define LDMSD_PLUGIN_LIBPATH_DEFAULT "/usr/local/lib/"

struct hostspec;
struct ldmsd_store_policy;
struct ldmsd_store_policy_ref {
	struct ldmsd_store_policy *lsp;
	LIST_ENTRY(ldmsd_store_policy_ref) entry;
};
LIST_HEAD(ldmsd_lsp_list, ldmsd_store_policy_ref);

struct hostset
{
	struct hostspec *host;
	char *name;

	/** List of storage policies to call on update_complete. */
	struct ldmsd_lsp_list lsp_list;

	enum {
		LDMSD_SET_CONFIGURED,
		LDMSD_SET_LOOKUP,
		LDMSD_SET_BUSY, /* updating & storing */
		LDMSD_SET_READY
	} state;

	pthread_mutex_t state_lock;
	ldms_set_t set;
	uint64_t gn;
	int refcount;
	pthread_mutex_t refcount_lock;
	LIST_ENTRY(hostset) entry;
	uint64_t curr_busy_count; /**< The count of current busy access */
	uint64_t total_busy_count; /**< The count of total busy access */
};

struct hostset_ref {
	struct hostset *hset;
	LIST_ENTRY(hostset_ref) entry;
};
LIST_HEAD(hostset_ref_list, hostset_ref);

struct hostspec
{
	struct sockaddr_in sin;	/* host address */
	char *hostname;		/* host name */
	char *xprt_name;	/* transport name */
	int connect_interval;	/* connect interval */
	unsigned long sample_interval;/* sample interval */
	long sample_offset;      /* sample offset */
	int synchronous;         /* 1 if synchronous */
	/*
	 * 0 if the aggregator is responsible for gathering sets from the host.
	 * Otherwise, the aggregator is standby to aggregate
	 * sets from the host.
	 */
	unsigned long standby;
	enum {
		HOST_DISCONNECTED=0,
		HOST_CONNECTING,
		HOST_CONNECTED,
		HOST_DISABLED
	} conn_state;
	pthread_mutex_t conn_state_lock;
	enum {
		ACTIVE, PASSIVE, BRIDGING, LOCAL
	} type;
	int thread_id;
	struct event *event;
	struct timeval timeout;
	ldms_t x;		/* !0 when valid and connected */
	pthread_mutex_t set_list_lock;
	LIST_HEAD(set_list, hostset) set_list;
	LIST_ENTRY(hostspec) link;
};

extern char *skip_space(char *s);
extern int parse_cfg(const char *config_file);
extern struct hostspec *host_first(void);
extern struct hostspec *host_next(struct hostspec *hs);

#define LDMSD_MAX_PLUGIN_NAME_LEN 64
#define LDMSD_MAX_CONFIG_STR_LEN 256
struct attr_value_list;
struct ldmsd_plugin {
	char name[LDMSD_MAX_PLUGIN_NAME_LEN];
	enum ldmsd_plugin_type {
		LDMSD_PLUGIN_SAMPLER,
		LDMSD_PLUGIN_STORE
	} type;
	enum ldmsd_plugin_type (*get_type)();
	int (*config)(struct attr_value_list *kwl, struct attr_value_list *avl);
	void (*term)(void);
	const char *(*usage)(void);
};

struct ldmsd_sampler {
	struct ldmsd_plugin base;
	ldms_set_t (*get_set)();
	int (*sample)(void);
};

struct ldmsd_plugin_cfg {
	void *handle;
	char *name;
	char *libpath;
	unsigned long sample_interval_us;
	long sample_offset_us;
	int synchronous;
	int thread_id;
	int ref_count;
	union {
		struct ldmsd_plugin *plugin;
		struct ldmsd_sampler *sampler;
		struct ldmsd_store *store;
	};
	struct timeval timeout;
	struct event *event;
	pthread_mutex_t lock;
	LIST_ENTRY(ldmsd_plugin_cfg) entry;
};

#define LDMSD_DEFAULT_SAMPLE_INTERVAL 1000000

extern void ldmsd_config_cleanup(void);
extern int ldmsd_config_init(char *name);
struct ldmsd_plugin_cfg *ldmsd_get_plugin(char *name);

extern void hset_ref_get(struct hostset *hset);
extern void hset_ref_put(struct hostset *hset);

typedef void *ldmsd_store_handle_t;

struct ldmsd_store_metric_list;
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
 *
 */
struct ldmsd_store {
	struct ldmsd_plugin base;
	void *ucontext;
	ldmsd_store_handle_t (*open)(struct ldmsd_store *s,
				    const char *container, const char *schema,
				    struct ldmsd_store_metric_list *metric_list,
				    void *ucontext);
	void (*close)(ldmsd_store_handle_t sh);
	int (*flush)(ldmsd_store_handle_t sh);
	void *(*get_context)(ldmsd_store_handle_t sh);
	int (*store)(ldmsd_store_handle_t sh, ldms_set_t set, int *, size_t count);
};

struct store_instance;
typedef void (*io_work_fn)(struct store_instance *);
struct store_instance {
	struct ldmsd_store *plugin; /**< The store plugin. */
	ldmsd_store_handle_t store_handle; /**< The store handle from store->new
						or store->get */
	struct flush_thread *ft; /**< The pointer to the assigned
				      flush_thread */
	enum {
		STORE_STATE_INIT=0,
		STORE_STATE_OPEN,
		STORE_STATE_CLOSED,
		STORE_STATE_ERROR
	} state;
	size_t dirty_count;
	pthread_mutex_t lock;
	TAILQ_ENTRY(store_instance) lru_entry;
	LIST_ENTRY(store_instance) flush_entry;
	io_work_fn work_fn;
	int work_pending;
};

struct ldmsd_store_metric {
	char *name;
	enum ldms_value_type type;
	LIST_ENTRY(ldmsd_store_metric) entry;
};

struct ldmsd_store_host {
	char *name;
	struct rbn rbn;
};

LIST_HEAD(ldmsd_store_metric_list, ldmsd_store_metric);

struct ldmsd_store_policy {
	char *name;
	char *container;
	char *schema;
	int metric_count;
	int *metric_arry;
	struct ldmsd_store_metric_list metric_list;
	struct rbt host_tree;
	struct ldmsd_store *plugin;
	struct store_instance *si;

	enum {
		STORE_POLICY_CONFIGURING=0, /* Need metric index list */
		STORE_POLICY_READY,
		STORE_POLICY_ERROR
	} state;
	pthread_mutex_t cfg_lock;
	LIST_ENTRY(ldmsd_store_policy) link;
};

void ldms_log(const char *fmt, ...);

/**
 * Initialize the ldmsd_store.
 *
 * \param __flush_N The number of flush threads.
 * \returns 0 on success.
 * \returns Error code on failure.
 */
int ldmsd_store_init(int __flush_N);
int ldmsd_store_data_add(struct ldmsd_store_policy *lsp, ldms_set_t set);

struct store_instance *
ldmsd_store_instance_get(struct ldmsd_store *store,
			 struct ldmsd_store_policy *sp);

static inline ldmsd_store_handle_t
ldmsd_store_open(struct ldmsd_store *store,
		const char *container, const char *schema,
		struct ldmsd_store_metric_list *metric_list,
		void *ucontext)
{
	return store->open(store, container, schema, metric_list, ucontext);
}

static inline void *ldmsd_store_get_context(struct ldmsd_store *store,
					    ldmsd_store_handle_t sh)
{
	return store->get_context(sh);
}

static inline void
ldmsd_store_flush(struct ldmsd_store *store, ldmsd_store_handle_t sh)
{
	store->flush(sh);
}

static inline void
ldmsd_store_close(struct ldmsd_store *store, ldmsd_store_handle_t sh)
{
	store->close(sh);
}

typedef void (*ldmsd_msg_log_f)(const char *fmt, ...);
typedef struct ldmsd_plugin *(*ldmsd_plugin_get_f)(ldmsd_msg_log_f pf);

/* ldmsctl command callback function definition */
typedef int (*ldmsctl_cmd_fn)(char *replybuf, struct attr_value_list *av_list,
			struct attr_value_list *kw_list);

#define LDMSCTL_LIST_PLUGINS	0    /* List Plugins */
#define LDMSCTL_LOAD_PLUGIN	1    /* Load Plugin */
#define LDMSCTL_TERM_PLUGIN	2    /* Term Plugin */
#define LDMSCTL_CFG_PLUGIN	3    /* Configure Plugin */
#define LDMSCTL_START_SAMPLER	4    /* Start Sampler */
#define LDMSCTL_STOP_SAMPLER	5    /* Stop Sampler */
#define LDMSCTL_ADD_HOST	6    /* Add a Host */
#define LDMSCTL_REM_HOST	7    /* Remove a Host */
#define LDMSCTL_STORE		8    /* Store Metrics */
#define LDMSCTL_INFO_DAEMON	9   /* Query daemon status */
#define LDMSCTL_SET_UDATA	10   /* Set user data of a metric */
#define LDMSCTL_EXIT_DAEMON	11   /* Shut down ldmsd */
#define LDMSCTL_UPDATE_STANDBY	12   /* Update the standby state */
#define LDMSCTL_ONESHOT_SAMPLE	13   /* Sample a set at a specific timestamp once */
#define LDMSCTL_LAST_COMMAND	13

extern ldmsctl_cmd_fn cmd_table[LDMSCTL_LAST_COMMAND + 1];

#define LDMSD_CONTROL_SOCKNAME "ldmsd/control"

#define LDMSD_CONNECT_TIMEOUT		20000000 /* 20 Seconds */
#define LDMSD_INITIAL_CONNECT_TIMEOUT	500000  /* 1/2 second */

/*
 * Max length of error strings while ldmsd is being configured.
 */
#define LEN_ERRSTR 128

int ldmsd_stop_sampler(char *plugin_name, char err_str[LEN_ERRSTR]);
int ldmsd_stop_sampler(char *plugin_name, char err_str[LEN_ERRSTR]);
void ldmsd_host_sampler_cb(int fd, short sig, void *arg);
void ldmsd_msg_logger(const char *fmt, ...);

#endif
