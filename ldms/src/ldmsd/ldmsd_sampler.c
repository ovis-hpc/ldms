/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
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

#include <grp.h>
#include <pwd.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>

#include "ldmsd.h"
#include "ldmsd_plugin.h"
#include "ldmsd_sampler.h"

static
const char *samp_desc(ldmsd_plugin_inst_t inst)
{
	return "Base implementation of all samplers.";
}

static
const char *samp_help(ldmsd_plugin_inst_t inst)
{
	return "...";
}

static
int samp_init(ldmsd_plugin_inst_t inst)
{
	ldmsd_sampler_type_t samp = (void*)inst->base;

	samp->uid = geteuid();
	samp->gid = getegid();
	samp->perm = 0777;
	samp->set_array_card = 1;
	samp->job_id_idx = -1;
	samp->app_id_idx = -1;
	samp->job_start_idx = -1;
	samp->job_end_idx = -1;

	pthread_mutex_init(&samp->lock, NULL);

	return 0;
}

static
void samp_del(ldmsd_plugin_inst_t inst)
{
	ldmsd_sampler_type_t samp = (void*)inst->base;
	ldmsd_set_entry_t ent;

	ldmsd_linfo("Plugin %s: Deleting ...\n", inst->inst_name);

	while ((ent = LIST_FIRST(&samp->set_list))) {
		LIST_REMOVE(ent, entry);
		ldmsd_linfo("Plugin %s: deleting set %s\n",
			    inst->inst_name,
			    ldms_set_instance_name_get(ent->set));
		ldms_set_unpublish(ent->set);
		ldms_set_delete(ent->set);
		free(ent);
	}

	free(samp->producer_name);
	free(samp->set_inst_name);
	free(samp->schema_name);

	pthread_mutex_destroy(&samp->lock);
}

static
int samp_config(ldmsd_plugin_inst_t inst, struct attr_value_list *avl,
		struct attr_value_list *kwl, char *ebuf, int ebufsz)
{
	ldmsd_sampler_type_t samp = (void*)inst->base;
	char *job_set_name;
	const char *value;
	const char *name = inst->inst_name;
	char buff[1024];

	value = av_value(avl, "producer");
	if (!value) {
		value = ldmsd_myname_get();
		ldmsd_log(LDMSD_LINFO, "%s: producer not specified, "
			  "using `%s`\n", name, value);
	}
	samp->producer_name = strdup(value);

	value = av_value(avl, "component_id");
	if (value)
		samp->component_id = atol(value);

	value = av_value(avl, "instance");
	if (!value) {
		snprintf(buff, sizeof(buff), "%s/%s", samp->producer_name,
			 inst->inst_name);
		value = buff;
	}
	samp->set_inst_name = strdup(value);

	value = av_value(avl, "schema");
	if (!value || value[0] == '\0')
		samp->schema_name = strdup(inst->plugin_name);
	else
		samp->schema_name = strdup(value);

	job_set_name = av_value(avl, "job_set");
	if (!job_set_name) {
		snprintf(buff, sizeof(buff), "%s/jobinfo",
			 samp->producer_name);
		job_set_name = buff;
	}
	samp->job_set = ldms_set_by_name(job_set_name);
	if (!samp->job_set) {
		ldmsd_log(LDMSD_LINFO, "%s: The job data set named, %s, "
			  "does not exist. Job data will not be associated "
			  "with the metric values.\n", name, job_set_name);
		samp->job_id_idx = -1;
	} else {
		value = av_value(avl, "job_id");
		if (!value)
			value = "job_id";
		samp->job_id_idx = ldms_metric_by_name(samp->job_set, value);
		if (samp->job_id_idx < 0) {
			snprintf(ebuf, ebufsz,
				 "%s: The specified job_set '%s' "
				 "is missing the 'job_id' attribute and "
				 "cannot be used.\n", name, job_set_name);
			goto einval;
		}
		value = av_value(avl, "app_id");
		if (!value)
			value = "app_id";
		samp->app_id_idx = ldms_metric_by_name(samp->job_set, value);
		if (samp->app_id_idx < 0) {
			snprintf(ebuf, ebufsz,
				 "%s: The specified job_set '%s' "
				 "is missing the 'app_id' attribute and "
				 "cannot be used.\n", name, job_set_name);
			goto einval;
		}
		value = av_value(avl, "job_start");
		if (!value)
			value = "job_start";
		samp->job_start_idx = ldms_metric_by_name(samp->job_set, value);
		if (samp->job_start_idx < 0) {
			snprintf(ebuf, ebufsz,
				 "%s: The specified job_set '%s' "
				 "is missing the 'job_start' attribute and "
				 "cannot be used.\n", name, job_set_name);
			goto einval;
		}
		value = av_value(avl, "job_end");
		if (!value)
			value = "job_end";
		samp->job_end_idx = ldms_metric_by_name(samp->job_set, value);
		if (samp->job_end_idx < 0) {
			snprintf(ebuf, ebufsz,
				 "%s: The specified job_set "
				 "'%s' is missing the 'job_end' attribute and "
				 "cannot be used.\n", name, job_set_name);
			goto einval;
		}
	}
	/* uid, gid, permission */
	value = av_value(avl, "uid");
	if (value) {
		if (isalpha(value[0])) {
			/* Try to lookup the user name */
			struct passwd _pwd;
			struct passwd *pwd;
			getpwnam_r(value, &_pwd, buff, sizeof(buff), &pwd);
			if (!pwd) {
				snprintf(ebuf, ebufsz, "%s: The specified "
					 "user '%s' does not exist\n",
					 name, value);
				goto einval;
			}
			samp->uid = pwd->pw_uid;
		} else {
			samp->uid = strtol(value, NULL, 0);
		}
	} else {
		samp->uid = geteuid();
	}
	value = av_value(avl, "gid");
	if (value) {
		if (isalpha(value[0])) {
			/* Try to lookup the group name */
			struct group _grp;
			struct group *grp;
			getgrnam_r(value, &_grp, buff, sizeof(buff), &grp);
			if (!grp) {
				snprintf(ebuf, ebufsz, "%s: The specified "
					 "group '%s' does not exist\n",
					 name, value);
				goto einval;
			}
			samp->gid = grp->gr_gid;
		} else {
			samp->gid = strtol(value, NULL, 0);
		}
	} else {
		samp->gid = getegid();
	}
	value = av_value(avl, "perm");
	if (value && value[0] != '0') {
		ldmsd_log(LDMSD_LINFO, "%s: Warning, the permission bits '%s' "
			  "are not specified as an Octal number.\n",
			  name, value);
	}
	samp->perm = (value)?(strtol(value, NULL, 0)):(0777);

	/* set_array_card */
	value = av_value(avl, "set_array_card");
	samp->set_array_card = (value)?(strtol(value, NULL, 0)):(1);

	return 0;
 einval:
	return EINVAL;
}

ldmsd_plugin_qresult_t ldmsd_sampler_query(ldmsd_plugin_inst_t i, const char *q)
{
	ldmsd_sampler_type_t samp = (void*)i->base;
	ldmsd_plugin_qresult_t qr;

	/* call `super` query first -- to handle the common plugin part */
	qr = ldmsd_plugin_query(i, q);
	if (!qr)
		return NULL;

	if (qr->rc)
		goto out;

	if (0 != strcmp(q, "status"))
		goto out;

	/* currently, samp_query only handle `status` */

	char sample_interval_us[32];
	char sample_offset_us[32];
	char uid[16];
	char gid[16];
	char perm[8];

	if (samp->smplr) {
		sprintf(sample_interval_us, "%ld", samp->smplr->interval_us);
		sprintf(sample_offset_us, "%ld", samp->smplr->offset_us);
	} else {
		sprintf(sample_interval_us, "0");
		sprintf(sample_offset_us, "%ld", LDMSD_UPDT_HINT_OFFSET_NONE);
	}
	sprintf(uid, "%d", samp->uid);
	sprintf(gid, "%d", samp->gid);
	sprintf(perm, "%#o", samp->perm);

	const ldmsd_plugin_qrent_type_t _str = LDMSD_PLUGIN_QRENT_STR;
	struct ldmsd_plugin_qrent_bulk_s bulk[] = {
		{ "sample_interval_us" , _str , sample_interval_us        },
		{ "sample_offset_us"   , _str , sample_offset_us          },
		{ "producer"           , _str , samp->producer_name       },
		{ "schema"             , _str , samp->schema_name         },
		{ "set_instance_name"  , _str , samp->set_inst_name       },
		{ "uid"                , _str , uid                       },
		{ "gid"                , _str , gid                       },
		{ "perm"               , _str , perm                      },
		{0},
	};
	qr->rc = ldmsd_plugin_qrent_add_bulk(&qr->coll, bulk);

out:
	return qr;
}

#define SAMPLER_COMP_IDX 0
#define SAMPLER_JOB_IDX  1
#define SAMPLER_APP_IDX  2

static
ldms_schema_t samp_create_schema(ldmsd_plugin_inst_t inst)
{
	ldmsd_sampler_type_t samp = (void*)inst->base;
	ldms_schema_t sch = ldms_schema_new(samp->schema_name);
	int idx;
	int rc;

	idx = ldms_schema_metric_add(sch, "component_id", LDMS_V_U64, "");
	if (idx < 0)
		goto err;
	idx = ldms_schema_metric_add(sch, "job_id", LDMS_V_U64, "");
	if (idx < 0)
		goto err;
	idx = ldms_schema_metric_add(sch, "app_id", LDMS_V_U64, "");
	if (idx < 0)
		goto err;
	samp->first_idx = idx + 1; /* first metric index to pupulate by the
				      sampler instance. */

	if (samp->update_schema) {
		rc = samp->update_schema(inst, sch);
		if (rc) {
			errno = rc;
			goto err;
		}
	}

	return sch;

 err:
	if (sch)
		ldms_schema_delete(sch);
	return NULL;
}

static
ldms_set_t samp_create_set(ldmsd_plugin_inst_t inst, const char *set_name,
			   ldms_schema_t schema, void *ctxt)
{
	ldmsd_sampler_type_t samp = (void*)inst->base;
	ldmsd_set_entry_t ent = NULL;

	ent = calloc(1, sizeof(*ent));
	if (!ent)
		goto err;
	ent->ctxt = ctxt;
	ent->set = ldms_set_new_with_auth(set_name, schema, samp->uid,
					  samp->gid, samp->perm);
	if (!ent->set)
		goto err;

	ldms_ctxt_set(ent->set, &samp->set_ctxt);

	if (samp->producer_name && samp->producer_name[0])
		ldms_set_producer_name_set(ent->set, samp->producer_name);

	ldms_set_publish(ent->set);
	LIST_INSERT_HEAD(&samp->set_list, ent, entry);

	return ent->set;

 err:
	if (ent)
		free(ent);
	return NULL;
}

ldms_set_t samp_create_set_group(ldmsd_plugin_inst_t inst,
				 const char *grp_name, void *ctxt)
{
	ldmsd_sampler_type_t samp = (void*)inst->base;
	ldmsd_set_entry_t ent = NULL;

	ent = calloc(1, sizeof(*ent));
	if (!ent)
		goto err;
	ent->ctxt = ctxt;
	ent->set = ldmsd_group_new(grp_name);
	if (!ent->set)
		goto err;

	ldms_ctxt_set(ent->set, &samp->set_ctxt);

	ldms_set_publish(ent->set);
	LIST_INSERT_HEAD(&samp->set_list, ent, entry);

	return ent->set;

 err:
	if (ent)
		free(ent);
	return NULL;
}

int samp_delete_set(ldmsd_plugin_inst_t inst, ldms_set_t set)
{
	/* NOTE: This must be called while samp->lock is held */
	ldmsd_sampler_type_t samp = (void*)inst->base;
	ldmsd_set_entry_t ent;
	LIST_FOREACH(ent, &samp->set_list, entry) {
		if (ent->set == set)
			break;
	}
	if (!ent)
		return ENOENT;
	LIST_REMOVE(ent, entry);
	ldms_set_delete(ent->set);
	free(ent);
	return 0;
}

static
int samp_base_update_set(ldmsd_plugin_inst_t inst, ldms_set_t set)
{
	uint64_t job_id = 0;
	uint64_t app_id = 0;
	uint32_t start, end;
	struct ldms_timestamp ts;
	ldmsd_sampler_type_t samp = (void*)inst->base;

	if (samp->component_id && !ldms_metric_get_u64(set, SAMPLER_COMP_IDX)) {
		/* set component_id only if it has not been set */
		ldms_metric_set_u64(set, SAMPLER_COMP_IDX, samp->component_id);
	}

	if (!samp->job_set)
		return 0; /* not an error */
	start = ldms_metric_get_u64(samp->job_set, samp->job_start_idx);
	end = ldms_metric_get_u64(samp->job_set, samp->job_end_idx);
	ts = ldms_transaction_timestamp_get(set);
	if ((ts.sec >= start) && ((end == 0) || (ts.sec <= end))) {
		job_id = ldms_metric_get_u64(samp->job_set, samp->job_id_idx);
		app_id = ldms_metric_get_u64(samp->job_set, samp->app_id_idx);
	}
	ldms_metric_set_u64(set, SAMPLER_JOB_IDX, job_id);
	ldms_metric_set_u64(set, SAMPLER_APP_IDX, app_id);
	return 0;
}

static
int samp_sample(ldmsd_plugin_inst_t inst)
{
	ldmsd_sampler_type_t samp = (void*)inst->base;
	ldmsd_set_entry_t ent;
	int rc = 0;

	LIST_FOREACH(ent, &samp->set_list, entry) {
		if (ldmsd_group_check(ent->set)) {
			/* skip: set group is not periodically updated */
			continue;
		}
		ldms_transaction_begin(ent->set);
		rc = samp->base_update_set(inst, ent->set);
		if (rc)
			goto end;
		if (samp->update_set)
			rc = samp->update_set(inst, ent->set, ent->ctxt);
	end:
		ldms_transaction_end(ent->set);
		if (rc)
			return rc;
	}
	return rc;
}

static void plugin_sampler_cb(ldmsd_task_t task, void *arg)
{
	ldmsd_sampler_type_t samp = arg;
	pthread_mutex_lock(&samp->lock);
	samp->sample(samp->base.inst);
	pthread_mutex_unlock(&samp->lock);
}

void *new()
{
	ldmsd_sampler_type_t samp;
	samp = calloc(1, sizeof(*samp));
	if (!samp)
		return samp;

	samp->base.type_name = LDMSD_SAMPLER_TYPENAME;
	LDMSD_PLUGIN_VERSION_INIT(&samp->base.version);

	/* setup plugin base interface */
	samp->base.desc = samp_desc;
	samp->base.help = samp_help;
	samp->base.init = samp_init;
	samp->base.del = samp_del;
	samp->base.config = samp_config;
	samp->base.query = ldmsd_sampler_query;

	/* setup sampler interface */
	samp->create_schema = samp_create_schema;
	samp->create_set = samp_create_set;
	samp->create_set_group = samp_create_set_group;
	samp->delete_set = samp_delete_set;
	samp->sample = samp_sample;
	samp->base_update_set = samp_base_update_set;

	samp->set_ctxt.type = LDMSD_SET_CTXT_SAMP;

	LIST_INIT(&samp->set_list);

	return samp;
}
