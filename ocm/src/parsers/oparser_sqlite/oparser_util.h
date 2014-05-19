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
/*
 * util.h
 *
 *  Created on: Oct 16, 2013
 *      Author: nichamon
 */

#ifndef UTIL_H_
#define UTIL_H_

#include <sys/queue.h>
#include <linux/limits.h>
#include <string.h>
#include <inttypes.h>
#include <sqlite3.h>

#define MAIN_BUF_SIZE (1024 * 1024)

#define ARRAY_SIZE(a)  (sizeof(a) / sizeof(a[0]))

#define TRUE 1
#define FALSE 0

struct oparser_name {
	char *name;
	TAILQ_ENTRY(oparser_name) entry;
};
TAILQ_HEAD(oparser_name_queue, oparser_name);

struct kw {
	char *token;
	void (*action)(char *value);
};

int kw_comparator(const void *a, const void *b);

LIST_HEAD(oparser_comp_list, oparser_comp);
TAILQ_HEAD(oparser_comp_type_list, oparser_comp_type);
struct oparser_scaffold {
	struct oparser_comp_list *children;
	int num_children;
	int num_nodes;
	int height;
	struct oparser_comp_type_list *all_type_list;
};

LIST_HEAD(metric_list, oparser_metric);
LIST_HEAD(mtype_list, metric_type);
struct oparser_comp_type {
	char *type; /* type */
	char gif_path[PATH_MAX]; /* path to the image */
	int visible; /* visibility on Web GUI. The value is either TRUE or FALSE */
	struct oparser_comp_list clist; /* list of all components of the type */
	int num_comp; /* Number of components of the type */
	struct mtype_list mtype_list; /* List of metric types that measure the comp type performace */
	int num_mtypes; /* Number of metrics that measure the performance */
	TAILQ_ENTRY(oparser_comp_type) entry; /* entry in the list of all component types */
};

struct metric_type {
	char *ldms_sampler;
	char *name;
	uint32_t mtype_id;
	LIST_ENTRY(metric_type) entry; /* entry of the list contained in a comp type */
};

struct oparser_metric {
	char *ldms_sampler;
	char *name;
	uint32_t mtype_id;
	uint64_t metric_id;
	struct oparser_comp *comp;
	LIST_ENTRY(oparser_metric) comp_metric_entry; /* entry of the list contained by a comp metirc */
	LIST_ENTRY(oparser_metric) entry; /* entry of the all metric list */
	LIST_ENTRY(oparser_metric) type_entry;
	LIST_ENTRY(oparser_metric) set_entry;
	LIST_ENTRY(oparser_metric) comp_entry; /* entry of the list contained by the component */
};

struct mae_metric {
	uint64_t metric_id;
	uint32_t prod_comp_id;
	uint32_t coll_comp_id;
	LIST_ENTRY(mae_metric) entry;
};
LIST_HEAD(mae_metric_list, mae_metric);

struct comp_array {
	int num_alloc;
	int num_comps;
	struct oparser_comp **comps;
	LIST_ENTRY(comp_array) entry;
};

LIST_HEAD(comp_array_list, comp_array);
struct oparser_comp {
	char *name; /* component name */
	char *uid; /* user identifier */
	uint32_t comp_id; /* component ID */
	struct oparser_comp_type *comp_type; /* component type */
	struct comp_array_list parents; /* list of parent array: each array of a type */
	int num_ptypes; /* Number of parent types */
	struct comp_array_list children; /* list of children array: each array of a type */
	int num_chtype; /* Number of children types */
	int is_stored;
	struct metric_list mlist; /* list of metrics */
	LIST_ENTRY(oparser_comp) type_entry; /* entry in the component type structure */
	LIST_ENTRY(oparser_comp) root_entry; /* entry in the all root list */
};

struct oparser_cmd {
	char verb[64];
	char attrs_values[MAIN_BUF_SIZE];
	TAILQ_ENTRY(oparser_cmd) entry;
};
TAILQ_HEAD(oparser_cmd_queue, oparser_cmd);

void destroy_oparser_cmd_queue(struct oparser_cmd_queue *cmd_queue);

int process_string_name(char *s, struct oparser_name_queue *nlist,
					char *sep_start, char *sep_end);

void empty_name_list(struct oparser_name_queue *nlist);

int count_leading_tabs(char *buf);

void oom_report(const char *fn_name);

uint64_t gen_metric_id(uint32_t comp_id, uint32_t metric_type_id);

/**
 * \brief Trim the trailing space
 *
 * The string \c s is modified to get rid of all trailing spaces.
 */
void trim_trailing_space(char *s);

/**
 * \brief Create a table in the database \c db
 */
void create_table(char *create_stmt, sqlite3 *db);

/**
 * \brief Create an index of an existing table in the database \c db
 */
void create_index(char *index_stmt, sqlite3 *db);

struct building_sqlite_table;
typedef void (*build_sqlite_table_fn)(struct building_sqlite_table *obj);
struct building_sqlite_table {
	char *table_name;
	void *obj;
	sqlite3 *db;
	build_sqlite_table_fn fn;
};

void oparser_bind_text(sqlite3 *db, sqlite3_stmt *stmt, int idx,
					char *value, const char *fn_name);

void oparser_finish_insert(sqlite3 *db, sqlite3_stmt *stmt,
						const char *fn_name);

#endif /* UTIL_H_ */
