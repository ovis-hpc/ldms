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

#include <openssl/sha.h>

#include "ovis_json/ovis_json.h"
#include "coll/rbt.h"

#include "ldmsd.h"
#include "ldmsd_request.h"

/* Implementation is in ldmsd_decomp.c */
ldmsd_decomp_t ldmsd_decomp_get(const char *decomp, ldmsd_req_ctxt_t reqc);

#define FLEX_ERR(reqc, rc, fmt, ...) do { \
	ldmsd_log(LDMSD_LERROR, fmt, ##__VA_ARGS__);	\
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

typedef struct flex_digest_s {
	struct rbn rbn;
	struct ldms_digest_s digest; /* also rbn key */
	int n_decomp; /* number of decomposers to apply */
	flex_decomp_t decomp[OVIS_FLEX]; /* refs to the utilized decomposers */
} *flex_digest_t;

typedef struct flex_cfg_s {
	struct ldmsd_decomp_s decomp;
	struct rbt digest_tree;
	struct rbt decomp_tree;
	flex_digest_t default_digest;
} *flex_cfg_t;

static ldmsd_decomp_t flex_config(ldmsd_strgp_t strgp,
			json_t *cfg, ldmsd_req_ctxt_t reqc);
static int flex_decompose(ldmsd_strgp_t strgp, ldms_set_t set,
				     ldmsd_row_list_t row_list, int *row_count);
static void flex_release_rows(ldmsd_strgp_t strgp,
					 ldmsd_row_list_t row_list);
static void flex_release_decomp(ldmsd_strgp_t strgp);

struct ldmsd_decomp_s __decomp_flex = {
	.config = flex_config,
	.decompose = flex_decompose,
	.release_rows = flex_release_rows,
	.release_decomp = flex_release_decomp,
};

ldmsd_decomp_t get()
{
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
	if (!jdigest) {
		FLEX_ERR(reqc, EINVAL, "'digest' attribute is missing\n");
		goto err_0;
	}
	if (!json_is_object(jdigest)) {
		FLEX_ERR(reqc, EINVAL, "'digest' must be a dictionary\n");
		goto err_0;
	}

	dcfg = calloc(1, sizeof(*dcfg));
	if (!dcfg)
		goto enomem;
	dcfg->decomp = __decomp_flex;
	rbt_init(&dcfg->decomp_tree, flex_decomp_cmp);
	rbt_init(&dcfg->digest_tree, flex_digest_cmp);

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
		if (n_decomp == 1) {
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

	return &dcfg->decomp;
enomem:
	FLEX_ERR(reqc, ENOMEM, "Insufficient memory.\n");
err_1:
	flex_cfg_free(dcfg);
err_0:
	return NULL;
}

static int flex_decompose(ldmsd_strgp_t strgp, ldms_set_t set,
				    ldmsd_row_list_t row_list, int *row_count)
{
	flex_cfg_t dcfg = (void*)strgp->decomp;
	ldms_digest_t digest = ldms_set_digest_get(set);
	struct ldmsd_row_list_s rlist;
	int rcount, i, rc;
	flex_digest_t digest_rbn;
	flex_decomp_t decomp_rbn;

	TAILQ_INIT(&rlist);

	digest_rbn = (void*)rbt_find(&dcfg->digest_tree, digest);
	if (!digest_rbn) {
		if (!dcfg->default_digest)
			return 0;
		digest_rbn = dcfg->default_digest;
	}
	*row_count = 0;
	for (i = 0; i < digest_rbn->n_decomp; i++) {
		rcount = 0;
		decomp_rbn = digest_rbn->decomp[i];
		rc = decomp_rbn->decomp_api->decompose(
				&decomp_rbn->strgp,
				set, &rlist, &rcount);
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
