/*
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
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
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
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
 *
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */
#ifndef __LDMSD_H__
#define __LDMSD_H__

#include <sys/queue.h>

#define LDMSD_PLUGIN_LIBPATH_DEFAULT "/usr/local/lib/"

struct hset_metric {
	char *name;
	ldms_metric_t metric;
	uint64_t comp_id;
	struct metric_store *metric_store;
	LIST_ENTRY(hset_metric) entry;
};

struct hostspec;
struct ldmsd_store;
struct hostset
{
	struct hostspec *host;
	char *name;
	ldms_set_t set;
	struct ldmsd_store *store;
	uint64_t gn;
	int refcount;
	pthread_mutex_t refcount_lock;
	LIST_ENTRY(hostset) entry;
	LIST_HEAD(metric_list, hset_metric) metric_list;
};

typedef void *ldmsd_metric_store_t;
struct hostspec
{
	struct sockaddr_in sin;	/* host address */
	char *hostname;		/* host name */
	char *xprt_name;	/* transport name */
	int connect_interval;	/* connect interval */
	int sample_interval;	/* sample interval */
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

struct metric_store;
typedef void (*io_work_fn)(struct metric_store *);
struct metric_store {
	struct ldmsd_store *store;
	ldmsd_metric_store_t lms;
	char *metric_name;
	char *comp_type;
	enum {
		MDS_STATE_INIT=0,
		MDS_STATE_OPEN,
		MDS_STATE_CLOSED,
		MDS_STATE_ERROR
	} state;
	size_t dirty_count;
	pthread_mutex_t lock;
	TAILQ_ENTRY(metric_store) lru_entry;
	LIST_ENTRY(metric_store) work_entry;
	io_work_fn work_fn;
	int work_pending;
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

/** 
 * \brief ldms_store
 *
 * A \c ldms_store encapsulates a storage strategy. For example,
 * MySQL, or SOS. Each strategy provides a library that exports the \c
 * ldms_store structure. This structure contains strategy routines for
 * initializing, configuring and storing metric set data to the
 * persistent storage used by the strategy. When a metric set is
 * sampled, if metrics in the set are associated with a storage
 * strategy, the sample is saved automatcially by \c ldmsd.
 *
 * An \c ldms_store manages Metric Series. A Metric Series is a named,
 * grouped, and time ordered series of metric samples. A Metric Series
 * is indexed by Component ID, and Time.
 *
 */
struct ldmsd_store {
	struct ldmsd_plugin base;
	void *ucontext;
	ldmsd_metric_store_t (*get)(struct ldmsd_store *s,
				    const char *comp_name,
				    const char *metric_name);
	ldmsd_metric_store_t (*new)(struct ldmsd_store *s,
				    const char *comp_name,
				    const char *metric_name,
				    void *context);
	void (*destroy)(ldmsd_metric_store_t);
	int (*flush)(ldmsd_metric_store_t);
	void (*close)(ldmsd_metric_store_t);
	void *(*get_context)(ldmsd_metric_store_t);
	int (*store)(ldmsd_metric_store_t ms,
		     uint32_t comp_id, struct timeval tv, ldms_metric_t m);
};

void ldms_log(const char *fmt, ...);

int ldmsd_store_init(void);
int ldmsd_store_tuple_add(struct metric_store *ms, ldmsd_store_tuple_t t);

struct metric_store *
ldmsd_metric_store_get(struct ldmsd_store *store,
		       char *comp_type, char *metric_name);

void ldmsd_close_metric_store(struct metric_store *m);

static inline ldmsd_metric_store_t
ldmsd_store_new(struct ldmsd_store *store,
		const char *comp_name, const char *metric_name,
		void *ucontext)
{
	return store->new(store, comp_name, metric_name, ucontext);
}

static inline void *ldmsd_store_get_context(struct ldmsd_store *store,
					    ldmsd_metric_store_t lms)
{
	return store->get_context(lms);
}

static inline ldmsd_metric_store_t
ldmsd_store_get(struct ldmsd_store *store,
		const char *comp_name, const char *metric_name)
{
	return store->get(store, comp_name, metric_name);
}

static inline void
ldmsd_store_destroy(struct ldmsd_store *store, ldmsd_metric_store_t ms)
{
	store->destroy(ms);
}

static inline int
ldmsd_store_metric(struct ldmsd_store *store, ldmsd_metric_store_t ms,
		   uint32_t comp_id, struct timeval tv, ldms_metric_t m)
{
	return store->store(ms, comp_id, tv, m);
}

static inline void
ldmsd_store_flush(struct ldmsd_store *store, ldmsd_metric_store_t ms)
{
	store->flush(ms);
}

static inline void
ldmsd_store_close(struct ldmsd_store *store, ldmsd_metric_store_t ms)
{
	store->close(ms);
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
