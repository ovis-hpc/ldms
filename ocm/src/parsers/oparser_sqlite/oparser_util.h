/*
 * util.h
 *
 *  Created on: Oct 16, 2013
 *      Author: nichamon
 */

#ifndef UTIL_H_
#define UTIL_H_

#include <sys/queue.h>
#include <string.h>
#include <inttypes.h>
#include <sqlite3.h>

#define ARRAY_SIZE(a)  (sizeof(a) / sizeof(a[0]))

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

LIST_HEAD(oparser_component_list, oparser_component);
TAILQ_HEAD(oparser_component_type_list, oparser_component_type);
struct oparser_scaffold {
	struct oparser_component_list *children;
	int num_child_types;
	int height;
	struct oparser_component_type_list *all_type_list;
};

LIST_HEAD(metric_list, oparser_metric);
struct oparser_component_type {
	char *type;
	char *parent_type;
	int count;
	struct oparser_component_type **elements;
	int num_element_types;
	struct oparser_component_list list;
	struct metric_list mlist;
	int num_comp;
	TAILQ_ENTRY(oparser_component_type) entry;
};

struct oparser_metric {
	char *name;
	uint32_t mtype_id;
	uint64_t metric_id;
	struct oparser_component *comp;
	LIST_ENTRY(oparser_metric) entry;
	LIST_ENTRY(oparser_metric) type_entry;
	LIST_ENTRY(oparser_metric) set_entry;
};

struct mae_metric {
	uint64_t metric_id;
	uint32_t prod_comp_id;
	uint32_t coll_comp_id;
	LIST_ENTRY(mae_metric) entry;
};
LIST_HEAD(mae_metric_list, mae_metric);

struct oparser_component {
	struct oparser_component *parent;
	struct oparser_component_list *children;
	int num_child_types;

	struct oparser_component_type *comp_type;
	char *name;
	uint32_t comp_id;

	struct metric_list mlist;
	LIST_ENTRY(oparser_component) entry;
	LIST_ENTRY(oparser_component) type_entry;
};

struct oparser_cmd {
	char verb[64];
	char attrs_values[2048];
	TAILQ_ENTRY(oparser_cmd) entry;
};
TAILQ_HEAD(oparser_cmd_queue, oparser_cmd);

void destroy_oparser_cmd_queue(struct oparser_cmd_queue *cmd_queue);

int process_string_name(char *s, struct oparser_name_queue *nlist,
					char *sep_start, char *sep_end);

void destroy_name_list(struct oparser_name_queue *nlist);

int count_leading_tabs(char *buf);

void oom_report(const char *fn_name);

uint64_t gen_metric_id(uint32_t comp_id, uint32_t metric_type_id);

/**
 * \brief Trim the trailing space
 *
 * The string \c s is modified to get rid of all trailing spaceses.
 */
void trim_trailing_space(char *s);

void create_table(char *create_stmt, char *index_stmt, sqlite3 *db);

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
