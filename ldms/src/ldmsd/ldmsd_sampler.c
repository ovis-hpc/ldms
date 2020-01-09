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
	samp->current_slot_idx = -1;

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
int samp_config(ldmsd_plugin_inst_t inst, json_entity_t json,
					char *ebuf, int ebufsz)
{
	ldmsd_sampler_type_t samp = (void*)inst->base;
	char *job_set_name;
	json_entity_t value;
	const char *name = inst->inst_name;
	char buff[1024];

	value = json_value_find(json, "producer");
	if (value) {
		if (value->type != JSON_STRING_VALUE) {
			ldmsd_log(LDMSD_LERROR, "%s: the 'producer' value is "
					"not a string.\n", name);
			goto einval;
		}
		samp->producer_name = strdup(json_value_str(value)->str);
	} else {
		ldmsd_log(LDMSD_LINFO, "%s: producer not specified, "
			  "using `%s`\n", name, ldmsd_myname_get());
		samp->producer_name = strdup(ldmsd_myname_get());
	}

	value = json_value_find(json, "component_id");
	if (value) {
		if (value->type != JSON_STRING_VALUE) {
			ldmsd_log(LDMSD_LERROR, "%s: the 'component_id' value "
					"is not a string.\n", name);
			goto einval;
		}
		samp->component_id = strtoull(json_value_str(value)->str, NULL, 0);
	}

	value = json_value_find(json, "instance");
	if (!value) {
		snprintf(buff, sizeof(buff), "%s/%s", samp->producer_name,
			 inst->inst_name);
	} else {
		if (value->type != JSON_STRING_VALUE) {
			ldmsd_log(LDMSD_LERROR, "%s: the 'instance' value "
					"is not a string.\n", name);
			goto einval;
		}
		snprintf(buff, sizeof(buff), "%s", json_value_str(value)->str);
	}

	samp->set_inst_name = strdup(buff);

	value = json_value_find(json, "schema");
	if (!value) {
		samp->schema_name = strdup(inst->plugin_name);
	} else {
		if (value->type != JSON_STRING_VALUE) {
			ldmsd_log(LDMSD_LERROR, "%s: the 'schema' value "
					"is not a string.\n", name);
			goto einval;
		}
		samp->schema_name = strdup(json_value_str(value)->str);
	}

	value = json_value_find(json, "job_set");
	if (!value) {
		snprintf(buff, sizeof(buff), "%s/jobinfo",
			 samp->producer_name);
		job_set_name = buff;
	} else {
		job_set_name = (char *)json_value_str(value)->str;
	}
	samp->job_set = ldms_set_by_name(job_set_name);
	if (!samp->job_set) {
		ldmsd_log(LDMSD_LINFO, "%s: The job data set named, %s, "
			  "does not exist. Job data will not be associated "
			  "with the metric values.\n", name, job_set_name);
		samp->job_id_idx = -1;
	} else {
		value = json_value_find(json, "job_id");
		if (!value) {
			snprintf(buff, sizeof(buff), "job_id");
		} else {
			if (value->type != JSON_STRING_VALUE) {
				ldmsd_log(LDMSD_LERROR, "%s: The 'job_id' value "
						"is not a string.\n", name);
				goto einval;
			}
			snprintf(buff, sizeof(buff), "%s", json_value_str(value)->str);
		}
		samp->job_id_idx = ldms_metric_by_name(samp->job_set, buff);
		if (samp->job_id_idx < 0) {
			snprintf(ebuf, ebufsz,
				 "%s: The specified job_set '%s' "
				 "is missing the 'job_id' attribute and "
				 "cannot be used.\n", name, job_set_name);
			goto einval;
		}
		value = json_value_find(json, "app_id");
		if (!value) {
			snprintf(buff, sizeof(buff), "app_id");
		} else {
			if (value->type != JSON_STRING_VALUE) {
				ldmsd_log(LDMSD_LERROR, "%s: The 'app_id' value "
						"is not a string.\n", name);
				goto einval;
			}
			snprintf(buff, sizeof(buff), "%s", json_value_str(value)->str);
		}
		samp->app_id_idx = ldms_metric_by_name(samp->job_set, buff);
		if (samp->app_id_idx < 0) {
			snprintf(ebuf, ebufsz,
				 "%s: The specified job_set '%s' "
				 "is missing the 'app_id' attribute and "
				 "cannot be used.\n", name, job_set_name);
			goto einval;
		}
		value = json_value_find(json, "job_start");
		if (!value) {
			snprintf(buff, sizeof(buff), "job_start");
		} else {
			if (value->type != JSON_STRING_VALUE) {
				ldmsd_log(LDMSD_LERROR, "%s: The 'job_start' value "
						"is not a string.\n", name);
				goto einval;
			}
			snprintf(buff, sizeof(buff), "%s", json_value_str(value)->str);
		}
		samp->job_start_idx = ldms_metric_by_name(samp->job_set, buff);
		if (samp->job_start_idx < 0) {
			snprintf(ebuf, ebufsz,
				 "%s: The specified job_set '%s' "
				 "is missing the 'job_start' attribute and "
				 "cannot be used.\n", name, job_set_name);
			goto einval;
		}
		value = json_value_find(json, "job_end");
		if (!value) {
			snprintf(buff, sizeof(buff), "job_end");
		} else {
			if (value->type != JSON_STRING_VALUE) {
				ldmsd_log(LDMSD_LERROR, "%s: The 'job_end' value "
						"is not a string.\n", name);
				goto einval;
			}
			snprintf(buff, sizeof(buff), "%s", json_value_str(value)->str);
		}
		samp->job_end_idx = ldms_metric_by_name(samp->job_set, buff);
		if (samp->job_end_idx < 0) {
			snprintf(ebuf, ebufsz,
				 "%s: The specified job_set "
				 "'%s' is missing the 'job_end' attribute and "
				 "cannot be used.\n", name, job_set_name);
			goto einval;
		}
		samp->current_slot_idx = ldms_metric_by_name(samp->job_set,
							     "current_slot");
	}
	/* uid, gid, permission */
	value = json_value_find(json, "uid");
	if (value) {
		if (value->type != JSON_STRING_VALUE) {
			ldmsd_log(LDMSD_LERROR, "%s: The 'uid' value "
					"is not a string.\n", name);
			goto einval;
		}

		char *uid_s = json_value_str(value)->str;

		if (isalpha(uid_s[0])) {
			/* Try to lookup the user name */
			struct passwd _pwd;
			struct passwd *pwd;
			getpwnam_r(uid_s, &_pwd, buff, sizeof(buff), &pwd);
			if (!pwd) {
				snprintf(ebuf, ebufsz, "%s: The specified "
					 "user '%s' does not exist\n",
					 name, uid_s);
				goto einval;
			}
			samp->uid = pwd->pw_uid;
		} else {
			samp->uid = strtol(uid_s, NULL, 0);
		}
	} else {
		samp->uid = geteuid();
	}
	value = json_value_find(json, "gid");
	if (value) {
		if (value->type != JSON_STRING_VALUE) {
			ldmsd_log(LDMSD_LERROR, "%s: The 'gid' value "
					"is not a string.\n", name);
			goto einval;
		}

		char *gid_s = json_value_str(value)->str;
		if (isalpha(gid_s[0])) {
			/* Try to lookup the group name */
			struct group _grp;
			struct group *grp;
			getgrnam_r(gid_s, &_grp, buff, sizeof(buff), &grp);
			if (!grp) {
				snprintf(ebuf, ebufsz, "%s: The specified "
					 "group '%s' does not exist\n",
					 name, gid_s);
				goto einval;
			}
			samp->gid = grp->gr_gid;
		} else {
			samp->gid = strtol(gid_s, NULL, 0);
		}
	} else {
		samp->gid = getegid();
	}
	value = json_value_find(json, "perm");
	if (!value) {
		samp->perm = 0777;
	} else {
		if (value->type != JSON_STRING_VALUE) {
			ldmsd_log(LDMSD_LERROR, "%s: The 'perm' value "
					"is not a string.\n", name);
			goto einval;
		}
		char *os = json_value_str(value)->str;
		if (os[0] != '0') {
			ldmsd_log(LDMSD_LINFO, "%s: Warning, the permission bits '%s' "
				  "are not specified as an Octal number.\n",
				  name, os);
			samp->perm = 0777;
		} else {
			samp->perm = strtol(json_value_str(value)->str, NULL, 0);
		}
	}

	/* set_array_card */
	value = json_value_find(json, "set_array_card");
	if (!value) {
		samp->set_array_card = 1;
	} else {
		if (value->type != JSON_STRING_VALUE) {
			ldmsd_log(LDMSD_LERROR, "%s: The 'set_array_card' value "
					"is not a string.\n", name);
			goto einval;
		}
		samp->set_array_card = strtol(json_value_str(value)->str, NULL, 0);
	}

	return 0;

 einval:
	if (samp->producer_name) {
		free(samp->producer_name);
		samp->producer_name = NULL;
	}
	if (samp->set_inst_name) {
		free(samp->set_inst_name);
		samp->set_inst_name = NULL;
	}
	if (samp->schema_name) {
		free(samp->schema_name);
		samp->schema_name = NULL;
	}
	if (samp->job_set) {
		ldms_set_put(samp->job_set);
		samp->job_set = NULL;
	}
	return EINVAL;
}

const char *ldmsd_sampler_help()
{
	return "\
Parameters:\n\
  [instance=]         The set instance name. The name must be unique among\n\
                      all metric sets in all LDMS daemons.\n\
                      The default '<producer>/<plugin instance name>'.\n\
  [producer=]         The unique name for the host providing the data.\n\
  [component_id=]     The unique number for the component being monitored.\n\
                      The default is zero.\n\
  [schema=]           The metric set schema name. The default is the plugin name.\n\
  [job_set=]          The set instance name of the set containing the job data. The default is '<producer>/jobinfo'.\n\
  [job_id=]           The name of the metric containing the Job Id. The default is 'job_id'.\n\
  [app_id=]           The name of the metric contaning the Application Id. The default is 'app_id'.\n\
  [job_start=]        The name of the metric containing the Job start time. The default is 'job_start'.\n\
  [job_end=]          The name of the metric containing the Job end time. The default is 'job_end'.\n\
  [uid=]              The user id of the set's owner. The default is the returned value of geteuid().\n\
  [gid=]              The group id of the set's owner. The default is the returned value of getegid().\n\
  [perm=]             The sampler plugin instance access permission. The default is 0777.\n\
";
}

json_entity_t ldmsd_sampler_query(ldmsd_plugin_inst_t inst, const char *q)
{
	ldmsd_sampler_type_t samp = (void*)inst->base;
	json_entity_t result, status_attr, status;
	int rc;

	/* call `super` query first -- to handle the common plugin part */
	result = ldmsd_plugin_query(inst, q);
	if (!result) {
		/* No query result found */
		return NULL;
	}

	if (0 != strcmp(q, "status")) {
		/*
		 * No additional attribute to add
		 */
		goto out;
	}

	/* Extend the 'status' result */
	status_attr = json_attr_find(result, "status");
	status = json_attr_value(status_attr);
	/* Add the additional attributes for the 'status' result. */
	char perm[8];
	long interval_us, offset_us;
	if (samp->smplr) {
		interval_us = samp->smplr->interval_us;
		offset_us = samp->smplr->offset_us;
	} else {
		interval_us = 0;
		offset_us = LDMSD_UPDT_HINT_OFFSET_NONE;
	}
	sprintf(perm, "%#o", samp->perm);
	enum json_value_e _str = JSON_STRING_VALUE;
	enum json_value_e _int = JSON_INT_VALUE;
	struct ldmsd_plugin_qjson_attrs  bulk[] = {
		{ "sample_interval_us" , _int , { .d = interval_us }  },
		{ "sample_offset_us"   , _int , { .d = offset_us }    },
		{ "producer"           , _str , { .s = samp->producer_name }       },
		{ "schema"             , _str , { .s = samp->schema_name }         },
		{ "set_instance_name"  , _str , { .s = samp->set_inst_name }       },
		{ "uid"                , _int , { .d = samp->uid }                 },
		{ "gid"                , _int , { .d = samp->gid }                 },
		{ "perm"               , _str , { .s = perm }                      },
		{0},
	};
	rc = ldmsd_plugin_qjson_attrs_add(status, bulk);
	if (rc)
		ldmsd_plugin_qjson_err_set(result, ENOMEM, "Out of memory");
out:
	return result;
}

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
	struct ldmsd_sec_ctxt sctxt;

	ent = calloc(1, sizeof(*ent));
	if (!ent)
		goto err;
	ent->ctxt = ctxt;
	ldmsd_sec_ctxt_get(&sctxt);
	ent->set = ldmsd_group_new_with_auth(grp_name, sctxt.crd.uid,
						sctxt.crd.gid, 0777);
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
	int curr_slot;
	struct ldms_timestamp ts;
	ldmsd_sampler_type_t samp = (void*)inst->base;

	if (samp->component_id && !ldms_metric_get_u64(set, SAMPLER_COMP_IDX)) {
		/* set component_id only if it has not been set */
		ldms_metric_set_u64(set, SAMPLER_COMP_IDX, samp->component_id);
	}

	if (!samp->job_set)
		return 0; /* not an error */
	if (samp->current_slot_idx > 0) {
		/* multi-tenant slurm job set */
		curr_slot = ldms_metric_get_u32(samp->job_set,
						samp->current_slot_idx);
		if (curr_slot >= 0) {
		}
		start = ldms_metric_array_get_u32(samp->job_set,
						  samp->job_start_idx,
						  curr_slot);
		end = ldms_metric_array_get_u32(samp->job_set,
						samp->job_end_idx,
						curr_slot);
		ts = ldms_transaction_timestamp_get(set);
		if ((ts.sec >= start) && ((end == 0) || (ts.sec <= end))) {
			job_id = ldms_metric_array_get_u64(samp->job_set,
							   samp->job_id_idx,
							   curr_slot);
			app_id = ldms_metric_get_u64(samp->job_set,
						     samp->app_id_idx);
		}
	} else {
		/* jobinfo job set */
		start = ldms_metric_get_u64(samp->job_set, samp->job_start_idx);
		end = ldms_metric_get_u64(samp->job_set, samp->job_end_idx);
		ts = ldms_transaction_timestamp_get(set);
		if ((ts.sec >= start) && ((end == 0) || (ts.sec <= end))) {
			job_id = ldms_metric_get_u64(samp->job_set,
						     samp->job_id_idx);
			app_id = ldms_metric_get_u64(samp->job_set,
						     samp->app_id_idx);
		}
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
