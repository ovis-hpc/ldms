/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010-2015 Sandia Corporation. All rights reserved.
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
#include <regex.h>
#include <sys/queue.h>
#include <ovis_util/util.h>
#include "ldms.h"

#define LDMSD_PLUGIN_LIBPATH_DEFAULT PLUGINDIR

#define LDMSD_VERSION_MAJOR	0x03
#define LDMSD_VERSION_MINOR	0x02
#define LDMSD_VERSION_PATCH	0x01
#define LDMSD_VERSION_FLAGS	0x00

struct ldmsd_version {
	uint8_t major;
	uint8_t minor;
	uint8_t patch;
	uint8_t flags;
};

/** Get the ldmsd version  */
void ldmsd_version_get(struct ldmsd_version *v);

struct hostspec;
struct ldmsd_store_policy;
struct ldmsd_store_policy_ref {
	struct ldmsd_store_policy *lsp;
	LIST_ENTRY(ldmsd_store_policy_ref) entry;
};
LIST_HEAD(ldmsd_lsp_list, ldmsd_store_policy_ref);

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
	int sched_us;
	int offset_us;
	pthread_mutex_t lock;
	pthread_cond_t join_cv;
	struct event *event;
	struct timeval timeout;
	enum ldmsd_task_state {
		LDMSD_TASK_STATE_STOPPED,
		LDMSD_TASK_STATE_STARTED,
		LDMSD_TASK_STATE_RUNNING
	} state;
	ldmsd_task_fn_t fn;
	void *fn_arg;
} *ldmsd_task_t;

typedef enum ldmsd_cfgobj_type {
	LDMSD_CFGOBJ_PRDCR = 1,
	LDMSD_CFGOBJ_UPDTR,
	LDMSD_CFGOBJ_STRGP,
} ldmsd_cfgobj_type_t;

struct ldmsd_cfgobj;
typedef void (*ldmsd_cfgobj_del_fn_t)(struct ldmsd_cfgobj *);

typedef struct ldmsd_cfgobj {
	char *name;		/* Unique producer name */
	uint32_t ref_count;
	ldmsd_cfgobj_type_t type;
	ldmsd_cfgobj_del_fn_t __del;
	struct rbn rbn;
	pthread_mutex_t lock;
} *ldmsd_cfgobj_t;

/**
 * Producer: Named instance of an LDMSD
 *
 * The Producer name, by policy, equals the name of this configuration object.
 */
typedef struct ldmsd_prdcr {
	struct ldmsd_cfgobj obj;

	struct sockaddr_storage ss;	/* Host address */
	socklen_t ss_len;
	char *host_name;	/* Host name */
	unsigned short port_no;		/* Port number */
	char *xprt_name;	/* Transport name */
	ldms_t xprt;
	int conn_intrvl_us;	/* connect interval */

	enum ldmsd_prdcr_state {
		/** Producer is disabled and idle */
		LDMSD_PRDCR_STATE_STOPPED,
		/** Ready for connect attempts */
		LDMSD_PRDCR_STATE_DISCONNECTED,
		/** Connection request is outstanding */
		LDMSD_PRDCR_STATE_CONNECTING,
		/** Connect complete */
		LDMSD_PRDCR_STATE_CONNECTED,
	} conn_state;

	enum ldmsd_prdcr_type {
		/** Connection initiated at this side */
		LDMSD_PRDCR_TYPE_ACTIVE,
		/** Connection initated by peer */
		LDMSD_PRDCR_TYPE_PASSIVE,
		/** Producer is local to this daemon */
		LDMSD_PRDCR_TYPE_LOCAL
	} type;

	struct ldmsd_task task;

	/** Maintains a tree of all metric sets available from this
	 * producer. It is a tree to allow quick lookup by the logic
	 * that handles dir_add and dir_del directory updates from the
	 * produer.
	 */
	struct rbt set_tree;
} *ldmsd_prdcr_t;

struct ldmsd_strgp;
typedef struct ldmsd_strgp *ldmsd_strgp_t;

typedef struct ldmsd_strgp_ref {
	ldmsd_strgp_t strgp;
	LIST_ENTRY(ldmsd_strgp_ref) entry;
} *ldmsd_strgp_ref_t;

typedef struct ldmsd_prdcr_set {
	char *inst_name;
	char *schema_name;
	ldmsd_prdcr_t prdcr;
	ldms_set_t set;
	enum ldmsd_prdcr_set_state {
		LDMSD_PRDCR_SET_STATE_START,
		LDMSD_PRDCR_SET_STATE_LOOKUP,
		LDMSD_PRDCR_SET_STATE_READY,
	} state;
	uint64_t last_gn;
	pthread_mutex_t lock;
	LIST_HEAD(ldmsd_strgp_ref_list, ldmsd_strgp_ref) strgp_list;
	struct rbn rbn;

	int ref_count;
} *ldmsd_prdcr_set_t;

typedef struct ldmsd_prdcr_ref {
	ldmsd_prdcr_t prdcr;
	LIST_ENTRY(ldmsd_prdcr_ref) entry;
} *ldmsd_prdcr_ref_t;

/**
 * Updater: Named set of rules for updating remote metric sets
 *
 * The prdcr_list specifies the set of LDMS from which to gather
 * metric sets. The match_list specifies which metric sets from each
 * producer will be updated. If the match_list is empty, all metric
 * sets on each producer will be updated.
 *
 */
struct ldmsd_name_match;
typedef struct ldmsd_updtr {
	struct ldmsd_cfgobj obj;

	int updt_intrvl_us;	/* update interval */
	int updt_offset_us;	/* update time offset */
	int updt_task_flags;

	enum ldmsd_updtr_state {
		/** Initial updater state */
		LDMSD_UPDTR_STATE_STOPPED = 0,
		/** Ready for update attempts */
		LDMSD_UPDTR_STATE_RUNNING,
	} state;

	struct ldmsd_task task;

	LIST_HEAD(prdcr_list, ldmsd_prdcr_ref) prdcr_list;
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

typedef void (*strgp_update_fn_t)(ldmsd_strgp_t strgp, ldmsd_prdcr_set_t prd_set);
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

	/** The container name in which the storage backend will place data */
	char *container;

	/** The storage backend plugin */
	char *plugin_name;
	struct ldmsd_store *store;
	/** The open instance of the container */
	ldmsd_store_handle_t store_handle;
	ldmsd_store_handle_t next_store_handle;

	enum ldmsd_strgp_state {
		LDMSD_STRGP_STATE_STOPPED,
		LDMSD_STRGP_STATE_RUNNING
	} state;

	struct ldmsd_task task;	/* rotate open task */

	/** Update function */
	strgp_update_fn_t update_fn;
};

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
#define LDMSD_MAX_CONFIG_STR_LEN 8192
struct attr_value_list;
struct ldmsd_plugin {
	char name[LDMSD_MAX_PLUGIN_NAME_LEN];
	enum ldmsd_plugin_type {
		LDMSD_PLUGIN_SAMPLER,
		LDMSD_PLUGIN_STORE
	} type;
	enum ldmsd_plugin_type (*get_type)(struct ldmsd_plugin *self);
	int (*config)(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl);
	void (*term)(struct ldmsd_plugin *self);
	const char *(*usage)(struct ldmsd_plugin *self);
};

struct ldmsd_sampler {
	struct ldmsd_plugin base;
	ldms_set_t (*get_set)(struct ldmsd_sampler *self);
	int (*sample)(struct ldmsd_sampler *self);
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
				    struct ldmsd_strgp_metric_list *metric_list,
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

struct ldmsd_store_host {
	char *name;
	struct rbn rbn;
};

struct ldmsd_store_policy {
	char *name;
	char *container;
	char *schema;
	int metric_count;
	int *metric_arry;
	struct ldmsd_strgp_metric_list metric_list;
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

#define LDMSD_STR_WRAP(NAME) #NAME
#define LDMSD_LWRAP(NAME) LDMSD_L ## NAME
/**
 * \brief ldmsd log levels
 *
 * The ldmsd log levels, in order of increasing importance, are
 *  - DEBUG
 *  - INFO
 *  - ERROR
 *  - CRITICAL
 *  - SUPREME
 *
 * SUPREME is for messages printed to the log file per users requests,
 * e.g, messages printed from the 'info' command.
 */
#define LOGLEVELS(WRAP) \
	WRAP (DEBUG), \
	WRAP (INFO), \
	WRAP (ERROR), \
	WRAP (CRITICAL), \
	WRAP (ALL), \
	WRAP (LASTLEVEL)

enum ldmsd_loglevel {
	LOGLEVELS(LDMSD_LWRAP)
};

extern const char *ldmsd_loglevel_names[];

void ldmsd_log(enum ldmsd_loglevel level, const char *fmt, ...);

int ldmsd_loglevel_set(char *verbose_level);

enum ldmsd_loglevel ldmsd_str_to_loglevel(const char *level_s);

void ldmsd_lerror(const char *fmt, ...);

void ldmsd_lcritical(const char *fmt, ...);

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
		struct ldmsd_strgp_metric_list *metric_list,
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

typedef void (*ldmsd_msg_log_f)(enum ldmsd_loglevel level, const char *fmt, ...);
typedef struct ldmsd_plugin *(*ldmsd_plugin_get_f)(ldmsd_msg_log_f pf);

/* ldmsctl command callback function definition */
typedef int (*ldmsctl_cmd_fn_t)(char *, struct attr_value_list*, struct attr_value_list *);

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
#define LDMSCTL_SET_UDATA_REGEX 14   /* Set user data of metrics using regex and increment */
#define LDMSCTL_VERSION		15   /* Get LDMS version */
#define LDMSCTL_VERBOSE	16   /* Change the log level */

#define LDMSCTL_INCLUDE		17  /* Include another configuration file */
#define LDMSCTL_ENV		18  /* Set environment variable */

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
#define LEN_ERRSTR 128

int ldmsd_stop_sampler(char *plugin_name, char err_str[LEN_ERRSTR]);
int ldmsd_stop_sampler(char *plugin_name, char err_str[LEN_ERRSTR]);
void ldmsd_host_sampler_cb(int fd, short sig, void *arg);
void ldmsd_msg_logger(enum ldmsd_loglevel level, const char *fmt, ...);

/** Configuration object management */
void ldmsd_cfgobj___del(ldmsd_cfgobj_t obj);
void ldmsd_cfgobj_init(void);
void ldmsd_cfg_lock(ldmsd_cfgobj_type_t type);
void ldmsd_cfg_unlock(ldmsd_cfgobj_type_t type);
void ldmsd_cfgobj_lock(ldmsd_cfgobj_t obj);
void ldmsd_cfgobj_unlock(ldmsd_cfgobj_t obj);
ldmsd_cfgobj_t ldmsd_cfgobj_new(const char *name, ldmsd_cfgobj_type_t type, size_t obj_size,
				ldmsd_cfgobj_del_fn_t __del);
ldmsd_cfgobj_t ldmsd_cfgobj_get(ldmsd_cfgobj_t obj);
void ldmsd_cfgobj_put(ldmsd_cfgobj_t obj);
int ldmsd_cfgobj_refcount(ldmsd_cfgobj_t obj);
ldmsd_cfgobj_t ldmsd_cfgobj_find(const char *name, ldmsd_cfgobj_type_t type);
void ldmsd_cfgobj_del(const char *name, ldmsd_cfgobj_type_t type);
ldmsd_cfgobj_t ldmsd_cfgobj_first(ldmsd_cfgobj_type_t type);
ldmsd_cfgobj_t ldmsd_cfgobj_next(ldmsd_cfgobj_t obj);

/** Producer configuration object management */
ldmsd_prdcr_t ldmsd_prdcr_first();
ldmsd_prdcr_t ldmsd_prdcr_next(struct ldmsd_prdcr *prdcr);
ldmsd_prdcr_set_t ldmsd_prdcr_set_first(ldmsd_prdcr_t prdcr);
ldmsd_prdcr_set_t ldmsd_prdcr_set_next(ldmsd_prdcr_set_t prv_set);
static inline void ldmsd_prdcr_lock(ldmsd_prdcr_t prdcr) {
	ldmsd_cfgobj_lock(&prdcr->obj);
}
static inline void ldmsd_prdcr_unlock(ldmsd_prdcr_t prdcr) {
	ldmsd_cfgobj_unlock(&prdcr->obj);
}
static inline ldmsd_prdcr_t ldmsd_prdcr_get(ldmsd_prdcr_t prdcr) {
	ldmsd_cfgobj_get(&prdcr->obj);
	return prdcr;
}
static inline void ldmsd_prdcr_put(ldmsd_prdcr_t prdcr) {
	ldmsd_cfgobj_put(&prdcr->obj);
}
static inline ldmsd_prdcr_t ldmsd_prdcr_find(const char *name)
{
	return (ldmsd_prdcr_t)ldmsd_cfgobj_find(name, LDMSD_CFGOBJ_PRDCR);
}
static inline const char *ldmsd_prdcr_set_state_str(enum ldmsd_prdcr_set_state state) {
	switch (state) {
	case LDMSD_PRDCR_SET_STATE_START:
		return "START";
	case LDMSD_PRDCR_SET_STATE_LOOKUP:
		return "LOOKUP";
	case LDMSD_PRDCR_SET_STATE_READY:
		return "READY";
	}
	return "BAD STATE";
}
void ldmsd_prdcr_set_ref_get(ldmsd_prdcr_set_t set);
void ldmsd_prdcr_set_ref_put(ldmsd_prdcr_set_t set);

ldmsd_updtr_t ldmsd_updtr_first();
ldmsd_updtr_t ldmsd_updtr_next(struct ldmsd_updtr *updtr);
ldmsd_name_match_t ldmsd_updtr_match_first(ldmsd_updtr_t updtr);
ldmsd_name_match_t ldmsd_updtr_match_next(ldmsd_name_match_t match);
ldmsd_prdcr_ref_t ldmsd_updtr_prdcr_first(ldmsd_updtr_t updtr);
ldmsd_prdcr_ref_t ldmsd_updtr_prdcr_next(ldmsd_prdcr_ref_t ref);
static inline ldmsd_updtr_t ldmsd_updtr_get(ldmsd_updtr_t updtr) {
	ldmsd_cfgobj_get(&updtr->obj);
	return updtr;
}
static inline void ldmsd_updtr_put(ldmsd_updtr_t updtr) {
	ldmsd_cfgobj_put(&updtr->obj);
}
static inline void ldmsd_updtr_lock(ldmsd_updtr_t updtr) {
	ldmsd_cfgobj_lock(&updtr->obj);
}
static inline void ldmsd_updtr_unlock(ldmsd_updtr_t updtr) {
	ldmsd_cfgobj_unlock(&updtr->obj);
}
static inline ldmsd_updtr_t ldmsd_updtr_find(const char *name) {
	return (ldmsd_updtr_t)ldmsd_cfgobj_find(name, LDMSD_CFGOBJ_UPDTR);
}

static inline const char *ldmsd_updtr_state_str(enum ldmsd_updtr_state state) {
	switch (state) {
	case LDMSD_UPDTR_STATE_STOPPED:
		return "STOPPED";
	case LDMSD_UPDTR_STATE_RUNNING:
		return "RUNNING";
	}
	return "BAD STATE";
}

ldmsd_strgp_t ldmsd_strgp_first();
ldmsd_strgp_t ldmsd_strgp_next(struct ldmsd_strgp *strgp);
ldmsd_name_match_t ldmsd_strgp_prdcr_first(ldmsd_strgp_t strgp);
ldmsd_name_match_t ldmsd_strgp_prdcr_next(ldmsd_name_match_t match);
ldmsd_strgp_metric_t ldmsd_strgp_metric_first(ldmsd_strgp_t strgp);
ldmsd_strgp_metric_t ldmsd_strgp_metric_next(ldmsd_strgp_metric_t metric);
static inline ldmsd_strgp_t ldmsd_strgp_get(ldmsd_strgp_t strgp) {
	ldmsd_cfgobj_get(&strgp->obj);
	return strgp;
}
static inline void ldmsd_strgp_put(ldmsd_strgp_t strgp) {
	ldmsd_cfgobj_put(&strgp->obj);
}
static inline void ldmsd_strgp_lock(ldmsd_strgp_t strgp) {
	ldmsd_cfgobj_lock(&strgp->obj);
}
static inline void ldmsd_strgp_unlock(ldmsd_strgp_t strgp) {
	ldmsd_cfgobj_unlock(&strgp->obj);
}
static inline ldmsd_strgp_t ldmsd_strgp_find(const char *name) {
	return (ldmsd_strgp_t)ldmsd_cfgobj_find(name, LDMSD_CFGOBJ_STRGP);
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

/* Function to update inter-dependent configuration objects */
void ldmsd_prdcr_update(ldmsd_strgp_t strgp);
void ldmsd_strgp_update(ldmsd_prdcr_set_t prd_set);
int ldmsd_strgp_update_prdcr_set(ldmsd_strgp_t strgp, ldmsd_prdcr_set_t prd_set);

/** Task scheduling */
void ldmsd_task_init(ldmsd_task_t task);
int ldmsd_task_start(ldmsd_task_t task,
		     ldmsd_task_fn_t task_fn, void *task_arg,
		     int flags, int sched_us, int offset_us);
void ldmsd_task_stop(ldmsd_task_t task);
void ldmsd_task_join(ldmsd_task_t task);

/** Regular expressions */
int ldmsd_compile_regex(regex_t *regex, const char *ex, char *errbuf, size_t errsz);

/** Get the LDMSD configured authentication secret already loaded. */
extern const char *ldmsd_secret_get(void);
/** Find out if authentication is required. 1 yes, 0 no. */
extern int ldmsd_authentication_required();

#define LDMSD_RECORD_MARKER 0xffffffff
enum ldmsd_request {
	LDMSD_CLI_REQ = 0x1,
	LDMSD_PRDCR_ADD_REQ = 0x100,
	LDMSD_PRDCR_DEL_REQ,
	LDMSD_PRDCR_START_REQ,
	LDMSD_PRDCR_STOP_REQ,
	LDMSD_PRDCR_STATUS_REQ,
	LDMSD_STRGP_ADD_REQ = 0x200,
	LDMSD_STRGP_DEL_REQ,
	LDMSD_STRGP_START_REQ,
	LDMSD_STRGP_STOP_REQ,
	LDMSD_STRGP_STATUS_REQ,
	LDMSD_UPDTR_ADD_REQ = 0x300,
	LDMSD_UPDTR_DEL_REQ,
	LDMSD_UPDTR_START_REQ,
	LDMSD_UPDTR_STOP_REQ,
	LDMSD_UPDTR_STATUS_REQ,
	LDMSD_SMPLR_ADD_REQ = 0X400,
	LDMSD_SMPLR_DEL_REQ,
	LDMSD_SMPLR_START_REQ,
	LDMSD_SMPLR_STOP_REQ,
	LDMSD_PLUGN_ADD_REQ = 0x500,
	LDMSD_PLUGN_DEL_REQ,
	LDMSD_PLUGN_START_REQ,
	LDMSD_PLUGN_STOP_REQ,
	LDMSD_PLUGN_STATUS_REQ,
};
#define LDMSD_REQ_SOM_F	1
#define LDMSD_REQ_EOM_F	2

typedef struct ldmsd_req_attr_s {
	uint32_t discrim;	/* If 0, end of attr_list */
	uint32_t attr_len;	/* Count of 4B words */
	uint32_t attr_id;	/* Attribute identifier, unique per ldmsd_req_hdr_s.cmd_id */
	uint32_t attr_value[0];	/* Size is attr_len */
} *ldmsd_req_attr_t;
typedef struct ldmsd_req_hdr_s {
	uint32_t marker;	/* Always has the value 0xff */
	uint32_t flags;		/* EOM==1 means this is the last record for this message */
	uint32_t msg_no;	/* Unique for each request */
	uint32_t cmd_id;	/* The unique command id */
	uint32_t rec_len;	/* Record length in bytes including this header */
} *ldmsd_req_hdr_t;

#endif
