/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
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
#include "ldms.h"

#define LDMSD_PLUGIN_LIBPATH_DEFAULT "/usr/local/lib/"

struct hostspec;
struct ldmsd_store_policy;
struct ldmsd_store_policy_ref {
	struct ldmsd_store_policy *lsp;
	LIST_ENTRY(ldmsd_store_policy_ref) entry;
};

LIST_HEAD(ldmsd_store_policy_ref_list, ldmsd_store_policy_ref);
struct hostset
{
	struct hostspec *host;
	char *name;
	struct ldmsd_store_policy_ref_list lsp_list; /**< Store policy list that
						       is related to this
						       hostset. */
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
	struct ldms_mvec *mvec; /**< Metric vector */
};

struct hostset_ref {
	char *hostname; /**< The hostname is here as a part of the
			     configuration. */
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
	int sample_interval;	/* sample interval */
	enum {
		HOST_DISCONNECTED=0,
		HOST_CONNECTING,
		HOST_CONNECTED
	} conn_state;
	pthread_mutex_t conn_state_lock;
	enum {
		ACTIVE, PASSIVE, BRIDGING
	} type;
	int thread_id;
	struct event *event;
	struct timeval timeout;
	ldms_t x;		/* !0 when valid and connected */
	pthread_mutex_t set_list_lock;
	LIST_HEAD(set_list, hostset) set_list;
	LIST_ENTRY(hostspec) link;
};

typedef struct ldmsd_store_tuple_s {
	struct timeval tv;
	uint32_t comp_id;
	ldms_metric_t value;
} *ldmsd_store_tuple_t;

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

typedef void *ldmsd_store_handle_t;

struct ldmsd_store_metric_index_list;
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
	ldmsd_store_handle_t (*get)(const char *container);
	ldmsd_store_handle_t (*new)(struct ldmsd_store *s, const char
			*comp_type, const char *container, struct
			ldmsd_store_metric_index_list *metric_list, void *ucontext);
	void (*destroy)(ldmsd_store_handle_t sh);
	int (*flush)(ldmsd_store_handle_t sh);
	void (*close)(ldmsd_store_handle_t sh);
	void *(*get_context)(ldmsd_store_handle_t sh);
	int (*store)(ldmsd_store_handle_t sh, ldms_set_t set,
		     ldms_mvec_t mvec);
};

struct store_instance;
typedef void (*io_work_fn)(struct store_instance *);
struct store_instance {
	struct ldmsd_store *store_engine; /**< The store plugin. */
	ldmsd_store_handle_t store_handle; /**< The store handle from store->new
					     	or store->get */
	enum {
		STORE_STATE_INIT=0,
		STORE_STATE_OPEN,
		STORE_STATE_CLOSED,
		STORE_STATE_ERROR
	} state;
	size_t dirty_count;
	pthread_mutex_t lock;
	TAILQ_ENTRY(store_instance) lru_entry;
	LIST_ENTRY(store_instance) work_entry;
	io_work_fn work_fn;
	int work_pending;
};

struct ldmsd_store_metric_index {
	char *name; /**< For configuration */
	int index; /**< The index */
	LIST_ENTRY(ldmsd_store_metric_index) entry;
};

LIST_HEAD(ldmsd_store_metric_index_list, ldmsd_store_metric_index);

struct ldmsd_store_policy {
	struct hostset_ref_list hset_ref_list;
	char *container; /**< This is store policy ID. */
	char *setname; /**< It is here for configuration. */
	int metric_count; /**< The number of metrics. */
	struct ldmsd_store_metric_index_list metric_list; /**< List of the indices. */
	char *comp_type;
	struct store_instance *si; /**< Store instance. */
	enum {
		STORE_POLICY_CONFIGURING=0, /* Need metric index list */
		STORE_POLICY_READY,
		STORE_POLICY_WRONG_CONFIG
	} state;
	pthread_mutex_t idx_create_lock;
	LIST_ENTRY(ldmsd_store_policy) link;
};

void ldms_log(const char *fmt, ...);

int ldmsd_store_init(void);
int ldmsd_store_data_add(struct ldmsd_store_policy *lsp,
		ldms_set_t set, struct ldms_mvec *mvec);

struct store_instance *
ldmsd_store_instance_get(struct ldmsd_store *store,
			struct ldmsd_store_policy *sp);

static inline ldmsd_store_handle_t
ldmsd_store_new(struct ldmsd_store *store,
		const char *comp_type, const char *container,
		struct ldmsd_store_metric_index_list *metric_list,
		void *ucontext)
{
	return store->new(store, comp_type, container, metric_list, ucontext);
}

static inline void *ldmsd_store_get_context(struct ldmsd_store *store,
					    ldmsd_store_handle_t sh)
{
	return store->get_context(sh);
}

static inline ldmsd_store_handle_t
ldmsd_store_get(struct ldmsd_store *store, const char *container)
{
	return store->get(container);
}

static inline void
ldmsd_store_destroy(struct ldmsd_store *store, ldmsd_store_handle_t sh)
{
	store->destroy(sh);
}

static inline int
ldmsd_store_store(struct ldmsd_store *store, ldmsd_store_handle_t sh,
		   ldms_set_t set, struct ldms_mvec *mvec)
{
	return store->store(sh, set, mvec);
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
typedef int (*ldmsctl_cmd_fn)(int fd,
			      struct sockaddr *sa, ssize_t sa_len,
			      char *command);

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
#define LDMSCTL_EXIT_DAEMON	10   /* Shut down ldmsd */
#define LDMSCTL_LAST_COMMAND	10

struct attr_value {
	char *name;
	char *value;
};

struct attr_value_list {
	int size;
	int count;
	struct attr_value list[0];
};

#include <netinet/in.h>
#include <sys/un.h>

struct ctrlsock {
	int sock;
	struct sockaddr *sa;
	size_t sa_len;
	struct sockaddr_in sin;
	struct sockaddr_un rem_sun;
	struct sockaddr_un lcl_sun;
};

#define LDMSD_CONTROL_SOCKNAME "ldmsd/control"
struct ctrlsock *ctrl_connect(char *my_name, char *sock_name);
struct ctrlsock *ctrl_inet_connect(struct sockaddr_in *sin);
int ctrl_request(struct ctrlsock *sock, int cmd_id,
		 struct attr_value_list *avl, char *err_str);
void ctrl_close(struct ctrlsock *sock);

char *av_value(struct attr_value_list *av_list, char *name);
char *av_name(struct attr_value_list *kw_list, int idx);
char *av_value_at_idx(struct attr_value_list *kw_list, int idx);
int tokenize(char *cmd, struct attr_value_list *kwl,
	     struct attr_value_list *avl);
struct attr_value_list *av_new(size_t size);

#endif
