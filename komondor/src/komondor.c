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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <dlfcn.h>

#include <netinet/ip.h>

#include "komondor.h"

#include "coll/str_map.h"
#include "coll/idx.h"
#include "zap/zap.h"
#include "sos/sos.h"

#ifdef ENABLE_OCM
#include "ocm/ocm.h"
ocm_t ocm;
char ocm_key[512]; /**< $HOSTNAME/komondor */
uint16_t ocm_port = OCM_DEFAULT_PORT;
int ocm_cb(struct ocm_event *e);
#endif

/***** PROGRAM OPTIONS *****/
const char *short_opt = "c:x:p:l:W:Fz:h?";

static struct option long_opt[] = {
	{"conf",            required_argument,  0,  'c'},
	{"xprt",            required_argument,  0,  'x'},
	{"port",            required_argument,  0,  'p'},
	{"log",             required_argument,  0,  'l'},
	{"worker-threads",  required_argument,  0,  'W'},
	{"foreground",      no_argument,        0,  'F'},
	{"ocm_port",        required_argument,  0,  'z'},
	{"help",            no_argument,        0,  'h'},
	{0,                 0,                  0,  0}
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
	k_log("DEBUG: getting, ref: %d\n", er_list->count);
	pthread_mutex_unlock(&er_list->mutex);
}

void event_ref_list_put(struct event_ref_list *er_list)
{
	pthread_mutex_lock(&er_list->mutex);
	er_list->count--;
	k_log("DEBUG: putting, ref: %d\n", er_list->count);
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
	TAILQ_ENTRY(k_act_qentry) link;
};

/***** GLOBAL VARIABLE *****/

char *conf = NULL;
char *xprt = "sock";
uint16_t port = 55555;
int FOREGROUND = 0;
sos_t event_sos = NULL; /**< Event storage */

struct attr_value_list *av_list;
struct attr_value_list *kw_list;
LIST_HEAD(kstore_head, kmd_store) store_list = {0};

/**
 * action queue
 */
TAILQ_HEAD(k_act_q, k_act_qentry) actq = TAILQ_HEAD_INITIALIZER(actq);
pthread_mutex_t actq_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t actq_non_empty = PTHREAD_COND_INITIALIZER;

/** action threads */
int N_act_threads = 2;
pthread_t *act_threads;

zap_t zap;
zap_ep_t listen_ep;

str_map_t cmd_map; /* map command_id to command (char*) */

/**
 * rule_idx[rule] |--> chain of actions
 */
idx_t rule_idx;

/**** Utilities ****/

zap_mem_info_t k_meminfo(void)
{
	return NULL;
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
	struct kmd_store *(*f)() = dlsym(dh, "create_store");
	if (!f) {
		k_log("ERROR: %s.create_store: %s\n", plugin, dlerror());
		return ENOENT;
	}
	struct kmd_store *s = f();
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
"	-c,--conf <file>	Configuration File.\n"
"	-x,--xprt <XPRT>	Transport (sock, rdma, ugni).\n"
"	-p,--port <PORT>	Port number.\n"
"	-W,--worker-threads <N>	Number of worker threads (default: 2).\n"
"	-F,--foreground		Foreground mode instead of daemon.\n"
"	-z,--ocm_port <PORT>	OCM port number.\n"
"	-h,--help		Print this help message.\n"
"\n"
	);
}

void* act_thread_routine(void *arg)
{
	struct event_ref *eref;
	char cmd[4096];
loop:
	pthread_mutex_lock(&actq_mutex);
	if (TAILQ_EMPTY(&actq))
		pthread_cond_wait(&actq_non_empty, &actq_mutex);
	struct k_act_qentry *ent = TAILQ_FIRST(&actq);
	TAILQ_REMOVE(&actq, ent, link);
	pthread_mutex_unlock(&actq_mutex);
	/* XXX ADD KMD_MSG here */
	sprintf(cmd, "KMD_MODEL_ID=%hu KMD_METRIC_ID=%lu KMD_SEVERITY_LEVEL=%hhu"
			" KMD_SEC=%"PRIu32" KMD_USEC=%"PRIu32" KMD_COMP_ID=%u"
			" KMD_METRIC_TYPE=%u %s",
			ent->msg.model_id, ent->msg.metric_id, ent->msg.level,
			ent->msg.sec, ent->msg.usec,
			(uint32_t) (ent->msg.metric_id >> 32),
			(uint32_t) (ent->msg.metric_id & 0xFFFFFFFF),
			ent->action->cmd);
	if (ent->action->type == KMD_ACTION_RESOLVE) {
		LIST_FOREACH(eref, &ent->eref_list->list, entry) {
			eref->store->event_update(eref->store,
					eref->event_handle,
					KMD_EVENT_RESOLVING);
		}
	}
	int rc = system(cmd);
	switch (rc) {
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
		k_log("Error %d in command: %s\n", rc, cmd);
		if (ent->action->type == KMD_ACTION_RESOLVE) {
			LIST_FOREACH(eref, &ent->eref_list->list, entry) {
				eref->store->event_update(eref->store,
						eref->event_handle,
						KMD_EVENT_RESOLVE_FAIL);
			}
		}
		break;
	}
	event_ref_list_put(ent->eref_list);
	free(ent);
	goto loop;
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
	rule_idx = idx_create();
	act_threads = malloc(N_act_threads * sizeof(act_threads[0]));
	if (!act_threads) {
		k_log("%s: cannot create act_threads\n", strerror(errno));
	}
	int i;
	for (i = 0; i < N_act_threads; i++) {
		pthread_create(&act_threads[i], NULL, act_thread_routine, NULL);
	}

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
	ocm_enable(ocm);
#endif
}

void handle_log(const char *path)
{
	FILE *f = fopen(path, "a");
	if (!f) {
		k_log("Cannot open log file %s\n", path);
		exit(-1);
	}
	int fd = fileno(f);
	if (dup2(fd, 1) < 0) {
		k_log("Cannot redirect log to %s\n", path);
		exit(-1);
	}
	if (dup2(fd, 2) < 0) {
		k_log("Cannot redirect log to %s\n", path);
		exit(-1);
	}
}

void process_args(int argc, char **argv)
{
	char c;

next_arg:
	c = getopt_long(argc, argv, short_opt, long_opt, NULL);
	switch (c) {
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
		handle_log(optarg);
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
		struct k_act_qentry *qent = calloc(1, sizeof(*qent));
		if (!qent) {
			k_log("Cannot allocate qent for msg:\n"
				" <%hu, %"PRIu64", %hhu, %"PRIu32", %"PRIu32">",
				msg->model_id, msg->metric_id, msg->level,
				msg->sec, msg->usec);
			continue;
		}
		qent->msg = *msg; /* copy entire message */
		qent->action = act;
		event_ref_list_get(eref_list);
		qent->eref_list = eref_list;
		pthread_mutex_lock(&actq_mutex);
		TAILQ_INSERT_TAIL(&actq, qent, link);
		pthread_cond_signal(&actq_non_empty);
		pthread_mutex_unlock(&actq_mutex);
	}
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

	struct kmd_store *store;
	struct event_ref *eref;
	struct event_ref_list *eref_list = event_ref_list_new();
	if (!eref_list) {
		k_log("ERROR: Out of memory at %s:%d:%s\n", __FILE__, __LINE__,
				__func__);
		goto err0;
	}
	event_ref_list_get(eref_list);
	LIST_FOREACH(store, &store_list, entry) {
		eref = event_ref_new(store, msg);
		if (!eref) {
			k_log("ERROR: Out of memory at %s:%d:%s\n", __FILE__,
					__LINE__, __func__);
			goto err0;
		}
		LIST_INSERT_HEAD(&eref_list->list, eref, entry);
	}

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
	LIST_INSERT_HEAD(&rule->action_list, action, link);
}

void handle_ocm_cmd_ACTION(ocm_cfg_cmd_t cmd)
{
	const struct ocm_value *action_id = ocm_av_get_value(cmd, "name");
	const struct ocm_value *action_exec = ocm_av_get_value(cmd, "execute");
	if (!action_id) {
		k_log("ocm: error 'name' is missing from verb 'action'\n");
		return;
	}
	if (!action_exec) {
		k_log("ocm: error 'exec' is missing from verb 'action'\n");
		return;
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
	exit(x);
}

int main(int argc, char **argv)
{
	int rc;

	struct sigaction cleanup_act;
	cleanup_act.sa_handler = kmd_cleanup;
	cleanup_act.sa_flags = 0;

	sigaction(SIGHUP, &cleanup_act, NULL);
	sigaction(SIGINT, &cleanup_act, NULL);
	sigaction(SIGTERM, &cleanup_act, NULL);

	process_args(argc, argv);
	if (!FOREGROUND) {
		/* daemonize  */
		rc = daemon(1, 1);
		if (rc) {
			perror("daemon");
			exit(-1);
		}
	}

	init();
	config();
	k_listen();

	k_log("Komondor Daemon started.\n");

	int i;
	for (i = 0; i < N_act_threads; i++)
		pthread_join(act_threads[i], NULL);

	return 0;
}
