/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2026 Open Grid Computing, Inc. All rights reserved.
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
#define _GNU_SOURCE

#include "coll/rbt.h"
#include "ovis_util/util.h"
#include "ovis_json/ovis_json.h"

#include "ldms.h"
#include "ldmsd.h"
#include "config.h"
#include "ldmsd_request.h"

#define SMPLRP_CFG_LOCK() ldmsd_cfg_lock(LDMSD_CFGOBJ_SMPLRP)
#define SMPLRP_CFG_UNLOCK() ldmsd_cfg_unlock(LDMSD_CFGOBJ_SMPLRP)

/* Must SMPLRP_CFG_LOCK() before FIND */
#define SMPLRP_FIND(name) (ldmsd_smplrp_t) __cfgobj_find(name, LDMSD_CFGOBJ_SMPLRP)

ovis_log_t smplrp_log;

#define SMPLRP_LOG(lvl, fmt, ...) ovis_log(smplrp_log, lvl, fmt, ##__VA_ARGS__ )
#define SMPLRP_DEBUG(fmt, ...) SMPLRP_LOG(OVIS_LDEBUG, fmt, ##__VA_ARGS__ )
#define SMPLRP_INFO(fmt, ...) SMPLRP_LOG(OVIS_LINFO, fmt, ##__VA_ARGS__ )
#define SMPLRP_WARN(fmt, ...) SMPLRP_LOG(OVIS_LWARN, fmt, ##__VA_ARGS__ )
#define SMPLRP_ERROR(fmt, ...) SMPLRP_LOG(OVIS_LERROR, fmt, ##__VA_ARGS__ )


typedef struct ldmsd_smplrp_action_s *ldmsd_smplrp_action_t;
struct ldmsd_smplrp_action_s {
	TAILQ_ENTRY(ldmsd_smplrp_action_s) entry;
	enum {
		SMPLRP_ACTION_LOAD_TERM,
		SMPLRP_ACTION_INTERVAL_MOD,
	} type;
	ldmsd_smplrp_t smplrp; /* convenient ref to smplrp of the action */
	void (*on_job_init)(ldmsd_smplrp_action_t a, const char *job_id, json_entity_t job_msg);
	void (*on_job_exit)(ldmsd_smplrp_action_t a, const char *job_id, json_entity_t job_msg);
};

/* load-term action */
typedef struct action_lt_s {
	struct ldmsd_smplrp_action_s action;
	char *plugin;
	char *name_tmp; /* plugin instance name template */
	char *inst_tmp; /* set instance template */
	char *producer;
	int64_t interval;
	int64_t offset;
	int64_t term_delay;
	int     use_xthread; /* exclusive thread: 0 or 1 */
	json_entity_t j_config;
	json_entity_t j_configs;
} *action_lt_t;

struct interval_spec_s {
	int64_t interval_usec;
	int64_t offset_usec;
};

struct smplrp_job_s {
	struct rbn rbn;
	char job_id[OVIS_FLEX];
};

/* interval mod action */
typedef struct action_imod_s {
	struct ldmsd_smplrp_action_s action;
	pthread_mutex_t mutex;
	char *plug_inst_name;
	ldmsd_plugin_t plug_inst;
	int n_jobs;
	struct interval_spec_s job_init_ival;
	struct interval_spec_s job_exit_ival;
} *action_imod_t;

/* ---- ldmsd prototypes ---------------------------------------------------- */
int ldmsd_load_plugin(char *cfg_name, char *plugin_name, char *errstr, size_t errlen);
int ldmsd_term_plugin(char *cfg_name);
int find_least_busy_thread();
ovis_scheduler_t get_ovis_scheduler(int idx);
/* -------------------------------------------------------------------------- */

/* ---- smplrp prototypes --------------------------------------------------- */
static void action_lt_free(action_lt_t lt);
static void action_imod_free(action_imod_t lt);
static const char * json_value_to_cstr(json_entity_t jval, char *buf, size_t buf_sz);
/* -------------------------------------------------------------------------- */

int rbn_strcmp(void *tree_key, const void *key)
{
	return strcmp(tree_key, key);
}

ldmsd_smplrp_t ldmsd_smplrp_new(const char *name, const char *json_cfg)
{
	return ldmsd_smplrp_new_with_auth(name, json_cfg, getuid(), getgid(), 0660);
}

static void smplrp_del_cb(ldmsd_cfgobj_t obj)
{
	ldmsd_smplrp_t p = container_of(obj, struct ldmsd_smplrp_s, obj);
	struct ldmsd_smplrp_action_s *act;
	assert(p->msg_client == NULL);
	while ((act = TAILQ_FIRST(&p->actions))) {
		TAILQ_REMOVE(&p->actions, act, entry);
		switch (act->type) {
		case SMPLRP_ACTION_LOAD_TERM:
			action_lt_free(container_of(act, struct action_lt_s, action));
			break;
		case SMPLRP_ACTION_INTERVAL_MOD:
			action_imod_free(container_of(act, struct action_imod_s, action));
			break;
		}
	}
	json_doc_free(p->json_doc);
	free(p);
}

static inline
int jdict_get_cstr(json_entity_t dict, const char *key, const char **out)
{
	json_entity_t val;
	if (json_entity_type(dict) != JSON_DICT_VALUE)
		return EINVAL;
	val = json_value_find(dict, key);
	if (!val)
		return ENOENT;
	*out = json_value_cstr(val);
	if (!*out)
		return EINVAL;
	return 0;
}

static inline
int jdict_get_int64(json_entity_t dict, const char *key, int64_t *out)
{
	json_entity_t val = json_value_find(dict, key);
	if (!val)
		return ENOENT;
	errno = 0;
	switch (json_entity_type(val)) {
	case JSON_INT_VALUE:
		*out = json_value_int(val);
		break;
	case JSON_STRING_VALUE:
		*out = strtol(json_value_cstr(val), NULL, 0);
		break;
	default:
		return EINVAL;
	}
	return errno;
}

static char *
tmp_expand(ldmsd_smplrp_t p, ldmsd_smplrp_action_t _a, const char *job_id,
	   json_entity_t job_msg, const char *tmp)
{
	int rc;
	char *ret;
	const char *s, *end;
	int len;
	const char *_p;
	char c;
	const char *workflow_id;
	json_entity_t job_data;
	struct ovis_buff_s obuf;

	ovis_buff_init(&obuf, BUFSIZ);

	/*
	 * Pattern Exapnsion:
	 * - "%W" - WORKFLOW_ID
	 * - "%J" - Job ID
	 * - "%C" - Component ID
	 * - "%N" - Sampler Policy Name
	 * - "%P" - Producer
	 * - "%L" - Plugin
	 */
	s = tmp;
	end = tmp + strlen(tmp);
	while (s < end) {
		_p = strchr(s, '%');
		if (!_p) {
			rc = ovis_buff_appendf(&obuf, "%s", s);
			if (rc)
				goto err;
			break;
		}
		len = _p - s;
		rc = ovis_buff_appendf(&obuf, "%.*s",  len, s);
		if (rc)
			goto err;
		s = _p;
		c = s[1];
		switch (c) {
		case 'J': case 'j': /* job_id */
			rc = ovis_buff_appendf(&obuf, "%s", job_id);
			break;
		case 'C': case 'c': /* component_id */
			rc = ovis_buff_appendf(&obuf, "%s", p->component_id);
			break;
		case 'N': case 'n': /* policy name */
			rc = ovis_buff_appendf(&obuf, "%s", p->obj.name);
			break;
		case 'P': case 'p': /* producer */
			if (_a->type == SMPLRP_ACTION_LOAD_TERM) {
				rc = ovis_buff_appendf(&obuf, "%s", container_of(_a, struct action_lt_s, action)->producer);
			} else {
				rc = ovis_buff_appendf(&obuf, "(null)");
			}
			break;
		case 'L': case 'l': /* plugin */
			if (_a->type == SMPLRP_ACTION_LOAD_TERM) {
				rc = ovis_buff_appendf(&obuf, "%s", container_of(_a, struct action_lt_s, action)->plugin);
			} else {
				rc = ovis_buff_appendf(&obuf, "(null)");
			}
			break;
		case 'W': case 'w': /* workflow_id */
			job_data = json_value_find(job_msg, "data");
			rc = jdict_get_cstr(job_data, "workflow_id", &workflow_id);
			if (rc == 0) {
				rc = ovis_buff_appendf(&obuf, "%s", workflow_id);
			}
			rc = 0;
			break;
		case '%': /* literal % */
			rc = ovis_buff_appendf(&obuf, "%%");
			break;
		default:
			rc = EINVAL;
			break;
		}
		if (rc) {
			if (rc == EINVAL)
				SMPLRP_ERROR("%s: Unrecognized '%%%c'\n", p->obj.name, c);
			else
				SMPLRP_ERROR("%s: not enough memory\n", p->obj.name);
			goto err;
		}
		s += 2;
	}

	ret = ovis_buff_str(&obuf);
	ovis_buff_purge(&obuf);
	return ret;

 err:
	ovis_buff_purge(&obuf);
	return NULL;
}

static void action_lt_free(action_lt_t lt)
{
	free(lt->plugin);
	free(lt->producer);
	free(lt->inst_tmp);
	free(lt->name_tmp);
	free(lt);
}

static void p_attr_error(ldmsd_smplrp_t p, const char *name, int rc, const char *exp_type)
{
	switch (rc) {
	case EINVAL:
		SMPLRP_ERROR("%s: attribute '%s' must be %s\n", p->obj.name, name, exp_type);
		break;
	case ENOENT:
		SMPLRP_ERROR("%s: attribute '%s' is missing\n", p->obj.name, name);
		break;
	default:
		SMPLRP_ERROR("%s: attribute '%s' error: %d\n", p->obj.name, name, rc);
	}
}

static int  p_attr_str_assign(ldmsd_smplrp_t p,
		char **out,
		json_entity_t dict,
		const char *name,
		int optional)
{
	int rc;
	const char *val;
	rc = jdict_get_cstr(dict, name, &val);
	if (optional && rc == ENOENT) {
		/* OK for optional attr */
		return 0;
	}
	if (rc) {
		p_attr_error(p, name, rc, "string");
		return rc;
	}
	*out = strdup(val);
	if (!*out) {
		SMPLRP_ERROR("%s: Not enough memory.\n", p->obj.name);
		return ENOMEM;
	}
	return 0;
}

static int p_attr_time_assign(ldmsd_smplrp_t p,
		int64_t *out,
		json_entity_t dict,
		const char *name, int optional)
{
	int rc;
	json_entity_t jval;
	jval = json_value_find(dict, name);
	if (!jval) {
		if (optional)
			return 0;
		SMPLRP_ERROR("%s: attribute '%s' does not exist.\n", p->obj.name, name);
		return ENOENT;
	}
	if (json_entity_type(jval) == JSON_INT_VALUE) {
		*out = json_value_int(jval);
		return 0;
	}
	if (json_entity_type(jval) != JSON_STRING_VALUE) {
		SMPLRP_ERROR("%s: attribute '%s' must be a string or a number\n", p->obj.name, name);
		return EINVAL;
	}
	rc = ovis_time_str2us(json_value_cstr(jval), out);
	if (rc) {
		SMPLRP_ERROR("%s: attribute '%s' has bad time format.\n", p->obj.name, name);
		return rc;
	}
	return 0;
}

static int p_attr_bool_assign(ldmsd_smplrp_t p,
		int *out,
		json_entity_t dict,
		const char *name, int optional)
{
	json_entity_t jval;
	jval = json_value_find(dict, name);
	if (!jval) {
		if (optional)
			return 0;
		SMPLRP_ERROR("%s: attribute '%s' does not exist.\n", p->obj.name, name);
		return ENOENT;
	}
	switch (json_entity_type(jval)) {
	case JSON_INT_VALUE:
		*out = !!json_value_int(jval);
		break;
	case JSON_FLOAT_VALUE:
		*out = !!json_value_float(jval);
		break;
	case JSON_STRING_VALUE:
		*out = atoi(json_value_cstr(jval));
		break;
	case JSON_BOOL_VALUE:
		*out = json_value_bool(jval);
		break;
	default:
		SMPLRP_ERROR("%s: attribute '%s' must be a boolean.\n", p->obj.name, name);
		return EINVAL;
	}
	return 0;
}

static const char * json_value_to_cstr(json_entity_t jval, char *buf, size_t buf_sz)
{
	switch (json_entity_type(jval)) {
	case JSON_STRING_VALUE:
		return json_value_cstr(jval);
	case JSON_FLOAT_VALUE:
		snprintf(buf, buf_sz, "%lf", json_value_float(jval));
		return buf;
	case JSON_INT_VALUE:
		snprintf(buf, buf_sz, "%ld", json_value_int(jval));
		return buf;
	default:
		errno = EINVAL;
		return NULL;
	}
}

static char *config2str(action_lt_t a,
			const char *cfg_name,
			const char *cfg_prdcr,
			const char *cfg_inst,
			json_entity_t jcfg)
{
	ldmsd_smplrp_t p = a->action.smplrp;
	int rc;
	struct ovis_buff_s obuf;
	json_entity_t jval, jattr;
	enum json_value_e jtype;
	const char *key;
	char *ret = NULL;

	ovis_buff_init(&obuf, BUFSIZ);

	rc = ovis_buff_appendf(&obuf, "config name=%s component_id=%s"
				      " producer=%s instance=%s",
				      cfg_name, p->component_id,
				      a->producer, cfg_inst);
	if (rc) {
		errno = ENOMEM;
		SMPLRP_ERROR("%s: Not enough memory.\n", p->obj.name);
		goto out;
	}
	for (jattr = json_attr_first(jcfg); jattr; jattr = json_attr_next(jattr)) {

		key = json_attr_name(jattr);
		jval = json_attr_value(jattr);
		jtype = json_entity_type(jval);

		switch (jtype) {
		case JSON_NULL_VALUE:
			rc = ovis_buff_appendf(&obuf, " %s", key);
			break;
		case JSON_INT_VALUE:
			rc = ovis_buff_appendf(&obuf, " %s=%ld", key, json_value_int(jval));
			break;
		case JSON_FLOAT_VALUE:
			rc = ovis_buff_appendf(&obuf, " %s=%lf", key, json_value_float(jval));
			break;
		case JSON_STRING_VALUE:
			rc = ovis_buff_appendf(&obuf, " %s=%s", key, json_value_cstr(jval));
			break;
		default:
			errno = EINVAL;
			SMPLRP_ERROR("%s: config['%s'] value must be a scalar value.\n",
				     p->obj.name, key);
			goto out;
		}

		if (rc) {
			SMPLRP_ERROR("%s: Not enough memory.\n", p->obj.name);
			goto out;
		}
	}
	ret = ovis_buff_str(&obuf);
	if (!ret)
		SMPLRP_ERROR("%s: Not enough memory.\n", p->obj.name);
 out:
	ovis_buff_purge(&obuf);
	return ret;
}

void action_lt_on_job_init(ldmsd_smplrp_action_t _a, const char *job_id, json_entity_t job_msg)
{
	/*
	 * msg :
	 * {
	 *   "schema": "slurm_job_data",
	 *   "event": "EVENT",
	 *   "timestamp": TIMESTAMP_S32,
	 *   "context": "CONTEXT", // running context, e.g. slurmd
	 *   "data": {
	 *     "job_id": JOB_ID_U32, <--- job_id alread extracted by caller.
	 *     "node_id": NODE_ID_U32,
	 *     "uid": UID_U32,
	 *     "gid": GID_U32,
	 *     "ncpus": U16,
	 *     "nnodes": U32,
	 *     "local_tasks": U32,
	 *     "total_tasks": U32
	 *   }
	 * }
	 */
	int rc;
	char *cfg_name = NULL; /* config name= */
	char *cfg_prdcr = NULL; /* config producer= */
	char *cfg_inst = NULL; /* config instance= */

	char *load_cmd = NULL;
	char *config_cmd = NULL;
	char *start_cmd = NULL;
	ldmsd_smplrp_t p = _a->smplrp;

	json_entity_t jcfg;

	action_lt_t a = container_of(_a, struct action_lt_s, action);

	struct ovis_buff_s obuf;

	assert(_a->type == SMPLRP_ACTION_LOAD_TERM);

	/* NOTE:
	 *   - form config string
	 *   - call process_config_str
	 */

	ovis_buff_init(&obuf, BUFSIZ);

	/* Prep parameters for all operations */
	cfg_name = tmp_expand(p, _a, job_id, job_msg, a->name_tmp);
	if (!cfg_name) /* error already logged */
		goto cleanup;
	cfg_prdcr = tmp_expand(p, _a, job_id, job_msg, a->producer);
	if (!cfg_prdcr)
		goto cleanup;
	cfg_inst = tmp_expand(p, _a, job_id, job_msg, a->inst_tmp);
	if (!cfg_inst)
		goto cleanup;

	/* Load */
	rc = ovis_buff_appendf(&obuf, "load name=%s plugin=%s", cfg_name, a->plugin);
	load_cmd = rc?NULL:ovis_buff_str(&obuf);
	if (!load_cmd) {
		SMPLRP_ERROR("%s: Not enough memory\n", p->obj.name);
		goto cleanup;
	}
	rc = process_config_str(load_cmd, NULL, 1);
	if (rc) {
		SMPLRP_ERROR("%s: plugin load error: %d\n", p->obj.name, rc);
		goto cleanup;
	}

	/* Config */
	if (a->j_config) {
		config_cmd = config2str(a, cfg_name, cfg_prdcr, cfg_inst, a->j_config);
		if (!config_cmd) /* already logged */
			goto cleanup;
		rc = process_config_str(config_cmd, NULL, 1);
		if (rc) {
			SMPLRP_ERROR("%s: %s plugin config error: %d, config_cmd: %s\n",
					p->obj.name, cfg_name, rc, config_cmd);
			goto err;
		}
		free(config_cmd);
		config_cmd = NULL;
	}

	/* configs */
	if (!a->j_configs)
		goto start;
	for (jcfg = json_item_first(a->j_configs); jcfg; jcfg = json_item_next(jcfg)) {
		config_cmd = config2str(a, cfg_name, cfg_prdcr, cfg_inst, jcfg);
		if (!config_cmd) /* already logged */
			goto cleanup;
		rc = process_config_str(config_cmd, NULL, 1);
		if (rc) {
			SMPLRP_ERROR("%s: %s plugin config error: %d, config_cmd: %s\n",
					p->obj.name, cfg_name, rc, config_cmd);
			goto err;
		}
		free(config_cmd);
		config_cmd = NULL;
	}

 start:
	/* Start */
	ovis_buff_purge(&obuf);
	rc = ovis_buff_appendf(&obuf, "start name=%s interval=%ld offset=%ld",
			cfg_name, a->interval, a->offset);
	start_cmd = rc?NULL:ovis_buff_str(&obuf);
	if (!start_cmd) {
		SMPLRP_ERROR("%s: Not enough memory\n", p->obj.name);
		goto err;
	}
	rc = process_config_str(start_cmd, NULL, 1);
	if (rc) {
		SMPLRP_ERROR("%s: %s plugin start error: %d\n", p->obj.name, cfg_name, rc);
		goto err;
	}

	goto cleanup;

 err:
	ldmsd_term_plugin(cfg_name);

 cleanup:
	if (cfg_name)
		free(cfg_name);
	if (cfg_prdcr)
		free(cfg_prdcr);
	if (cfg_inst)
		free(cfg_inst);
	if (load_cmd)
		free(load_cmd);
	if (config_cmd)
		free(config_cmd);
	if (start_cmd)
		free(start_cmd);
	return;
}

struct delayed_term_event_s {
	struct ovis_event_s ev;
	ovis_scheduler_t sched;
	action_lt_t act;
	char *cfg_name;
};

void delayed_term_cb(ovis_event_t ev)
{
	int rc;
	struct delayed_term_event_s *dtev = container_of(ev, struct delayed_term_event_s, ev);

	ovis_scheduler_event_del(dtev->sched, ev);
	rc = ldmsd_term_plugin(dtev->cfg_name);
	if (rc) {
		SMPLRP_ERROR("%s: %s termination error after delayed time: %d\n",
				dtev->act->action.smplrp->obj.name,
				dtev->cfg_name,
				rc);
	}
	free(dtev->cfg_name);
	free(dtev);
}

void action_lt_on_job_exit(ldmsd_smplrp_action_t _a, const char *job_id, json_entity_t job_msg)
{
	/*
	 * msg :
	 * {
	 *   "schema": "slurm_job_data",
	 *   "event": "EVENT",
	 *   "timestamp": TIMESTAMP_S32,
	 *   "context": "CONTEXT", // running context, e.g. slurmd
	 *   "data": {
	 *     "job_id": JOB_ID_U32, <--- job_id alread extracted by caller.
	 *     "node_id": NODE_ID_U32
	 *   }
	 * }
	 */
	int rc;
	char *cfg_name = NULL; /* config name= */
	ldmsd_smplrp_t p = _a->smplrp;
	action_lt_t a = container_of(_a, struct action_lt_s, action);

	cfg_name = tmp_expand(p, _a, job_id, job_msg, a->name_tmp);

	/* Stop sampling */
	rc = ldmsd_sampler_stop(cfg_name);
	if (rc) {
		SMPLRP_ERROR("%s: '%s' stop error: %d\n", p->obj.name, cfg_name, rc);
	}

	if (!a->term_delay)
		goto term;

	/* schedule delayed termination */
	struct delayed_term_event_s *dtev;

	dtev = calloc(1, sizeof(*dtev));
	if (!dtev) {
		SMPLRP_ERROR("%s: '%s' delayed termination error: out of memory\n", p->obj.name, cfg_name);
		goto term;
	}
	OVIS_EVENT_INIT(&dtev->ev);
	dtev->ev.param.type = OVIS_EVENT_TIMEOUT;
	dtev->ev.param.cb_fn = delayed_term_cb;
	dtev->ev.param.timeout.tv_sec = a->term_delay / 1000000;
	dtev->ev.param.timeout.tv_usec = a->term_delay % 1000000;
	dtev->sched = get_ovis_scheduler(find_least_busy_thread());
	dtev->act = a;
	dtev->cfg_name = cfg_name;
	rc = ovis_scheduler_event_add(dtev->sched, &dtev->ev);
	assert(rc == 0);
	goto out;

 term:
	rc = ldmsd_term_plugin(cfg_name);
	if (rc) {
		SMPLRP_ERROR("%s: '%s' termination error: %d\n", p->obj.name, cfg_name, rc);
	}

	if (cfg_name)
		free(cfg_name);
 out:
	return;
}

static int
config_sanity_check(ldmsd_smplrp_t p, json_entity_t jcfg)
{
	/* Just make sure that jcfg is a dict with scalar values */
	const char *key;
	json_entity_t jval, jattr;
	enum json_value_e jtype;
	if (json_entity_type(jcfg) != JSON_DICT_VALUE) {
		SMPLRP_ERROR("%s: config must be a dictionary.\n", p->obj.name);
		return EINVAL;
	}
	for (jattr = json_attr_first(jcfg); jattr; jattr = json_attr_next(jattr)) {
		key = json_attr_name(jattr);
		jval = json_attr_value(jattr);
		jtype = json_entity_type(jval);
		switch (jtype) {
		case JSON_INT_VALUE:
		case JSON_FLOAT_VALUE:
		case JSON_STRING_VALUE:
		case JSON_NULL_VALUE:
			/* OK */
			break;
		default:
			SMPLRP_ERROR("%s: config['%s'] must be a scalar value\n", p->obj.name, key);
			return EINVAL;
		}
	}
	return 0;
}

static int
smplrp_process_act_load_term(ldmsd_smplrp_t p, json_entity_t j_act)
{
	/*
	 * {
	 *   "type": "load_term",
	 *   "plugin"; "<PLUGIN>",
	 *   "name": "<PATTERN>", // plugin instance name
	 *   "producer": "<PRODUCER>",
	 *   "instance": "<PATTERN>", // config instance=
	 *   "interval": "<TIME>",
	 *   "offset": "<TIME>", // optional
	 *   "term_delay": "<TIME>", // optional
	 *   "exclusive_thread": bool,
	 *   "config": {
	 *     // additional config
	 *     // For "keyword" w/o value, use:
	 *     //   "key": null
	 *   },
	 *   "configs": [
	 *     { }, // multiple config
	 *     ...
	 *   ]
	 * }
	 */

	action_lt_t lt;
	json_entity_t jval;
	int rc;

	lt = calloc(1, sizeof(*lt));
	if (!lt)
		return ENOMEM;

	lt->action.smplrp = p;
	lt->action.type = SMPLRP_ACTION_LOAD_TERM;
	lt->action.on_job_init = action_lt_on_job_init;
	lt->action.on_job_exit = action_lt_on_job_exit;

	lt->j_config = json_value_find(j_act, "config");
	if (lt->j_config) {
		if (json_entity_type(lt->j_config) != JSON_DICT_VALUE) {
			rc = EINVAL;
			SMPLRP_ERROR("%s: 'config' attribute must be a dictionary.\n", p->obj.name);
			goto err;
		}
		rc = config_sanity_check(p, lt->j_config);
		if (rc) {
			/* already logged */
			goto err;
		}
	}

	lt->j_configs = json_value_find(j_act, "configs");
	if (lt->j_configs) {
		if (json_entity_type(lt->j_configs) != JSON_LIST_VALUE) {
			rc = EINVAL;
			SMPLRP_ERROR("%s: 'configs' attribute must be a list of config objects\n", p->obj.name);
			goto err;
		}
		for (jval = json_item_first(lt->j_configs); jval; jval = json_item_next(jval)) {
			rc = config_sanity_check(p, jval);
			if (rc) {
				/* already logged */
				goto err;
			}
		}
	}

	if (!lt->j_config && !lt->j_configs) {
		rc = EINVAL;
		SMPLRP_ERROR("%s: 'config' or 'configs' (list of config) is required\n", p->obj.name);
		goto err;
	}

	lt->interval = 1000000; /* default 1s */

	/* Extract a bunch of attributes */
	rc = p_attr_str_assign(p, &lt->plugin, j_act, "plugin", 0);
	rc = rc?rc:p_attr_str_assign(p, &lt->name_tmp, j_act, "name", 1);
	rc = rc?rc:p_attr_str_assign(p, &lt->producer, j_act, "producer", 1);
	rc = rc?rc:p_attr_str_assign(p, &lt->inst_tmp, j_act, "instance", 1);
	rc = rc?rc:p_attr_time_assign(p, &lt->interval, j_act, "interval", 1);
	rc = rc?rc:p_attr_time_assign(p, &lt->offset, j_act, "offset", 1);
	rc = rc?rc:p_attr_time_assign(p, &lt->term_delay, j_act, "term_delay", 1);
	rc = rc?rc:p_attr_bool_assign(p, &lt->use_xthread, j_act, "exclusive_thread", 1);

	if (rc) /* error already logged */
		goto err;

	if (!lt->name_tmp) {
		lt->name_tmp = strdup("%N-%L-%J");
		if (!lt->name_tmp) {
			SMPLRP_ERROR("%s: Not enough memory.\n", p->obj.name);
			goto err;
		}
	}

	if (!lt->inst_tmp) {
		lt->inst_tmp = strdup("%P/%N-%L-%J");
		if (!lt->inst_tmp) {
			SMPLRP_ERROR("%s: Not enough memory.\n", p->obj.name);
			goto err;
		}
	}

	if (!lt->producer) {
		char buf[256];
		gethostname(buf, sizeof(buf));
		lt->producer = strdup(buf);
		if (!lt->producer) {
			SMPLRP_ERROR("%s: Not enough memory.\n", p->obj.name);
			goto err;
		}
	}

	TAILQ_INSERT_TAIL(&p->actions, &lt->action, entry);
	rc = 0;
	goto out;

 err:
	action_lt_free(lt);
 out:
	return rc;
}

static int
p_attr_interval_assign(ldmsd_smplrp_t p, json_entity_t j_act, const char *key,
		       struct interval_spec_s *ival)
{
	int rc;
	json_entity_t j_dict = json_value_find(j_act, key);

	if (!j_dict) {
		SMPLRP_ERROR("%s: '%s' not found in 'interval_mod' action\n", p->obj.name, key);
		return ENOENT;
	}
	if (json_entity_type(j_dict) != JSON_DICT_VALUE) {
		SMPLRP_ERROR("%s: '%s' in 'interval_mod' must be a dict\n", p->obj.name, key);
		return EINVAL;
	}

	rc = p_attr_time_assign(p, &ival->interval_usec, j_dict, "interval", 0);
	rc = rc?rc:p_attr_time_assign(p, &ival->offset_usec, j_dict, "offset", 1);

	return rc;
}

static void
action_imod_stop_start(action_imod_t a, const char *cfg_name, struct interval_spec_s *ival)
{
	int rc;
	char interval[64];
	char offset[64];
	rc = ldmsd_sampler_stop(cfg_name);
	if (rc) {
		SMPLRP_WARN("%s: '%s' interval mod stop error: %d\n",
				a->action.smplrp->obj.name,
				cfg_name, rc);
		/* proceed */
	}
	snprintf(interval, sizeof(interval), "%ld", ival->interval_usec);
	snprintf(offset, sizeof(offset), "%ld", ival->offset_usec);
	rc = ldmsd_sampler_start(cfg_name, interval, offset, NULL);
	if (rc) {
		SMPLRP_WARN("%s: '%s' interval mod start error: %d\n",
				a->action.smplrp->obj.name,
				cfg_name, rc);
	}
}

static void
action_imod_on_job_init(ldmsd_smplrp_action_t _a, const char *job_id, json_entity_t job_msg)
{
	ldmsd_smplrp_t p = _a->smplrp;
	action_imod_t a = container_of(_a, struct action_imod_s, action);
	char *cfg_name;
	cfg_name = tmp_expand(p, _a, job_id, job_msg, a->plug_inst_name);
	if (!cfg_name) /* error already logged */
		return;

	pthread_mutex_lock(&a->mutex);
	if (0 == a->n_jobs) /* already using 'job_init' interval */
		action_imod_stop_start(a, cfg_name, &a->job_init_ival);
	a->n_jobs++;
	pthread_mutex_unlock(&a->mutex);
}

static void
action_imod_on_job_exit(ldmsd_smplrp_action_t _a, const char *job_id, json_entity_t job_msg)
{
	ldmsd_smplrp_t p = _a->smplrp;
	action_imod_t a = container_of(_a, struct action_imod_s, action);

	char *cfg_name;
	cfg_name = tmp_expand(p, _a, job_id, job_msg, a->plug_inst_name);
	if (!cfg_name) /* error already logged */
		return;
	pthread_mutex_lock(&a->mutex);
	a->n_jobs--;
	if (0 == a->n_jobs) /* this is the last job, switch to 'job_exit' interval */
		action_imod_stop_start(a, cfg_name, &a->job_exit_ival);
	pthread_mutex_unlock(&a->mutex);
}

static void action_imod_free(action_imod_t lt)
{
	free(lt);
}

static int
smplrp_process_act_interval_mod(ldmsd_smplrp_t p, json_entity_t j_act)
{
	/*
	 * {
	 *   "type": "interval_mod",
	 *   "name": "<PATTERN>", // plugin instance name
	 *   "job_init": {
	 *     "interval": "<TIME>",
	 *     "offset": "<TIME>"
	 *   },
	 *   "job_exit": {
	 *     "interval": "<TIME>",
	 *     "offset": "<TIME>"
	 *   }
	 * }
	 */

	action_imod_t imod;
	int rc = ENOSYS;

	imod = calloc(1, sizeof(*imod));
	if (!imod) {
		SMPLRP_ERROR("%s: Not enough memory.\n", p->obj.name);
		rc = errno;
		goto out;
	}

	pthread_mutex_init(&imod->mutex, NULL);

	imod->action.smplrp = p;
	imod->action.type = SMPLRP_ACTION_INTERVAL_MOD;
	imod->action.on_job_init = action_imod_on_job_init;
	imod->action.on_job_exit = action_imod_on_job_exit;

	rc = p_attr_str_assign(p, &imod->plug_inst_name, j_act, "name", 0);
	rc = rc?rc:p_attr_interval_assign(p, j_act, "job_init", &imod->job_init_ival);
	rc = rc?rc:p_attr_interval_assign(p, j_act, "job_exit", &imod->job_exit_ival);
	if (rc)
		goto err; /* error is already logged */

	TAILQ_INSERT_TAIL(&p->actions, &imod->action, entry);
	rc = 0;
	goto out;

 err:
	action_imod_free(imod);
 out:
	return rc;
}

static int
smplrp_process_act(ldmsd_smplrp_t p, json_entity_t j_act)
{
	/*
	 * j_act:
	 * {
	 *   "type": "<TYPE>",
	 *   ...
	 * }
	 */
	const char *s_type;
	int rc;
	rc = jdict_get_cstr(j_act, "type", &s_type);
	if (rc) {
		SMPLRP_ERROR("%s: 'type' error\n", p->obj.name);
		return rc;
	}
	if (0 == strcasecmp(s_type, "load_term")) {
		return smplrp_process_act_load_term(p, j_act);
	} else if (0 == strcasecmp(s_type, "interval_mod") ||
		   0 == strcasecmp(s_type, "imod")) {
		return smplrp_process_act_interval_mod(p, j_act);
	}
	SMPLRP_ERROR("Unsupported ...\n");
	return ENOTSUP;
}

static int
smplrp_process_cfg(ldmsd_smplrp_t p)
{
	int rc;
	json_entity_t j_acts;
	json_entity_t j_act;
	json_entity_t j_val;

	/* NOTE
	 * ----
	 * json_cfg format:
	 * {
	 *   "message_tag": "TAG",
	 *   "component_id": "COMP_ID",
	 *   "job_event_actions": [
	 *     {
	 *       "type": "load_term",
	 *       ...
	 *     },
	 *     {
	 *       "type": "interval_mod",
	 *       ...
	 *     }
	 *     ...
	 *   ]
	 * }
	 */

	rc = jdict_get_cstr(p->json_cfg, "message_tag", &p->msg_tag);
	switch (rc) {
	case ENOENT:
		/* OK */
		p->msg_tag = "slurm";
	case 0:
		/* OK */
		break;
	case EINVAL:
		SMPLRP_ERROR("'message_tag' attribute must be a string.\n");
		return rc;
	default:
		assert(0 == "BAD ERROR");
		SMPLRP_ERROR("'message_tag' unexpected error: %d.\n", rc);
		return rc;
	}

	j_val = json_value_find(p->json_cfg, "component_id");
	if (!j_val) {
		SMPLRP_ERROR("'component_id' attribute not found.\n");
		return ENOENT;
	}
	switch (json_entity_type(j_val)) {
	case JSON_STRING_VALUE:
		snprintf(p->component_id, sizeof(p->component_id),
			 "%s", json_value_cstr(j_val));
		break;
	case JSON_INT_VALUE:
		snprintf(p->component_id, sizeof(p->component_id),
			 "%ld", json_value_int(j_val));
		break;
	default:
		SMPLRP_ERROR("Unsupported component_id value\n");
		return EINVAL;
	}

	j_acts = json_value_find(p->json_cfg, "job_event_actions");
	if (!j_acts) {
		SMPLRP_ERROR("'job_event_actions' not found.\n");
		return EINVAL;
	}
	if (json_entity_type(j_acts) != JSON_LIST_VALUE) {
		SMPLRP_ERROR("'job_event_actions' must be a list.\n");
		return EINVAL;
	}

	for (j_act = json_item_first(j_acts); j_act; j_act = json_item_next(j_act)) {
		rc = smplrp_process_act(p, j_act);
		if (rc) /* error is already logged */
			return rc;
	}

	return 0;
}

ldmsd_smplrp_t
ldmsd_smplrp_new_with_auth(const char *name, const char *json_cfg,
			   uid_t uid, gid_t gid, int perm)
{
	ldmsd_smplrp_t p;
	int rc;

	p = (void*)ldmsd_cfgobj_new_with_auth(name, LDMSD_CFGOBJ_SMPLRP,
			sizeof(*p), smplrp_del_cb, uid, gid, perm);
	if (!p)
		return NULL;
	pthread_mutex_init(&p->mutex, NULL);
	rbt_init(&p->job_rbt, rbn_strcmp);
	TAILQ_INIT(&p->actions);
	rc = json_parse_buffer((char*)json_cfg, strlen(json_cfg), &p->json_doc);
	if (rc) {
		errno = EINVAL;
		goto err_0;
	}
	p->json_cfg = json_doc_root(p->json_doc);
	rc = smplrp_process_cfg(p);
	if (rc) {
		errno = rc;
		goto err_0;
	}
	ldmsd_smplrp_unlock(p);

	return p;

 err_0:
	ldmsd_smplrp_unlock(p);
	ldmsd_cfgobj_del(&p->obj);
	return NULL;
}

extern struct rbt *cfgobj_trees[];
ldmsd_cfgobj_t __cfgobj_find(const char *name, ldmsd_cfgobj_type_t type);

int ldmsd_smplrp_del(const char *name, ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;
	ldmsd_smplrp_t p;
	SMPLRP_CFG_LOCK();
	p = SMPLRP_FIND(name);
	if (!p) {
		SMPLRP_ERROR("'%s' not found\n", name);
		rc = ENOENT;
		goto out_0;
	}
	ldmsd_smplrp_lock(p);
	if (p->state != LDMSD_SMPLRP_STOPPED) {
		rc = EBUSY;
		goto out_1;
	}
	rbt_del(cfgobj_trees[LDMSD_CFGOBJ_SMPLRP], &p->obj.rbn);
	ldmsd_cfgobj_put(&p->obj, "cfgobj_tree"); /* tree reference */
	ldmsd_cfgobj_put(&p->obj, "init");        /* create reference */
 out_1:
	ldmsd_smplrp_unlock(p);
 out_0:
	SMPLRP_CFG_UNLOCK();
	if (p)
		ldmsd_smplrp_put(p, "find");
	return rc;
}

static json_doc_t
json_empty_doc()
{
	static json_doc_t edoc = NULL;

	if (NULL != __atomic_load_n(&edoc, __ATOMIC_SEQ_CST))
		goto out;

	int rc;
	json_doc_t d;
	json_doc_t null = NULL;
	rc = json_parse_buffer("{}", 3, &d);
	if (rc)
		goto out;
	if (0 == __atomic_compare_exchange_n(&edoc, &null, d, 0,
					__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
		/* another thread won */
		json_doc_free(d);
	}

  out:
	return edoc;
}

static inline json_entity_t
json_empty_root()
{
	json_doc_t edoc = json_empty_doc();
	return edoc?json_doc_root(edoc):NULL;
}

static void smplrp_on_msg_client_close(ldmsd_smplrp_t p)
{
	/* clean up the outstanding jobs */

	struct smplrp_job_s *job;
	struct rbn *rbn;
	ldmsd_smplrp_action_t act;

	pthread_mutex_lock(&p->mutex);
	while ((rbn = rbt_min(&p->job_rbt))) {
		rbt_del(&p->job_rbt, rbn);
		job = container_of(rbn, struct smplrp_job_s, rbn);
		pthread_mutex_unlock(&p->mutex);

		TAILQ_FOREACH(act, &p->actions, entry) {
			act->on_job_exit(act, job->job_id, json_empty_root());
		}

		pthread_mutex_lock(&p->mutex);
	}

	assert(p->state == LDMSD_SMPLRP_STOPPING);
	p->state = LDMSD_SMPLRP_STOPPED;
	p->msg_client = NULL;
	ldmsd_smplrp_unlock(p);
}

static void smplrp_on_msg_recv(ldmsd_smplrp_t p, ldms_msg_event_t ev)
{
	json_entity_t obj = ev->recv.json;
	json_entity_t data, data_job_id;
	int rc, len;
	ldmsd_smplrp_action_t act;
	char buf[128];
	const char *val;
	const char *job_id;
	struct smplrp_job_s *job;
	struct rbn *rbn;

	if (!obj) {
		SMPLRP_WARN("Unexpected data format\n");
		return ;
	}

	rc = jdict_get_cstr(obj, "event", &val);
	if (rc) {
		SMPLRP_WARN("event error: %d\n", rc);
		return;
	}

	data = json_value_find(obj, "data");
	if (!data) {
		SMPLRP_WARN("'data' not found\n");
		return;
	}
	if (json_entity_type(data) != JSON_DICT_VALUE) {
		SMPLRP_WARN("'data' is not a dictionary.\n");
		return;
	}
	data_job_id = json_value_find(data, "job_id");
	if (!data_job_id) {
		SMPLRP_WARN("data['job_id'] not found\n");
		return;
	}
	job_id = json_value_to_cstr(data_job_id, buf, sizeof(buf));
	if (!job_id) {
		SMPLRP_WARN("data['job_id'] value error: %d\n", errno);
		return;
	}

	if (0 == strcmp("init", val)) {
		len = strlen(job_id);
		job = malloc(sizeof(*job) + len + 1);
		if (!job) {
			SMPLRP_WARN("%s: not enough memory\n", p->obj.name);
			return;
		}
		memcpy(&job->job_id, job_id, len + 1);
		rbn_init(&job->rbn, job->job_id);
		pthread_mutex_lock(&p->mutex);
		rbt_ins(&p->job_rbt, &job->rbn);
		pthread_mutex_unlock(&p->mutex);
		TAILQ_FOREACH(act, &p->actions, entry) {
			act->on_job_init(act, job_id, ev->recv.json);
		}
	} else if (0 == strcmp("exit", val)) {
		pthread_mutex_lock(&p->mutex);
		rbn = rbt_find(&p->job_rbt, job_id);
		if (!rbn) {
			/* we have not seen 'job_init' of this job */
			pthread_mutex_unlock(&p->mutex);
			return;
		}
		rbt_del(&p->job_rbt, rbn);
		pthread_mutex_unlock(&p->mutex);
		job = container_of(rbn, struct smplrp_job_s, rbn);
		free(job);
		TAILQ_FOREACH(act, &p->actions, entry) {
			act->on_job_exit(act, job_id, ev->recv.json);
		}
	}
	/* ignore other events; no need to log */
}

static int smplrp_msg_cb(ldms_msg_event_t ev, void *cb_arg)
{
	ldmsd_smplrp_t p = cb_arg;
	switch (ev->type) {
	case LDMS_MSG_EVENT_CLIENT_CLOSE:
		smplrp_on_msg_client_close(p);
		break;
	case LDMS_MSG_EVENT_RECV:
		smplrp_on_msg_recv(p, ev);
		break;
	case LDMS_MSG_EVENT_SUBSCRIBE_STATUS:
	case LDMS_MSG_EVENT_UNSUBSCRIBE_STATUS:
		/* no-op */
		break;
	}
	return 0;
}

int ldmsd_smplrp_start(const char *name, ldmsd_sec_ctxt_t ctxt)
{
	int rc;
	ldmsd_smplrp_t p;
	char buf[512];

	SMPLRP_CFG_LOCK();
	p = SMPLRP_FIND(name);
	SMPLRP_CFG_UNLOCK();
	if (!p) {
		SMPLRP_ERROR("'%s' not found\n", name);
		rc = ENOENT;
		goto out_0;
	}
	ldmsd_smplrp_lock(p);
	if (p->state != LDMSD_SMPLRP_STOPPED) {
		SMPLRP_ERROR("'%s' is busy\n", name);
		rc = EBUSY;
		goto out_1;
	}

	snprintf(buf, sizeof(buf), "smplrp:%s", name);

	p->msg_client = ldms_msg_subscribe(p->msg_tag, 0, smplrp_msg_cb, p, buf);
	if (!p->msg_client) {
		rc = errno;
		SMPLRP_ERROR("%s: ldms_msg_subscribe(\"%s\") failed: %d\n",
				p->obj.name, p->msg_tag, errno);
		goto out_1;
	}
	p->state = LDMSD_SMPLRP_STARTED;
	rc = 0;
	/* fall through */
 out_1:
	ldmsd_smplrp_unlock(p);
 out_0:
	if (p)
		ldmsd_smplrp_put(p, "find");
	return rc;
}

int ldmsd_smplrp_stop(const char *name, ldmsd_sec_ctxt_t ctxt)
{
	int rc;
	ldmsd_smplrp_t p;

	SMPLRP_CFG_LOCK();
	p = SMPLRP_FIND(name);
	SMPLRP_CFG_UNLOCK();
	if (!p) {
		SMPLRP_ERROR("'%s' not found\n", name);
		rc = ENOENT;
		goto out_0;
	}
	ldmsd_smplrp_lock(p);
	if (p->state != LDMSD_SMPLRP_STARTED) {
		SMPLRP_ERROR("'%s' is busy\n", name);
		rc = EBUSY;
		goto out_1;
	}

	assert(p->msg_client);

	p->state = LDMSD_SMPLRP_STOPPING;
	ldms_msg_client_close(p->msg_client);
	rc = 0;
	/* fall through */
 out_1:
	ldmsd_smplrp_unlock(p);
 out_0:
	if (p)
		ldmsd_smplrp_put(p, "find");
	return rc;
}

__attribute__((constructor))
static void __init__()
{
	smplrp_log = ovis_log_register("ldmsd_smplrp", "LDMSD Sampler Policy");
}
