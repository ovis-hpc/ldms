/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
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
/**
 * \file komondor.c
 * \author Narate Taeat
 * \brief Komondor.
 */

#include <assert.h>
#include <ctype.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <netinet/ip.h>

#include "komondor.h"

#include "coll/str_map.h"
#include "coll/idx.h"
#include "coll/rbt.h"
#include "zap/zap.h"
#include "sos/sos.h"
/* This is a hack */
#include "../../sos/src/sos_priv.h"

#ifdef ENABLE_OCM
#include "ocm/ocm.h"
ocm_t ocm;
char ocm_key[512]; /**< $HOSTNAME/komondor */
uint16_t ocm_port = OCM_DEFAULT_PORT;
int ocm_cb(struct ocm_event *e);
#endif

/***** PROGRAM OPTIONS *****/
const char *short_opt = "a:c:x:p:l:W:Fz:h?";

static struct option long_opt[] = {
	{"act_sos",         required_argument,  0,  'a'},
	{"conf",            required_argument,  0,  'c'},
	{"xprt",            required_argument,  0,  'x'},
	{"port",            required_argument,  0,  'p'},
	{"log",             required_argument,  0,  'l'},
	{"worker-threads",  required_argument,  0,  'W'},
	{"foreground",      no_argument,        0,  'F'},
	{"ocm_port",        required_argument,  0,  'z'},
	{"help",            no_argument,        0,  'h'},
	{0,                 0,                  0,  0},
};

/***** TYPES *****/

typedef enum {
	K_COND_KEY_MODEL,
	K_COND_KEY_COMP,
	K_COND_KEY_SEVERITY,
	K_COND_KEY_LAST
} k_cond_key_t;

char *__k_cond_keys[] = {
	"model_id",
	"metric_id",
	"severity",
};

k_cond_key_t k_cond_key(const char *s)
{
	int i;
	for (i=0; i<sizeof(__k_cond_keys)/sizeof(__k_cond_keys[0]); i++) {
		if (strcmp(__k_cond_keys[i], s) == 0)
			return i;
	}
	return i;
}

struct k_range {
	int left;
	int right;
};

struct __attribute__ ((__packed__)) k_cond {
	uint16_t model_id;
	uint64_t metric_id;
};

#define K_IDX_KEYLEN (sizeof(struct k_cond))

typedef enum k_action_type {
	KMD_ACTION_OTHER,
	KMD_ACTION_RESOLVE,
} k_action_type_t;

/**
 * Chain of action to be used with rule_idx.
 */
struct k_action {
	/* seveirty_range is here because it is not part of the index,
	 * instead it will be tested against before executing the action */
	struct k_range severity_range;
	k_action_type_t type;
	char *cmd;
	LIST_ENTRY(k_action) link;
};

LIST_HEAD(k_action_head, k_action);
typedef struct k_action_head k_action_head_s;
typedef k_action_head_s* k_action_head_t;

/**
 * Rule entry, identified by cond.
 */
struct k_rule {
	struct k_cond cond;
	k_action_head_s action_list;
};

typedef int (*handle_cmd_fn_t)(char*);

#define K_WRAP_STR(X) #X
#define K_WRAP_ENUM(X) X
#define K_WRAP_HANDLE(X) handle_ ## X

#define K_CMD_WRAP_STR(X) "K_CMD_" #X
#define K_CMD_WRAP_ENUM(X) K_CMD_ ## X
#define K_CMD_WRAP_HANDLE(X) handle_K_CMD_ ## X

#define K_CMD_LIST(WRAP) \
	WRAP(ACTION), \
	WRAP(RULE), \
	WRAP(STORE), \
	WRAP(LAST)

const char *__k_cmd_str[] = {
	K_CMD_LIST(K_CMD_WRAP_STR)
};

const char *strcmd[] = {
	K_CMD_LIST(K_WRAP_STR)
};

typedef enum {
	K_CMD_LIST(K_CMD_WRAP_ENUM)
} k_cmd_t;

int handle_K_CMD_ACTION(char *args);
int handle_K_CMD_RULE(char*);
int handle_K_CMD_STORE(char*);
int handle_K_CMD_LAST(char*);

handle_cmd_fn_t handle_cmd_fn[] = {
	K_CMD_LIST(K_CMD_WRAP_HANDLE)
};

k_cmd_t str_k_cmd(const char *str)
{
	int i;
	for (i = 0; i < K_CMD_LAST; i++) {
		if (strcmp(__k_cmd_str[i], str) == 0)
			break;
	}
	return i;
}

/**
 * string command to k_cmd_t
 */
k_cmd_t strcmd_k_cmd(const char *cmd)
{
	int i;
	for (i = 0; i < K_CMD_LAST; i++) {
		if (strcasecmp(strcmd[i], cmd) == 0)
			break;
	}
	return i;
}

const char* k_cmd_str(k_cmd_t cmd)
{
	if (0 <= cmd && cmd < K_CMD_LAST)
		return __k_cmd_str[cmd];
	return "K_CMD_UNKNOWN";
}

struct event_ref {
	void *event_handle;
	struct kmd_store *store;
	LIST_ENTRY(event_ref) entry;
};

struct event_ref_list {
	int count;
	pthread_mutex_t mutex;
	LIST_HEAD(, event_ref) list;
};

struct event_ref_list* event_ref_list_new()
{
	struct event_ref_list *p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;
	pthread_mutex_init(&p->mutex, NULL);
	return p;
}

struct event_ref* event_ref_new(struct kmd_store *store, struct kmd_msg *msg)
{
	struct event_ref *eref = calloc(1, sizeof(*eref));
	if (!eref)
		return NULL;
	eref->store = store;
	eref->event_handle = store->get_event_object(store, msg);
	if (!eref->event_handle) {
		free(eref);
		return NULL;
	}
	return eref;
}

void event_ref_list_get(struct event_ref_list *er_list)
{
	pthread_mutex_lock(&er_list->mutex);
	er_list->count++;
	pthread_mutex_unlock(&er_list->mutex);
}

void event_ref_list_put(struct event_ref_list *er_list)
{
	pthread_mutex_lock(&er_list->mutex);
	er_list->count--;
	pthread_mutex_unlock(&er_list->mutex);
	if (!er_list->count) {
		struct event_ref *eref;
		while (eref = LIST_FIRST(&er_list->list)) {
			LIST_REMOVE(eref, entry);
			eref->store->put_event_object(eref->store,
					eref->event_handle);
			free(eref);
		}
		free(er_list);
	}
}

/**
 * action queue entry.
 */
struct k_act_qentry {
	struct kmd_msg msg; /* the message that cause the action */
	struct k_action *action; /* Reference to the action */
	struct event_ref_list *eref_list;
	char *cmd;
	obj_ref_t evact_ref;
	TAILQ_ENTRY(k_act_qentry) link;
};

static
struct k_act_qentry *k_act_qentry_alloc(struct kmd_msg *msg,
					struct k_action *act,
					struct event_ref_list *eref_list)
{
	struct k_act_qentry *qent = calloc(1, sizeof(*qent));
	if (!qent)
		return NULL;
	qent->msg = *msg; /* copy entire message */
	qent->action = act;
	event_ref_list_get(eref_list);
	qent->eref_list = eref_list;
	return qent;
}

static
void k_act_qentry_free(struct k_act_qentry *e)
{
	if (e->eref_list)
		event_ref_list_put(e->eref_list);
	if (e->cmd)
		free(e->cmd);
	free(e);
}

struct pid_rbn {
	struct rbn rbn; /* rbn->key is pid */
	struct k_act_qentry *act_ent; /**< action entry of the pid, pid_rbn owns the act_ent */
};

static inline pid_t pid_rbn_get_pid(struct pid_rbn *n)
{
	return (pid_t)(uint64_t)n->rbn.key;
}

static
struct pid_rbn *pid_rbn_alloc(pid_t pid, struct k_act_qentry *act_ent)
{
	struct pid_rbn *n = calloc(1, sizeof(*n));
	if (!n)
		return NULL;
	n->rbn.key = (void*)(uint64_t)pid;
	n->act_ent = act_ent;
	return n;
}

static
void pid_rbn_free(struct pid_rbn *n)
{
	k_act_qentry_free(n->act_ent);
	free(n);
}

static int pid_rbn_cmp(void *tree_key, void *key)
{
	pid_t a = (pid_t)(uint64_t)tree_key;
	pid_t b = (pid_t)(uint64_t)key;
	return a - b;
}

/***** GLOBAL VARIABLE *****/

char *conf = NULL;
char *xprt = "sock";
uint16_t port = 55555;
FILE *logfp = NULL;
pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
char *log_path = NULL;
int FOREGROUND = 0;
int32_t act_sos_gn;
sos_t act_sos;
sos_iter_t act_sos_iter = NULL;
const char *act_sos_path = "./act_sos/act";
pthread_mutex_t act_sos_mutex = PTHREAD_MUTEX_INITIALIZER;
event_action_t evact = NULL;
obj_key_t evact_key = NULL;
pid_t pid;

SOS_OBJ_BEGIN(act_sos_class, "ActSOSClass")
	SOS_OBJ_ATTR_WITH_KEY("event_action", SOS_TYPE_BLOB),
	SOS_OBJ_ATTR("pid", SOS_TYPE_INT32),
	SOS_OBJ_ATTR("gn", SOS_TYPE_INT32),
SOS_OBJ_END(3);

static int __add_event(struct k_act_qentry *ent);
static int __remove_event(struct k_act_qentry *ent);

struct rbt pid_rbt; /**< red-black tree for child process info */
pthread_mutex_t pid_rbt_mutex = PTHREAD_MUTEX_INITIALIZER;

struct attr_value_list *av_list;
struct attr_value_list *kw_list;
LIST_HEAD(kstore_head, kmd_store) store_list = {0};
pthread_mutex_t store_list_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * action queue
 */
TAILQ_HEAD(k_act_q, k_act_qentry) actq = TAILQ_HEAD_INITIALIZER(actq);
pthread_mutex_t actq_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t actq_non_empty = PTHREAD_COND_INITIALIZER;

/** action threads */
int N_act_threads = 1;
pthread_t *act_threads;

zap_t zap;
zap_ep_t listen_ep;

str_map_t cmd_map; /* map command_id to command (char*) */
str_map_t act_type_map;

/**
 * rule_idx[rule] |--> chain of actions
 */
idx_t rule_idx;

/**** Utilities ****/

zap_mem_info_t k_meminfo(void)
{
	return NULL;
}

void k_log(const char *fmt, ...)
{
	char tstr[32];
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	strftime(tstr, 32, "%a %b %d %T %Y", &tm);
	pthread_mutex_lock(&log_lock);
	fprintf(stderr, "%s ", tstr);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fflush(stderr);
	pthread_mutex_unlock(&log_lock);
}

/**
 * Trim the trailing white spaces of \c s and return the pointer to the first
 * non white space character of \c s.
 */
char* trim(char *s)
{
	while (*s && isspace(*s))
		s++;
	if (!*s) /* s is now empty */
		return s;
	char *e = s + strlen(s) - 1;
	while (isspace(*e))
		e--;
	*(e+1) = '\0';
	return s;
}

/**
 * Network byte order to host byte order for ::kmd_msg.
 */
void ntoh_kmd_msg(struct kmd_msg *msg)
{
	msg->metric_id = be64toh(msg->metric_id);
	msg->model_id = ntohs(msg->model_id);
	msg->sec = ntohl(msg->sec);
	msg->usec = ntohl(msg->usec);
}

/**
 * Test whether \c a is in the given range \c range.
 * \param range The range to test against.
 * \param a The number to test.
 * \return 0 if \c a is not in \c range.
 * \return 1 if \c a is in \c range.
 */
int in_range(struct k_range *range, int a)
{
	return range->left <= a && a <= range->right;
}

/**** COMMAND HANDLING *****/

/**
 * Parse a single integer value.
 * \param[in,out] s \c *s is the string to parse. On parse success, \c *s will
 * 			to the latest consumed string.
 * \param[out] value \c *value will store the value if the parsing is
 * 			successful.
 * \return 0 on success.
 * \return Error code on failure.
 */
int parse_cond_value(char **s, uint64_t *value)
{
	int i, n;
	char c;
	/* try integer */
	n = sscanf(*s, " %"PRIu64"%n", value, &i);
	if (n != 1) {
		/* try '*' */
		n = sscanf(*s, " %c%n", &c, &i);
		if (n != 1)
			return EINVAL;
		if (c != '*') {
			k_log("Expecting '*' but got '%c'\n", c);
			return EINVAL;
		}
		*value = 0;
	}
	*s = (*s) + i;
	return 0;
}

/**
 * \param[in,out] s \c *s is the input string. On success, \c *s will point to
 * 			just one character after the parsed portion of the
 * 			string.
 * \param[out] r The output range.
 * \return 0 on success.
 * \return Error code on error.
 */
int parse_range(char **s, struct k_range *r)
{
	int n, i, v;
	char c;
	r->left = 0;
	r->right = (1<<(sizeof(int)*8-1))^-1;

normal_range:
	n = sscanf(*s, " [ %d .. %d ]%n", &r->left, &r->right, &i);
	if (n == 2)
		goto out; /* OK */
	/* else, try another format */

leq_range:
	n = sscanf(*s, " [ %c %d ]%n", &c, &v, &i);
	if (n < 2)
		goto number; /* try a number if range does not work */
	switch (c) {
	case '<':
		r->right = v;
		break;
	case '>':
		r->left = v;
		break;
	default:
		k_log("Expecting '<' or '>', but got '%c'\n", c);
		return EINVAL;
	}
	goto out;

number:
	n = sscanf(*s, " %d%n", &v, &i);
	if (n < 1)
		goto star; /* try '*' if number doesn't work */
	r->right = r->left = v;
	goto out;

star:
	n = sscanf(*s, " %c%n", &c, &i);
	if (n < 1)
		return EINVAL;
	if (c != '*') {
		k_log("Expecting '*' or range, but got '%c'\n", c);
		return EINVAL;
	}

out:
	/* step the string pointer */
	*s = *s + i;
	return 0;
}

int parse_condition(char *lb, char *rb, struct k_cond *cond, struct k_range *r)
{
	char key[64];
	char c, d;
	int n, i;
	int rc = EINVAL;
	int model_again = 0;
	int comp_again = 0;
	int severity_again = 0;
	uint64_t value;
	cond->model_id = cond->metric_id = 0;
	r->left = r->right = 0;
	*rb = '\0';
	*lb = ',';
	while (lb < rb) {
		n = sscanf(lb, " %c %[^ =] %c%n", &d, key, &c, &i);
		if (n == EOF)
			break;
		if (n < 2) {
			k_log("Parse error\n");
			goto err;
		}
		if (d != ',') {
			k_log("Parse error, expecting ',' but got '%c'\n", d);
			goto err;
		}
		if (c != '=') {
			k_log("Parse error, expecting '=' but got '%c'\n", c);
			goto err;
		}
		lb += i;
		k_cond_key_t k = k_cond_key(key);
		switch (k) {
		case K_COND_KEY_MODEL:
			if (model_again) {
				k_log("Setting model_id twice\n");
				goto err;
			}
			model_again = 1;
			rc = parse_cond_value(&lb, &value);
			if (rc)
				goto err;
			cond->model_id = (uint16_t)value;
			break;
		case K_COND_KEY_COMP:
			if (comp_again) {
				k_log("Setting metric_id twice\n");
				goto err;
			}
			comp_again = 1;
			rc = parse_cond_value(&lb, &value);
			if (rc)
				goto err;
			cond->metric_id = value;
			break;
		case K_COND_KEY_SEVERITY:
			if (severity_again) {
				k_log("Setting severity_range twice\n");
				goto err;
			}
			severity_again = 1;
			rc = parse_range(&lb, r);
			if (rc)
				goto err;
			break;
		default:
			k_log("Unknown condition key: %s\n", key);
			goto err;
		}
	}

	char *left_over = lb;
	while (lb < rb) {
		if (!isspace(*lb)) {
			k_log("Rule condition parse error, left-over: %s\n",
								left_over);
			goto err;
		}
		lb++;
	}
	rc = 0;
err:
	return rc;
}

struct k_rule* get_rule(struct k_cond *cond)
{
	struct k_rule *rule = idx_find(rule_idx, cond, sizeof(*cond));
	if (rule)
		return rule;
	/* if rule == NULL, create a new one */
	rule = calloc(1, sizeof(*rule));
	rule->cond = *cond;
	int rc = idx_add(rule_idx, cond, K_IDX_KEYLEN, rule);
	if (rc) {
		errno = rc;
		goto err;
	}
	return rule;
err:
	free(rule);
	return NULL;
}

int handle_K_CMD_LAST(char *args)
{
	return ENOSYS;
}

/**
 * Rule command handling.
 * WARNING: \c args is subject to be modified.
 * \param args The arguments of the command.
 * \return 0 on success.
 * \return Error code on error.
 */
int handle_K_CMD_RULE(char *args)
{
	/* Format: { X=Y, ... }: ACTION
	 * ACTION can be action_id or program with args
	 *
	 * This is quite a long function, so please bear with me.
	 */
	uint64_t value;
	int n, i;
	char c;
	char *lb, *rb, *act;
	int rc;

	rc = EINVAL; /* set rc to EINVAL by default */

	/* Handling the rule part */
	lb = strchr(args, '{');
	if (!lb) {
		k_log("Missing '{' in the \n");
		goto out;
	}
	rb = strchr(args, '}');
	if (!rb) {
		k_log("Missing '}' in the \n");
		goto out;
	}

	act = rb+1;
	struct k_cond cond;
	struct k_range range;
	rc = parse_condition(lb, rb, &cond, &range);
	if (rc)
		goto out;

	rc = EINVAL; /* if rc == 0, set it back to EINVAL as default out */

	/* Action part */
	c = 0;
	n = sscanf(act, " %c%n", &c, &i);
	if (c != ':') {
		k_log("Expecting ':', but got '%c'\n", c);
		goto out;
	}
	act = act + i;
	act = trim(act);
	if (! *act) {
		k_log("No action for the \n");
		goto out;
	}
	uint64_t ptr = str_map_get(cmd_map, act);
	char *cmd;
	int free_cmd_on_err = 0;
	if (ptr) {
		/* found */
		cmd = (char*)ptr;
	} else {
		/* not found, just create new cmd out of act */
		cmd = strdup(act);
		if (!cmd) {
			k_log("Cannot create cmd\n");
			rc = ENOMEM;
			goto out;
		}
		free_cmd_on_err = 1;
	}

	/* we have a rule, let's add it into the rule index */
	struct k_rule *rule = get_rule(&cond);
	if (!rule) {
		k_log("Cannot get rule\n");
		rc = ENOMEM;
		goto err0;
	}

	struct k_action *action = calloc(1, sizeof(*action));
	if (!action) {
		k_log("Cannot create action\n");
		goto err0;
		/* It's OK to not clear the rule head */
	}
	action->severity_range = range;
	action->cmd = cmd;
	LIST_INSERT_HEAD(&rule->action_list, action, link);

	rc = 0; /* reset code to no error */
	goto out;

err0:
	if (free_cmd_on_err)
		free(cmd);
out:
	return rc;
}

/**
 * Action command handling.
 * WARNING: \c args is subject to be modified.
 * \param args The arguments of the command.
 * \return 0 on success.
 * \return Error code on error.
 */
int handle_K_CMD_ACTION(char *args)
{
	char action_id[256];
	char c;
	int n, i;
	int rc = EINVAL;
	n = sscanf(args, " %[^ =] %c%n", action_id, &c, &i);
	if (n !=2 || c != '=') {
		k_log("Action parse error.\n");
		goto out;
	}
	args += i;
	char *cmd = (void*)str_map_get(cmd_map, action_id);
	if (cmd) {
		k_log("Double assign action: %s\n", action_id);
		rc = EEXIST;
		goto out;
	}
	args = trim(args);
	cmd = strdup(args);
	if (!cmd) {
		rc = ENOMEM;
		goto out;
	}

	rc = str_map_insert(cmd_map, action_id, (uint64_t)cmd);
	if (rc) {
		free(cmd);
		goto out;
	}
out:
	return rc;
}

int handle_store(struct attr_value_list *av_list)
{
	char pname[128];
	char *plugin = av_value(av_list, "plugin");
	if (!plugin)
		plugin = KSTORE_DEFAULT;
	sprintf(pname, "lib%s.so", plugin);
	void *dh = dlopen(pname, RTLD_LAZY);
	if (!dh) {
		k_log("ERROR: Cannot load store %s: %s\n", plugin, dlerror());
		return ENOENT;
	}
	kmd_create_store_f f = dlsym(dh, "create_store");
	if (!f) {
		k_log("ERROR: %s.create_store: %s\n", plugin, dlerror());
		return ENOENT;
	}
	struct kmd_store *s = f(k_log);
	if (!s) {
		k_log("ERROR: Cannot create store.\n");
		return ENOMEM;
	}
	int rc = s->config(s, av_list);
	if (rc) {
		k_log("ERROR: Store config error: %d\n", rc);
		s->destroy(s);
		return EINVAL;
	}
	LIST_INSERT_HEAD(&store_list, s, entry);
	return 0;
}

int handle_K_CMD_STORE(char *args)
{
	char tmp[4096];
	sprintf(tmp, "store %s", args);

	int rc = tokenize(tmp, kw_list, av_list);
	if (rc) {
		k_log("ERROR: Command tokenization failed, cmd: %s\n", tmp);
		return EINVAL;
	}

	return handle_store(av_list);
}

/**
 * Handle configuration command.
 *
 * WARNING: \c cmd is subject to be modified in command handling call chain.
 *
 * \return 0 on success.
 * \return Error code on error.
 */
int handle_conf_command(char *cmd)
{
	k_log("parsing cmd: %s\n", cmd);
	char *args = strchr(cmd, ' ');
	if (args)
		*args++ = '\0';
	else
		return EINVAL;
	k_cmd_t code = strcmd_k_cmd(cmd);
	if (code < K_CMD_LAST)
		return handle_cmd_fn[code](args);
	/* otherwise, invalid */
	return EINVAL;
}

void print_usage()
{
	printf(
"Synopsis: komondor [OPTIONS]\n"
"\n"
"OPTIONS:\n"
"	-a,--act_sos <file>	Action SOS path (default: ./act_sos/act).\n"
"	-c,--conf <file>	Configuration File.\n"
"	-x,--xprt <XPRT>	Transport (sock, rdma, ugni).\n"
"	-p,--port <PORT>	Port number.\n"
"	-W,--worker-threads <N>	Number of worker threads (default: 1).\n"
"	-F,--foreground		Foreground mode instead of daemon.\n"
"	-z,--ocm_port <PORT>	OCM port number.\n"
"	-h,--help		Print this help message.\n"
"\n"
	);
}

#define BASH "/bin/bash"

pid_t execute(const char *cmd)
{
	k_log("INFO: executing: %s\n", cmd);
	char *argv[] = {BASH, "-c", NULL, NULL};
	pid_t pid = fork();
	int i, flags;
	if (pid)
		return pid;
	/* close all parent's file descriptor */
	for (i = getdtablesize() - 1; i > -1; i--) {
		close(i); /* blindly closes everything before exec */
	}
	argv[2] = (char*)cmd;
	execv(BASH, argv);
}

/**
 * \return 0 if evact is running with PID \c pid
 * \return -1 otherwise
 */
static int __check_running(event_action_t evact, pid_t pid)
{
	char buf[2048];
	int fd;
	ssize_t count;
	int i;
	sprintf(buf, "/proc/%d/cmdline", pid);
	fd = open(buf, O_RDONLY);
	if (fd < 0)
		return -1;
	count = read(fd, buf, sizeof(buf) - 1);
	if (count < 0) {
		k_log("ERROR: %s: read error: %d\n", __func__, errno);
		assert(0);
	}
	buf[count] = 0;
	for (i = 0; i < count; i++) {
		if (buf[i] == 0)
			buf[i] = ' ';
	}
	if (strstr(buf, evact->data.action))
		return 0;
	return -1;
}

/**
 * \return 0 if OK
 * \return Error code if error
 */
static int __add_event(struct k_act_qentry *ent)
{
	int rc = 0;
	int32_t gn;
	pid_t pid;
	obj_ref_t ref;
	sos_obj_t obj;
	pthread_mutex_lock(&act_sos_mutex);
	evact->data.metric_id = ent->msg.metric_id;
	evact->data.model_id = ent->msg.model_id;
	strcpy(evact->data.action, ent->action->cmd);
	evact->blob.len = evact_blob_len(evact);
	obj_key_set(evact_key, evact, SOS_BLOB_SIZE(&evact->blob));
	rc = sos_iter_seek(act_sos_iter, evact_key);
	if (rc == 0) {
		/* event exists in DB, check if it is still running. */
		obj = sos_iter_obj(act_sos_iter);
		pid = sos_obj_attr_get_int32(act_sos, 1, obj);
		gn = sos_obj_attr_get_int32(act_sos, 2, obj);
		if (gn == act_sos_gn) {
			/* Komondor has not restarted, the event will be cleaned
			 * in sigchld_action() */
			rc = EEXIST;
			goto out;
		}
		if (__check_running(evact, pid) == 0) {
			rc = EEXIST;
			goto out;
		}
		/* it is not running, and from old komondor run -- remove it */
		sos_obj_remove(act_sos, obj);
		sos_obj_delete(act_sos, obj);

	}
	obj = sos_obj_new(act_sos);
	if (!obj) {
		rc = ENOMEM;
		goto out;
	}
	ref = ods_obj_ptr_to_ref(act_sos->ods, obj);
	sos_obj_attr_set(act_sos, 0, obj, evact);
	obj = ods_obj_ref_to_ptr(act_sos->ods, ref);
	rc = sos_obj_add(act_sos, obj);
	if (rc) {
		sos_obj_delete(act_sos, obj);
		rc = ENOMEM;
		goto out;
	}
	ent->evact_ref = ref;
out:
	pthread_mutex_unlock(&act_sos_mutex);
	return rc;
}

static int __remove_event(struct k_act_qentry *ent)
{
	assert(ent->evact_ref);

	pthread_mutex_lock(&act_sos_mutex);
	sos_obj_t obj = ods_obj_ref_to_ptr(act_sos->ods, ent->evact_ref);
	sos_obj_remove(act_sos, obj);
	sos_obj_delete(act_sos, obj);
	pthread_mutex_unlock(&act_sos_mutex);

	return 0;

}

void* act_thread_routine(void *arg)
{
	int rc;
	struct event_ref *eref;
	sos_obj_t obj;
	char *str;
	pid_t pid;
	char *cmd = malloc(65536);
	if (!cmd) {
		k_log("ERROR in %s: Cannot allocate command buffer\n", __func__);
		return NULL;
	}

	/* Action threads should not handle SIGCHLD */
	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGCHLD);
	rc = pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	if (rc) {
		k_log("pthread_sigmask: errno %d\n", errno);
		assert(0);
	}

loop:
	pthread_mutex_lock(&actq_mutex);
	while (TAILQ_EMPTY(&actq))
		pthread_cond_wait(&actq_non_empty, &actq_mutex);
	struct k_act_qentry *ent = TAILQ_FIRST(&actq);
	TAILQ_REMOVE(&actq, ent, link);
	pthread_mutex_unlock(&actq_mutex);
	rc = __add_event(ent);
	if (rc) {
		k_log("__add_event rc: %d\n", rc);
		k_act_qentry_free(ent);
		goto loop;
	}
	/* XXX ADD KMD_MSG here */
	sprintf(cmd, "KMD_MODEL_ID=%hu KMD_METRIC_ID=%lu "
			"KMD_SEVERITY_LEVEL=%hhu"
			" KMD_SEC=%"PRIu32" KMD_USEC=%"PRIu32" KMD_COMP_ID=%u"
			" KMD_METRIC_TYPE=%u %s",
			ent->msg.model_id, ent->msg.metric_id, ent->msg.level,
			ent->msg.sec, ent->msg.usec,
			(uint32_t) (ent->msg.metric_id >> 32),
			(uint32_t) (ent->msg.metric_id & 0xFFFFFFFF),
			ent->action->cmd);
	ent->cmd = strdup(cmd);
	if (!ent->cmd) {
		k_log("ERROR: Not enough memory (command: '%s')\n", cmd);
		k_act_qentry_free(ent);
		goto loop;
	}
	if (ent->action->type == KMD_ACTION_RESOLVE) {
		LIST_FOREACH(eref, &ent->eref_list->list, entry) {
			eref->store->event_update(eref->store,
					eref->event_handle,
					KMD_EVENT_RESOLVING);
		}
	}
	pid = execute(ent->cmd);
	if (pid == -1) {
		k_log("ERROR: Failed to execute command '%s'\n", ent->cmd);
		__remove_event(ent);
		/*
		 * TODO: XXX: Consider whether we need to update the
		 * event status to be KMD_EVENT_RESOLVE_FAILED or
		 * another status
		 */
		k_act_qentry_free(ent);
		goto loop;
	}

	obj = ods_obj_ref_to_ptr(act_sos->ods, ent->evact_ref);
	sos_obj_attr_set_int32(act_sos, 1, obj, pid);
	sos_obj_attr_set_int32(act_sos, 2, obj, act_sos_gn);

	struct pid_rbn *n = pid_rbn_alloc(pid, ent);
	if (!n) {
		k_log("ERROR in %s: Cannot allocate pid_rbn.\n", __func__);
		k_act_qentry_free(ent);
		goto loop;
	}

	pthread_mutex_lock(&pid_rbt_mutex);
	rbt_ins(&pid_rbt, (void*)n);
	pthread_mutex_unlock(&pid_rbt_mutex);

	goto loop;
}

static int __act_sos_gn_routine()
{
	sos_obj_t obj;
	obj_ref_t objref;
	int rc;
	pthread_mutex_lock(&act_sos_mutex);
	evact->data.model_id = 0;
	evact->data.metric_id = 0;
	evact->data.action[0] = 0;
	evact->blob.len = evact_blob_len(evact);
	obj_key_set(evact_key, evact, SOS_BLOB_SIZE(&evact->blob));
	rc = sos_iter_seek(act_sos_iter, evact_key);
	if (rc == 0) {
		obj = sos_iter_obj(act_sos_iter);
		act_sos_gn = sos_obj_attr_get_int32(act_sos, 2, obj);
	} else {
		obj = sos_obj_new(act_sos);
		if (!obj) {
			rc = ENOMEM;
			goto out;
		}
		objref = ods_obj_ptr_to_ref(act_sos->ods, obj);
		sos_obj_attr_set_int32(act_sos, 1, obj, 0);
		sos_obj_attr_set_int32(act_sos, 2, obj, 0);
		sos_obj_attr_set(act_sos, 0, obj, evact);
		obj = ods_obj_ref_to_ptr(act_sos->ods, objref);
		rc = sos_obj_add(act_sos, obj);
		if (rc) {
			sos_obj_delete(act_sos, obj);
			rc = ENOMEM;
			goto out;
		}
		act_sos_gn = 0;
	}
	act_sos_gn++;
	sos_obj_attr_set_int32(act_sos, 2, obj, act_sos_gn);
	sos_commit(act_sos, ODS_COMMIT_ASYNC);
	rc = 0;
out:
	pthread_mutex_unlock(&act_sos_mutex);
	return rc;
}

void init()
{
	av_list = av_new(256);
	if (!av_list) {
		perror("av_new");
		_exit(-1);
	}
	kw_list = av_new(256);
	if (!kw_list) {
		perror("av_new");
		_exit(-1);
	}
	cmd_map = str_map_create(4099);
	if (!cmd_map) {
		k_log("%s: cannot create str_map for cmd_map\n", strerror(errno));
		exit(-1);
	}
	act_type_map = str_map_create(4099);
	if (!act_type_map) {
		k_log("%s: cannot create str_map for act_type_map\n", strerror(errno));
		exit(-1);
	}
	rule_idx = idx_create();
	if (!rule_idx) {
		k_log("Cannot create rule index\n");
		exit(-1);
	}
	rbt_init(&pid_rbt, pid_rbn_cmp);
	act_threads = malloc(N_act_threads * sizeof(act_threads[0]));
	if (!act_threads) {
		k_log("%s: cannot create act_threads\n", strerror(errno));
	}
	int i;
	for (i = 0; i < N_act_threads; i++) {
		pthread_create(&act_threads[i], NULL, act_thread_routine, NULL);
	}

	act_sos = sos_open(act_sos_path, O_RDWR|O_CREAT, 0660, &act_sos_class);
	if (!act_sos) {
		k_log("Cannot open sos: %s\n", act_sos_path);
		exit(-1);
	}

	act_sos_iter = sos_iter_new(act_sos, 0);
	if (!act_sos_iter) {
		k_log("Cannot create iterator for sos: %s\n", act_sos_path);
		exit(-1);
	}

	evact = malloc(65536);
	if (!evact) {
		k_log("Cannot preallocate evact\n");
		exit(-1);
	}

	evact_key = obj_key_new(65536);
	if (!evact_key) {
		k_log("Cannot preallocate evact_key\n");
		exit(-1);
	}

	__act_sos_gn_routine();

	int rc;
	rc = zap_get(xprt, &zap, k_log, k_meminfo);
	if (rc) {
		k_log("zap_get failed: %m\n");
		exit(-1);
	}
#if ENABLE_OCM
	ocm = ocm_create("sock", ocm_port, ocm_cb, k_log);
	if (!ocm) {
		k_log("Cannot create ocm\n");
		exit(-1);
	}
	char h[128];
	rc = gethostname(h, 128);
	if (rc) {
		k_log("gethostname failed: %m\n");
		exit(-1);
	}
	sprintf(ocm_key, "%s/komondor", h);
	ocm_register(ocm, ocm_key, ocm_cb);
	rc = ocm_enable(ocm);
	if (rc) {
		k_log("ocm_enable failed: %d\n", rc);
		exit(-1);
	}
#endif
}

FILE *kmd_open_log()
{
	FILE *f = fopen(log_path, "a");
	if (!f) {
		k_log("Cannot open log file %s\n", log_path);
		exit(-1);
	}
	int fd = fileno(f);
	if (dup2(fd, 1) < 0) {
		k_log("Cannot redirect log to %s\n", log_path);
		exit(-1);
	}
	if (dup2(fd, 2) < 0) {
		k_log("Cannot redirect log to %s\n", log_path);
		exit(-1);
	}
	return f;
}

void kmd_logrotate(int x)
{
	if (log_path) {
		pthread_mutex_lock(&log_lock);
		FILE *new_log = kmd_open_log();
		fflush(logfp);
		fclose(logfp);
		logfp = new_log;
		pthread_mutex_unlock(&log_lock);
	}
}

void process_args(int argc, char **argv)
{
	char c;

next_arg:
	c = getopt_long(argc, argv, short_opt, long_opt, NULL);
	switch (c) {
	case 'a':
		act_sos_path = optarg;
		break;
	case 'c':
		conf = optarg;
		break;
	case 'x':
		xprt = optarg;
		break;
	case 'p':
		errno = 0;
		port = strtoul(optarg, NULL, 10);
		if (errno) {
			perror("strtoul");
			exit(-1);
		}
		break;
	case 'W':
		N_act_threads = atoi(optarg);
		if (!N_act_threads) {
			k_log("%s is not a number.\n", optarg);
			exit(-1);
		}
		break;
	case 'z':
#ifdef ENABLE_OCM
		ocm_port = atoi(optarg);
#else
		k_log("OCM is not enabled.\n");
		exit(-1);
#endif
		break;
	case 'l':
		log_path = strdup(optarg);
		if (!log_path) {
			k_log("Failed to copy the log path\n");
			exit(ENOMEM);
		}
		logfp = kmd_open_log();
		break;
	case 'F':
		FOREGROUND = 1;
		break;
	case 'h':
	case '?':
		print_usage();
		exit(-1);
	case -1:
		goto out;
	default:
		k_log("Unknown argument: %s\n", argv[optind]);
		exit(-1);
	}

	goto next_arg;

out:
	return;
}

/**
 * Configure the komondor.
 */
void config()
{
	if (!conf)
		return; /* No configuration file */
	FILE *f = fopen(conf, "rt");
	if (!f) {
		k_log("Cannot open file: %s: %s\n", conf, strerror(errno));
		exit(-1);
	}
	char buff[4096];
	char kmd[4096];
	char *str;
	int n, i;
	while (fgets(buff, sizeof(buff), f)) {
		str = trim(buff);
		switch (*str) {
		case 0:   /* empty string */
		case '#': /* commented out */
			continue;
		}
		if (handle_conf_command(str)) {
			k_log("Invalid parsing in command: %s\n", str);
			exit(-1);
		}
	}
}

void handle_action_list(k_action_head_t a_list, struct kmd_msg *msg,
			struct event_ref_list *eref_list)
{
	struct k_action *act;
	LIST_FOREACH(act, a_list, link) {
		if (!in_range(&act->severity_range, msg->level))
			continue;
		struct k_act_qentry *qent = k_act_qentry_alloc(msg, act, eref_list);
		if (!qent) {
			k_log("Cannot allocate qent for msg:\n"
				" <%hu, %"PRIu64", %hhu, %"PRIu32", %"PRIu32">",
				msg->model_id, msg->metric_id, msg->level,
				msg->sec, msg->usec);
			continue;
		}
		pthread_mutex_lock(&actq_mutex);
		TAILQ_INSERT_TAIL(&actq, qent, link);
		pthread_cond_signal(&actq_non_empty);
		pthread_mutex_unlock(&actq_mutex);
	}
}

void log_zap_event(struct kmd_msg *msg)
{
	k_log("INFO: <%u.%u, %hu, %lu, %hhu>\n", msg->sec, msg->usec,
			msg->model_id, msg->metric_id, msg->level);
}

void process_recv(zap_ep_t ep, zap_event_t ev)
{
	if (ev->data_len != sizeof(struct kmd_msg)) {
		k_log("Wrong Komondor message length: %zu bytes\n",
								ev->data_len);
		return;
	}
	struct kmd_msg *msg = ev->data;
	ntoh_kmd_msg(msg);
	log_zap_event(msg);

	struct kmd_store *store;
	struct event_ref *eref;
	struct event_ref_list *eref_list = event_ref_list_new();
	if (!eref_list) {
		k_log("ERROR: Out of memory at %s:%d:%s\n", __FILE__, __LINE__,
				__func__);
		goto err0;
	}
	event_ref_list_get(eref_list);
	pthread_mutex_lock(&store_list_lock);
	LIST_FOREACH(store, &store_list, entry) {
		eref = event_ref_new(store, msg);
		if (!eref) {
			pthread_mutex_unlock(&store_list_lock);
			k_log("ERROR: Out of memory at %s:%d:%s\n", __FILE__,
					__LINE__, __func__);
			goto err0;
		}
		LIST_INSERT_HEAD(&eref_list->list, eref, entry);
	}
	pthread_mutex_unlock(&store_list_lock);

	struct k_rule *rule;
	struct k_cond cond = {0};
	/* (*, *) */
	rule = idx_find(rule_idx, &cond, sizeof(cond));
	if (rule)
		handle_action_list(&rule->action_list, msg, eref_list);
	/* (M, *) */
	cond.model_id = msg->model_id;
	rule = idx_find(rule_idx, &cond, sizeof(cond));
	if (rule)
		handle_action_list(&rule->action_list, msg, eref_list);
	/* (M, C) */
	cond.metric_id = msg->metric_id;
	rule = idx_find(rule_idx, &cond, sizeof(cond));
	if (rule)
		handle_action_list(&rule->action_list, msg, eref_list);
	/* (*, C) */
	cond.model_id = 0;
	rule = idx_find(rule_idx, &cond, sizeof(cond));
	if (rule)
		handle_action_list(&rule->action_list, msg, eref_list);

	/* Intended to pass through */
err0:
	event_ref_list_put(eref_list);
}

void k_zap_cb(zap_ep_t ep, zap_event_t ev)
{
	zap_err_t zerr;
	switch (ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		zerr = zap_accept(ep, k_zap_cb);
		if (zerr != ZAP_ERR_OK) {
			k_log("zap_accept error: %s\n", zap_err_str(zerr));
		}
		break;
	case ZAP_EVENT_RECV_COMPLETE:
		process_recv(ep, ev);
		break;
	case ZAP_EVENT_DISCONNECTED:
		/* Peer disconnected ... do nothing */
		k_log("INFO: Peer disconnected\n");
		zap_close(ep);
		break;
	case ZAP_EVENT_CONNECT_ERROR:
		k_log("ERROR: Error in zap connection.\n");
		zap_close(ep);
		break;
	case ZAP_EVENT_CONNECTED:
		k_log("INFO: Peer connected\n");
		break;
	default:
		k_log("Unhandled zap event: %s\n", zap_event_str(ev->type));
	}
}

#ifdef ENABLE_OCM
void handle_ocm_cmd_RULE(ocm_cfg_cmd_t cmd)
{
	const struct ocm_value *ov_model_id = ocm_av_get_value(cmd, "model_id");
	const struct ocm_value *ov_metric_id = ocm_av_get_value(cmd, "metric_id");
	const struct ocm_value *ov_severity = ocm_av_get_value(cmd, "severity");
	const struct ocm_value *ov_action = ocm_av_get_value(cmd, "action");

	if (!ov_model_id) {
		k_log("ocm: error, RULE missing 'model_id' attribute.\n");
		return;
	}
	if (!ov_metric_id) {
		k_log("ocm: error, RULE missing 'metric_id' attribute.\n");
		return;
	}
	if (!ov_severity) {
		k_log("ocm: error, RULE missing 'severity' attribute.\n");
		return;
	}
	if (!ov_action) {
		k_log("ocm: error, RULE missing 'action' attribute.\n");
		return;
	}
	struct k_cond cond;
	cond.model_id = ov_model_id->u16;
	cond.metric_id = ov_metric_id->u64;
	const char *exec = (const char *)str_map_get(cmd_map, ov_action->s.str);
	enum k_action_type act_type = (int) str_map_get(act_type_map, ov_action->s.str);
	if (!exec) {
		k_log("ocm: error, action '%s' not found.\n", ov_action->s.str);
		return;
	}

	struct k_rule *rule = get_rule(&cond);
	if (!rule) {
		k_log("ocm: error, cannot get/create rule, errno: %d\n", errno);
		return;
	}

	struct k_action *action = calloc(1, sizeof(*action));
	if (!action) {
		k_log("ocm: error, cannot create action, errno: %d\n", errno);
		return;
		/* It's OK to not clear the rule head */
	}
	action->severity_range.left = ov_severity->i16;
	action->severity_range.right = ov_severity->i16;
	action->cmd = (char*)exec;
	action->type = act_type;
	LIST_INSERT_HEAD(&rule->action_list, action, link);
}

void handle_ocm_cmd_ACTION(ocm_cfg_cmd_t cmd)
{
	const struct ocm_value *action_id = ocm_av_get_value(cmd, "name");
	const struct ocm_value *action_exec = ocm_av_get_value(cmd, "execute");
	const struct ocm_value *action_type = ocm_av_get_value(cmd, "type");
	enum k_action_type type = KMD_ACTION_OTHER;
	if (!action_id) {
		k_log("ocm: error 'name' is missing from verb 'action'\n");
		return;
	}
	if (!action_exec) {
		k_log("ocm: error 'exec' is missing from verb 'action'\n");
		return;
	}
	if (action_type) {
		int x = atoi(action_type->s.str);
		if (x == KMD_ACTION_RESOLVE)
			type = KMD_ACTION_RESOLVE;
	}
	char *exec = strdup(action_exec->s.str);
	if (!exec) {
		k_log("ocm: ENOMEM at %s:%d in %s.\n", __FILE__, __LINE__,
				__func__);
		return ;
	}
	int rc = str_map_insert(cmd_map, action_id->s.str, (uint64_t)exec);
	if (rc) {
		free(exec);
		k_log("ocm: action insertion error code: %d, %s.\n", rc,
				strerror(rc));
		return ;
	}

	if (type == KMD_ACTION_OTHER)
		return;

	rc = str_map_insert(act_type_map, action_id->s.str, type);
	if (rc) {
		str_map_remove(cmd_map, action_id->s.str);
		free(exec);
		k_log("ocm: action type insertion error code: %d, %s.\n", rc,
				strerror(rc));
	}
}

int ocm_cfg_to_av_list(ocm_cfg_cmd_t cmd, struct attr_value_list *av_list)
{
	struct ocm_av_iter itr;
	ocm_av_iter_init(&itr, cmd);
	int count = 0;
	const char *attr;
	const struct ocm_value *value;
	while (ocm_av_iter_next(&itr, &attr, &value) == 0) {
		if (count == av_list->size) {
			k_log("ocm: error, too many command arguments.\n");
			return EINVAL;
		}
		av_list->list[count].name = (void*)attr;
		av_list->list[count].value = (void*)value->s.str;
		count++;
	}
	av_list->count = count;
	return 0;
}

void handle_ocm_cmd_STORE(ocm_cfg_cmd_t cmd)
{
	int rc = ocm_cfg_to_av_list(cmd, av_list);
	if (rc) {
		k_log("ocm: error, ocm_cfg_to_av_list: %d\n", rc);
		return;
	}
	handle_store(av_list);
}

void process_ocm_cmd(ocm_cfg_cmd_t cmd)
{
	const char *verb = ocm_cfg_cmd_verb(cmd);
	k_cmd_t kmd = strcmd_k_cmd(verb);
	switch (kmd) {
	case K_CMD_ACTION:
		handle_ocm_cmd_ACTION(cmd);
		break;
	case K_CMD_RULE:
		handle_ocm_cmd_RULE(cmd);
		break;
	case K_CMD_STORE:
		handle_ocm_cmd_STORE(cmd);
		break;
	default:
		k_log("ocm: error, unknown verb '%s'\n", verb);
	}
}

void process_ocm_cfg(ocm_cfg_t cfg)
{
	ocm_cfg_cmd_t cmd;
	struct ocm_cfg_cmd_iter iter;
	ocm_cfg_cmd_iter_init(&iter, cfg);
	while (ocm_cfg_cmd_iter_next(&iter, &cmd) == 0) {
		process_ocm_cmd(cmd);
	}
}

int ocm_cb(struct ocm_event *e)
{
	switch (e->type) {
	case OCM_EVENT_CFG_REQUESTED:
		ocm_event_resp_err(e, ENOSYS, ocm_cfg_req_key(e->req),
							"Not implemented.");
		break;
	case OCM_EVENT_CFG_RECEIVED:
		process_ocm_cfg(e->cfg);
		break;
	case OCM_EVENT_ERROR:
		k_log("ocm event error, key: %s, code: %d, msg: %s\n",
				ocm_err_key(e->err),
				ocm_err_code(e->err),
				ocm_err_msg(e->err));
		break;
	}
	return 0;
}
#endif

/**
 * Listen to the \c port, using transport \c xprt.
 */
void k_listen()
{
	zap_err_t zerr;
	zerr = zap_new(zap, &listen_ep, k_zap_cb);
	if (zerr != ZAP_ERR_OK) {
		k_log("zap_new error: %s\n", zap_err_str(zerr));
		exit(-1);
	}
	struct sockaddr_in sin = {0};
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	zap_listen(listen_ep, (void*)&sin, sizeof(sin));
}

void kmd_cleanup(int x)
{
	k_log("Komondor Daemon exiting ... status %d\n", x);

	/* Destroy all stores */
	struct kmd_store *s;
	pthread_mutex_lock(&store_list_lock);
	s = LIST_FIRST(&store_list);
	while (s) {
		LIST_REMOVE(s, entry);
		s->destroy(s);
		s = LIST_FIRST(&store_list);
	}
	pthread_mutex_unlock(&store_list_lock);

	exit(x);
}

void child_terminated_handler(pid_t pid, int status)
{
	struct event_ref *eref;
	struct pid_rbn *n;
	struct k_act_qentry *ent;

	pthread_mutex_lock(&pid_rbt_mutex);
	n = (void*)rbt_find(&pid_rbt, (void*)(uint64_t)pid);
	if (!n) {
#ifdef DEBUG
		k_log("WARN: %s: cannot find entry for pid: %d\n", __func__, pid);
#endif
		pthread_mutex_unlock(&pid_rbt_mutex);
		return;
	}
	ent = n->act_ent; /* NOTE: n owns ent */
	__remove_event(ent);
	rbt_del(&pid_rbt, (void*)n);
	pthread_mutex_unlock(&pid_rbt_mutex);

	int rc_sig; /* rc or sig */

	if (WIFSIGNALED(status))
		goto signaled;

exited: /* child exited */
	rc_sig = WEXITSTATUS(status);
	switch (rc_sig) {
	case KMD_RESOLVED_CODE:
		/* Change event status to resolved */
		LIST_FOREACH(eref, &ent->eref_list->list, entry) {
			eref->store->event_update(eref->store, eref->event_handle,
					KMD_EVENT_RESOLVED);
		}
		break;
	case 0:
		/* do nothing */
		break;
	default:
		k_log("Error %d in command: %s\n", rc_sig, ent->cmd);
		if (ent->action->type == KMD_ACTION_RESOLVE) {
			LIST_FOREACH(eref, &ent->eref_list->list, entry) {
				eref->store->event_update(eref->store,
						eref->event_handle,
						KMD_EVENT_RESOLVE_FAIL);
			}
		}
		break;
	}

	goto cleanup;

signaled: /* child terminated by signal */
	rc_sig = WTERMSIG(status);
	k_log("Error: command: %s got signal %d\n", ent->cmd, rc_sig);
	if (ent->action->type == KMD_ACTION_RESOLVE) {
		LIST_FOREACH(eref, &ent->eref_list->list, entry) {
			eref->store->event_update(eref->store,
					eref->event_handle,
					KMD_EVENT_RESOLVE_FAIL);
		}
	}

cleanup:
	pid_rbn_free(n);
}

static void sigchld_action(int sig, siginfo_t *siginfo, void *ctxt)
{
	pid_t pid;
	int status;
	int rc;
loop:
	pid = waitpid(-1, &status, WNOHANG);
	if (pid < 0) {
		k_log("ERROR in %s:  waitpid return %d, errno: %d\n",
				__func__, pid, errno);
		return;
	}

	if (pid == 0) {
		return; /* nothing to do */
	}

	if (WIFEXITED(status) || WIFSIGNALED(status)) {
		child_terminated_handler(pid, status);
	}
	/* otherwise, do nothing */
	goto loop;
}

int main(int argc, char **argv)
{
	int rc;

	struct sigaction cleanup_act, logrotate_act;
	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGCHLD);
	sigaddset(&sigset, SIGUSR1);
	cleanup_act.sa_handler = kmd_cleanup;
	cleanup_act.sa_flags = 0;
	cleanup_act.sa_mask = sigset;

	sigaction(SIGHUP, &cleanup_act, NULL);
	sigaction(SIGINT, &cleanup_act, NULL);
	sigaction(SIGTERM, &cleanup_act, NULL);

	sigaddset(&sigset, SIGHUP);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGTERM);
	logrotate_act.sa_handler = kmd_logrotate;
	logrotate_act.sa_flags = 0;
	logrotate_act.sa_mask = sigset;
	sigaction(SIGUSR1, &logrotate_act, NULL);

	process_args(argc, argv);
	if (!FOREGROUND) {
		/* daemonize  */
		rc = daemon(1, 1);
		if (rc) {
			perror("daemon");
			exit(-1);
		}
	}

	k_log("Komondor Daemon initializing ...\n");
	umask(0);
	init();

	sigset_t child_set;
	sigemptyset(&child_set);
	sigaddset(&child_set, SIGCHLD);
	sigaddset(&child_set, SIGHUP);
	sigaddset(&child_set, SIGINT);
	sigaddset(&child_set, SIGTERM);
	sigaddset(&child_set, SIGUSR1);
	struct sigaction child_act = {
		.sa_sigaction = sigchld_action,
		.sa_mask = child_set,
		.sa_flags = SA_SIGINFO|SA_NOCLDSTOP,
	};
	rc = sigaction(SIGCHLD, &child_act, NULL);
	if (rc) {
		k_log("sigaction: error %d\n", errno);
		assert(0);
	}

	config();
	k_listen();

	k_log("Komondor Daemon started.\n");

	int i;
	for (i = 0; i < N_act_threads; i++)
		pthread_join(act_threads[i], NULL);

	return 0;
}
