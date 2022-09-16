/**
 * Copyright (c) 2010,2016,2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010,2016,2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file procstat.c
 * \brief /proc/stat data provider
 */
#define _GNU_SOURCE // for getline(). use a compat lib if not glibc platform
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"

#define SAMP "procstat"

/*
 * Depending on the kernel version, not all of the rows will
 * necessarily be used. Since new columns are added at the end of the
 * line, this works across all kernels up to at least 4.4.
 */
static const char *default_metric_name[] = {
	"cpu_enabled",
	"user",
	"nice",
	"sys",
	"idle",
	"iowait",
	"irq",
	"softirq",
	"steal",
	"guest",
	"guest_nice",
};

static const char *array_metric_name[] = {
	"per_core_cpu_enabled",
	"per_core_user",
	"per_core_nice",
	"per_core_sys",
	"per_core_idle",
	"per_core_iowait",
	"per_core_irq",
	"per_core_softirqd",
	"per_core_steal",
	"per_core_guest",
	"per_core_guest_nice",
};

#define NUM_MID 7
#define MID_PROCESSES g.scalar_pos[0]
#define MID_PROCS_RUNNING g.scalar_pos[1]
#define MID_PROCS_BLOCKED g.scalar_pos[2]
#define MID_SOFTIRQ g.scalar_pos[3]
#define MID_INTR g.scalar_pos[4]
#define MID_CTXT g.scalar_pos[5]
#define MID_NCORE g.scalar_pos[6]
static int mid_tick;

#define MAX_CPU_METRICS sizeof(default_metric_name)/sizeof(default_metric_name[0])

static
struct sampler_data {
	int maxcpu; /* include space for this many cpus in metric set */
	base_data_t base;
	ldms_schema_t schema;
	ldms_set_t set;
	FILE *mf;	/* /proc/stat file pointer */
	char *line;
	size_t line_sz;
	ldmsd_msg_log_f msglog;
	int warn_col_count;
	int warn_row_count;
	int scalar_pos[NUM_MID];
	uint64_t compid;
	char *producer_name;
	long tick;
	int sum_pos[MAX_CPU_METRICS]; /* mid for summary core metrics */
	uint64_t sum_data[MAX_CPU_METRICS];
	int core_pos[MAX_CPU_METRICS]; /* mid for per core metrics */
	char *core_data; /* buffer for percore data */
	uint64_t **core_metric; /* pointers into core_data per core.
			core_metric[cpu_no][col_no] */

} g = {
	.maxcpu = -2, // note: -2, not -1 for count_cpu to work right.
	.base = NULL,
	.line = NULL,
	.line_sz = 0,
	.warn_col_count = 0,
	.warn_row_count = 0,
	.compid = 0,
	.tick = 0
};
// global instance, to become per instance later.


static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return g.set;
}

/* records data, accounting for vagaries of cpu reporting in /proc/stat.
On initial call of a sample, cpu_count, should have -1
and it will be the size of the total stats row after that.
*/
int measure_cpu(int *cpu_count, int *column_count, char *token, char **saveptr)
{

	int column = 0;
	int rc = 0;
	long curcpu = -1;
	if (*cpu_count >= g.maxcpu) {
		/* ignore the rest, if more than configured. */
		return 0;
	}
	if (*cpu_count > -1) {
		if (token) {
			char *tmp = token+3;
			char *end = NULL;
			curcpu = strtol(tmp, &end, 10);
			if (end == tmp) {
				return EINVAL;
			}
		} else {
			curcpu = g.maxcpu;
		}
		while (curcpu > (*cpu_count)) {
			/* we fill a downed cpu line per the prior cpu line. */
			for (column = 0; column < *column_count; column++) {
				/* offline/no values */
				g.core_metric[*cpu_count][column] = 0;
			}
			(*cpu_count)++;
		}
	}
	if (!token) {
		return 0;
	}
	column = 0;
	uint64_t *row;
	if (curcpu == -1) {
		row = g.sum_data;
	} else {
		row = g.core_metric[curcpu];
		row[column] = 1;
	}
	for (token = strtok_r(NULL, " \t\n", saveptr);
		token;
		token = strtok_r(NULL, " \t\n", saveptr)) {

		if (column >= MAX_CPU_METRICS) {
			break;
		}
		uint64_t val = 0;
		char *end = NULL;
		val = strtoull(token,&end,10);
		if (end == token) {
			return EINVAL;
		}
		column++;
		row[column] = val;
	}
	if ( (*column_count) > 0 && column < (*column_count)) {
		rc = 1;
		g.msglog(LDMSD_LERROR,
			SAMP ": short read in cpu row %d\n",
			*cpu_count);
	}
	*column_count = column;
	(*cpu_count)++;
	return rc;
}

/* Counts up data, accounting for vagaries of cpu reporting
in /proc/stat. Warns if user input is less than cores found.
result is undefined if token!='cpu*' (where * is 0 or more digits).
on normal exit, cpu_count has the number of cores so far found.
On initial call, cpu_count, should have -1.
*/
int count_cpu( int *cpu_count, int *column_count, char *token, char **saveptr) {

	int column = 0;
	long curcpu = -1;
	if (*cpu_count == g.maxcpu) {
		if ( g.warn_row_count < 2) {
			g.msglog(LDMSD_LINFO,
				SAMP ": unlogged cpu row! user given max: %d\n",
				g.maxcpu);
			g.warn_row_count++;
		}
		return 0;
	}
	if (*cpu_count > -1) {
		if (token) {
			char *tmp = token+3;
			char *end = NULL;
			curcpu = strtol(tmp,&end,10);
			if (end == tmp) {
				g.msglog(LDMSD_LERROR,
				SAMP ": extra unnumbered cpu row!\n");
				return EINVAL;
			}
		} else {
			curcpu = g.maxcpu;
		}
		while (curcpu > (*cpu_count)) {
			(*cpu_count)++;
		}
	}
	if (!token) {
		return 0;
	}
	column = 1;

	for (token = strtok_r(NULL, " \t\n", saveptr);
		token;
		token = strtok_r(NULL, " \t\n", saveptr)) {

		if (column >= MAX_CPU_METRICS) {
			if (g.warn_col_count < 3) {
				g.msglog(LDMSD_LERROR,
				SAMP ": extra cpu metric found\n");
				g.warn_col_count++;
			}
			break;
		}
		column++;
	}
	*column_count = column;
	(*cpu_count)++;
	return 0;
}

#define CTXT_ALIAS "context_switches"
#define SOFTIRQ_ALIAS "softirq_count"
#define INTR_ALIAS "hwintr_count"
/*
	.maxcpu = -1, // assume cores all locally , or 0-.maxcpu
*/
static int create_metric_set(base_data_t base)
{
	int rc,i;
	int column_count = 0;
	int cpu_count;
	char *saveptr;

	g.mf = fopen("/proc/stat", "r");
	if (!g.mf) {
		g.msglog(LDMSD_LERROR,"Could not open the /proc/stat file.\n");
		return ENOENT;
	}

	g.schema = base_schema_new(g.base);
	if (!g.schema) {
		g.msglog(LDMSD_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       SAMP, base->schema_name, errno);
		rc = ENOMEM;
		goto err;
	}

	fseek(g.mf, 0, SEEK_SET);

	MID_NCORE = ldms_schema_metric_add(g.schema, "cores_up", LDMS_V_U64);

	for (i = 0; i < MAX_CPU_METRICS; i++) {
		g.sum_data[i] = 0;
		g.sum_pos[i] = ldms_schema_metric_add(g.schema,
			default_metric_name[i], LDMS_V_U64);
		if (g.sum_pos[i] < 0) {
			rc = -g.sum_pos[i];
			goto err1;
		}
	}

	cpu_count = -1;
	do {
		char *token;
		int nchar=0;
		nchar = getline(&g.line, &g.line_sz, g.mf);
		if (nchar < 4)
			break;

		/* Do not throw away first column which is the CPU 'name'.
		 on systems where a core is downed, linux does not report
		it at all. keep core number in the metrics;
		issue empty row if missing.
		 */
		saveptr = NULL;
		token = strtok_r(g.line, " \t\n", &saveptr);
		if (token == NULL)
			continue;

#define STAT_UNEXPECTED(S) \
	g.msglog(LDMSD_LINFO,SAMP ": unexpected %s in /proc/stat names\n",S)

#define STAT_SCALAR(X,Y, POS) \
if (strcmp(token,X)==0) { \
	token = strtok_r(NULL, " \t\n", &saveptr); \
	rc = ldms_schema_metric_add(g.schema, Y, LDMS_V_U64); \
	if (rc < 0) { \
		g.msglog(LDMSD_LERROR, SAMP ": add " X " failed (finish).\n"); \
		goto err1; \
	} \
	POS = rc; \
} else \
	STAT_UNEXPECTED(token)

/* cannot be certain of /proc/stat layout evolution, except
 assume cpu lines will come first. so in switch check this
 for all other cases.
*/
#define FINISH_CPUS \
	if (cpu_count < g.maxcpu) { \
		int errcpu = count_cpu(&cpu_count, \
			&column_count, NULL, NULL); \
		if (errcpu) { \
			g.msglog(LDMSD_LERROR, SAMP ": count_cpu" \
				" failed (finish).\n"); \
			rc = errcpu; \
			goto err1; \
		} \
	}

		switch (token[0]) {
		case 'c':
			if (0 == strncmp(token, "cpu", 3)) {
				int errcpu = count_cpu(&cpu_count,
					&column_count, token, &saveptr);
				if (errcpu) {
					// log something here?
					rc = errcpu;
					goto err1;
				}
			} else {
				STAT_SCALAR("ctxt", CTXT_ALIAS, MID_CTXT);
			}
			break;
		case 'b':
			FINISH_CPUS;
			/* ignore btime constant */
			if (strcmp(token,"btime")!=0) {
				STAT_UNEXPECTED("btime");
			}
			break;
		case 'i':
			FINISH_CPUS;
			STAT_SCALAR("intr", INTR_ALIAS, MID_INTR);
			break;
		case 's':
			FINISH_CPUS;
			STAT_SCALAR("softirq", SOFTIRQ_ALIAS, MID_SOFTIRQ);
			break;
		case 'p':
			FINISH_CPUS;
			if (strcmp(token,"page")==0) {
				break;
			}
			if (strlen(token)<6) {
				STAT_UNEXPECTED(token);
				break;
			}
			switch (token[6]) {
			case 's':
				STAT_SCALAR("processes", "processes", MID_PROCESSES);
				break;
			case 'r':
				STAT_SCALAR("procs_running", "procs_running",
					MID_PROCS_RUNNING);
				break;
			case 'b':
				STAT_SCALAR("procs_blocked", "procs_blocked",
					MID_PROCS_BLOCKED);
				break;
			default:
				STAT_UNEXPECTED(token);
			}
			break;
		default:
			FINISH_CPUS;
			STAT_UNEXPECTED(token);
		}

	} while (1);

	if (g.maxcpu < 1 && cpu_count >= 0) {
		g.maxcpu = cpu_count;
	}
	for (i = 0; i < MAX_CPU_METRICS; i++) {
		g.core_pos[i] = ldms_schema_metric_array_add(g.schema,
			array_metric_name[i], LDMS_V_U64_ARRAY, g.maxcpu);
		if (g.core_pos[i] < 0) {
			rc = -g.core_pos[i];
			goto err1;
		}
	}
	if (g.tick) {
		mid_tick = ldms_schema_meta_add(g.schema, "sc_clk_tck", LDMS_V_U64);
		if (mid_tick < 0) {
			rc = -mid_tick;
			goto err1;
		}
	}


	g.set = base_set_new(g.base);
	if (!g.set) {
		rc = errno;
		g.msglog(LDMSD_LERROR, SAMP ": ldms_create_set failed.\n");
		goto err1;
	}

	return 0;

 err1:
	ldms_schema_delete(g.schema);
	g.schema = NULL;
 err:
	if (g.mf)
		fclose(g.mf);
	return rc ;
#undef STAT_UNEXPECTED
#undef STAT_SCALAR
#undef FINISH_CPUS

}

/**
 * check for invalid flags, with particular emphasis on warning the user about
 */
static int config_check(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	int i;

	char* deprecated[]={"set", "metrics_type"};

	for (i = 0; i < (sizeof(deprecated)/sizeof(deprecated[0])); i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			g.msglog(LDMSD_LERROR, SAMP ": config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP " maxcpu=<ncpu> sc_clk_tck=1" BASE_CONFIG_USAGE
		"    maxcpu       The number of cpus to record. If fewer exist, report 0s; if more ignore them.\n"
		"    sc_clk_tck   Enable reporting sc_clk_tck (1) or not (any other value).\n"
	;
}

/**
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value, *endp = NULL;
	int rc = EINVAL;
	uint64_t utmp;

	rc = config_check(kwl, avl);
	if (rc != 0){
		return rc;
	}
	if (g.set) {
		g.msglog(LDMSD_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	g.base = base_config(avl, SAMP, SAMP, g.msglog);
	if (!g.base) {
		rc = errno;
		goto out;
	}

	value = av_value(avl, "maxcpu");
	if (value) {
		utmp = strtoull(value, &endp, 0);
		if (endp == value) {
			g.msglog(LDMSD_LERROR, SAMP
				": config: maxcpu value bad: %s\n", value);
			rc = EINVAL;
			goto out;
		}
		g.maxcpu = utmp;
	} else {
		g.maxcpu = -2;
	}
	value = av_value(avl, "sc_clk_tck");
	if (value && value[0] == '1') {
		g.tick = sysconf(_SC_CLK_TCK);
	}

	rc = create_metric_set(g.base);
	if (rc) {
		g.msglog(LDMSD_LERROR, SAMP " create_metric_set err %d\n",
			rc);
		goto out;
	}

	size_t csize = MAX_CPU_METRICS * g.maxcpu * sizeof(uint64_t)
		+ g.maxcpu * sizeof(uint64_t *);
	g.core_data = calloc(csize,1);
	if (!g.core_data) {
		ldms_set_delete(g.set);
		ldms_schema_delete(g.schema);
		g.set = NULL;
		g.schema = NULL;
		rc = ENOMEM;
		goto out;
	}
	g.core_metric = (uint64_t **)g.core_data;
	uint64_t *head = (uint64_t *)
		(g.core_data + g.maxcpu * sizeof(uint64_t *));
	int i;
	for (i = 0; i < g.maxcpu; i++) {
		g.core_metric[i] = head;
		head += MAX_CPU_METRICS;
	}
 out:
	if (g.base && rc != 0) {
		base_del(g.base);
		g.base = NULL;
	}
	return rc;
}

static int sample(struct ldmsd_sampler *self)
{
	int rc = 0;
	char *saveptr = NULL;
	int column_count = 0;
	int cpu_count;

	if (!g.set ){
		g.msglog(LDMSD_LERROR, SAMP ": plugin not initialized\n");
		return EINVAL;
	}
	int err = fseek(g.mf, 0, SEEK_SET);
	if (err < 0) {
		g.msglog(LDMSD_LERROR, SAMP ": failure seeking /proc/stat.\n");
		return 0;
	}

	base_sample_begin(g.base);

	cpu_count = -1;
	do {
		ssize_t nchar = 0;
		nchar = getline(&g.line, &g.line_sz, g.mf);
		if (nchar < 4)
			break;

#define S_STAT_UNEXPECTED(S) \
	g.msglog(LDMSD_LINFO,SAMP ": unexpected %s in /proc/stat names\n",S)

/* verify name and set value. */
#define GET_STAT_SCALAR(X, pos) \
	if (strcmp(X, ldms_metric_name_get(g.set, pos))==0) { \
		token = strtok_r(NULL, " \t\n", &saveptr); \
		char *endp = NULL; \
		uint64_t val = strtoull(token,&endp,10); \
		if (endp == token) { \
			g.msglog(LDMSD_LINFO,SAMP ": non-int value " \
			"in line %s in /proc/stat: %s\n", X, token); \
		} else { \
			ldms_metric_set_u64(g.set, pos, val); \
		} \
	} else { \
		g.msglog(LDMSD_LERROR,SAMP ": format changed? " \
		"in line %s in /proc/stat: %s\n", X, token); \
		break; \
	}

/* Cannot be certain of /proc/stat layout evolution, except
 assume cpu lines will come first. In switch , check this
 for all other cases.
 */
#define S_FINISH_CPUS \
	if (cpu_count < g.maxcpu) { \
		int errcpu = measure_cpu(&cpu_count, &column_count, \
			NULL, NULL ); \
		if (errcpu) { \
			rc = errcpu; \
			goto err1; \
		} \
	}

		char *token;
		if (nchar < 4)
			break;
		saveptr = NULL;
		token = strtok_r(g.line, " \t\n", &saveptr);
		/* First time have to check for corner case NULL  */
		if (token == NULL)
			continue;

		switch (token[0]) {
		case 'c':
			if (0 == strncmp(token, "cpu", 3)) {
				int errcpu = measure_cpu(&cpu_count,
					&column_count,
					token, &saveptr);
				if (errcpu) {
					/* log something here? */
					rc = errcpu;
					goto err1;
				}
			} else {
				GET_STAT_SCALAR(CTXT_ALIAS, MID_CTXT);
			}
			break;
		case 'b':
			S_FINISH_CPUS;
			/* ignore btime constant and any other b */
			break;
		case 'i':
			S_FINISH_CPUS;
			GET_STAT_SCALAR(INTR_ALIAS, MID_INTR);
			break;
		case 's':
			S_FINISH_CPUS;
			GET_STAT_SCALAR(SOFTIRQ_ALIAS, MID_SOFTIRQ);
			break;
		case 'p':
			S_FINISH_CPUS;
			if (strcmp(token,"page")==0) {
				break;
			}
			if (strlen(token)<6)
				break;
			switch (token[6]) {
			case 's':
				GET_STAT_SCALAR("processes", MID_PROCESSES);
				break;
			case 'r':
				GET_STAT_SCALAR("procs_running", MID_PROCS_RUNNING);
				break;
			case 'b':
				GET_STAT_SCALAR("procs_blocked", MID_PROCS_BLOCKED);
				break;
			default:
				break;
			}
			break;
		default:
			S_FINISH_CPUS;
		}
	} while (1);

	int i,j;
	uint64_t ncore = 0;
	for (i = 0; i < g.maxcpu; i++) {
		for (j = 0; j < MAX_CPU_METRICS; j++) {
			if (j == 0) {
				ncore += g.core_metric[i][j];
			}
			ldms_metric_array_set_u64(g.set, g.core_pos[j], i,
				g.core_metric[i][j]);
		}
	}
	g.sum_data[0] = (ncore > 0) ? 1 : 0;
	for (j = 0; j < MAX_CPU_METRICS; j++) {
		ldms_metric_set_u64(g.set, g.sum_pos[j], g.sum_data[j]);
	}
	ldms_metric_set_u64(g.set, MID_NCORE, ncore);
	if (g.tick) {
		ldms_metric_set_u64(g.set, mid_tick, g.tick);
		g.tick = 0; /* do only once */
	}


err1:
	if (rc) {
		g.msglog(LDMSD_LERROR,
			SAMP ": incomplete sample call.\n");
	}
	base_sample_end(g.base);
	return rc;
#undef S_STAT_UNEXPECTED
#undef S_FINISH_CPUS
#undef GET_STAT_SCALAR
}

static void term(struct ldmsd_plugin *self)
{
	if (g.core_data) {
		free(g.core_data);
		g.core_data = NULL;
	}
	if (g.base) {
		base_del(g.base);
		g.base = NULL;
	}
	if (g.set) {
		ldms_set_delete(g.set);
		g.set = NULL;
	}
	free(g.line);
	g.line = NULL;
	if (g.mf) {
		fclose(g.mf);
		g.mf = NULL;
	}
}

static struct ldmsd_sampler procstat_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	g.msglog = pf;
	return &procstat_plugin.base;
}
