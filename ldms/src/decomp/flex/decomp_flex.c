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

static ldmsd_decomp_t __decomp_flex_config(ldmsd_strgp_t strgp,
			json_entity_t cfg, ldmsd_req_ctxt_t reqc);
static int __decomp_flex_decompose(ldmsd_strgp_t strgp, ldms_set_t set,
				     ldmsd_row_list_t row_list, int *row_count);
static void __decomp_flex_release_rows(ldmsd_strgp_t strgp,
					 ldmsd_row_list_t row_list);
static void __decomp_flex_release_decomp(ldmsd_strgp_t strgp);

struct ldmsd_decomp_s __decomp_flex = {
	.config = __decomp_flex_config,
	.decompose = __decomp_flex_decompose,
	.release_rows = __decomp_flex_release_rows,
	.release_decomp = __decomp_flex_release_decomp,
};

ldmsd_decomp_t get()
{
	return &__decomp_flex;
}

/* ==== JSON helpers ==== */

static json_entity_t __jdict_ent(json_entity_t dict, const char *key)
{
	json_entity_t attr;
	json_entity_t val;

	attr = json_attr_find(dict, key);
	if (!attr) {
		errno = ENOKEY;
		return NULL;
	}
	val = json_attr_value(attr);
	return val;
}

#define JSTR(P) ((P)->value.str_)

/* ==== generic decomp ==== */
/* convenient macro to put error message in both ldmsd log and `reqc` */
#define DECOMP_ERR(reqc, rc, fmt, ...) do { \
		ldmsd_lerror("decomposer: " fmt, ##__VA_ARGS__); \
		if (reqc) { \
			(reqc)->errcode = rc; \
			Snprintf(&(reqc)->line_buf, &(reqc)->line_len, "decomposer: " fmt, ##__VA_ARGS__); \
		} \
	} while (0)

/* ==== flex decomposition === */

typedef struct __decomp_flex_decomp_rbn_s {
	struct rbn rbn;
	struct ldmsd_decomp_s *decomp_api;
	struct ldmsd_strgp strgp;
	char name[OVIS_FLEX]; /* also rbn key */
} *__decomp_flex_decomp_rbn_t;

static
int __decomp_flex_decomp_rbn_s_cmp(void *tree_key, const void *key)
{
	return strcmp(tree_key, key);
}

typedef struct __decomp_flex_digest_rbn_s {
	struct rbn rbn;
	struct ldms_digest_s digest; /* also rbn key */
	int n_decomp; /* number of decomposers to apply */
	__decomp_flex_decomp_rbn_t decomp_rbn[OVIS_FLEX]; /* refs to the utilized decomposers */
} *__decomp_flex_digest_rbn_t;

static
int __decomp_flex_digest_rbn_s_cmp(void *tree_key, const void *key)
{
	return memcmp(tree_key, key, sizeof(struct ldms_digest_s));
}

typedef struct __decomp_flex_cfg_s {
	struct ldmsd_decomp_s decomp;
	struct rbt digest_rbt;
	struct rbt decomp_rbt;
	__decomp_flex_digest_rbn_t default_digest;
} *__decomp_flex_cfg_t;

static void __decomp_flex_cfg_free(__decomp_flex_cfg_t dcfg)
{
	struct rbn *rbn;
	__decomp_flex_decomp_rbn_t decomp_rbn;
	if (dcfg->default_digest)
		free(dcfg->default_digest);
	while ((rbn = rbt_min(&dcfg->digest_rbt))) {
		rbt_del(&dcfg->digest_rbt, rbn);
		free(rbn);
	}
	while ((decomp_rbn = (void*)rbt_min(&dcfg->decomp_rbt))) {
		rbt_del(&dcfg->decomp_rbt, &decomp_rbn->rbn);
		if (decomp_rbn->strgp.decomp)
			decomp_rbn->decomp_api->release_decomp(&decomp_rbn->strgp);
		free(decomp_rbn);
	}
	free(dcfg);
}

static void
__decomp_flex_release_decomp(ldmsd_strgp_t strgp)
{
	if (strgp->decomp) {
		__decomp_flex_cfg_free((void*)strgp->decomp);
		strgp->decomp = NULL;
	}
}

static ldmsd_decomp_t
__decomp_flex_config(ldmsd_strgp_t strgp, json_entity_t jcfg,
		      ldmsd_req_ctxt_t reqc)
{
	__decomp_flex_cfg_t dcfg = NULL;
	int n_decomp, i;
	json_entity_t jdecomp, jdigest, jattr, jkey, jval, jtype;

	/* decomposition */
	jdecomp = __jdict_ent(jcfg, "decomposition");
	if (!jdecomp) {
		DECOMP_ERR(reqc, EINVAL, "'decomposition' attribute is missing\n");
		goto err_0;
	}
	if (jdecomp->type != JSON_DICT_VALUE) {
		DECOMP_ERR(reqc, EINVAL, "'decomposition' must be a dictionary\n");
		goto err_0;
	}

	/* digest */
	jdigest = __jdict_ent(jcfg, "digest");
	if (!jdigest) {
		DECOMP_ERR(reqc, EINVAL, "'digest' attribute is missing\n");
		goto err_0;
	}
	if (jdigest->type != JSON_DICT_VALUE) {
		DECOMP_ERR(reqc, EINVAL, "'digest' must be a dictionary\n");
		goto err_0;
	}

	dcfg = calloc(1, sizeof(*dcfg));
	if (!dcfg) {
		DECOMP_ERR(reqc, ENOMEM, "Not enough memory\n");
		goto err_0;
	}
	dcfg->decomp = __decomp_flex;
	rbt_init(&dcfg->decomp_rbt, __decomp_flex_decomp_rbn_s_cmp);
	rbt_init(&dcfg->digest_rbt, __decomp_flex_digest_rbn_s_cmp);

	__decomp_flex_decomp_rbn_t decomp_rbn;
	struct ldmsd_decomp_s *decomp_api;

	/* processing decompotision */
	for (jattr = json_attr_first(jdecomp); jattr; jattr = json_attr_next(jattr)) {
		jkey = jattr->value.attr_->name;
		assert(jkey->type == JSON_STRING_VALUE);
		jval = jattr->value.attr_->value;
		if (jval->type != JSON_DICT_VALUE) {
			DECOMP_ERR(reqc, EINVAL, "decomposition['%s'] must be "
					 "a dictionary\n", JSTR(jkey)->str);
			goto err_1;
		}
		jtype = __jdict_ent(jval, "type");
		if (!jtype) {
			DECOMP_ERR(reqc, EINVAL, "decomposition['%s'] must "
					 "specify 'type' attribute\n",
					 JSTR(jkey)->str);
			goto err_1;
		}
		if (jtype->type != JSON_STRING_VALUE) {
			DECOMP_ERR(reqc, EINVAL, "decomposition['%s']['type'] "
					 "must be a string\n",
					 JSTR(jkey)->str);
			goto err_1;
		}
		decomp_api = ldmsd_decomp_get(JSTR(jtype)->str, reqc);
		if (!decomp_api) {
			/* ldmsd_decomp_get() already populate reqc error */
			goto err_1;
		}
		decomp_rbn = calloc(1, sizeof(*decomp_rbn) +
					jkey->value.str_->str_len + 1);
		if (!decomp_rbn) {
			DECOMP_ERR(reqc, ENOMEM, "Not enough memory\n");
			goto err_1;
		}
		memcpy(decomp_rbn->name, JSTR(jkey)->str, JSTR(jkey)->str_len);
		decomp_rbn->decomp_api = decomp_api;
		decomp_rbn->strgp.decomp = decomp_api->config(&decomp_rbn->strgp, jval, reqc);
		if (!decomp_rbn->strgp.decomp) {
			/* reqc error has been populated */
			free(decomp_rbn);
			goto err_1;
		}
		rbn_init(&decomp_rbn->rbn, decomp_rbn->name);
		rbt_ins(&dcfg->decomp_rbt, &decomp_rbn->rbn);
	}

	json_entity_t jlist;
	__decomp_flex_digest_rbn_t digest_rbn;

	/* processing digest */
	for (jattr = json_attr_first(jdigest); jattr; jattr = json_attr_next(jattr)) {
		jkey = jattr->value.attr_->name;
		assert(jkey->type == JSON_STRING_VALUE);
		jval = jattr->value.attr_->value;
		jlist = NULL;
		if (jval->type == JSON_LIST_VALUE) {
			jlist = jval;
			jval = TAILQ_FIRST(&jlist->value.list_->item_list);
			n_decomp = jlist->value.list_->item_count;
		} else if (jval->type == JSON_STRING_VALUE) {
			n_decomp = 1;
		} else {
			/* invalid value */
			DECOMP_ERR(reqc, EINVAL, "digest['%s'] value must be"
					"a string or a list of strings.\n",
					JSTR(jkey)->str);
			goto err_1;
		}
		int rc;
		struct ldms_digest_s digest = {};
		if (0 == strcmp(JSTR(jkey)->str, "*")) {
			digest_rbn = dcfg->default_digest;
		} else {
			rc = ldms_str_digest(JSTR(jkey)->str, &digest);
			if (rc) {
				DECOMP_ERR(reqc, rc, "Invalid digest '%s'.\n",
					JSTR(jkey)->str);
				goto err_1;
			}
			digest_rbn = (void*)rbt_find(&dcfg->digest_rbt, &digest);
		}
		if (digest_rbn) {
			DECOMP_ERR(reqc, EINVAL,
				   "Multiple definition of digest['%s'].\n",
				   JSTR(jkey)->str);
			goto err_1;
		}
		digest_rbn = calloc(1, sizeof(*digest_rbn) +
					n_decomp*sizeof(digest_rbn->decomp_rbn[0]));
		if (!digest_rbn) {
			DECOMP_ERR(reqc, ENOMEM, "Not enough memory\n");
			goto err_1;
		}
		memcpy(&digest_rbn->digest, &digest, sizeof(digest));
		rbn_init(&digest_rbn->rbn, &digest_rbn->digest);
		digest_rbn->n_decomp = n_decomp;
		if (0 == strcmp(JSTR(jkey)->str, "*")) {
			dcfg->default_digest = digest_rbn;
		} else {
			rbt_ins(&dcfg->digest_rbt, &digest_rbn->rbn);
		}
		i = 0;
		while (jval) {
			if (jval->type != JSON_STRING_VALUE) {
				DECOMP_ERR(reqc, EINVAL, "digest['%s'] value must be"
					"a string or a list of strings.\n",
					JSTR(jkey)->str);
				goto err_1;
			}
			decomp_rbn = (void*)rbt_find(&dcfg->decomp_rbt, JSTR(jval)->str);
			if (!decomp_rbn) {
				DECOMP_ERR(reqc, ENOENT, "decomposition '%s' is not defined.\n",
						JSTR(jval)->str);
				goto err_1;
			}
			digest_rbn->decomp_rbn[i] = decomp_rbn;
			if (jlist) {
				jval = TAILQ_NEXT(jval, item_entry);
			} else {
				jval = NULL;
			}
			i++;
		}
		assert(i == n_decomp);
	}

	return &dcfg->decomp;
 err_1:
	__decomp_flex_cfg_free(dcfg);
 err_0:
	return NULL;
}

static int __decomp_flex_decompose(ldmsd_strgp_t strgp, ldms_set_t set,
				    ldmsd_row_list_t row_list, int *row_count)
{
	__decomp_flex_cfg_t dcfg = (void*)strgp->decomp;
	ldms_digest_t digest = ldms_set_digest_get(set);
	struct ldmsd_row_list_s rlist;
	int rcount, i, rc;
	__decomp_flex_digest_rbn_t digest_rbn;
	__decomp_flex_decomp_rbn_t decomp_rbn;

	TAILQ_INIT(&rlist);

	digest_rbn = (void*)rbt_find(&dcfg->digest_rbt, digest);
	if (!digest_rbn) {
		if (!dcfg->default_digest)
			return 0;
		digest_rbn = dcfg->default_digest;
	}
	for (i = 0; i < digest_rbn->n_decomp; i++) {
		rcount = 0;
		decomp_rbn = digest_rbn->decomp_rbn[i];
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
	__decomp_flex_release_rows(strgp, row_list);
	return rc;
}

static void __decomp_flex_release_rows(ldmsd_strgp_t strgp,
					 ldmsd_row_list_t row_list)
{
	ldmsd_row_t row;
	while ((row = TAILQ_FIRST(row_list))) {
		TAILQ_REMOVE(row_list, row, entry);
		free(row);
	}
}
