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
struct oparser_component_type_list *all_type_list;
FILE *log_fp;

struct oparser_component_type *component_type;

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

struct oparser_component_type *find_component_type(char *type)
{
	struct oparser_component_type *comp_type;
	TAILQ_FOREACH(comp_type, all_type_list, entry) {
		if (strcmp(comp_type->type, type) == 0)
			return comp_type;
	}
	return NULL;
}

void oparser_component_parser_init(FILE *log_file)
{
	num_components = 0;
	if (log_file)
		log_fp = log_file;
	else
		log_fp = stderr;
}

struct oparser_component *new_comp(struct oparser_component_type *comp_type)
{
	struct oparser_component *comp = calloc(1, sizeof(*comp));
	LIST_INSERT_HEAD(&comp_type->list, comp, type_entry);
	comp_type->num_comp++;
	comp->comp_type = comp_type;
	num_components++;
	comp->comp_id = num_components;
	LIST_INIT(&comp->mlist);
	return comp;
}

struct oparser_component_type *new_comp_type(char *type)
{
	struct oparser_component_type *comp_type = calloc(1, sizeof(*comp_type));
	if (!comp_type) {
		comp_parser_log("%s: out of memory.\n", __FUNCTION__);
		exit(ENOMEM);
	}
	comp_type->type = strdup(type);
	LIST_INIT(&comp_type->list);
	LIST_INIT(&comp_type->mlist);
	return comp_type;
}

static void handle_component(char *value)
{
}


static void handle_type(char *value)
{
	component_type = NULL;
	component_type = find_component_type(value);
	if (!component_type) {
		component_type = new_comp_type(value);
		TAILQ_INSERT_HEAD(all_type_list, component_type, entry);
	}
}

static struct oparser_component_type *process_elem_type(char *s)
{
	char type[64];
	struct oparser_component_type *comp_type;
	int count, rc;

	rc = sscanf(s, "%[^[][%d]", type, &count);

	comp_type = find_component_type(type);
	if (!comp_type) {
		comp_type = new_comp_type(type);
		TAILQ_INSERT_TAIL(all_type_list, comp_type, entry);
	} else {
		/*
		 * The comp types that don't have parent will
		 * be at the top of the queue.
		 */
		TAILQ_REMOVE(all_type_list, comp_type, entry);
		TAILQ_INSERT_TAIL(all_type_list, comp_type, entry);
	}

	if (comp_type->parent_type) {
		comp_parser_log("INVALID: %s type has multiple parents"
				" (%s, %s).\n", type, comp_type->parent_type,
				component_type->type);
		exit(EINVAL);
	}

	comp_type->parent_type = strdup(component_type->type);
	if (rc == 2)
		comp_type->count = count;
	else
		comp_type->count = 1;

	return comp_type;
}

static void handle_elements(char *value)
{
	char *_value = strdup(value);
	int count = 0;
	char *child_type_tmp;
	char *elem_type = strtok(_value, ",");

	while (elem_type) {
		count++;
		elem_type = strtok(NULL, ",");
	}
	free(_value);

	component_type->num_element_types = count;
	component_type->elements = calloc(count, 
					sizeof(struct oparser_component_type *));

	elem_type = strtok(value, ",");
	int i = 0;
	while (elem_type && i < count) {
		component_type->elements[i] = process_elem_type(elem_type);
		i++;
		elem_type = strtok(NULL, ",");
	}
}

static struct kw label_tbl[] = {
	{ "component", handle_component },
	{ "elements", handle_elements },
	{ "type", handle_type },
};

void create_subtree(struct oparser_component *comp,
			struct oparser_component_type *type,
			struct oparser_scaffold *scaffold,
			int is_root)
{
	static int depth = 0;
	if (is_root)
		depth = 0;
	else
		depth++;
	if (scaffold->height < depth)
		scaffold->height = depth;
	comp->num_child_types = type->num_element_types;
	comp->children = calloc(comp->num_child_types,
				sizeof(struct oparser_component_list));
	if (!comp->children) {
		comp_parser_log("name_map: %s: Out of memory\n", __FUNCTION__);
		exit(ENOMEM);
	}

	struct oparser_component *child;
	struct oparser_component_type *child_type;
	int i, j;
	for (i = 0; i < type->num_element_types; i++) {
		LIST_INIT(&comp->children[i]);
		child_type = type->elements[i];
		for (j = 0; j < child_type->count; j++) {
			child = new_comp(child_type);
			create_subtree(child, child_type, scaffold, 0);
			LIST_INSERT_HEAD(&comp->children[i], child, entry);
			child->parent = comp;
		}
	}
	depth--;
}

struct oparser_scaffold *oparser_create_scaffold()
{
	struct oparser_scaffold *scaffold = malloc(sizeof(*scaffold));
	if (!scaffold) {
		comp_parser_log("name_map: Could not create a scaffold. "
							"Error %d\n", ENOMEM);
		exit(ENOMEM);
	}

	scaffold->height = 2;
	scaffold->all_type_list = all_type_list;

	struct oparser_component_type *top_type;
	TAILQ_FOREACH(top_type, all_type_list, entry) {
		/*
		 * Iterate through only the component types
		 * that don't have parents.
		 */
		if (top_type->parent_type)
			break;

		scaffold->num_child_types++;
	}
	scaffold->children = calloc(scaffold->num_child_types,
					sizeof(struct oparser_component_list));
	if (!scaffold->children) {
		comp_parser_log("name_map: %s: Out of memory.\n", __FUNCTION__);
		exit(ENOMEM);
	}

	int i = 0;
	struct oparser_component *comp;
	TAILQ_FOREACH(top_type, all_type_list, entry) {
		/*
		 * Iterate through only the component types
		 * that don't have parents.
		 */
		if (top_type->parent_type)
			break;
		comp = new_comp(top_type);
		comp->parent = NULL;
		LIST_INIT(&scaffold->children[i]);
		LIST_INSERT_HEAD(&scaffold->children[i], comp, entry);
		create_subtree(comp, top_type, scaffold, 1);
		i++;
	}

	return scaffold;
}



void oparser_print_component_def(struct oparser_component_type_list *list,
								FILE *outputf)
{
	int i;
	struct oparser_component_type *comp_type;
	TAILQ_FOREACH(comp_type, all_type_list, entry) {
		fprintf(outputf, "component:\n");
		fprintf(outputf, "	type: %s\n", comp_type->type);
		fprintf(outputf, "	num_elem_type: %d\n",
					comp_type->num_element_types);
		fprintf(outputf, "	elements: ");
		for (i = 0; i < comp_type->num_element_types; i ++) {
			fprintf(outputf, "%s[%d], ",
					comp_type->elements[i]->type,
					comp_type->elements[i]->count);
		}
		fprintf(outputf, "\n");
	}
}

void print_scaffold(FILE *out, struct oparser_component *comp, int depth)
{
	int i, j;
	for (i = 0; i < depth; i++) {
		fprintf(out, "\t");
	}
	if (comp->name)
		fprintf(out, "%s[%" PRIu32 "]: %s\n", comp->comp_type->type,
						comp->comp_id, comp->name);
	else
		fprintf(out, "%s[%" PRIu32 "]\n", comp->comp_type->type,
							comp->comp_id);

	struct oparser_component *child;

	for (i = 0; i < comp->num_child_types; i++) {
		LIST_FOREACH(child, &comp->children[i], entry) {
			print_scaffold(out, child, depth + 1);
		}
	}
}

void oparser_print_scaffold(struct oparser_scaffold *scaffold, FILE *outf)
{
	struct oparser_component *comp;
	int i, j;
	fprintf(outf, "Component tree: height = %d\n", scaffold->height);
	for (i = 0; i < scaffold->num_child_types; i++) {
		LIST_FOREACH(comp, &scaffold->children[i], entry)
			print_scaffold(outf, comp, 1);

	}
}

struct component_list_ref {
	struct oparser_component_list *list;
	TAILQ_ENTRY(component_list_ref) entry;
};
TAILQ_HEAD(clist_ref_list, component_list_ref);

struct oparser_component_list *bf_search_scaffold(char *type, char *name,
					struct oparser_component *scaffold)
{
	int i;
	struct clist_ref_list queue;
	struct component_list_ref *ref;
	TAILQ_INIT(&queue);

	/* Enqueue of all components that have no parent */
	for (i = 0; i < scaffold->num_child_types; i++) {
		ref = malloc(sizeof(*ref));
		ref->list = &scaffold->children[i];
		TAILQ_INSERT_TAIL(&queue, ref, entry);
	}

	/* Do breadth-first search */
	struct oparser_component *child;
	while (!TAILQ_EMPTY(&queue)) {
		ref = TAILQ_FIRST(&queue);
		child = LIST_FIRST(ref->list);
		if (strcmp(child->comp_type->type, type) == 0)
			return ref->list;
	}
	return NULL;
}

struct oparser_component_type *find_comp_type(char *type)
{
	struct oparser_component_type *comp_type;
	TAILQ_FOREACH(comp_type, all_type_list, entry) {
		if (strcmp(comp_type->type, type) == 0)
			return comp_type;
	}
	return NULL;
}

struct src_array *handle_one_comp_one_name(
			struct oparser_component_type *comp_type,
					struct oparser_name *name)
{
	struct oparser_component *comp = LIST_FIRST(&comp_type->list);;
	struct src_array *comps = malloc(sizeof(*comps));
	comps->comp_array = malloc(sizeof(struct oparser_component *));
	comps->comp_array[0] = comp;
	comps->num_comps = 1;
	if (!comps->comp_array[0]->name) {
		comp->name = strdup(name->name);
	} else {
		if (strcmp(comps->comp_array[0]->name, name->name)) {
			comp_parser_log("Invalid name\n");
			exit(EINVAL);
		}
	}
	return comps;
}

int _handle_comp_names(struct oparser_component_list *list,
			struct oparser_name_queue *nlist,
			struct oparser_component **comp_array,
			int idx)
{
	int count = 0;
	struct oparser_name *oname;
	struct oparser_component *comp;
	TAILQ_FOREACH(oname, nlist, entry) {
		LIST_FOREACH(comp, list, entry) {
			if (comp->name) {
				if (strcmp(oname->name, comp->name) == 0) {
					comp_array[idx + count] = comp;
					count++;
					break;
				}
			}
		}
		if (!comp) {
			LIST_FOREACH(comp, list, entry) {
				if (!comp->name) {
					comp->name = strdup(oname->name);
					comp_array[idx + count] = comp;
					count++;
					break;
				}
			}
		}
		/* All components of the type are names. */
		if (!comp) {
			comp_parser_log("Could not find component "
					"type '%s' of name '%s'\n",
					LIST_FIRST(list)->comp_type->type,
								oname->name);
			exit(ENOENT);
		}
	}
	return count;
}

struct src_array *handle_comps_names(struct src_array *src,
				struct oparser_component_type *comp_type,
					struct oparser_name_queue *nlist,
					int num_names)
{
	struct oparser_name *oname;
	struct oparser_component *comp, *tmp_comp;
	struct src_array *comps = malloc(sizeof(*comps));
	comps->num_comps = num_names * src->num_comps;
	comps->comp_array = malloc(comps->num_comps *
				sizeof(struct oparser_component *));


	if (!src) {
		_handle_comp_names(&comp_type->list, nlist,
						comps->comp_array, 0);
		goto out;
	}

	int i, j;
	int idx = 0;
	struct oparser_component_list *list;
	for (i = 0; i < src->num_comps; i++) {
		for (j = 0; j < src->comp_array[i]->num_child_types; j++) {
			if (src->comp_array[i]->num_child_types > 0) {
				list = &src->comp_array[i]->children[j];
				tmp_comp = LIST_FIRST(list);
				if (tmp_comp->comp_type == comp_type) {
					idx += _handle_comp_names(list, nlist,
						comps->comp_array, idx);
					break;
				}
			}
		}
	}
out:
	return comps;
}

struct src_array *handle_all_comp(struct src_array *src,
					struct oparser_component_type *comp_type)
{
	struct src_array *comps;
	struct oparser_component *comp;

	int count = 0;
	if (!src) {
		comps = malloc(sizeof(*comps));
		comps->comp_array = malloc(comp_type->num_comp *
					sizeof(struct oparser_component *));
		comps->num_comps = comp_type->num_comp;
		LIST_FOREACH(comp, &comp_type->list, type_entry) {
			comps->comp_array[count] = comp;
			count++;
		}
		goto out;
	}

	int i, j;
	struct oparser_component_list *list;
	for (i = 0; i < src->num_comps; i++) {
		for (j = 0; j < src->comp_array[i]->num_child_types; j++) {
			list = &src->comp_array[i]->children[j];
			if (LIST_FIRST(list)->comp_type == comp_type) {
				LIST_FOREACH(comp, list, entry)
					count++;
				break;
			}
		}
	}

	comps = malloc(sizeof(*comps));
	comps->comp_array = malloc(count * sizeof(struct oparser_component *));
	comps->num_comps = count;
	count = 0;
	for (i = 0; i < src->num_comps; i++) {
		for (j = 0; j < src->comp_array[i]->num_child_types; j++) {
			list = &src->comp_array[i]->children[j];
			if (LIST_FIRST(list)->comp_type == comp_type) {
				LIST_FOREACH(comp, list, entry) {
					comps->comp_array[count] = comp;
					count++;
				}
				break;
			}
		}
	}
out:
	return comps;
}

struct oparser_component *handle_name_map(FILE *conff,
				struct oparser_scaffold *scaffold)
{
	struct src_list src_queue;
	LIST_INIT(&src_queue);
	struct src_array *src, *comps;

	struct oparser_component *comp;
	char buf[1024];
	char type[512];
	char names[512];
	char *s;
	int is_leaf;
	int rc, level;
	struct oparser_component_type *comp_type;

	while (s = fgets(buf, sizeof(buf), conff)) {
		is_leaf = 0;
		/* Discard the line name_map */
		if (s = strchr(buf, ':')) {
			s = strtok(buf, ":");
			if (strcmp(s, "name_map") == 0) {
				continue;
			} else {
				comp_parser_log("Invalid: label '%s'. "
					"Expecting 'name_map'\n", s);
				exit(EINVAL);
			}
		}

		rc = sscanf(buf, " %[^{/\n]{%[^}]/", type, names);
		comp_type = find_comp_type(type);
		if (!comp_type) {
			comp_parser_log("No component type '%s'\n", type);
			exit(ENOENT);
		}

		level = count_leading_tabs(buf);
		if (level == 1) {
			while (!LIST_EMPTY(&src_queue)) {
				src = LIST_FIRST(&src_queue);
				LIST_REMOVE(src, entry);
				free(src->comp_array);
				free(src);
			}
			src = NULL;
		} else {
			if (LIST_EMPTY(&src_queue)) {
				comp_parser_log("s/t wrong: the src_queue "
						"should not be empty.\n");
				exit(EPERM);
			}

			src = LIST_FIRST(&src_queue);
			while (src->level >= level) {
				LIST_REMOVE(src, entry);
				free(src->comp_array);
				free(src);
				src = LIST_FIRST(&src_queue);
			}
		}

		if (!strchr(buf, '/'))
			is_leaf = 1;

		if (rc == 1)
			names[0] = '\0';

		/* No given names */
		if (names[0] == '\0') {
			if (comp_type->num_comp > 1) {
				comp_parser_log("Need 'names' for type '%s'\n",
									type);
				exit(EINVAL);
			} else {
				comps = malloc(sizeof(*comps));
				comps->comp_array = malloc(sizeof(
						struct oparser_component *));
				comps->comp_array[0] = LIST_FIRST(
							&comp_type->list);
				comps->num_comps = 1;
				goto add_src;
				continue;
			}
		}

		/* at least a name is given. */
		if (strcmp(names, "*") == 0) {
			comps = handle_all_comp(src, comp_type);
			goto add_src;
			continue;
		}

		struct oparser_name_queue nlist;
		int num_names = process_string_name(names, &nlist, NULL, NULL);

		/* number of component of type == 1*/
		if (comp_type->num_comp == 1) {
			if (num_names > 1) {
				comp_parser_log("More names are given than "
						"number of components of type "
						"'%s'\n", type);
				exit(EINVAL);
			} else {
				comps = handle_one_comp_one_name(
						comp_type, TAILQ_FIRST(&nlist));
				goto add_src;
				continue;
			}
		}

		/* Number of components of type > 1 */
		comps = handle_comps_names(src, comp_type, &nlist, num_names);

add_src:
		if (!is_leaf) {
			comps->level = level;
			LIST_INSERT_HEAD(&src_queue, comps, entry);
		}
	}

	return NULL;
}


struct oparser_scaffold *oparser_parse_component_def(FILE *conff)
{
	char buf[1024];
	char key[128], value[512];
	char *s;

	struct kw keyword;
	struct kw *kw;
	struct oparser_scaffold *scaffold = NULL;

	all_type_list = malloc(sizeof(*all_type_list));
	if (!all_type_list) {
		comp_parser_log("comp_def: %s[%d]: Out of memory.\n",
					__FUNCTION__, __LINE__);
		exit(ENOMEM);
	}
	TAILQ_INIT(all_type_list);
	fseek(conff, 0, SEEK_SET);

	while (s = fgets(buf, sizeof(buf), conff)) {
		sscanf(buf, " %[^:]: %[^#\t\n]", key, value);
		trim_trailing_space(value);

		/* Ignore the comment line */
		if (key[0] == '#')
			continue;

		if (strcmp(key, "name_map") == 0) {
			/* This should come after all other labels */
			scaffold = oparser_create_scaffold();
			handle_name_map(conff, scaffold);
			break;
		}

		keyword.token = key;
		kw = bsearch(&keyword, label_tbl, ARRAY_SIZE(label_tbl),
				sizeof(*kw), kw_comparator);

		if (kw) {
			kw->action(value);
		} else {
			fprintf(stderr, "Invalid key '%s'\n", key);
			exit(EINVAL);
		}
	}
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
