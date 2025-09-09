/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2011-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2011-2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file file_exporter.c
 * \brief Exports the contents of files in a directory to a metric set
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <ftw.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include "sampler_base.h"

#define LBUFSZ		2048

typedef struct rec_def_s {
	char *name;		/* The record name */
	ldms_record_t ldms_rec;	/* The LDMS record definition */
	int rec_id;		/* Schema record def id */
	char *key_name;		/* The record instance descriminator */
	int key_id;		/* The metric id in the record for the key */

	struct rbn rbn;		/* set->record_tree entry */
} *rec_def_t;

typedef struct set_s {
	char *fname;		/* The set-file basename */
	ldms_set_t set;		/* Currently the file path */
	base_data_t base;	/* The base class for set above */
	int metric_offset;	/* First non-base-class metric */
	FILE *fp;

	struct rbn rbn;		/* Set entry in the set_tree */
	struct rbt mdef_tree;	/* The tree of metric names for this set/schema */
	struct rbt recdef_tree; /* Tree of record definitions indexed by 'key' */
} *set_t;

typedef struct dir_exp_s {
	ovis_log_t log;		/* Our log handle */
	ldmsd_plug_handle_t handle;
	char *cfg_name;		/* The configuration name (ldms_plug_cfg_name_get) */
	char *plug_name;	/* The plugin name (ldms_plug_name_get) */
	struct attr_value_list *avl;
	int line_no;            /* The current line number (for error reporting) */
	char *path;		/* The directory containing the files to scrape */
	char *channel;		/* The message channel to receive sample triggers from */
	ldms_msg_client_t msg_client;
	struct rbt set_tree;	/* The set/schema from this directory */
} *dir_exp_t;

typedef struct type_info_s {
	char *list_name;	/* The the list containing the records */
	char *record_name;	/* The record of which this metric is a member */
	char *unit_name;	/* The unit string for ldms_metric_create */
	char *metric_type;	/* The LDMS metric type name */
	enum ldms_value_type value_type; /* The primitive type for the metric */
	char *key_name;		/* The record key metric name */
	char *key_type;		/* The record key metric type */
	char *p_type;		/* The Prometheus type, i.e. counter, gauge, ... */
	int array_len;		/* If type_name is an array, the array length */
} *type_info_t;

enum info_id_e {
	LIST_NAME_INFO = 1,
	RECORD_NAME_INFO,
	KEY_NAME_INFO,
	KEY_TYPE_INFO,
	METRIC_TYPE_INFO,
	METRIC_UNIT_INFO
};

typedef struct mdef_s {
	char *name;		/* metric name */
	char *help;		/* HELP string */
	type_info_t info;	/* Metric type information */
	char *type;		/* Prometheus type, "gauge", "counter", ... */
	enum ldms_value_type vtype;
	char *tag;		/* TAG string, i.e. {...} */
	struct mdef_s *list_mdef;	/* list containing the record */
	rec_def_t rec_def;	/* recdef if part of a record */
	int list_id;		/* The list containing the record */
	int metric_id;		/* Metric id in the schema or record */
	struct rbn rbn;		/* (recdef|set)->mdef_tree entry */
} *mdef_t;

static int sample(ldmsd_plug_handle_t handle);

static int tree_cmp(void *tree_key, const void *key)
{
	return strcmp(tree_key, key);
}

static char *copy_next_token(char *s, char **endptr)
{
	size_t cnt = 0;
	char mname[LBUFSZ];
	while (isspace(*s))
		s++;
	while (!isspace(*s) && (*s != '\0' && *s != '\n' && *s != '{' && *s != ',')) {
		if (*s != '"')
			mname[cnt++] = *s++;
		else
			s++;
	}
	mname[cnt] = '\0';
	if (endptr)
		*endptr = s;
	return strdup(mname);
}

/*
 * parse:
 * # HELP ldmsd_metric_total_sets Returns for a ldmsd, count the number of samplers accross producers
 */
static int parse_help(dir_exp_t e, set_t set, char *lbuf, char **mname, char **help)
{
	char *s = lbuf;
	char *endptr;

	/* Skip whitespace */
	while (isspace(*s))
		s++;

	if (*s != '#') {
		ovis_log(e->log, OVIS_LERROR,
			 "%s[%d] : Expecting '#' but found '%c'.\n",
			 set->fname, e->line_no, *s);
		return EINVAL;
	}

	/* Skip '#' */
	s++;

	/* Skip interposing spaces */
	while (isspace(*s))
		s++;

	if (strncasecmp(s, "HELP", 4)) {
		ovis_log(e->log, OVIS_LERROR,
			 "%s[%d] : Expecting 'HELP', as first token on line.\n",
			 set->fname, e->line_no);
		return EINVAL;
	}

	if (!mname)
		return 0;

	/* Skip 'HELP' */
	s += 4;

	while (isspace(*s))
		s++;

	*mname = copy_next_token(s, &endptr);
	if (mname)
		s = endptr;

	if (!help)
		return 0;

	*help = copy_next_token(s, &endptr);
	return 0;
}

/*
 * parses:
 * list - The name of the metric list
 * record - The name of the record schema
 * type - The LDMS metric type
 * units - The LDMS metric unit string
 * key - The key to use to match metrics to record schema
 */
static int parse_type_info(dir_exp_t e, set_t set, char *lbuf, type_info_t *info_ret)
{
	type_info_t info = calloc(1, sizeof *info);
	enum ldms_value_type vtype;
	int i, rc = 0;

	struct token_s {
		char *name;
		enum info_id_e id;
	};
	static struct token_s tokens[] = {
		{ "list", LIST_NAME_INFO },
		{ "record", RECORD_NAME_INFO },
		{ "key_name", KEY_NAME_INFO },
		{ "key_type", KEY_TYPE_INFO },
		{ "value_type", METRIC_TYPE_INFO },
		{ "unit", METRIC_UNIT_INFO }
	};
	char *mname;
	char *s, *endptr;

	*info_ret = NULL;
	for (i = 0; i < sizeof(tokens) / sizeof(tokens[0]); i++) {
		s = strstr(lbuf, tokens[i].name);
		if (!s)
			continue;

		/* skip attribute name */
		s += strlen(tokens[i].name);
		if (*s != '=') {
			ovis_log(e->log, OVIS_LERROR,
				 "%s[%d] : Expecting '=' but found '%c'\n",
				 set->fname, e->line_no, *s);
			rc = EINVAL;
			goto err;
		}
		s += 1;		/* skip '=' */

		mname = copy_next_token(s, &endptr);

		switch (tokens[i].id) {
		case LIST_NAME_INFO:
			info->list_name = mname;
			break;
		case RECORD_NAME_INFO:
			info->record_name = mname;
			break;
		case KEY_NAME_INFO:
			info->key_name = mname;
			break;
		case KEY_TYPE_INFO:
			info->key_type = mname;
			break;
		case METRIC_TYPE_INFO:
			info->metric_type = mname;
			break;
		case METRIC_UNIT_INFO:
			info->unit_name = mname;
			break;
		}
	}
	/* Verify info */
	if (!info->list_name) {
		ovis_log(e->log, OVIS_LERROR,
			 "%s[%d] : The 'list' name is required in the the ldms= "
			 "attribute.\n",
			 set->fname, e->line_no);
		rc = EINVAL;
		goto err;
	}
	if (!info->list_name) {
		ovis_log(e->log, OVIS_LERROR,
			 "%s[%d] : The 'list' name is required in the ldms= "
			 "attribute.\n",
			 set->fname, e->line_no);
		rc = EINVAL;
		goto err;
	}
	if (!info->record_name) {
		ovis_log(e->log, OVIS_LERROR,
			 "%s[%d] : The 'record' name is required in the ldms= "
			 "attribute.\n",
			 set->fname, e->line_no);
		rc = EINVAL;
		goto err;
	}
	if (!info->key_name) {
		ovis_log(e->log, OVIS_LERROR,
			 "%s[%d] : The 'key_name' name is required in the ldms= "
			 "attribute.\n",
			 set->fname, e->line_no);
		rc = EINVAL;
		goto err;
	}
	if (!info->key_type) {
		ovis_log(e->log, OVIS_LERROR,
			 "%s[%d] : The 'key_type' name is required in the "
			 "ldms={} attribute.\n",
			 set->fname, e->line_no);
		rc = EINVAL;
		goto err;
	}
	vtype = ldms_metric_str_to_type(info->key_type);
	if (vtype == LDMS_V_NONE) {
		ovis_log(e->log, OVIS_LERROR,
			 "%s[%d] : The key_type specified, '%s' is not a vaid "
			 "type name.\n",
			 set->fname, e->line_no, info->key_type);
		rc = EINVAL;
		goto invalid_type_err;
	}
	if (!info->metric_type) {
		ovis_log(e->log, OVIS_LERROR,
			 "%s[%d] : The 'value_type' name is required in the "
			 "ldms={} attribute.\n",
			 set->fname, e->line_no);
		rc = EINVAL;
		goto err;
	}
	vtype = ldms_metric_str_to_type(info->metric_type);
	if (vtype == LDMS_V_NONE) {
		ovis_log(e->log, OVIS_LERROR,
			 "%s[%d] : The value_type specified, '%s' is not a vaid "
			 "type name.\n",
			 set->fname, e->line_no, info->metric_type);
		rc = EINVAL;
		goto invalid_type_err;
	}
	info->value_type = vtype;
	if (!info->unit_name) {
		info->unit_name = strdup("");
		if (!info->unit_name) {
			rc = errno;
			goto err;
		}
	}
	*info_ret = info;
	return 0;
 invalid_type_err:
	/* List valid type */
	ovis_log(e->log, OVIS_LERROR,
		 "Valid types include: char, char_array, d64, f32, s16, s32, "
		 "s64, timestamp, u16, u32, u64, and u8\n");
 err:
	free(info->list_name);
	free(info->record_name);
	free(info->key_name);
	free(info->key_type);
	free(info->metric_type);
	free(info->unit_name);
	free(info);
	return rc;
}

static int update_record(dir_exp_t e, set_t set, mdef_t mdef)
{
	assert(mdef->info);
	mdef_t list_mdef;
	rec_def_t recdef;
	struct rbn *rbn;
	size_t len;
	int rc = 0;

	/* Find the list and create if not present */
	rbn = rbt_find(&set->mdef_tree, mdef->info->list_name);
	if (!rbn) {
		/* The list mdef has not been created yet */
		list_mdef = calloc(1, sizeof *list_mdef);
		list_mdef->name = strdup(mdef->info->list_name);
		Snprintf(&list_mdef->help, &len, "List of '%s' records",
			 mdef->info->record_name);
		list_mdef->info = NULL;
		list_mdef->type = strdup("list");
		/* leave tag NULL */
		rbn_init(&list_mdef->rbn, list_mdef->name);
		rbt_ins(&set->mdef_tree, &list_mdef->rbn);

		/* Add the list to the schema */
		list_mdef->metric_id = ldms_schema_metric_list_add(
						set->base->schema,
						list_mdef->name,
						mdef->info->record_name,
						0);
	} else {
		list_mdef = container_of(rbn, struct mdef_s, rbn);
	}
	/* Find the record and create if not present */
	rbn = rbt_find(&set->recdef_tree, mdef->info->record_name);
	if (!rbn) {
		/* The list mdef has not been created yet */
		recdef = calloc(1, sizeof *recdef);
		if (!recdef) {
			rc = errno;
			goto err;
		}
		recdef->name = strdup(mdef->info->record_name);
		if (!recdef->name) {
			rc = errno;
			goto err;
		}
		recdef->key_name = strdup(mdef->info->key_name);
		if (!recdef->key_name) {
			rc = errno;
			goto err;
		}
		/* Create the record and add the key to it */
		recdef->ldms_rec = ldms_record_create(mdef->info->record_name);
		if (!recdef->ldms_rec) {
			rc = ENOMEM;
			goto err;
		}
		recdef->rec_id = ldms_schema_record_add(set->base->schema,
							recdef->ldms_rec);
		recdef->key_id = ldms_record_metric_add(
					recdef->ldms_rec,
					recdef->key_name,
					"Key",
					LDMS_V_CHAR_ARRAY,
					256);
		if (recdef->key_id < 0) {
			ovis_log(e->log, OVIS_LERROR,
				 "%s[%d] : The record metric, '%s', could not be "
				 "created.\n",
				 set->fname, e->line_no, recdef->key_name);
			rc = EINVAL;
			goto err;
		}

		/* Insert this record in the set tree for records */
		rbn_init(&recdef->rbn, recdef->name);
		rbt_ins(&set->recdef_tree, &recdef->rbn);
	} else {
		recdef = container_of(rbn, struct rec_def_s, rbn);
	}
	/* Add the metric in question to the record, and add the mdef
	 * to the set mdef_tree
	 */
	enum ldms_value_type v_type =
		ldms_metric_str_to_type(mdef->info->metric_type);
	if (LDMS_V_NONE == v_type) {
		ovis_log(e->log, OVIS_LERROR,
			 "%s[%d] : The string '%s' is not a valid LDMS value type.\n",
			 set->fname, e->line_no, mdef->info->metric_type);
		goto invalid_type_err;
		rc = EINVAL;
	}
	mdef->list_mdef = list_mdef;
	mdef->rec_def = recdef;
	mdef->metric_id = ldms_record_metric_add(
						 recdef->ldms_rec,
						 mdef->name,
						 mdef->info->unit_name,
						 v_type,
						 mdef->info->array_len);
	if (mdef->metric_id < 0) {
		ovis_log(e->log, OVIS_LERROR,
			 "%s[%d] : The  metric '%s' could not be created.\n",
			 set->fname, e->line_no, mdef->name);
		rc = EINVAL;
		goto err;
	}
	rbn_init(&mdef->rbn, mdef->name);
	rbt_ins(&set->mdef_tree, &mdef->rbn);

	return 0;
 invalid_type_err:
	/* List valid type */
	ovis_log(e->log, OVIS_LERROR,
		 "Valid types include: char, char_array, d64, f32, s16, s32, "
		 "s64, timestamp, u16, u32, u64, and u8\n");
 err:
	/* Cleanup ... */
	return rc;
}

/*
 * Parse:
 * # TYPE ldmsd_metric_total_sets gauge
 */
static int parse_type(dir_exp_t e, set_t set, char *lbuf, char **mname, char **type,
		      type_info_t *info)
{
	char mname_[LBUFSZ];
	char *s = lbuf;
	char *endptr;

	while (isspace(*s))
		s++;

	if (*s != '#') {
		ovis_log(e->log, OVIS_LERROR,
			 "%s[%d] : Expecting '#' but found '%c'.\n",
			 set->fname, e->line_no, *s);
		return EINVAL;
	}

	/* Skip '#' */
	s++;

	/* Skip interposing spaces */
	while (isspace(*s))
		s++;

	if (strncasecmp(s, "TYPE", 4)) {
		ovis_log(e->log, OVIS_LERROR,
			 "%s[%d] : Execting 'TYPE', as first token.\n",
			 set->fname, e->line_no);
		return EINVAL;
	}

	if (!mname)
		return 0;

	/* Skip 'TYPE' */
	s += 4;

	*mname = copy_next_token(s, &endptr);
	if (mname)
		s = endptr;

	if (!type)
		return 0;

	/* This next token is the type string. We will simply store
	 * the value in the mdef, but it is otherwise not used.
	 */
	while (isspace(*s))
		s++;
	if (*s == '\0') {
		/* Type is missing default to "gauge" */
		*type = strdup("gauge");
		*info = NULL;
		return 0;
	}

	*type = copy_next_token(s, &endptr);
	if (type)
		s = endptr;

	if (!info)
		return 0;

	/* Parse the 'ldms' annotation if present:
	 * ldms={ ... }
	 */
	*info = NULL;
	char *t = strstr(s, "ldms");
	if (!t)
		/* The ldms annotation is not present */
		return 0;
	s = t + 4;
	if (*s != '=') {
		ovis_log(e->log, OVIS_LINFO,
			 "%s[%d] : Expecting '=', but got '%c', ignoring annotation.\n",
			 set->fname, e->line_no, *s);
		return EINVAL;
	}
	s++;			/* skip '=' */
	size_t cnt = 0;
	if (*s != '{') {
		ovis_log(e->log, OVIS_LERROR,
			 "%s[%d] : Expecting '{', but got '%c'.\n",
			 set->fname, e->line_no, *s);
		return EINVAL;
	}
	s++;			/* skip '{' */

	/* Consume up to closing '}' */
	cnt = 0;
	while (*s != '}' && *s != '\0' && *s != '\n')
		mname_[cnt++] = *s++;
	mname_[cnt] = '\0';
	if (*s != '}') {
		ovis_log(e->log, OVIS_LERROR,
			 "%s[%d] : Syntax Error, the type attribute '%s' is missing the closing '}'\n",
			 set->fname, e->line_no, t);
		return EINVAL;
	}

	return parse_type_info(e, set, mname_, info);
}

/*
 * Parse:
 * ldmsd_metric_total_sets{ldms_group="mana",ldms_name="agg-mana-0",ldms_type="agg"} 61.0
 */
static int parse_value(dir_exp_t e, char *lbuf, char **mname, char **tag, char **value)
{
	char mname_[LBUFSZ];
	char *s = lbuf;
	char *endptr;

	/* Skip interposing spaces */
	while (isspace(*s))
		s++;

	/*
	 * The metric name
	 */
	*mname = copy_next_token(s, &endptr);
	if (mname)
		s = endptr;

	/* Skip '{' */
	s++;

	/*
	 * The tag string (Attribute)
	 */
	size_t cnt = 0;
	while (*s != '}')
		mname_[cnt++] = *s++;
	mname_[cnt] = '\0';
	*tag = strdup(mname_);

	/* Skip '}' */
	s++;

	*value = copy_next_token(s, &endptr);

	return 0;
}

static void destroy_set(dir_exp_t e, set_t set)
{
	mdef_t mdef;
	rec_def_t recdef;

	rbt_del(&e->set_tree, &set->rbn);

	free(set->fname);
	base_del(set->base);
	if (set->set)
		ldms_set_delete(set->set);

	/* Free all the rec_def entries in each set */
	while (!rbt_empty(&set->recdef_tree)) {
		struct rbn *rec_rbn = rbt_min(&set->recdef_tree);
		rbt_del(&set->recdef_tree, rec_rbn);
		recdef = container_of(rec_rbn, struct rec_def_s, rbn);
		free(recdef->name);
		free(recdef->key_name);
		ldms_record_delete(recdef->ldms_rec);
		free(recdef);
	}

	/* Free all the mdef entries in each set */
	while (!rbt_empty(&set->mdef_tree)) {
		struct rbn *mdef_rbn = rbt_min(&set->mdef_tree);
		rbt_del(&set->mdef_tree, mdef_rbn);
		mdef = container_of(mdef_rbn, struct mdef_s, rbn);
		free(mdef->name);
		free(mdef->help);
		free(mdef->type);
		free(mdef->tag);
		if (mdef->info) {
			free(mdef->info->list_name);
			free(mdef->info->record_name);
			free(mdef->info->unit_name);
			free(mdef->info->metric_type);
			free(mdef->info->key_name);
			free(mdef->info->key_type);
			free(mdef->info->p_type);
			free(mdef->info);
		}
		free(mdef);
	}
	fclose(set->fp);
	free(set);
}

/*
 * The nftw() function does not accept a context pointer. This means
 * that any asset parameter list has to be set as a global variable
 * and then manipulated within the callback. The issue with this is
 * that if one were to configure this sampler with multiple
 * configurations, one configuration could be running on one thread,
 * and the other on another thread. If we are setting a global
 * variable (dir_exp in this case), the two would stomp each other. To
 * resolve this issue, we declare this variable to be thread_local
 * storage which means there will be a different instance of this
 * variable for each thread.
 *
 * The file format is as follows:
 *
 * # HELP <metric_name> <An unquoted help string>
 * # TYPE <metric_name> <counter|gauge|histogram|summary> \
 * #        ldms=\{list=<name>,record=<name>,key=<name> \
 *                 type=<ldms-type>,units=<unit-str>}
 *
 * If list is present, record must be present. Absent these
 * attributes, the metric is a primitive metric type. If type missing
 * altogether, the ldms-type is set to LDMS_V_DOUBLE.
 */
static __thread dir_exp_t dir_exp;

static int parse_file(dir_exp_t e, set_t set, FILE *fp)
{
	int rc = 0;
	char mname[LBUFSZ];
	char lbuf[LBUFSZ];
	char *s;

	/* Add all of the metric definitions for this set and schema */
	s = fgets(lbuf, sizeof(lbuf), fp);
	e->line_no = 1;
	if (!s) {
		ovis_log(e->log, OVIS_LINFO,
			 "%s[%d] : Empty files are ignored.\n",
			 set->fname, e->line_no);
		rc = ENOENT;
		goto err;
	}
	do {
		mdef_t mdef = calloc(1, sizeof *mdef);
		/* Process the "# HELP ..." line */
		rc = parse_help(e, set, lbuf, &mdef->name, &mdef->help);
		if (rc) {
			rc = EINVAL;
			goto err;
		}

		/* Read the "# TYPE ..." line */
		s = fgets(lbuf, sizeof(lbuf), fp);
		e->line_no++;
		if (!s) {
			ovis_log(e->log, OVIS_LERROR,
				 "%s[%d] : The '# TYPE' line is missing.\n",
				 set->fname, e->line_no);
			rc = EINVAL;
			goto err;
		}
		char *ignore;
		rc = parse_type(e, set, lbuf, &ignore, &mdef->type, &mdef->info);
		if (rc)
			goto err;
		free(ignore);

		/* If this is a record entry (info != NULL), create or
		 * update the associated record */
		if (mdef->info) {
			rc = update_record(e, set, mdef);
			if (rc)
				goto err;
		}

		/* Read the 1st VALUE line */
		s = fgets(lbuf, sizeof(lbuf), fp);
		e->line_no ++;
		if (!s) {
			ovis_log(e->log, OVIS_LERROR,
				 "%s[%d] : Unexpected EOF looking for metric value. "
				 "Ignoring file.\n",
				 set->fname, e->line_no);
			rc = EINVAL;
			goto err;
		}

		char *value;
		rc = parse_value(e, lbuf, &ignore, &mdef->tag, &value);
		if (rc) {
			ovis_log(e->log, OVIS_LERROR,
				 "%s[%d] : Error %d parsing VALUE line. Ignoring file.\n",
				 set->fname, e->line_no, rc);
			goto err;
		}

		/* Read all values until the next help line */
		while (NULL != (s = fgets(lbuf, sizeof(lbuf), fp))) {
			e->line_no ++;
			if (*s == '#')
				break;
		}

		if (!mdef->info) {
			rbn_init(&mdef->rbn, mdef->name);
			rbt_ins(&set->mdef_tree, &mdef->rbn);

			/* This adds meta-metrics for help, type, and tag */
			sprintf(mname, "%s_help", mdef->name);
			mdef->metric_id =
				ldms_schema_meta_array_add(set->base->schema,
							   mname,
							   LDMS_V_CHAR_ARRAY,
							   strlen(mdef->help) + 1);
			sprintf(mname, "%s_type", mdef->name);
			mdef->metric_id =
				ldms_schema_meta_array_add(set->base->schema,
							   mname,
							   LDMS_V_CHAR_ARRAY,
							   strlen(mdef->type) + 1);

			sprintf(mname, "%s_tag", mdef->name);
			mdef->metric_id =
				ldms_schema_meta_array_add(set->base->schema,
							   mname,
							   LDMS_V_CHAR_ARRAY,
							   strlen(mdef->tag) + 1);

			/* This adds a metric for the value */
			mdef->metric_id =
				ldms_schema_metric_add(set->base->schema,
						       mdef->name,
						       LDMS_V_D64);
		}
	} while (s);
	return 0;
 err:
	return rc;
}

/*
 * Messages on this channel trigger sample() events
 */
static int msg_cb_fn(ldms_msg_event_t ev, void *cb_arg)
{
	dir_exp_t e = cb_arg;

	switch (ev->type) {
	case LDMS_MSG_EVENT_RECV:
		if (0 != strncmp(ev->recv.data, "import", 6)) {
			ovis_log(e->log, OVIS_LINFO,
				 "Unrecognized message '%s' received "
				 "on message channel '%s'\n",
				 ev->recv.data, e->channel);
		}
		sample(e->handle);
		break;
	case LDMS_MSG_EVENT_CLIENT_CLOSE:
		/* This is the last event guaranteed to delivered to this client. The
		 * resources application associate to this client (e.g. cb_arg) could be
		 * safely freed at this point. */
		break;
	default:
		/* ignore other events */;
	}
	return 0;
}

/*
 * Each file is its own set/schema
 */
static int create_set_cb(const char *fpath, const struct stat *sb, int typeflag,
			 struct FTW *ftwbuf)
{
	dir_exp_t e = dir_exp;
	int rc, id;
	set_t set;
	char mname[PATH_MAX];
	struct rbn *rbn;

	if (typeflag != FTW_F)
		return 0;

	/* See if we already have this set (fpath) */
	rbn = rbt_find(&e->set_tree, fpath);
	if (rbn) {
		ovis_log(e->log, OVIS_LERROR,
			 "%s[%d] : A set and schema are already present.\n",
			 fpath, e->line_no);
		assert(NULL == "The same fpath appears twice in the directory?\n");
		return EEXIST;
	}

	/* Make certain we can open the file */
	FILE *fp = fopen(fpath, "r");
	if (!fp) {
		/* The only reasonable error here would be permission */
		if (errno == EPERM || errno == EACCES) {
			ovis_log(e->log, OVIS_LWARN,
				 "%s[%d] : The daemon lacks permission to open the file. "
				 "Ignoring file.\n",
				 fpath, e->line_no);
			return 0;
		}
		return errno;
	}
	set = calloc(1, sizeof *set);
	strcpy(mname, fpath);
	set->fname = strdup(basename(mname));
	set->fp = fp;

	rbn_init(&set->rbn, set->fname);
	rbt_ins(&e->set_tree, &set->rbn);
	rbt_init(&set->mdef_tree, tree_cmp);
	rbt_init(&set->recdef_tree, tree_cmp);

	/* The schema name is <plugin-name>'.'<config-name>'.'<basename(fpath)>*/
	sprintf(mname, "%s.%s.%s", e->plug_name, e->cfg_name, set->fname);
	set->base = base_config(e->avl, e->cfg_name, mname, e->log);
	if (!set->base) {
		rc = errno;
		goto err;
	}

	if (!base_schema_new(set->base)) {
		ovis_log(e->log, OVIS_LERROR,
		       "%s:%d The schema '%s' could not be created, errno=%d.\n",
			 __func__, __LINE__, set->base->schema_name, errno);
		rc = errno;
		goto err;
	}

	/* Location of first metric after the base metrics */
	set->metric_offset = ldms_schema_metric_count_get(set->base->schema);

	rc = parse_file(e, set, fp);
	if (rc)
		goto err;

	/* Replace the instance name in base, or we will get a collision */
	size_t len;
	char *instance_name = NULL;
	len = Snprintf(&instance_name, &len, "%s/%s",
		       set->base->instance_name, set->base->schema_name);
	free(set->base->instance_name);
	set->base->instance_name = strdup(instance_name);

	/* Create the metric set from the schema */
	size_t heap_sz = 0;
	RBT_FOREACH(rbn, &set->mdef_tree) {
		mdef_t mdef = container_of(rbn, struct mdef_s, rbn);
		if (mdef->rec_def)
			heap_sz += ldms_record_heap_size_get(mdef->rec_def->ldms_rec);
	}
	set->set = base_set_new_heap(set->base, heap_sz);
	if (!set->set) {
		ovis_log(e->log, OVIS_LERROR,
			 "%s[%d] : Error %d creating the set '%s'.\n",
			 set->fname, e->line_no, errno, set->base->instance_name);
		goto err;
	}

	/*
	 * Now loop through all the metrics and set the values for the
	 * meta metrics
	 */
	for (id = set->metric_offset;
	     id < ldms_schema_metric_count_get(set->base->schema);
	     id++) {
		const char *mname = ldms_metric_name_get(set->set, id);
		char nname[LBUFSZ];
		mdef_t mdef;
		struct rbn *rbn;
		size_t len = strlen(mname);
		if (0 == strncasecmp(&mname[len-4], "help", 4)) {
			strcpy(nname, mname);
			nname[len-5] = '\0';
			rbn = rbt_find(&set->mdef_tree, nname);
			mdef = container_of(rbn, struct mdef_s, rbn);
			ldms_metric_array_set_str(set->set, id, mdef->help);
		} else if (0 == strncasecmp(&mname[len-4], "type", 4)) {
			strcpy(nname, mname);
			nname[len-5] = '\0';
			rbn = rbt_find(&set->mdef_tree, nname);
			mdef = container_of(rbn, struct mdef_s, rbn);
			ldms_metric_array_set_str(set->set, id, mdef->type);
		} else if (0 == strncasecmp(&mname[len-3], "tag", 3)) {
			strcpy(nname, mname);
			nname[len-4] = '\0';
			rbn = rbt_find(&set->mdef_tree, nname);
			mdef = container_of(rbn, struct mdef_s, rbn);
			ldms_metric_array_set_str(set->set, id, mdef->tag);
		}
	}
	return 0;
 err:
	destroy_set(e, set);
	/* Ignore the file */
	return 0;
}

static int create_sets(dir_exp_t e)
{
	int rc;

	dir_exp = e;
	rc = nftw(e->path, create_set_cb, 16, FTW_PHYS);
	return rc;
}

#define SAMP "file_exporter"
static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "config name=" SAMP " " BASE_CONFIG_USAGE;
}

/*
 * load name=scraper1 plugin=/dev/shm/dir1
 * config name=scraper1 directory=/dev/shm/dir1 output=metric_set instance=${HOSTNAME}/dir1 producer=${HOSTNAME}
 */
static int config(ldmsd_plug_handle_t handle,
		  struct attr_value_list *kwl, struct attr_value_list *avl)
{
	dir_exp_t e = ldmsd_plug_ctxt_get(handle);
	int rc;

	e->log = ldmsd_plug_log_get(handle);

	if (!rbt_empty(&e->set_tree)) {
		ovis_log(e->log, OVIS_LERROR,
			 "The directory '%s' is already configured\n",
			 e->path);
		return EBUSY;
	}

	e->cfg_name = strdup(ldmsd_plug_cfg_name_get(handle));
	e->plug_name = strdup(ldmsd_plug_name_get(handle));
	e->avl = avl;

	char *dir = av_value(avl, "dir_path");
	if (!dir) {
		ovis_log(e->log, OVIS_LERROR,
			 "The sampler requires the 'path' configuration parameter.\n");
		rc = EINVAL;
		goto err;
	}
	e->path = strdup(dir);

	char *channel = av_value(avl, "channel");
	if (!channel)
		channel = "file_importer";
	e->channel = strdup(channel);
	e->msg_client = ldms_msg_subscribe(e->channel, 1,
					   msg_cb_fn, e,
					   "file_importer message channel");
	if (!e->msg_client) {
		ovis_log(e->log, OVIS_LERROR,
			 "Error %d subscribe to message channel '%s'\n",
			 errno, e->channel);
		return 1;
	}

	/* Create a set/schema for file in the directory */
	rc = create_sets(e);
	if (rc) {
		ovis_log(e->log, OVIS_LERROR,
			 "Error %d creating the sets/schemas in '%s'\n",
			 rc, e->path);
		goto err;
	}

	return 0;
 err:
	return rc;
}

ldms_mval_t parse_mval(char *value, enum ldms_value_type type, size_t count,
		       char *vbuf, size_t vbuf_len)
{
	ldms_mval_t v = (ldms_mval_t)vbuf;
	switch (type) {
	case LDMS_V_CHAR:
		v->v_char = *value;
		break;
	case LDMS_V_U8:
		v->v_u8 = (unsigned char)*value;
		break;
	case LDMS_V_S8:
		v->v_s8 = *value;
		break;
	case LDMS_V_U16:
		v->v_u16 = (uint16_t)strtol(value, NULL, 0);
		break;
	case LDMS_V_S16:
		v->v_s16 = (int16_t)strtol(value, NULL, 0);
		break;
	case LDMS_V_U32:
		v->v_u32 = (uint32_t)strtoul(value, NULL, 0);
		break;
	case LDMS_V_S32:
		v->v_s32 = (int32_t)strtol(value, NULL, 0);
		break;
	case LDMS_V_U64:
		v->v_u64 = strtoul(value, NULL, 0);
		break;
	case LDMS_V_S64:
		v->v_s64 = strtol(value, NULL, 0);
		break;
	case LDMS_V_F32:
		v->v_f = strtof(value, NULL);
		break;
	case LDMS_V_D64:
		v->v_d = strtod(value, NULL);
		break;
	case LDMS_V_CHAR_ARRAY:
		strncpy(v->a_char, value, count);
		break;
	default:
		errno = EINVAL;
		return NULL;
	}
	return v;
}

static int sample_file(dir_exp_t e, set_t set)
{
	int rc = 0;
	char lbuf[LBUFSZ];
	char *mname = NULL;
	char *value = NULL;
	char *tag = NULL;
	char *key_str = NULL;
	char *s;
	FILE *fp = set->fp;

	/* Rewind fp */
	rc = fseek(fp, 0, SEEK_SET);
	if (rc) {
		ovis_log(e->log, OVIS_LERROR,
			 "%s[%d] : Error %d rewinding file. No sample taken.\n",
			 set->fname, e->line_no, rc);
		return 0;
	}

	/* Read the HELP line */
	s = fgets(lbuf, sizeof(lbuf), fp);
	e->line_no ++;
	if (!s) {
		/* end of file */
		ovis_log(e->log, OVIS_LERROR,
			 "%s[%d] : The file is empty. No sample taken.\n",
			 set->fname, e->line_no);
		return 0;
	}
	do {
		rc = 0;

		/* Get the metric name from the HELP */
		rc = parse_help(e, set, lbuf, &mname, NULL);
		if (rc) {
			ovis_log(e->log, OVIS_LERROR,
				 "%s[%d] : Error %d parsing HELP line.\n",
				 set->fname, e->line_no, rc);
			rc = EINVAL;
			goto err;
		}

		struct rbn *rbn = rbt_find(&set->mdef_tree, mname);
		if (!rbn)  {
			ovis_log(e->log, OVIS_LERROR,
				 "%s[%d] : Ignoring metric value ('%s') with no type information.\n",
				 set->fname, e->line_no, mname);
			rc = ENOENT;
			goto err;
		}

		mdef_t mdef = container_of(rbn, struct mdef_s, rbn);

		/* Read the TYPE line */
		s = fgets(lbuf, sizeof(lbuf), fp);
		e->line_no ++;
		if (!s) {
			rc = errno;
			if (!rc)
				break;
			goto err;
		}
		/* Skip the type line */
		rc = parse_type(e, set, lbuf, NULL, NULL, NULL);
		if (rc) {
			rc = EINVAL;
			goto err;
		}

		/* Read the VALUE line */
		s = fgets(lbuf, sizeof(lbuf), fp);
		e->line_no ++;
		if (!s) {
			rc = errno;
			if (rc)
				goto err;
			break;
		}

		free(mname);
		mname = NULL;

		do {
			rc = parse_value(e, lbuf, &mname, &tag, &value);
			if (rc)
				goto err;
			if (!mdef->rec_def) {
				ldms_metric_set_double(set->set, mdef->metric_id,
						       strtod(value, NULL));
				rc = 0;
				goto next;
			}

			/* Pull the key value from the tag string */
			char *s = strstr(tag, mdef->info->key_name);
			if (!s) {
				ovis_log(e->log, OVIS_LERROR,
					 "%s[%d] :The 'key_name' is missing from the value "
					 "attributes.\n", set->fname, e->line_no);
				goto err;
			}
			while (*s != '=' && *s != '\0' && *s != '\n')
				s++;
			if (*s == '=') {
				s++;
				key_str = copy_next_token(s, NULL);
			} else {
				ovis_log(e->log, OVIS_LERROR,
					 "%s[%d] : Expecting '%s'=string in value line.\n",
					 set->fname, e->line_no, mdef->info->key_name);
				rc = EINVAL;
				goto err;
			}

			/* Search list for a record with the same key */
			ldms_mval_t list_mval = ldms_metric_get(set->set,
								mdef->list_mdef->metric_id);
			enum ldms_value_type mtype;
			size_t count;
			ldms_mval_t rec_mval;
			ldms_mval_t mval;
			ldms_mval_t key_mval;

			for (rec_mval = ldms_list_first(set->set, list_mval, &mtype, &count);
			     rec_mval;
			     rec_mval = ldms_list_next(set->set, rec_mval, &mtype, &count)) {

				key_mval = ldms_record_metric_get(
							rec_mval,
							mdef->rec_def->key_id);
				if (0 == strcmp(key_mval->a_char, key_str))
					break;
			}
			if (!rec_mval) {
				/* Record instance with key not found, create one */
				rec_mval = ldms_record_alloc(set->set,
							     mdef->rec_def->rec_id);
				if (!rec_mval) {
					rc = errno;
					ovis_log(e->log, OVIS_LERROR,
						 "%s[%d] : Error %d creating new record "
						 "instance '%s'.\n",
						 set->fname, e->line_no, rc, mdef->rec_def->name);
					goto err;
				}
				/* Set the key in the record */
				ldms_record_array_set_str(rec_mval, mdef->rec_def->key_id, key_str);

				/* Add it to the list */
				rc = ldms_list_append_record(set->set, list_mval, rec_mval);
				if (rc) {
					ovis_log(e->log, OVIS_LERROR,
						 "%s[%d] : Error %d appending "
						 "new record to list.\n",
						 set->fname, e->line_no, rc);
					goto err;
				}
			}
			if (rec_mval) {
				char vbuf[LBUFSZ];
				mval = parse_mval(value, mdef->info->value_type,
						  strlen(value)+1,
						  vbuf, sizeof(vbuf));
				ldms_record_metric_set(rec_mval,
						       mdef->metric_id, mval);
			}
		next:
			free(key_str);
			free(mname);
			free(value);
			free(tag);
			key_str = mname = value = tag = NULL;

			s = fgets(lbuf, sizeof(lbuf), fp);
			e->line_no ++;
			if (!s) {
				/* End of file */
				goto out;
			}
			if (*s == '#')
				/* Next set of values */
				break;
		} while (s);
	} while (1);
 out:
	return 0;
 err:
	free(key_str);
	free(mname);
	free(tag);
	free(value);
	return rc;
}

static int sample(ldmsd_plug_handle_t handle)
{
	int rc;
	struct rbn *rbn;
	set_t set;
	dir_exp_t e = ldmsd_plug_ctxt_get(handle);

	RBT_FOREACH(rbn, &e->set_tree) {
		set = container_of(rbn, struct set_s, rbn);
		assert(set->set);

		base_sample_begin(set->base);
		rc = sample_file(e, set);
		base_sample_end(set->base);
	}
	return rc;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	dir_exp_t e = calloc(1, sizeof(*e));
	if (e) {
		rbt_init(&e->set_tree, tree_cmp);
		ldmsd_plug_ctxt_set(handle, e);
		e->handle = handle;
		return 0;
	}
	return ENOMEM;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	struct rbn *rbn;
	dir_exp_t e = ldmsd_plug_ctxt_get(handle);
	set_t set;
	while (!rbt_empty(&e->set_tree)) {
		rbn = rbt_min(&e->set_tree);
		rbt_del(&e->set_tree, rbn);
		set = container_of(rbn, struct set_s, rbn);

		destroy_set(e, set);
	}
};

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base.type = LDMSD_PLUGIN_SAMPLER,
	.base.flags = LDMSD_PLUGIN_MULTI_INSTANCE,
	.base.config = config,
	.base.usage = usage,
	.base.constructor = constructor,
	.base.destructor = destructor,

	.sample = sample,
};
