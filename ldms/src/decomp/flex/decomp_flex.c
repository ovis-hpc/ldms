/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2022 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2022 Open Grid Computing, Inc. All rights reserved.
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

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <sys/queue.h>
#include <regex.h>
#include <stdlib.h>

#include <openssl/sha.h>

#include "coll/rbt.h"

#include "ldmsd.h"
#include "ldmsd_request.h"

/* Implementation is in ldmsd_decomp.c */
ldmsd_decomp_t ldmsd_decomp_get(const char *decomp, ldmsd_req_ctxt_t reqc);

static ovis_log_t flex_log;
#define FLEX_ERR(reqc, rc, fmt, ...) do { \
	ovis_log(flex_log, OVIS_LERROR, fmt, ##__VA_ARGS__);	\
	if (reqc) {						\
		(reqc)->errcode = rc;				\
		Snprintf(&(reqc)->line_buf, &(reqc)->line_len, fmt, ##__VA_ARGS__); \
	}							\
} while (0)

typedef struct flex_decomp_s {
	struct rbn rbn;
	struct ldmsd_decomp_s *decomp_api;
	struct ldmsd_strgp strgp;
	char name[OVIS_FLEX]; /* also rbn key */
} *flex_decomp_t;

typedef struct flex_decomp_array_s {
	int n_decomp;
	flex_decomp_t decomp[OVIS_FLEX];
} *flex_decomp_array_t;

typedef struct flex_digest_s {
	struct rbn rbn;
	struct ldms_digest_s digest; /* also rbn key */
	int n_decomp; /* number of decomposers to apply */
	flex_decomp_t decomp[OVIS_FLEX]; /* refs to the utilized decomposers */
} *flex_digest_t;

typedef struct flex_match_s {
	STAILQ_ENTRY( flex_match_s ) entry;
	regex_t reg_sch; /* regex for schema */
	regex_t reg_inst; /* regex for instance */
	int n_decomp;
	flex_decomp_t decomp[OVIS_FLEX];
} *flex_match_t;

typedef struct flex_cfg_s {
	struct ldmsd_decomp_s decomp;
	struct rbt digest_tree;
	struct rbt decomp_tree;
	flex_digest_t default_digest;
	STAILQ_HEAD(, flex_match_s) matches;
	flex_decomp_array_t default_decomps;
} *flex_cfg_t;

static ldmsd_decomp_t flex_config(ldmsd_strgp_t strgp,
			json_t *cfg, ldmsd_req_ctxt_t reqc);
static int flex_decompose(ldmsd_strgp_t strgp, ldms_set_t set,
				     ldmsd_row_list_t row_list, int *row_count,
				     void **ctxt);
static void flex_release_rows(ldmsd_strgp_t strgp,
					 ldmsd_row_list_t row_list);
static void flex_release_decomp(ldmsd_strgp_t strgp);

static void flex_decomp_ctxt_release(ldmsd_strgp_t strgp, void **ctxt_ptr);

struct ldmsd_decomp_s __decomp_flex = {
	.config = flex_config,
	.decompose = flex_decompose,
	.release_rows = flex_release_rows,
	.release_decomp = flex_release_decomp,
	.decomp_ctxt_release = flex_decomp_ctxt_release,
};

ldmsd_decomp_t get()
{
	flex_log = ovis_log_register("store.decomp.flex", "Messages for the flex decomposition plugin");
	if (!flex_log) {
		ovis_log(NULL, OVIS_LWARN, "Failed to create the flex decomposition "
					   "plugin's log subsytem. Error %d.\n", errno);
	}
	return &__decomp_flex;
}

static int flex_decomp_cmp(void *tree_key, const void *key)
{
	return strcmp(tree_key, key);
}

static
int flex_digest_cmp(void *tree_key, const void *key)
{
	return memcmp(tree_key, key, sizeof(struct ldms_digest_s));
}

static void flex_cfg_free(flex_cfg_t dcfg)
{
	struct rbn *rbn;
	flex_decomp_t decomp_rbn;
	flex_match_t match;

	while ((match = STAILQ_FIRST(&dcfg->matches))) {
		STAILQ_REMOVE_HEAD(&dcfg->matches, entry);
		regfree(&match->reg_sch);
		regfree(&match->reg_inst);
		free(match);
	}

	if (dcfg->default_digest)
		free(dcfg->default_digest);
	while ((rbn = rbt_min(&dcfg->digest_tree))) {
		rbt_del(&dcfg->digest_tree, rbn);
		free(rbn);
	}
	while ((decomp_rbn = (void*)rbt_min(&dcfg->decomp_tree))) {
		rbt_del(&dcfg->decomp_tree, &decomp_rbn->rbn);
		if (decomp_rbn->strgp.decomp)
			decomp_rbn->decomp_api->release_decomp(&decomp_rbn->strgp);
		free(decomp_rbn);
	}
	free(dcfg);
}

static void
flex_release_decomp(ldmsd_strgp_t strgp)
{
	if (strgp->decomp) {
		flex_cfg_free((void*)strgp->decomp);
		strgp->decomp = NULL;
	}
}

static int
qsort_uniq(void *base, int n_member, size_t member_sz,
			    int (*cmp)(const void*, const void *))
{
	int i, j;
	qsort(base, n_member, member_sz, cmp);
	j = 0;
	for (i = 1; i < n_member; i++) {
		if (0 == cmp(base + i*member_sz, base + j*member_sz))
			continue;
		j += 1;
		if (j < i)
			memcpy(base + j*member_sz, base + i*member_sz, member_sz);
	}
	return j+1;
}

int ptr_cmp(const void *_a, const void *_b)
{
	uint64_t a = (uint64_t)_a;
	uint64_t b = (uint64_t)_b;
	return (a<b)?(-1):((b<a)?(1):(0));
}

static ldmsd_decomp_t
flex_config(ldmsd_strgp_t strgp, json_t *jcfg,
		      ldmsd_req_ctxt_t reqc)
{
	flex_cfg_t dcfg = NULL;
	flex_decomp_t decomp;
	flex_digest_t digest;
	struct ldmsd_decomp_s *decomp_api;
	int n_decomp, i;
	const char *key;
	json_t *jdecomp, *jdigest, *jschema, *jtype;
	json_t *jdefault;
	json_t *jmatches, *jobj, *jinstance, *japply;
	flex_match_t match;
	int idx, rc;

	/* decomposition */
	jdecomp = json_object_get(jcfg, "decomposition");
	if (!jdecomp) {
		FLEX_ERR(reqc, EINVAL, "'decomposition' attribute is missing\n");
		goto err_0;
	}
	if (!json_is_object(jdecomp)) {
		FLEX_ERR(reqc, EINVAL, "'decomposition' must be a dictionary\n");
		goto err_0;
	}

	/* digest */
	jdigest = json_object_get(jcfg, "digest");
	if (jdigest && !json_is_object(jdigest)) {
		FLEX_ERR(reqc, EINVAL, "'digest' must be a dictionary\n");
		goto err_0;
	}

	/* matches */
	jmatches = json_object_get(jcfg, "matches");
	if (jmatches && !json_is_array(jmatches)) {
		FLEX_ERR(reqc, EINVAL, "'matches' must be an array of match objects\n");
		goto err_0;
	}

	/* default */
	jdefault = json_object_get(jcfg, "default");
	if (jdefault && !(json_is_array(jdefault) || json_is_string(jdefault))) {
		FLEX_ERR(reqc, EINVAL, "'default' must be a string or an array "
					"of strings.\n");
		goto err_0;
	}

	dcfg = calloc(1, sizeof(*dcfg));
	if (!dcfg)
		goto enomem;
	dcfg->decomp = __decomp_flex;
	rbt_init(&dcfg->decomp_tree, flex_decomp_cmp);
	rbt_init(&dcfg->digest_tree, flex_digest_cmp);
	STAILQ_INIT(&dcfg->matches);

	/* processing decomposition */
	json_object_foreach(jdecomp, key, jschema) {
		if (!json_is_object(jschema)) {
			FLEX_ERR(reqc, EINVAL,
				"decomposition['%s'] must be "
				"a dictionary\n", key);
			goto err_1;
		}
		jtype = json_object_get(jschema, "type");
		if (!jtype) {
			FLEX_ERR(reqc, EINVAL,
				"decomposition['%s'] must "
				"decomposition 'type' attribute is required: "
				"'as_is', or 'static'.\n", key);
			goto err_1;
		}
		if (!json_is_string(jtype)) {
			FLEX_ERR(reqc, EINVAL,
				"decomposition['%s']['type'] "
				"must be a string\n", key);
			goto err_1;
		}
		decomp_api = ldmsd_decomp_get(json_string_value(jtype), reqc);
		if (!decomp_api) {
			/* ldmsd_decomp_get() already populate reqc error */
			goto err_1;
		}
		decomp = calloc(1, sizeof(*decomp) + strlen(key) + 1);
		if (!decomp)
			goto enomem;
		strcpy(decomp->name, key);
		(void)asprintf(&decomp->strgp.obj.name, "%s.%s", strgp->obj.name, key);
		decomp->decomp_api = decomp_api;
		decomp->strgp.decomp = decomp_api->config(&decomp->strgp, jschema, reqc);
		if (!decomp->strgp.decomp) {
			free(decomp);
			goto err_1;
		}
		rbn_init(&decomp->rbn, decomp->name);
		rbt_ins(&dcfg->decomp_tree, &decomp->rbn);
	}

	/* Process digest */
	json_object_foreach(jdigest, key, jschema) {
		/* "01FC.....0102" : [ "schema_a", "schema_b" ],
		 * "02FC.....0102" : "schema_c",
		 * "*"             : "the_default"
		 * ...
		 */
		int rc;
		struct ldms_digest_s digest_key = {};

		/* Check for duplicate definition */
		digest = NULL;
		if (0 == strcmp(key, "*")) {
			digest = dcfg->default_digest;
		} else {
			rc = ldms_str_digest(key, &digest_key);
			if (rc) {
				FLEX_ERR(reqc, rc, "Invalid digest key '%s'.\n", key);
				goto err_1;
			}
			struct rbn *rbn = rbt_find(&dcfg->digest_tree, &digest_key);
			if (rbn)
				digest = container_of(rbn, struct flex_digest_s, rbn);
		}
		if (digest) {
			FLEX_ERR(reqc, EINVAL,
				"Ignoring duplicate definition of "
				"digest['%s'].\n", key);
			continue;
		}

		/* Determine how many decompositions are matched to the digest */
		if (json_is_string(jschema)) {
			n_decomp = 1;
		} else if (json_is_array(jschema)) {
			n_decomp = json_array_size(jschema);
		} else {
			FLEX_ERR(reqc, EINVAL,
				"Invalid value for decomposition attribute "
				"in 'digest'.\n");
			goto err_1;
		}

		/* Allocate a new digest */
		digest = calloc(1, sizeof(*digest) +
					n_decomp * sizeof(digest->decomp[0]));
		if (!digest)
			goto enomem;

		/* Insert digest into tree */
		memcpy(&digest->digest, &digest_key, sizeof(digest_key));
		rbn_init(&digest->rbn, &digest->digest);
		digest->n_decomp = n_decomp;
		if (0 == strcmp(key, "*")) {
			dcfg->default_digest = digest;
		} else {
			rbt_ins(&dcfg->digest_tree, &digest->rbn);
		}
		/* Look up the decomposition for each decomp key */
		if (json_is_string(jschema)) {
			struct rbn *rbn = rbt_find(&dcfg->decomp_tree,
					json_string_value(jschema));
			if (!rbn) {
				FLEX_ERR(reqc, EINVAL,
					"The specified decomposition "
					"schema ('%s') was not found.\n",
					json_string_value(jschema));
				goto err_1;
			}
			decomp = container_of(rbn, struct flex_decomp_s, rbn);
			digest->decomp[0] = decomp;
		} else {
			json_array_foreach(jschema, i, jdecomp) {
				struct rbn *rbn = rbt_find(&dcfg->decomp_tree,
						json_string_value(jdecomp));
				if (!rbn) {
					FLEX_ERR(reqc, EINVAL,
						"The specified decomposition "
						"schema ('%s') was not found.\n",
						json_string_value(jdecomp));
					goto err_1;
				}
				decomp = container_of(rbn, struct flex_decomp_s, rbn);
				digest->decomp[i] = decomp;
			}
		}
	}

	/* Process matches */
	json_array_foreach(jmatches, idx, jobj) {
		/* jobj := {
		 * 	"schema": "_SCHEMA_", "instance": "_INST_",
		 * 	"apply": "_DECOMP_" | [ "_DECOMP_", "_DECOMP_" ]
		 * }
		 */
		jschema = json_object_get(jobj, "schema");
		jinstance = json_object_get(jobj, "instance");
		japply = json_object_get(jobj, "apply");
		if (!jschema && !jinstance) {
			FLEX_ERR(reqc, EINVAL, "'schema' and/or 'instance' is "
				 "needed in a match object.");
			goto err_1;
		}
		if (jschema && json_typeof(jschema) != JSON_STRING) {
			FLEX_ERR(reqc, EINVAL, "'schema' must be string");
			goto err_1;
		}
		if (jinstance && json_typeof(jinstance) != JSON_STRING) {
			FLEX_ERR(reqc, EINVAL, "'instance' must be string");
			goto err_1;
		}
		if (!japply) {
			FLEX_ERR(reqc, EINVAL, "'apply' is needed in a match object.");
			goto err_1;
		}

		switch (json_typeof(japply)) {
		case JSON_STRING:
			n_decomp = 1;
			break;
		case JSON_ARRAY:
			n_decomp = json_array_size(japply);
			if (n_decomp <= 0) {
				FLEX_ERR(reqc, EINVAL, "Invalid 'apply' array length");
				goto err_1;
			}
			break;
		default:
			FLEX_ERR(reqc, EINVAL, "'apply' value type must be "
				 "string or array of strings");
			goto err_1;
		}

		match = calloc(1, sizeof(*match) +
				  n_decomp*sizeof(match->decomp[0]));
		if (!match)
			goto enomem;
		/* Insert now; if error occurred flex_cfg_free() will clean it up. */
		STAILQ_INSERT_TAIL(&dcfg->matches, match, entry);

		if (jschema) {
			rc = regcomp(&match->reg_sch,
				json_string_value(jschema),
				REG_EXTENDED|REG_NOSUB);
			if (rc) {
				FLEX_ERR(reqc, EINVAL,
					 "schema regex compilation error: "
					 "%d, regex: %s",
					 rc, json_string_value(jschema));
				goto err_1;
			}
		}

		if (jinstance) {
			rc = regcomp(&match->reg_inst,
				json_string_value(jinstance),
				REG_EXTENDED|REG_NOSUB);
			if (rc) {
				FLEX_ERR(reqc, EINVAL,
					 "instance regex compilation error: "
					 "%d, regex: %s",
					 rc, json_string_value(jschema));
				goto err_1;
			}
		}
		/* Look up the decomposition for each decomp key */
		if (json_is_string(japply)) {
			struct rbn *rbn = rbt_find(&dcfg->decomp_tree,
					json_string_value(japply));
			if (!rbn) {
				FLEX_ERR(reqc, EINVAL,
					"The specified decomposition "
					"'%s' was not found.\n",
					json_string_value(japply));
				goto err_1;
			}
			decomp = container_of(rbn, struct flex_decomp_s, rbn);
			match->decomp[0] = decomp;
			match->n_decomp = 1;
		} else {
			json_array_foreach(japply, i, jdecomp) {
				struct rbn *rbn = rbt_find(&dcfg->decomp_tree,
						json_string_value(jdecomp));
				if (!rbn) {
					FLEX_ERR(reqc, EINVAL,
						"The specified decomposition "
						"'%s' was not found.\n",
						json_string_value(jdecomp));
					goto err_1;
				}
				decomp = container_of(rbn, struct flex_decomp_s, rbn);
				match->decomp[i] = decomp;
			}
			match->n_decomp = json_array_size(japply);
		}
	}

	if (jdefault) {
		if (json_is_string(jdefault)) {
			n_decomp = 1;
		} else {
			n_decomp = json_array_size(jdefault);
		}
		if (!n_decomp) {
			FLEX_ERR(reqc, EINVAL, "'default' cannot be an "
						 "empty array.");
			goto err_1;
		}
		flex_decomp_array_t da;
		da = calloc(1, sizeof(*dcfg->default_decomps) +
			  n_decomp*sizeof(dcfg->default_decomps->decomp[0]));
		if (!da) {
			FLEX_ERR(reqc, ENOMEM, "Out of memory");
			goto err_1;
		}
		da->n_decomp = n_decomp;
		dcfg->default_decomps = da;
		struct rbn *rbn;
		/* fill decomp references */
		if (json_is_string(jdefault)) {
			rbn = rbt_find(&dcfg->decomp_tree,
					json_string_value(jdefault));
			if (!rbn) {
				FLEX_ERR(reqc, EINVAL,
					"The specified decomposition "
					"'%s' was not found.\n",
					json_string_value(japply));
				goto err_1;
			}
			da->decomp[0] = container_of(rbn, struct flex_decomp_s, rbn);
		} else {
			json_array_foreach(jdefault, i, jdecomp) {
				struct rbn *rbn = rbt_find(&dcfg->decomp_tree,
						json_string_value(jdecomp));
				if (!rbn) {
					FLEX_ERR(reqc, EINVAL,
						"The specified decomposition "
						"'%s' was not found.\n",
						json_string_value(jdecomp));
					goto err_1;
				}
				da->decomp[i] = container_of(rbn, struct flex_decomp_s, rbn);
			}
			da->n_decomp = qsort_uniq(da->decomp, da->n_decomp,
					sizeof(da->decomp[0]), ptr_cmp);
		}
	}

	return &dcfg->decomp;
enomem:
	FLEX_ERR(reqc, ENOMEM, "Insufficient memory.\n");
err_1:
	flex_cfg_free(dcfg);
err_0:
	return NULL;
}

struct flex_decomp_list_entry_s {
	flex_decomp_t decomp;
	void *decomp_ctxt;
};

typedef struct flex_decomp_list_s {
	int n_decomp;
	struct flex_decomp_list_entry_s decomp[];
} *flex_decomp_list_t;

int flex_decomp_list_entry_cmp(const void *_a, const void *_b)
{
	const struct flex_decomp_list_entry_s *a = _a;
	const struct flex_decomp_list_entry_s *b = _b;
	return ((uint64_t)a->decomp) - ((uint64_t)b->decomp);
}

/*
 * \retval 1 matched
 * \retval 0 did NOT match
 */
static int __match_set(flex_match_t match, ldms_set_t set)
{
	if (match->reg_sch.used) {
		if (0 != regexec(&match->reg_sch,
				 ldms_set_schema_name_get(set), 0, NULL, 0))
			return 0; /* does not match schema */
		if (match->reg_inst.used)
			goto match_inst;
		return 1;
	}

	if (match->reg_inst.used) {
 match_inst:
		return (0 == regexec(&match->reg_inst,
				 ldms_set_instance_name_get(set), 0, NULL, 0));
	}
	return 0;
}

static int flex_decompose(ldmsd_strgp_t strgp, ldms_set_t set,
				    ldmsd_row_list_t row_list, int *row_count,
				    void **decomp_ctxt)
{
	flex_cfg_t dcfg = (void*)strgp->decomp;
	ldms_digest_t digest = ldms_set_digest_get(set);
	struct ldmsd_row_list_s rlist;
	int rcount, i, rc;
	flex_digest_t digest_rbn;
	flex_decomp_list_t flex_list;
	flex_match_t match;
	int have_matches = 0;
	int n_decomp = 0, di;
	char dbuff[512];

	flex_list = *decomp_ctxt;

	if (flex_list)
		goto decompose;

	/* collect all decompositions from the config */

	/* get the number first */
	digest_rbn = (void*)rbt_find(&dcfg->digest_tree, digest);
	if (!digest_rbn) {
		digest_rbn = dcfg->default_digest;
	}
	if (digest_rbn) {
		n_decomp = digest_rbn->n_decomp;
	}
	STAILQ_FOREACH(match, &dcfg->matches, entry) {
		if (__match_set(match, set)) {
			n_decomp += match->n_decomp;
			have_matches = 1;
		}
	}

	if (0 == n_decomp) {
		/* does not match; apply the default */
		if (!dcfg->default_decomps) {
			ovis_log(flex_log, OVIS_LWARN,
				 "strgp '%s': no default decomposition, "
				 "unhandled set '%s' (%s)",
				 strgp->obj.name,
				 ldms_set_name_get(set),
				 ldms_digest_str(ldms_set_digest_get(set),
							 dbuff, sizeof(dbuff)));
			return 0;
		}
		n_decomp = dcfg->default_decomps->n_decomp;
		flex_list = calloc(1, sizeof(struct flex_decomp_list_s) +
				      n_decomp*sizeof(flex_decomp_t));
		if (!flex_list) {
			rc = ENOMEM;
			goto err_0;
		}
		flex_list->n_decomp = n_decomp;
		for (i = 0; i < n_decomp; i++) {
			flex_list->decomp[i].decomp = dcfg->default_decomps->decomp[i];
		}
		*decomp_ctxt = flex_list;
		goto decompose;
	}

	flex_list = calloc(1, sizeof(struct flex_decomp_list_s) +
			      n_decomp*sizeof(flex_list->decomp[0]));
	if (!flex_list) {
		rc = ENOMEM;
		goto err_0;
	}

	/* copy target decompositions */

	/* by digest */
	di = 0;
	for (i = 0; digest_rbn && i < digest_rbn->n_decomp; i++) {
		flex_list->decomp[di++].decomp = digest_rbn->decomp[i];
	}

	/* by matches */
	for (match = STAILQ_FIRST(&dcfg->matches);
			have_matches && match;
			match = STAILQ_NEXT(match, entry)) {
		if (__match_set(match, set)) {
			/* matched */
			for (i = 0; i < match->n_decomp; i++) {
				flex_list->decomp[di++].decomp = match->decomp[i];
			}
		}
	}

	/* sort & uniq */
	flex_list->n_decomp = qsort_uniq(flex_list->decomp, n_decomp,
					 sizeof(flex_list->decomp[0]),
					 flex_decomp_list_entry_cmp);
	*decomp_ctxt = flex_list;

 decompose:
	TAILQ_INIT(&rlist);

	*row_count = 0;
	for (i = 0; i < flex_list->n_decomp; i++) {
		struct flex_decomp_list_entry_s *d;
		rcount = 0;
		d = &flex_list->decomp[i];
		rc = d->decomp->decomp_api->decompose(
				&d->decomp->strgp,
				set, &rlist, &rcount,
				&d->decomp_ctxt);
		if (rc)
			goto err_0;
		TAILQ_CONCAT(row_list, &rlist, entry);
		/* rlist is now empty */
		*row_count += rcount;
	}
	return 0;

 err_0:
	flex_release_rows(strgp, row_list);
	return rc;
}

static void flex_release_rows(ldmsd_strgp_t strgp,
				ldmsd_row_list_t row_list)
{
	ldmsd_row_t row;
	while ((row = TAILQ_FIRST(row_list))) {
		TAILQ_REMOVE(row_list, row, entry);
		free(row);
	}
}

static void flex_decomp_ctxt_release(ldmsd_strgp_t strgp, void **ctxt_ptr)
{
	int i;
	flex_decomp_list_t list = *ctxt_ptr;
	struct ldmsd_decomp_s *decomp_api;
	if (!list)
		return;
	/* releases ctxt for sub-decomposer */
	for (i = 0; i < list->n_decomp; i++) {
		decomp_api = list->decomp[i].decomp->decomp_api;
		if (decomp_api->decomp_ctxt_release) {
			decomp_api->decomp_ctxt_release(strgp,
					&list->decomp[i].decomp_ctxt);
		}
	}
	free(list);
	*ctxt_ptr = NULL;
}
