/**
 * Copyright (c) 2010,2016,2018-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010,2016,2018-2019 Open Grid Computing, Inc. All rights
 * reserved.
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
#include "ldmsd_sampler.h"

static const char *procfile = "/proc/stat";

#define CTXT_ALIAS "context_switches"
#define SOFTIRQ_ALIAS "softirq_count"
#define INTR_ALIAS "hwintr_count"

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

#define ARRAY_LEN(a) (sizeof(a)/sizeof((a)[0]))
#define MAX_CPU_METRICS (ARRAY_LEN(default_metric_name))

static const char *array_metric_name[] = {
	"per_core_cpu_enabled",
	"per_core_user",
	"per_core_nice",
	"per_core_sys",
	"per_core_idle",
	"per_core_iowait",
	"per_core_irq",
	"per_core_softirq",
	"per_core_steal",
	"per_core_guest",
	"per_core_guest_nice",
};

typedef struct procstat_inst_s *procstat_inst_t;
struct procstat_inst_s {
	struct ldmsd_plugin_inst_s base;

	int maxcpu; /* include space for this many cpus in metric set */
	FILE *mf;	/* /proc/stat file pointer */
	char *line;
	size_t line_sz;

	int warn_row_count;
	int warn_col_count;

	int mid_ncore;
	int mid_intr;
	int mid_ctxt;
	int mid_processes;
	int mid_procs_running;
	int mid_procs_blocked;
	int mid_softirq;

	int sum_pos[MAX_CPU_METRICS]; /* mid for summary core metrics */
	int core_pos[MAX_CPU_METRICS]; /* mid for per core metrics */
};

int count_cpu(procstat_inst_t inst, int *cpu_count, int *column_count,
	      char *token, char **saveptr)
{

	int column = 0;
	long curcpu = -1;
	if (*cpu_count == inst->maxcpu) {
		if (inst->warn_row_count < 2) {
			ldmsd_log(LDMSD_LINFO,
				  "%s: unlogged cpu row! user given max: %d\n",
				  inst->base.inst_name, inst->maxcpu);
			inst->warn_row_count++;
		}
		return 0;
	}
	if (*cpu_count > -1) {
		if (token) {
			char *tmp = token+3;
			char *end = NULL;
			curcpu = strtol(tmp,&end,10);
			if (end == tmp) {
				ldmsd_log(LDMSD_LERROR,
					  "%s: extra unnumbered cpu row!\n",
					  inst->base.inst_name);
				return EINVAL;
			}
		} else {
			curcpu = inst->maxcpu;
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
			if (inst->warn_col_count < 3) {
				ldmsd_log(LDMSD_LERROR,
					  "%s: extra cpu metric found\n",
					  inst->base.inst_name);
				inst->warn_col_count++;
			}
			break;
		}
		column++;
	}
	*column_count = column;
	(*cpu_count)++;
	return 0;
}

/* records data, accounting for vagaries of cpu reporting in /proc/stat.
On initial call of a sample, cpu_count, should have -1
and it will be the size of the total stats row after that.
*/
int measure_cpu(procstat_inst_t inst, int *cpu_count, int *column_count,
		char *token, char **saveptr, ldms_set_t set)
{

	int column = 0;
	int rc = 0;
	long curcpu = -1;
	if (*cpu_count >= inst->maxcpu) {
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
			curcpu = inst->maxcpu;
		}
		while (curcpu > (*cpu_count)) {
			/* we fill a downed cpu line per the prior cpu line. */
			for (column = 0; column < *column_count; column++) {
				/* offline/no values */
				ldms_metric_array_set_u64(set,
					inst->core_pos[column], *cpu_count, 0);
			}
			(*cpu_count)++;
		}
	}
	if (!token) {
		return 0;
	}
	column = 0;
	if (curcpu > -1) {
		/* cpu_enabled metric */
		ldms_metric_array_set_u64(set, inst->core_pos[column],
					  curcpu, 1);
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
		if (curcpu == -1) {
			ldms_metric_set_u64(set, inst->sum_pos[column], val);
		} else {
			ldms_metric_array_set_u64(set, inst->core_pos[column],
						  curcpu, val);
		}
	}
	if ( (*column_count) > 0 && column < (*column_count)) {
		rc = 1;
		ldmsd_log(LDMSD_LERROR, "%s: short read in cpu row %d\n",
			  inst->base.inst_name, *cpu_count);
	}
	*column_count = column;
	(*cpu_count)++;
	return rc;
}
/* ============== Sampler Plugin APIs ================= */

static inline
int __metric_add(ldms_schema_t sch, const char *name, enum ldms_value_type type,
		 const char *unit, int n, int *idx)
{
	int rc;
	if (ldms_type_is_array(type))
		rc = ldms_schema_metric_array_add(sch, name, type, unit, n);
	else
		rc = ldms_schema_metric_add(sch, name, type, unit);
	if (rc < 0)
		return -rc;
	*idx = rc;
	return 0;
}

static
int procstat_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t sch)
{
	int i, rc;
	int cpu_count, column_count;
	char *token, *saveptr;
	procstat_inst_t inst = (void*)pi;

	rc = __metric_add(sch, "cores_up", LDMS_V_U64, "", 1, &inst->mid_ncore);
	if (rc)
		return rc;

	for (i = 0; i < MAX_CPU_METRICS; i++) {
		rc = __metric_add(sch, default_metric_name[i], LDMS_V_U64,
				  "", 1, &inst->sum_pos[i]);
		if (rc)
			return rc;
	}

	fseek(inst->mf, 0, SEEK_SET);

	cpu_count = -1;
	do {
		int nchar=0;
		nchar = getline(&inst->line, &inst->line_sz, inst->mf);
		if (nchar < 4)
			break;

		/* Do not throw away first column which is the CPU 'name'.
		 on systems where a core is downed, linux does not report
		it at all. keep core number in the metrics;
		issue empty row if missing.
		 */
		saveptr = NULL;
		token = strtok_r(inst->line, " \t\n", &saveptr);
		if (token == NULL)
			continue;

#define STAT_UNEXPECTED(S) \
	ldmsd_log(LDMSD_LINFO, "%s: unexpected %s in /proc/stat names\n", \
		  pi->inst_name, S)

#define STAT_SCALAR(X,Y, POS) \
if (strcmp(token,X)==0) { \
	token = strtok_r(NULL, " \t\n", &saveptr); \
	rc = ldms_schema_metric_add(sch, Y, LDMS_V_U64, ""); \
	if (rc < 0) { \
		ldmsd_log(LDMSD_LERROR, "%s: add " X " failed (finish).\n", \
			  pi->inst_name); \
		return -rc; \
	} \
	POS = rc; \
} else \
	STAT_UNEXPECTED(token)

/* cannot be certain of /proc/stat layout evolution, except
 assume cpu lines will come first. so in switch check this
 for all other cases.
*/
#define FINISH_CPUS \
	if (cpu_count < inst->maxcpu) { \
		int errcpu = count_cpu(inst, &cpu_count, \
			&column_count, NULL, NULL); \
		if (errcpu) { \
			ldmsd_log(LDMSD_LERROR, "%s: count_cpu" \
				" failed (finish).\n", pi->inst_name); \
			return errcpu; \
		} \
	}

		switch (token[0]) {
		case 'c':
			if (0 == strncmp(token, "cpu", 3)) {
				int errcpu = count_cpu(inst, &cpu_count,
					&column_count, token, &saveptr);
				if (errcpu) {
					// log something here?
					return errcpu;
				}
			} else {
				STAT_SCALAR("ctxt", CTXT_ALIAS, inst->mid_ctxt);
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
			STAT_SCALAR("intr", INTR_ALIAS, inst->mid_intr);
			break;
		case 's':
			FINISH_CPUS;
			STAT_SCALAR("softirq", SOFTIRQ_ALIAS,
				    inst->mid_softirq);
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
				STAT_SCALAR("processes", "processes",
					    inst->mid_processes);
				break;
			case 'r':
				STAT_SCALAR("procs_running", "procs_running",
					    inst->mid_procs_running);
				break;
			case 'b':
				STAT_SCALAR("procs_blocked", "procs_blocked",
					    inst->mid_procs_blocked);
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

	if (inst->maxcpu < 1 && cpu_count >= 0)
		inst->maxcpu = cpu_count;

	for (i = 0; i < MAX_CPU_METRICS; i++) {
		rc = __metric_add(sch, array_metric_name[i], LDMS_V_U64_ARRAY,
				  "", inst->maxcpu, &inst->core_pos[i]);
		if (rc)
			return rc;
	}

	return 0;
}

static
int procstat_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	procstat_inst_t inst = (void*)pi;
	int rc = 0;
	char *token;
	char *saveptr = NULL;
	int column_count = 0;
	int cpu_count;
	int err = fseek(inst->mf, 0, SEEK_SET);
	if (err < 0) {
		ldmsd_log(LDMSD_LERROR, "%s: failure seeking /proc/stat.\n",
			  pi->inst_name);
		return errno;
	}

	cpu_count = -1;
	do {
		ssize_t nchar = 0;
		nchar = getline(&inst->line, &inst->line_sz, inst->mf);
		if (nchar < 4)
			break;

#define S_STAT_UNEXPECTED(S) \
	ldmsd_log(LDMSD_LINFO,"%s: unexpected %s in /proc/stat names\n", \
		  pi->inst_name, S)

/* verify name and set value. */
#define GET_STAT_SCALAR(X, pos) \
	if (strcmp(X, ldms_metric_name_get(set, pos))==0) { \
		token = strtok_r(NULL, " \t\n", &saveptr); \
		char *endp = NULL; \
		uint64_t val = strtoull(token,&endp,10); \
		if (endp == token) { \
			ldmsd_log(LDMSD_LINFO,"%s: non-int value " \
			"in line %s in /proc/stat: %s\n", pi->inst_name, \
			X, token); \
		} else { \
			ldms_metric_set_u64(set, pos, val); \
		} \
	} else { \
		ldmsd_log(LDMSD_LERROR, "%s: format changed? " \
			  "in line %s in /proc/stat: %s\n", pi->inst_name, \
			  X, token); \
		break; \
	}

/* Cannot be certain of /proc/stat layout evolution, except
 assume cpu lines will come first. In switch , check this
 for all other cases.
 */
#define S_FINISH_CPUS \
	if (cpu_count < inst->maxcpu) { \
		int errcpu = measure_cpu(inst, &cpu_count, &column_count, \
			NULL, NULL, set); \
		if (errcpu) { \
			rc = errcpu; \
			goto err1; \
		} \
	}

		if (nchar < 4)
			break;
		saveptr = NULL;
		token = strtok_r(inst->line, " \t\n", &saveptr);
		/* First time have to check for corner case NULL  */
		if (token == NULL)
			continue;

		switch (token[0]) {
		case 'c':
			if (0 == strncmp(token, "cpu", 3)) {
				int errcpu = measure_cpu(inst, &cpu_count,
						&column_count, token,
						&saveptr, set);
				if (errcpu) {
					/* log something here? */
					rc = errcpu;
					goto err1;
				}
			} else {
				GET_STAT_SCALAR(CTXT_ALIAS, inst->mid_ctxt);
			}
			break;
		case 'b':
			S_FINISH_CPUS;
			/* ignore btime constant and any other b */
			break;
		case 'i':
			S_FINISH_CPUS;
			GET_STAT_SCALAR(INTR_ALIAS, inst->mid_intr);
			break;
		case 's':
			S_FINISH_CPUS;
			GET_STAT_SCALAR(SOFTIRQ_ALIAS, inst->mid_softirq);
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
				GET_STAT_SCALAR("processes",
						inst->mid_processes);
				break;
			case 'r':
				GET_STAT_SCALAR("procs_running",
						inst->mid_procs_running);
				break;
			case 'b':
				GET_STAT_SCALAR("procs_blocked",
						inst->mid_procs_blocked);
				break;
			default:
				break;
			}
			break;
		default:
			S_FINISH_CPUS;
		}
	} while (1);

	int i;
	uint64_t ncore = 0;
	for (i = 0; i < inst->maxcpu; i++) {
		ncore += ldms_metric_array_get_u64(set, inst->core_pos[0], i);
	}
	ldms_metric_set_u64(set, inst->sum_pos[0], (ncore>0)?1:0);
	ldms_metric_set_u64(set, inst->mid_ncore, ncore);


err1:
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "%s: incomplete sample call.\n",
			  pi->inst_name);
	}
	return rc;
#undef S_STAT_UNEXPECTED
#undef S_FINISH_CPUS
#undef GET_STAT_SCALAR
}


/* ============== Common Plugin APIs ================= */

static
const char *procstat_desc(ldmsd_plugin_inst_t pi)
{
	return "procstat - /proc/stat data sampler";
}

static char *_help = "\
procstat synopsis:\n\
    config name=<INST> [COMMON_OPTIONS] maxcpu=<INT>\n\
\n\
Option descriptions:\n\
    maxcpu       The number of cpus to record. If fewer exist, report 0s; \n\
                 if more ignore them.\n\
";
static
const char *procstat_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
void procstat_del(ldmsd_plugin_inst_t pi)
{
	procstat_inst_t inst = (void*)pi;

	if (inst->mf)
		fclose(inst->mf);
}

static
int procstat_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	procstat_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	const char *value;
	char *endp;
	int rc;
	long long utmp;

	if (inst->mf) {
		snprintf(ebuf, ebufsz, "%s: plugin already loaded.\n",
			 pi->inst_name);
		return EALREADY;
	}

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	value = json_attr_find_str(json, "maxcpu");
	if (value) {
		utmp = strtoull(value, &endp, 0);
		if (endp == value) {
			snprintf(ebuf, ebufsz,
				"%s: config: maxcpu value bad: %s\n",
				pi->inst_name, value);
			return EINVAL;
		}
		inst->maxcpu = utmp;
	}

	inst->mf = fopen(procfile, "r");
	if (!inst->mf) {
		snprintf(ebuf, ebufsz, "%s: cannot open file: %s\n",
			 pi->inst_name, procfile);
		return errno;
	}

	/* create schema + set */
	samp->schema = samp->create_schema(pi);
	if (!samp->schema)
		return errno;
	set = samp->create_set(pi, samp->set_inst_name, samp->schema, NULL);
	if (!set)
		return errno;
	return 0;
}

static
int procstat_init(ldmsd_plugin_inst_t pi)
{
	procstat_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	/* override update_schema() and update_set() */
	samp->update_schema = procstat_update_schema;
	samp->update_set = procstat_update_set;

	inst->maxcpu = -2;
	return 0;
}

static
struct procstat_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "procstat",

                /* Common Plugin APIs */
		.desc   = procstat_desc,
		.help   = procstat_help,
		.init   = procstat_init,
		.del    = procstat_del,
		.config = procstat_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	procstat_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
