/*
 * template_parser.c
 *
 *  Created on: Oct 17, 2013
 *      Author: nichamon
 */

#include <sys/queue.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stddef.h>
#include <time.h>
#include <stdarg.h>
#include <getopt.h>

#include "template_parser.h"
#include "component_parser.h"

FILE *log_fp;

void tmpl_parser_log(const char *fmt, ...)
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

struct template_def_list tmpl_def_list;
struct oparser_component_type_list *sys_ctype_list; /* The list of all comp types */
struct oparser_scaffold *sys_scaffold; /* The scaffold of the system */

struct template_def *tmpl_def = NULL;
struct oparser_component_type *prod_ctype = NULL; /* The component type that holds metrics. */
struct set **set_array = NULL;
struct oparser_component ***prod_carray = NULL;
int num_prod_carray;
int num_prod_comps;
int is_type;
int is_name;

void oparser_template_parser_init(FILE *_log_fp)
{
	LIST_INIT(&tmpl_def_list);
	set_array = NULL;
	prod_carray = NULL;
}

static void handle_template(char *value)
{
	tmpl_def = calloc(1, sizeof(*tmpl_def));
	LIST_INSERT_HEAD(&tmpl_def_list, tmpl_def, entry);
}

static void handle_template_name(char *value)
{
	tmpl_def->name = strdup(value);
}

static void handle_component_type(char *value)
{
	struct oparser_component_type *comp_type;
	TAILQ_FOREACH(comp_type, sys_ctype_list, entry) {
		if (strcmp(comp_type->type, value) == 0) {
			tmpl_def->comp_type = comp_type;
			prod_ctype = comp_type;
			return;
		}
	}

	if (!comp_type) {
		tmpl_parser_log("template: Could not find the "
					"comp type '%s'\n", value);
		exit(ENOENT);
	}
}

void handle_apply_all_comps()
{
	int num_comps = tmpl_def->comp_type->num_comp;
	tmpl_def->num_tmpls = num_comps;
	tmpl_def->templates = malloc(num_comps * sizeof(struct template));
	if (!tmpl_def->templates) {
		tmpl_parser_log("tmpl: %s: Out of memory\n", __FUNCTION__);
		exit(ENOMEM);
	}

	int i = 0;
	struct oparser_component *comp;
	LIST_FOREACH(comp, &tmpl_def->comp_type->list, type_entry) {
		tmpl_def->templates[i].comp = comp;
		LIST_INIT(&tmpl_def->templates[i].slist);
		i++;
	}
}

void handle_apply_some_comps(char *value)
{
	struct oparser_name_queue nqueue;
	TAILQ_INIT(&nqueue);
	int num_names = process_string_name(value, &nqueue, NULL, NULL);
	tmpl_def->num_tmpls = num_names;
	tmpl_def->templates = malloc(num_names * sizeof(struct template));
	if (!tmpl_def->templates) {
		tmpl_parser_log("template: Failed to create templates. "
				"Error %d\n", ENOMEM);
		exit(ENOMEM);
	}

	struct oparser_component *comp;
	struct oparser_name *name;
	int i = 0;
	TAILQ_FOREACH(name, &nqueue, entry) {
		LIST_FOREACH(comp, &tmpl_def->comp_type->list, type_entry) {
			if (strcmp(comp->name, name->name) == 0) {
				tmpl_def->templates[i].comp = comp;
				LIST_INIT(&(tmpl_def->templates[i].slist));
				i++;
				break;
			}
		}

		if (!comp) {
			tmpl_parser_log("template: Could not find the name"
					" '%s' of type '%s'\n", name->name,
					tmpl_def->comp_type->type);
			exit(ENOENT);
		}
	}
}

static void handle_apply_on(char *value)
{
	if (strcmp(value, "*") == 0)
		handle_apply_all_comps();
	else
		handle_apply_some_comps(value);
}

static void handle_component(char *value)
{
	int i;
	if (prod_carray) {
		for (i = 0; i < num_prod_carray; i++)
			free(prod_carray[i]);
		free(prod_carray);
		prod_carray = NULL;
	}
	num_prod_carray = 0;
	num_prod_comps = 0;
	is_name = 0;
}

static void handle_set(char *value)
{
	is_type = is_name = 0;
	if (set_array) {
		free(set_array);
		set_array = NULL;
	}

	set_array = calloc(tmpl_def->num_tmpls, sizeof(struct set *));
	if (!set_array) {
		tmpl_parser_log("tmpl: %s: Out of memory\n",
						__FUNCTION__);
		exit(ENOMEM);

	}

	struct set *set;
	struct template *tmpl;
	int i;
	for (i = 0; i < tmpl_def->num_tmpls; i++) {
		set = calloc(1, sizeof(*set));
		if (!set) {
			tmpl_parser_log("tmpl: %s: Out of memory\n",
							__FUNCTION__);
			exit(ENOMEM);
		}
		set->sampler_pi = strdup(value);
		if (!set->sampler_pi) {
			tmpl_parser_log("tmpl: %s: Out of memory\n",
					__FUNCTION__);
			exit(ENOMEM);
		}
		tmpl = &tmpl_def->templates[i];
		LIST_INSERT_HEAD(&tmpl->slist, set, entry);
		set_array[i] = set;
		LIST_INIT(&set->mlist);
	}

	handle_component(NULL);
}

struct oparser_component_type *find_prod_ctype(char *type)
{
	struct oparser_component_type *ctype;
	TAILQ_FOREACH(ctype, sys_ctype_list, entry) {
		if (strcmp(ctype->type, type) == 0) {
			return ctype;
		}
	}
	return NULL;
}

static void handle_type(char *value)
{
	struct template *tmpl;
	struct set *set;
	is_type = 1;
	num_prod_carray = tmpl_def->num_tmpls;
	prod_carray = calloc(num_prod_carray,
				sizeof(struct oparser_component *));
	if (!prod_carray) {
		tmpl_parser_log("tmpl: %s: Out of memory\n",
						__FUNCTION__);
		exit(ENOMEM);
	}

	prod_ctype = find_prod_ctype(value);
	if (!prod_ctype) {
		fprintf(stderr, "Could not find comp type '%s'.\n", value);
		exit(ENOENT);
	}
}

struct oparser_component *find_successor_component(
				struct oparser_component *current_comp,
				char *name,
				struct oparser_component_type *skipped_comp_type)
{
	struct oparser_component *comp;
	struct oparser_component_list *clist;
	int i;

	if (current_comp->num_child_types == 0)
		return NULL;

	for (i = 0; i < current_comp->num_child_types; i++) {
		clist = &current_comp->children[i];
		comp = LIST_FIRST(clist);
		/*
		 * If the comp type is the same as the comp type that
		 * the template is applied on, skip this child type.
		 */
		if (comp->comp_type == skipped_comp_type)
			continue;

		if (comp->comp_type == prod_ctype) {

			if (!name) {
				if (comp->entry.le_next) {
					tmpl_parser_log("template: Need name of"
							" the comp type '%s'\n",
							prod_ctype->type);
					exit(EPERM);
				} else {
					return comp;
				}
			}

			comp = NULL;
			LIST_FOREACH(comp, clist, entry) {
				if (strcmp(comp->name, name) == 0) {
					return comp;
				}
			}

			if (!comp) {
				tmpl_parser_log("template: The comp type '%s' "
					"has no '%s' under the comp '%s[%s]'\n",
					prod_ctype->type, name,
					current_comp->comp_type->type,
					current_comp->name);
				exit(ENOENT);
			}
		}
	}

	for (i = 0; i < current_comp->num_child_types; i++) {
		comp = LIST_FIRST(&current_comp->children[i]);
		/*
		 * If the comp type is the same as the comp type that
		 * the template is applied on, skip this child type.
		 */
		if (comp->comp_type == skipped_comp_type)
			continue;

		LIST_FOREACH(comp, &current_comp->children[i], entry) {
			return find_successor_component(comp, name,
							skipped_comp_type);
		}
	}
}

struct oparser_component *find_predecessor_component(
					struct oparser_component *current_comp,
					char *name)
{
	struct oparser_component *comp;
	if (!current_comp->parent)
		return NULL;

	struct oparser_component *parent = current_comp->parent;
	if (parent->comp_type == prod_ctype) {
		if (!name)
			return parent;

		if (strcmp(parent->name, name) == 0)
			return parent;
		else
			return NULL;
	} else {
		comp = find_successor_component(parent, name,
						current_comp->comp_type);
		if (!comp)
			return find_predecessor_component(parent, name);
		else
			return comp;
	}
}

struct oparser_component *find_closest_component(
				struct oparser_component *applied_comp,
				char *name)
{
	struct oparser_component *comp;
	struct compinent_list *clist;
	struct oparser_component_type *comp_type;
	int i;
	comp = find_successor_component(applied_comp, name, NULL);
	if (comp)
		return comp;

	return find_predecessor_component(applied_comp, name);
}

void handle_name(char *value)
{
	is_name = 1;

	struct oparser_name_queue cnqueue;
	struct oparser_component *comp;
	struct template *tmpl;

	TAILQ_INIT(&cnqueue);

	int i, j;
	if (!value) {
		for (i = 0; i < tmpl_def->num_tmpls; i++) {
			tmpl = &tmpl_def->templates[i];
			comp = find_closest_component(tmpl->comp, value);
			if (!comp) {
				tmpl_parser_log("template: Could not find "
						"%s\n", prod_ctype->type);
				exit(ENOENT);
			}
			prod_carray[i] = malloc(sizeof(
						struct oparser_component *));
			prod_carray[i][0] = comp;
			num_prod_comps = 1;
		}
		return;
	}

	struct oparser_name *name;
	num_prod_comps = process_string_name(value, &cnqueue, NULL, NULL);
	for (i = 0; i < tmpl_def->num_tmpls; i++) {
		prod_carray[i] = malloc(num_prod_comps *
					sizeof(struct oparser_component *));
		if (!prod_carray[i]) {
			tmpl_parser_log("tmpl: %s: Out of memory\n",
							__FUNCTION__);
			exit(ENOMEM);
		}
		j = 0;
		TAILQ_FOREACH(name, &cnqueue, entry) {
			tmpl = &tmpl_def->templates[i];
			comp = find_closest_component(tmpl->comp, name->name);
			if (!comp) {
				tmpl_parser_log("template: Could not find "
						"%s[%s]\n",
						prod_ctype->type, name->name);
				exit(ENOENT);
			}
			prod_carray[i][j] = comp;
			j++;
		}
	}
}

struct oparser_metric *new_metric(uint32_t mtype_id, uint32_t comp_id,
				char *name, struct oparser_component *comp)
{
	struct oparser_metric *metric = malloc(sizeof(*metric));
	if (mtype_id) {
		metric->mtype_id = mtype_id;
		if (comp_id)
			metric->metric_id = gen_metric_id(comp_id, mtype_id);
	}
	if (name)
		metric->name = strdup(name);
	metric->comp = comp;
	return metric;
}

struct oparser_metric *search_metric(struct metric_list *list, char *name)
{
	struct oparser_metric *m;
	LIST_FOREACH(m, list, entry) {
		if (strcmp(m->name, name) == 0)
			return m;
	}
	return NULL;
}

static void handle_metrics(char *value)
{
	if (!is_type) {
		num_prod_carray = tmpl_def->num_tmpls;
		prod_carray = calloc(num_prod_carray,
					sizeof(struct oparser_component *));
		if (!prod_carray) {
			tmpl_parser_log("tmpl: %s: Out of memory\n",
							__FUNCTION__);
			exit(ENOMEM);
		}

		int i;
		for (i = 0; i < tmpl_def->num_tmpls; i++) {
			prod_carray[i] = malloc(sizeof(
					struct oparser_component *));
			prod_carray[i][0] = tmpl_def->templates[i].comp;
			num_prod_comps = 1;
		}
	} else {
		if (!is_name) {
			handle_name(NULL);
		}
	}

	struct oparser_name_queue nqueue;
	struct oparser_name *name;
	struct oparser_component *comp;
	int num_names = process_string_name(value, &nqueue, NULL, NULL);
	int is_found = 0;
	struct oparser_metric *m, *metric;

	struct metric_list *mlist = &prod_ctype->mlist;
	struct metric_list *prod_mlist;
	char full_name[128];
	uint32_t mtype_id;
	int i, j;

	TAILQ_FOREACH(name, &nqueue, entry) {
		LIST_FOREACH(m, mlist, type_entry) {
			if (strcmp(name->name, m->name) == 0) {
				is_found = 1;
				break;
			}
		}

		/* not found */
		if (!m) {
			m = LIST_FIRST(mlist);
			/* If this is the first metric of the comp type. */
			if (!m)
				mtype_id = 1;
			else
				mtype_id = m->mtype_id + 1;

			m = new_metric(mtype_id, 0, name->name, NULL);
			LIST_INSERT_HEAD(mlist, m, type_entry);
		}
		for (i = 0; i < tmpl_def->num_tmpls; i++) {
			for (j = 0; j < num_prod_comps; j++) {
				prod_mlist = &prod_carray[i][j]->mlist;
				if (prod_carray[i][j]->name) {
					sprintf(full_name, "%s#%s", name->name,
						prod_carray[i][j]->name);
				} else {
					sprintf(full_name, "%s", name->name);
				}

				if (is_found) {
					metric = search_metric(prod_mlist,
								full_name);
					if (metric)
						goto add_to_set;
				}

				metric = new_metric(m->mtype_id,
						prod_carray[i][j]->comp_id,
						full_name, prod_carray[i][j]);
				LIST_INSERT_HEAD(prod_mlist, metric, entry);
add_to_set:
				LIST_INSERT_HEAD(&set_array[i]->mlist, metric,
								set_entry);
			}
		}
	}
}

static struct kw label_tbl[] = {
	{ "apply_on", handle_apply_on },
	{ "component", handle_component },
	{ "component_type", handle_component_type },
	{ "metrics", handle_metrics },
	{ "name", handle_name },
	{ "set", handle_set },
	{ "template", handle_template },
	{ "template_name", handle_template_name },
	{ "type", handle_type },
};

static void handle_config(FILE *conf)
{
	char buf[1024];
	char key[128], value[512];
	char *s;

	struct kw keyword;
	struct kw *kw;

	int i, is_first;
	for (i = 0; i < tmpl_def->num_tmpls; i++)
		set_array[i]->cfg[0] = '\0';

	is_first = 1;

	while (s = fgets(buf, sizeof(buf), conf)) {
		sscanf(buf, " %[^:]: %[^#\t\n]", key, value);
		trim_trailing_space(value);

		/* Ignore the comment line */
		if (key[0] == '#')
			continue;

		keyword.token = key;
		kw = bsearch(&keyword, label_tbl, ARRAY_SIZE(label_tbl),
					sizeof(*kw), kw_comparator);

		if (kw) {
			kw->action(value);
			return;
		} else {
			for (i = 0; i < tmpl_def->num_tmpls; i++) {
				if (is_first) {
					sprintf(set_array[i]->cfg, "%s:%s",
								key, value);
					is_first = 0;
				} else {
					sprintf(set_array[i]->cfg, "%s;%s:%s",
						set_array[i]->cfg, key, value);
				}
			}
		}
	}
}

struct template_def_list *oparser_parse_template(FILE *conf,
					struct oparser_scaffold *scaffold)
{
	char buf[1024];
	char key[128], value[512];
	char *s;

	struct kw keyword;
	struct kw *kw;
	struct oparser_component_type_list *type_list = scaffold->all_type_list;

	sys_ctype_list = type_list;
	sys_scaffold = scaffold;

	while (s = fgets(buf, sizeof(buf), conf)) {
		sscanf(buf, " %[^:]: %[^#\t\n]", key, value);
		trim_trailing_space(value);

		/* Ignore the comment line */
		if (key[0] == '#')
			continue;

		if (strcmp(key, "config") == 0) {
			handle_config(conf);
			continue;
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
	return &tmpl_def_list;
}

void print_set(struct set *set, FILE *output)
{
	fprintf(output, "		set: %s\n", set->sampler_pi);
	fprintf(output, "		cfg: %s\n", set->cfg);
	fprintf(output, "		metrics: \n");

	struct oparser_metric *m = LIST_FIRST(&set->mlist);
	if (!m) {
		tmpl_parser_log("tmpl: no metric in set '%s'\n",
						set->sampler_pi);
		exit(EPERM);
	}
	uint32_t comp_id;
	comp_id = (uint32_t) (m->metric_id >> 32);
	fprintf(output, ",%s	%" PRIu64 "	%" PRIu32 "\n", m->name,
						m->metric_id, comp_id);

	while (m = LIST_NEXT(m, set_entry)) {
		comp_id = (uint32_t) (m->metric_id >> 32);
		fprintf(output, ",%s	%" PRIu64 "	%" PRIu32 "\n",
					m->name, m->metric_id, comp_id);
	}

	fprintf(output, "\n");
}

void print_template(struct template *tmpl, FILE *output)
{
	fprintf(output, "	component: %s[%s]\n", tmpl->comp->name,
						tmpl->comp->comp_type->type);
	struct set *set;
	LIST_FOREACH(set, &tmpl->slist, entry) {
		print_set(set, output);
	}
}

void oparser_print_template_def_list(struct template_def_list *tmpl_def_list,
							FILE *output)
{
	struct template_def *tmpl_def;
	int i;
	struct template *tmpl;
	struct set *set;
	LIST_FOREACH(tmpl_def, tmpl_def_list, entry) {
		fprintf(output, "template:\n");
		fprintf(output, "	name: %s\n", tmpl_def->name);
		fprintf(output, "	comp_type: %s\n",
					tmpl_def->comp_type->type);
		for (i = 0; i < tmpl_def->num_tmpls; i++) {
			print_template(&tmpl_def->templates[i], output);
		}
	}

}

#ifdef MAIN
#define FMT "c:t:s:o:"

int main(int argc, char **argv) {
	int op;
	char *comp_file, *tmpl_file, *soutput_file, *toutput_file;
	FILE *compf, *tmplf, *soutf, *toutf;

	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'c':
			comp_file = strdup(optarg);
			break;
		case 't':
			tmpl_file = strdup(optarg);
			break;
		case 's':
			soutput_file = strdup(optarg);
			break;
		case 'o':
			toutput_file = strdup(optarg);
			break;
		default:
			fprintf(stderr, "Invalid argument '%c'\n", op);
			exit(EINVAL);
			break;
		}
	}
	log_fp = stdout;
	compf = fopen(comp_file, "r");
	if (!compf) {
		fprintf(stderr, "Cannot open the file '%s'\n", comp_file);
		exit(errno);
	}

	tmplf = fopen(tmpl_file, "r");
	if (!tmplf) {
		fprintf(stderr, "Cannot open the file '%s'\n", tmpl_file);
		exit(errno);
	}

	soutf = fopen(soutput_file, "w");
	if (!soutf) {
		fprintf(stderr, "Cannot open the file '%s'\n", soutput_file);
		exit(errno);
	}

	toutf = fopen(toutput_file, "w");
	if (!toutf) {
		fprintf(stderr, "Cannot open the file '%s'\n", toutput_file);
		exit(errno);
	}

	struct template_def_list *tmpl_def_list;
	struct scaffold *scaffold = oparser_parse_component_def(compf);
	oparser_print_scaffold(scaffold, soutf);
	tmpl_def_list = oparser_parse_template(tmplf, scaffold);
	oparser_print_template_def_list(tmpl_def_list, toutf);
	return 0;
}
#endif
