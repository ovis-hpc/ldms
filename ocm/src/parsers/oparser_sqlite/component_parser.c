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
 * component_parser.c

 *
 *  Created on: Oct 14, 2013
 *      Author: nichamon
 */
#include <sys/queue.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <time.h>
#include <stdarg.h>

#include "component_parser.h"

#define MAX(X,Y) ((X) < (Y) ? (Y) : (X))

uint32_t num_components;
struct oparser_comp_type_list *all_type_list;
struct oparser_comp_list *all_root_list;
FILE *log_fp;

struct oparser_comp_type *curr_ctype;
struct oparser_name_queue curr_uids;
int curr_nids;
struct oparser_name_queue curr_names;
int curr_nnames;

static char *main_buf;
static char *main_value;

void comp_parser_log(const char *fmt, ...)
{
	va_list ap;
	time_t t;
	struct tm *tm;
	char dtsz[200];

	t = time(NULL);
	tm = localtime(&t);
	if (strftime(dtsz, sizeof(dtsz), "%a %b %d %H:%M:%S %Y", tm))
		fprintf(log_fp, "%s: ", dtsz);
	va_start(ap, fmt);
	vfprintf(log_fp, fmt, ap);
	fflush(log_fp);
}

void oparser_component_parser_init(FILE *log_file, char *read_buf, char *value_buf)
{
	num_components = 0;
	if (log_file)
		log_fp = log_file;
	else
		log_fp = stderr;

	main_buf = read_buf;
	main_value = value_buf;
}

struct oparser_comp *
new_comp(struct oparser_comp_type *comp_type, char *uid, char *name)
{
	struct oparser_comp *comp = calloc(1, sizeof(*comp));
	LIST_INSERT_HEAD(&comp_type->clist, comp, type_entry);
	comp_type->num_comp++;
	comp->comp_type = comp_type;
	num_components++;
	comp->comp_id = num_components;
	LIST_INIT(&comp->mlist);
	comp->uid = strdup(uid);
	comp->name = strdup(name);
	LIST_INSERT_HEAD(all_root_list, comp, root_entry);
	LIST_INIT(&comp->children);
	LIST_INIT(&comp->parents);

	return comp;
}

struct oparser_comp *find_comp(struct oparser_comp_type *ctype, char *uid)
{
	struct oparser_comp *comp;
	LIST_FOREACH(comp, &ctype->clist , type_entry) {
		if (strcmp(comp->uid, uid) == 0)
			return comp;
	}
	return NULL;
}

struct oparser_comp_type *new_comp_type(char *type)
{
	struct oparser_comp_type *comp_type = calloc(1, sizeof(*comp_type));
	if (!comp_type) {
		comp_parser_log("%s: out of memory.\n", __FUNCTION__);
		exit(ENOMEM);
	}
	comp_type->type = strdup(type);
	comp_type->visible = TRUE;
	LIST_INIT(&comp_type->clist);
	LIST_INIT(&comp_type->mtype_list);
	return comp_type;
}

struct oparser_comp_type *find_comp_type(char *type)
{
	struct oparser_comp_type *comp_type;
	TAILQ_FOREACH(comp_type, all_type_list, entry) {
		if (strcmp(comp_type->type, type) == 0)
			return comp_type;
	}
	return NULL;
}

static void handle_component(char *value)
{
}

static void handle_type(char *value)
{
	curr_ctype = NULL;
	curr_ctype = find_comp_type(value);
	if (!curr_ctype) {
		curr_ctype = new_comp_type(value);
		TAILQ_INSERT_HEAD(all_type_list, curr_ctype, entry);
	}
}

static void handle_gif_path(char *value)
{
	sprintf(curr_ctype->gif_path, "%s", value);
}

static void handle_visible(char *value)
{
	if ((0 == strcmp(value, "true")) ||
			(0 == strcmp(value, "True")) ||
			(0 == strcmp(value, "TRUE"))) {
		curr_ctype->visible = TRUE;
	} else if ((0 == strcmp(value, "false")) ||
			(0 == strcmp(value, "False")) ||
			(0 == strcmp(value, "FALSE"))) {
		curr_ctype->visible = FALSE;
	} else {
		fprintf(stderr, "comp_type '%s': Invalid visibility "
				"flag '%s'", curr_ctype->type, value);
		exit(EINVAL);
	}
}

static void handle_give_name(char *value)
{
	struct oparser_name *name;
	if (!TAILQ_EMPTY(&curr_uids)) {
		empty_name_list(&curr_uids);
		TAILQ_INIT(&curr_uids);
	}

	if (!TAILQ_EMPTY(&curr_names)) {
		empty_name_list(&curr_names);
		TAILQ_INIT(&curr_names);
	}
}

static void handle_identifiers(char *value)
{
	empty_name_list(&curr_uids);
	char *_value = strdup(value);
	curr_nids = process_string_name(_value, &curr_uids, NULL, NULL);
	free(_value);
	if (curr_nids < 1) {
		fprintf(stderr, "%s: type [%s]: Invalid identifiers. [%s]\n",
				__FUNCTION__, curr_ctype->type, value);
		exit(EINVAL);
	}
}

void handle_many_ids_one_name()
{
	struct oparser_name *uid;
	struct oparser_name *name = TAILQ_FIRST(&curr_names);
	struct oparser_comp *comp;
	TAILQ_FOREACH(uid, &curr_uids, entry)
		comp = new_comp(curr_ctype, uid->name, name->name);
}

void handle_many_ids_names()
{
	struct oparser_name *uid;
	struct oparser_name *name;
	struct oparser_comp *comp;

	uid = TAILQ_FIRST(&curr_uids);
	name = TAILQ_FIRST(&curr_names);
	while (uid && name) {
		comp = new_comp(curr_ctype, uid->name, name->name);
		uid = TAILQ_NEXT(uid, entry);
		name = TAILQ_NEXT(name, entry);
	}
}

void handle_one_id_name()
{
	struct oparser_name *uid = TAILQ_FIRST(&curr_uids);
	struct oparser_name *name = TAILQ_FIRST(&curr_names);
	struct oparser_comp *comp = new_comp(curr_ctype, uid->name,
							name->name);
}

static void handle_names(char *value)
{
	empty_name_list(&curr_names);
	char *_value = strdup(value);
	curr_nnames = process_string_name(_value, &curr_names, NULL, NULL);
	free(_value);
	if (curr_nnames < 1) {
		fprintf(stderr, "%s: type [%s]: Invalid names. [%s]\n",
				__FUNCTION__, curr_ctype->type, value);
		exit(EINVAL);
	}

	if (curr_nids > 1) {
		if (curr_nnames == 1) {
			handle_many_ids_one_name();
		} else if (curr_nnames == curr_nids) {
			handle_many_ids_names();
		} else {
			fprintf(stderr, "%s: # of names[%d]: # of ids[%d]: "
					"Require # of names == # of ids. "
					"(Except # of names = 1).\n",
					__FUNCTION__, curr_nnames, curr_nids);
			exit(EINVAL);
		}
	} else {
		if (curr_nnames == 1) {
			handle_one_id_name();
		} else {
			fprintf(stderr, "%s: # of names[%d]: # of ids[%d]: "
					"Require # of names == # of ids. "
					"(Except # of names = 1). \n",
					__FUNCTION__, curr_nnames, curr_nids);
			exit(EINVAL);
		}
	}
}

static struct kw label_tbl[] = {
	{ "component", handle_component },
	{ "gif_path", handle_gif_path },
	{ "give_name", handle_give_name },
	{ "identifiers", handle_identifiers },
	{ "names", handle_names },
	{ "type", handle_type },
	{ "visible", handle_visible },
};

void clear_src_stack(struct src_stack *srcq, int level)
{
	struct source *src = LIST_FIRST(srcq);
	while (src && src->level >= level) {
		LIST_REMOVE(src, entry);
		free(src);
		src = LIST_FIRST(srcq);
	}
}

void add_parent(struct oparser_comp *comp, struct oparser_comp *parent)
{
	struct comp_array *carray;
	LIST_FOREACH(carray, &comp->parents, entry) {
		if (carray->comps[0]->comp_type == parent->comp_type)
			break;
	}

	if (!carray) {
		carray = calloc(1, sizeof(*carray));
		LIST_INSERT_HEAD(&comp->parents, carray, entry);
		comp->num_ptypes++;
	}

	oparser_add_comp(carray, parent);
}

void add_child(struct oparser_comp *comp, struct oparser_comp *child)
{
	struct comp_array *carray;
	LIST_FOREACH(carray, &comp->children, entry) {
		if (carray->comps[0]->comp_type == child->comp_type)
			break;
	}

	if (!carray) {
		carray = calloc(1, sizeof(*carray));
		LIST_INSERT_HEAD(&comp->children, carray, entry);
		comp->num_chtype++;
	}

	oparser_add_comp(carray, child);
}

void process_node(struct oparser_comp *comp, struct src_stack *srcq, int level)
{
	struct source *src;
	LIST_FOREACH(src, srcq, entry) {
		if (src->level != level - 1)
			break;

		add_parent(comp, src->comp);
		add_child(src->comp, comp);
	}
}

/*
 * Create all the edges in the software and hardware tree
 *
 * \param[in]   conff   The handle to the component cfg file
 * \param[in/out]   scaffold   The tree of the software and hardware
 */
void handle_component_tree(FILE *conff, struct oparser_scaffold *scaffold)
{
	struct src_stack src_stack;
	LIST_INIT(&src_stack);
	struct source *src, *comps;

	struct oparser_comp *comp;
	char type[512];
	char *s;
	int is_leaf;
	int rc, level, num_names;
	struct oparser_comp_type *comp_type;
	struct oparser_name_queue nlist;
	struct oparser_name *uid;

	int num_roots = num_components;
	scaffold->num_nodes = num_components;

	while (s = fgets(main_buf, MAIN_BUF_SIZE, conff)) {
		/* Ignore the comment line */
		if (main_buf[0] == '#')
			continue;

		is_leaf = 0;
		/* Discard the line component_tree */
		if (s = strchr(main_buf, ':')) {
			s = strtok(main_buf, ":");
			if (strcmp(s, "component_tree") == 0) {
				continue;
			} else {
				comp_parser_log("Invalid: label '%s'. "
					"Expecting 'component_tree'\n", s);
				exit(EINVAL);
			}
		}

		rc = sscanf(main_buf, " %[^{/\n]{%[^}]/", type, main_value);
		if (rc == 1) {
			comp_parser_log("%s: %s: No identifier.\n",
						__FUNCTION__, main_buf);
			exit(EINVAL);
		}

		comp_type = find_comp_type(type);
		if (!comp_type) {
			comp_parser_log("No component type '%s'\n", type);
			exit(ENOENT);
		}

		level = count_leading_tabs(main_buf);
		clear_src_stack(&src_stack, level);

		if (scaffold->height < level -1)
			scaffold->height = level - 1;

		if (level > 1) {
			if (LIST_EMPTY(&src_stack)) {
				comp_parser_log("%s: s/t wrong: the src_queue "
						"should not be empty.\n", main_buf);
				exit(EPERM);
			}
		}

		if (!strchr(main_buf, '/'))
			is_leaf = 1;

		num_names = process_string_name(main_value, &nlist, NULL, NULL);
		TAILQ_FOREACH(uid, &nlist, entry) {
			comp = find_comp(comp_type, uid->name);
			if (!comp) {
				comp_parser_log("%s: could not find "
					"'%s{%s}'.\n", __FUNCTION__,
					comp_type->type, uid->name);
				exit(EPERM);
			}

			if (level > 1) {
				TAILQ_REMOVE(all_type_list, comp_type, entry);
				TAILQ_INSERT_HEAD(all_type_list, comp_type,
									entry);

				LIST_REMOVE(comp, root_entry);
				num_roots--;
				process_node(comp, &src_stack, level);
			}

			if (!is_leaf) {
				src = calloc(1, sizeof(*src));
				src->comp = comp;
				src->level = level;
				LIST_INSERT_HEAD(&src_stack, src, entry);
			}
		}

		empty_name_list(&nlist);

	}
	clear_src_stack(&src_stack, 1);
	scaffold->num_children = num_roots;
}

struct oparser_scaffold *oparser_parse_component_def(FILE *conff)
{
	char key[128];
	char *s;

	struct kw keyword;
	struct kw *kw;
	struct oparser_scaffold *scaffold = malloc(sizeof(*scaffold));
	scaffold->height = 0;

	all_type_list = malloc(sizeof(*all_type_list));
	if (!all_type_list) {
		comp_parser_log("comp_def: %s[%d]: Out of memory.\n",
					__FUNCTION__, __LINE__);
		exit(ENOMEM);
	}
	TAILQ_INIT(all_type_list);

	all_root_list = malloc(sizeof(*all_root_list));
	if (!all_root_list) {
		comp_parser_log("comp_def: %s[%d]: Out of memory.\n",
					__FUNCTION__, __LINE__);
		exit(ENOMEM);
	}
	LIST_INIT(all_root_list);

	fseek(conff, 0, SEEK_SET);
	while (s = fgets(main_buf, MAIN_BUF_SIZE, conff)) {
		sscanf(main_buf, " %[^:]: %[^\t\n]", key, main_value);
		trim_trailing_space(main_value);

		/* Ignore the comment line */
		if (key[0] == '#')
			continue;

		if (strcmp(key, "component_tree") == 0) {
			handle_component_tree(conff, scaffold);
			break;
		}

		keyword.token = key;
		kw = bsearch(&keyword, label_tbl, ARRAY_SIZE(label_tbl),
				sizeof(*kw), kw_comparator);

		if (kw) {
			kw->action(main_value);
		} else {
			fprintf(stderr, "Invalid key '%s'\n", key);
			exit(EINVAL);
		}
	}

	scaffold->all_type_list = all_type_list;
	scaffold->children = all_root_list;

	return scaffold;
}

#ifdef MAIN
#include <getopt.h>

#define FMT "c:o:"

char *conf_file;
char *output_file;

int main(int argc, char **argv) {
	int op;

	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'c':
			conf_file = strdup(optarg);
			break;
		case 'o':
			output_file = strdup(optarg);
			break;
		default:
			fprintf(stderr, "Invalid argument '%c'\n", op);
			exit(EINVAL);
			break;
		}
	}

	oparser_component_parser_init(NULL);

	FILE *conff = fopen(conf_file, "r");
	if (!conff) {
		fprintf(stderr, "Cannot open the file '%s'\n", conf_file);
		exit(errno);
	}

	FILE *outputf = fopen(output_file, "w");
	if (!outputf) {
		fprintf(stderr, "Cannot open the output file '%s'\n", output_file);
		exit(errno);
	}

	struct scaffold *scaffold;

	scaffold = oparser_parse_component_def(conff);
	fclose(conff);

//	oparser_print_component_def(&type_list, outputf);

	fprintf(outputf, "---------------------\n");
	fprintf(outputf, "Num components = %d\n", num_components);
	fprintf(outputf, "---------------------\n");
	oparser_print_scaffold(scaffold, outputf);

	fclose(outputf);
	return 0;
}
#endif
