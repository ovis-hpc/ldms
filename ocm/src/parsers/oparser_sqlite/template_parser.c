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

struct template_def_list all_tmpl_def_list;
struct tmpl_list all_tmpl_list;
LIST_HEAD(, sampler_host) all_host_list;
struct metric_list all_metric_list;

struct oparser_comp_type_list *all_ctype_list; /* The list of all comp types */
struct oparser_scaffold *sys_scaffold; /* The scaffold of the system */

struct template_def *curr_tmpl_def = NULL;
struct template *curr_tmpl;
struct sampler_host *curr_host;

int num_mtypes;
int is_apply_on;

static char *main_buf;
static char *main_value;

void oparser_template_parser_init(FILE *_log_fp)
{
	LIST_INIT(&all_tmpl_def_list);
	LIST_INIT(&all_tmpl_list);
	LIST_INIT(&all_host_list);
	LIST_INIT(&all_metric_list);
	is_apply_on = 0;
	num_mtypes = 0;

	main_buf = malloc(MAIN_BUF_SIZE);
	main_value = malloc(MAIN_BUF_SIZE);
}

struct template_def *find_tmpl_def(char *tmpl_name)
{
	struct template_def *tmpl_def;
	LIST_FOREACH(tmpl_def, &all_tmpl_def_list, entry) {
		if (strcmp(tmpl_def->name, tmpl_name) == 0)
			return tmpl_def;
	}
	return NULL;
}

static void handle_template(char *value)
{
	is_apply_on = 0;
	curr_tmpl_def = calloc(1, sizeof(*curr_tmpl_def));
	LIST_INSERT_HEAD(&all_tmpl_def_list, curr_tmpl_def, entry);
	LIST_INIT(&curr_tmpl_def->templates);
	LIST_INIT(&curr_tmpl_def->cmtlist);
}

static void handle_template_name(char *value)
{
	if (!is_apply_on) {
		curr_tmpl_def->name = strdup(value);
	} else {
		curr_tmpl = calloc(1, sizeof(*curr_tmpl));
		curr_tmpl->tmpl_def = find_tmpl_def(value);
		if (!curr_tmpl->tmpl_def) {
			tmpl_parser_log("%s: could not find '%s'\n",
					__FUNCTION__, value);
			exit(ENOENT);
		}
		curr_tmpl->host = curr_host->host;
		LIST_INIT(&curr_tmpl->cma_list);
		LIST_INSERT_HEAD(&all_tmpl_list, curr_tmpl, entry);
		LIST_INSERT_HEAD(&curr_host->tmpl_list, curr_tmpl, host_entry);
		LIST_INSERT_HEAD(&curr_tmpl_def->templates,
						curr_tmpl, def_entry);
	}
}

struct oparser_comp_type *find_ctype(char *type)
{
	struct oparser_comp_type *ctype;
	TAILQ_FOREACH(ctype, all_ctype_list, entry) {
		if (strcmp(ctype->type, type) == 0) {
			return ctype;
		}
	}
	return NULL;
}

static void handle_host(char *value)
{
	char type[512], uid[512];
	int rc = sscanf(value, " %[^{/\n]{%[^}]/", type, uid);

	struct oparser_name_queue nqueue;
	int numuid = process_string_name(uid, &nqueue, NULL, NULL);
	if (numuid > 1) {
		tmpl_parser_log("%s: %s: Accept on one uid.\n",
					__FUNCTION__, uid);
		exit(EINVAL);
	}
	empty_name_list(&nqueue);

	curr_host = calloc(1, sizeof(*curr_host));
	LIST_INIT(&curr_host->tmpl_list);

	struct oparser_comp_type *ctype = find_ctype(type);
	if (!ctype) {
		tmpl_parser_log("%s: couldn't find comp type '%s'.\n",
				__FUNCTION__, type);
		exit(ENOENT);
	}

	curr_host->host = find_comp(ctype, uid);
	if (!curr_host->host) {
		tmpl_parser_log("%s: couldn't find host '%s'.\n",
				__FUNCTION__, value);
		exit(ENOENT);
	}

	LIST_INSERT_HEAD(&all_host_list, curr_host, entry);
}

void new_metric(struct comp_metric *cm, struct metric_type *mtype,
			char *ldms_sampler, char *metric_name)
{
	struct oparser_metric *m = malloc(sizeof(*m));
	m->comp = cm->comp;
	m->ldms_sampler = strdup(ldms_sampler);
	if (!m->ldms_sampler) {
		tmpl_parser_log("%s: out of memory.\n", __FUNCTION__);
		exit(ENOMEM);
	}

	m->name = strdup(metric_name);
	if (!m->name) {
		tmpl_parser_log("%s: out of memory.\n", __FUNCTION__);
		exit(ENOMEM);
	}

	m->mtype_id = mtype->mtype_id;
	m->metric_id = gen_metric_id(cm->comp->comp_id, mtype->mtype_id);

	LIST_INSERT_HEAD(&cm->mlist, m, comp_metric_entry);
	LIST_INSERT_HEAD(&cm->comp->mlist, m, comp_entry);
	LIST_INSERT_HEAD(&all_metric_list, m, entry);
}

void create_metrics(struct comp_metric_array *cma)
{
	struct comp_metric *cm;
	struct comp_metric_type *cmt = cma->cmt;
	struct metric_type_ref *mt_ref;

	char *sampler = curr_tmpl->tmpl_def->ldms_sampler;

	char mname[1024];

	if (cma->num_cms == 1) {
	/* Only one component  in the comp metric array */
		cm = &(cma->cms[0]);
		LIST_INIT(&cm->mlist);
		LIST_FOREACH(mt_ref, &cmt->mt_ref_list, entry) {
			/* Don't attach the component name to the metric name */
			new_metric(cm, mt_ref->mtype, sampler,
					mt_ref->mtype->name);
		}
		return;
	}

	/* There are more than one component in the comp metric array */
	struct oparser_name *name;
	int i, j;
	for (i = 0; i < cma->num_cms; i++) {
		cm = &(cma->cms[i]);
		LIST_INIT(&cm->mlist);
		LIST_FOREACH(mt_ref, &cmt->mt_ref_list, entry) {
			/* Attach the component name to the metric name */
			sprintf(mname, "%s#%s", mt_ref->mtype->name,
						cm->comp->name);
			new_metric(cm, mt_ref->mtype, sampler, mname);
		}
	}
}

void _handle_component_apply_on(char *value)
{
	char type[512], uids[512];
	int rc = sscanf(value, " %[^{/\n]{%[^}]/", type, uids);

	struct oparser_comp_type *ctype = find_ctype(type);
	if (!ctype) {
		tmpl_parser_log("%s: Could not find '%s'.\n",
				__FUNCTION__, type);
		exit(ENOENT);
	}

	struct comp_metric_type *cmt;
	LIST_FOREACH(cmt, &curr_tmpl->tmpl_def->cmtlist, entry) {
		if (cmt->ctype == ctype)
			break;
	}

	if (!cmt) {
		tmpl_parser_log("%s: No comp type '%s' in template '%s'.\n",
				__FUNCTION__, type, curr_tmpl->tmpl_def->name);
		exit(ENOENT);
	}

	struct oparser_name_queue nqueue;
	int nuids = process_string_name(uids, &nqueue, NULL, NULL);

	struct comp_metric_array *cma;
	cma = calloc(1, sizeof(struct comp_metric_array));
	if (!cma) {
		tmpl_parser_log("%s: Out of memory.\n", __FUNCTION__);
		exit(ENOMEM);
	}

	cma->num_cms = nuids;
	cma->cms = malloc(nuids * sizeof(struct comp_metric));
	if (!cma->cms) {
		tmpl_parser_log("%s: Out of memory.\n", __FUNCTION__);
		exit(ENOMEM);
	}
	cma->cmt = cmt;

	struct oparser_comp *comp;
	struct oparser_name *uid;
	int i = 0;
	TAILQ_FOREACH(uid, &nqueue, entry) {
		comp = find_comp(ctype, uid->name);
		if (!comp) {
			tmpl_parser_log("%s: Could not find '%s{%s}'.\n",
					__FUNCTION__, type, uid->name);
			exit(ENOENT);
		}
		cma->cms[i++].comp = comp;

	}
	LIST_INSERT_HEAD(&curr_tmpl->cma_list, cma, entry);
	empty_name_list(&nqueue);
	create_metrics(cma);
}

static void handle_component(char *value)
{
	if (!is_apply_on) {
		struct comp_metric_type *cm = calloc(1, sizeof(*cm));
		LIST_INIT(&cm->mt_ref_list);
		LIST_INSERT_HEAD(&curr_tmpl_def->cmtlist, cm, entry);
	} else {
		_handle_component_apply_on(value);
	}
}

static void handle_ldms_sampler(char *value)
{
	curr_tmpl_def->ldms_sampler = strdup(value);
}

static void handle_type(char *value)
{
	struct comp_metric_type *cmt = LIST_FIRST(&curr_tmpl_def->cmtlist);
	cmt->ctype = find_ctype(value);
	if (!cmt->ctype) {
		tmpl_parser_log("%s: Couldn't find the type '%s'.\n",
				__FUNCTION__, value);
		exit(ENOENT);
	}
}

void change_metric_names(struct comp_metric_array *cma,
			struct oparser_name_queue *mnqueue)
{
	struct comp_metric *cm;
	struct comp_metric_type *cmt = cma->cmt;
	struct metric_type_ref *mt_ref;

	char *sampler = curr_tmpl->tmpl_def->ldms_sampler;
	char mname[1024];
	struct oparser_name *name;
	int i;
	for (i = 0; i < cma->num_cms; i++) {
		cm = &(cma->cms[i]);
		/*
		 * The 'name' label is used.
		 * A component name is given. Change the metric names.
		 */
		if (i == 0)
			name = TAILQ_FIRST(mnqueue);
		else
			name = TAILQ_NEXT(name, entry);

		struct oparser_metric *m;
		char core_name[1024];
		int len;
		LIST_FOREACH(m, &cm->mlist, comp_metric_entry) {
			len = strchr(m->name, '#') - m->name;
			if (len > 0) {
				snprintf(core_name, len + 1, "%s", m->name);
			} else {
				sprintf(core_name, "%s", m->name);
			}
			free(m->name);
			sprintf(mname, "%s#%s", core_name, name->name);
			m->name = strdup(mname);
		}
	}
}

void handle_name(char *value)
{
	struct oparser_name_queue nqueue;
	int nnames = process_string_name(value, &nqueue, NULL, NULL);
	struct comp_metric_array *cma = LIST_FIRST(&curr_tmpl->cma_list);
	if (cma->num_cms != nnames) {
		tmpl_parser_log("%s: The number of names[%d] must be equal "
				"to the number of components[%d].\n",
				__FUNCTION__, nnames, cma->num_cms);
		exit(EINVAL);
	}

	change_metric_names(cma, &nqueue);
	empty_name_list(&nqueue);
}

struct metric_type *
new_metric_type(struct oparser_comp_type *ctype,
		char *name, char *ldms_sampler)
{
	num_mtypes++;
	struct metric_type *mtype = calloc(1, sizeof(*mtype));
	mtype->name = strdup(name);
	mtype->ldms_sampler = strdup(ldms_sampler);
	mtype->mtype_id = num_mtypes;
	ctype->num_mtypes++;
	LIST_INSERT_HEAD(&ctype->mtype_list, mtype, entry);
	return mtype;
}

struct metric_type *
find_metric_type(struct mtype_list *mtlist, char *name, char *ldms_sampler)
{
	struct metric_type *mt;
	LIST_FOREACH(mt, mtlist, entry) {
		if (strcmp(mt->name, name) == 0)
			if (strcmp(mt->ldms_sampler, ldms_sampler) == 0)
				return mt;
	}
	return NULL;
}

static void handle_metrics(char *value)
{
	struct oparser_name_queue nqueue;
	int num_names = process_string_name(value, &nqueue, NULL, NULL);
	char *sampler = curr_tmpl_def->ldms_sampler;
	struct comp_metric_type *cmt = LIST_FIRST(&curr_tmpl_def->cmtlist);

	struct oparser_name *mtname;
	struct metric_type_ref *mtref;
	struct metric_type *mtype;
	TAILQ_FOREACH(mtname, &nqueue, entry) {
		LIST_FOREACH(mtref, &cmt->mt_ref_list, entry) {
			mtype = mtref->mtype;
			if (strcmp(mtype->name, mtname->name) == 0) {
				if (strcmp(mtype->ldms_sampler, sampler) == 0) {
					break;
				}
			}
		}

		if (!mtref) {
			mtype = find_metric_type(&cmt->ctype->mtype_list,
						mtname->name, sampler);

			mtref = calloc(1, sizeof(*mtref));
			if (!mtype) {
				mtref->mtype = new_metric_type(cmt->ctype,
						mtname->name, sampler);
			} else {
				mtref->mtype = mtype;
			}


			LIST_INSERT_HEAD(&cmt->mt_ref_list, mtref, entry);
		}
	}
	empty_name_list(&nqueue);
}

static void handle_apply_on(char *value)
{
	is_apply_on = 1;
}

static struct kw label_tbl[] = {
	{ "apply_on", handle_apply_on },
	{ "component", handle_component },
	{ "host", handle_host },
	{ "metrics", handle_metrics },
	{ "name", handle_name },
	{ "sampler", handle_ldms_sampler },
	{ "template", handle_template },
	{ "template_name", handle_template_name },
	{ "type", handle_type },
};

static void handle_config(FILE *conf)
{
	char key[128];
	char *s;

	struct kw keyword;
	struct kw *kw;

	curr_tmpl_def->cfg[0] = '\0';

	int is_first = 1;
	int i;

	while (s = fgets(main_buf, MAIN_BUF_SIZE, conf)) {
		sscanf(main_buf, " %[^:]: %[^\t\n]", key, main_value);
		trim_trailing_space(main_value);

		/* Ignore the comment line */
		if (key[0] == '#')
			continue;

		keyword.token = key;
		kw = bsearch(&keyword, label_tbl, ARRAY_SIZE(label_tbl),
					sizeof(*kw), kw_comparator);

		if (kw) {
			kw->action(main_value);
			return;
		} else {

			if (is_first) {
				sprintf(curr_tmpl_def->cfg, "%s:%s",
							key, main_value);
				is_first = 0;
			} else {
				sprintf(curr_tmpl_def->cfg, "%s;%s:%s",
					curr_tmpl_def->cfg, key, main_value);
			}
		}
	}
}

struct tmpl_list *oparser_parse_template(FILE *conf,
					struct oparser_scaffold *scaffold)
{
	char key[128];
	char *s;

	struct kw keyword;
	struct kw *kw;
	struct oparser_comp_type_list *type_list = scaffold->all_type_list;

	all_ctype_list = type_list;
	sys_scaffold = scaffold;

	while (s = fgets(main_buf, MAIN_BUF_SIZE, conf)) {
		sscanf(main_buf, " %[^:]: %[^\t\n]", key, main_value);
		trim_trailing_space(main_value);

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
			kw->action(main_value);
		} else {
			fprintf(stderr, "Invalid key '%s'\n", key);
			exit(EINVAL);
		}
	}
	free(main_buf);
	free(main_value);
	return &all_tmpl_list;
}

void print_template_metrics(struct comp_metric *cm, FILE *output)
{
	fprintf(output, "		metrics:\n");
	struct oparser_metric *m;
	LIST_FOREACH(m, &cm->mlist, comp_metric_entry) {
		fprintf(output, "		%s	%" PRIu64 "\n",
						m->name, m->metric_id);
	}
}

void print_template_cma(struct comp_metric_array *cma, FILE *output)
{
	fprintf(output, "	comp_type: %s\n", cma->cmt->ctype->type);
	int i;
	struct oparser_comp *comp;
	for (i = 0; i < cma->num_cms; i++) {
		comp = cma->cms->comp;
		fprintf(output, "		component: %s{%s} (%s)\n",
						comp->comp_type->type,
						comp->uid, comp->name);
		print_template_metrics(&cma->cms[i], output);
	}
}

void oparser_print_template_list(struct template_def_list *tmpl_def_list,
							FILE *output)
{
	struct sampler_host *host;
	struct template *tmpl;
	struct comp_metric_array *cma;
	LIST_FOREACH(host, &all_host_list, entry) {
		fprintf(output, "host: %s{%s} (%s)\n",
				host->host->comp_type->type, host->host->uid,
							host->host->name);
		LIST_FOREACH(tmpl, &host->tmpl_list, host_entry) {
			fprintf(output, "	ldms_sampler: %s\n",
						tmpl->tmpl_def->ldms_sampler);
			LIST_FOREACH(cma, &tmpl->cma_list, entry) {
				print_template_cma(cma, output);
			}
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

	struct template_def_list *all_tmpl_def_list;
	struct scaffold *scaffold = oparser_parse_component_def(compf);
	oparser_print_scaffold(scaffold, soutf);
	all_tmpl_def_list = oparser_parse_template(tmplf, scaffold);
	oparser_print_template_list(all_tmpl_def_list, toutf);
	return 0;
}
#endif
