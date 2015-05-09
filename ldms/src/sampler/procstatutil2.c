/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
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
/**
 * \file procstatutil.c
 * \brief /proc/stat/util data provider
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

static const size_t PROCSTATUTIL_NAME_MAX=256;
static const int NCORESUFFIX = 3; //used for getting sum names from per core names (#%d)

static
struct sampler_data {
	bool inumbered; // bool: collect numbered interrupts or not from /proc/stat
	bool isoftnumbered; // bool: collect numbered soft interrupts or not from /proc/stat
	int maxcpu; // include space for this many cpus in metric set
	ldms_set_t set;
	FILE *mf;	// /proc/stat file pointer
	char *line;
	size_t line_sz;
	ldms_metric_t *metric_table;
	ldmsd_msg_log_f msglog;
	int warn_col_count;
	int warn_row_count;

	uint64_t comp_id;

#undef CHECK_PROCSTATUTIL_TIMING
#ifdef CHECK_PROCSTATUTIL_TIMING
	//Some temporary for testing
	ldms_metric_t tv_sec_metric_handle2;
	ldms_metric_t tv_nsec_metric_handle2;
	ldms_metric_t tv_dnsec_metric_handle;
#endif

} g = { 
	.maxcpu = -2, // note: -2, not -1 for count_cpu to work right.
	.line = NULL,
	.line_sz = 0,
	.warn_col_count = 0,
	.warn_row_count = 0,
	.comp_id = UINT64_MAX,
};
// global instance, to become per instance later.

// scratch list element. v3 schemas will do more, but this
// give the minimum to stay sane for now in create_set
struct metric_tmp {
	char *name;
	enum ldms_value_type type;
	struct metric_tmp *next;
};
struct metric_tmp_list {
	struct metric_tmp *head;
	struct metric_tmp *tail;
	size_t size;
};

static
int mtl_append(struct metric_tmp_list *l, const char *name,
		 enum ldms_value_type type, size_t *meta_sz, size_t *data_sz)
{
	if (!l || !name || !meta_sz || !data_sz) {
		return EINVAL;
	}
	char *n = strdup(name);
	if (!n) goto err;
	struct metric_tmp *mt = malloc(sizeof(struct metric_tmp));
	if (!mt) goto err;
	g.msglog(LDMS_LDEBUG,"mtlappend: %s, %"PRIu64"\n",name, type);
	ldms_get_metric_size(name, LDMS_V_U64, meta_sz, data_sz);
	mt->name = n;
	mt->type = type;
	mt->next = NULL;
	if (!l->head) {
		l->head = mt;
		l->tail = mt;
	} else {
		l->tail->next = mt;
		l->tail = mt;
	}
	l->size++;
	return 0;
err:
	if (n) free(n);
	if (mt) free(mt);
	return ENOMEM;
}

void mtl_destroy(struct metric_tmp_list *l, int freetop) {
	if (!l || !(l->head))
		return;
	while (l->head) {
		struct metric_tmp *t = l->head;
		l->head = t->next;
		free(t->name);
		free(t);
	}
	if (freetop)
		free(l);
}

static ldms_set_t get_set()
{
	return g.set;
}

/*
 * Depending on the kernel version, not all of the rows will
 * necessarily be used. Since new columns are added at the end of the
 * line, this should work across all kernels up to 3.5.
 */
static const char *default_metric_name_fmt[] = {
	"cpu_enabled#%d",
	"user#%d",
	"nice#%d",
	"sys#%d",
	"idle#%d",
	"iowait#%d",
	"irq#%d",
	"softirq#%d",
	"steal#%d",
	"guest#%d",
	"guest_nice#%d",
};

static const size_t MAX_CPU_METRICS=sizeof(default_metric_name_fmt)/sizeof(char *);

/* records data, accounting for vagaries of cpu reporting
in /proc/stat.
On initial call of a sample, cpu_count, should have -1
and it will be the size of the total stats row after that.
*/
int measure_cpu( int *cpu_count, int *metric_count, int *column_count,
	char *token, char **saveptr, char* metric_name) {

	int column = 0;
	int rc = 0;
	long curcpu = -1;
	if (*cpu_count == g.maxcpu) {
		// ignore the rest.
		return 0;
	}
	if (*cpu_count > -1) {
		if (token) {
			char *tmp = token+3; 
			char *end = NULL;
			curcpu = strtol(tmp,&end,10);
			if (end == tmp) {
				return EINVAL;
			}
		} else {
			curcpu = g.maxcpu;
		}
		while (curcpu > (*cpu_count)) {
			// we fill a downed cpu line per the prior cpu line.
			for (column = 0; column < *column_count; column++) {
				// offline/no values
				ldms_set_u64(g.metric_table[*metric_count], 0);
				(*metric_count)++;
			}
			(*cpu_count)++;
		}
	}
	if (!token) {
		return 0;
	}
	column = 0;
	column++;
	ldms_set_u64(g.metric_table[*metric_count], 1); // online
	(*metric_count)++;
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
		ldms_set_u64(g.metric_table[*metric_count], val);
		(*metric_count)++;
	}
	if ( (*column_count) > 0 && column < (*column_count)) {
		rc = 1;
		g.msglog(LDMS_LERROR,
			"procstatutil2: short read in cpu row %d\n",
			*cpu_count);
	}
	*column_count = column;
	(*cpu_count)++;
	return rc;
}

/* counts up data and metadata, accounting for vagaries of cpu reporting
in /proc/stat.
result is undefined if token!='cpu*' (where * is 0 or more digits).
on normal exit, cpu_count has the number of cores so far added.
On initial call, cpu_count, should have -1.
*/
int count_cpu( struct metric_tmp_list *lp,
	int *cpu_count, int *metric_count, int *column_count,
	char *token, char **saveptr, char* metric_name,
	size_t *total_meta_sz, size_t *total_data_sz) {

	int column = 0;
	int rc = 0;
	size_t meta_sz, data_sz;
	long curcpu = -1;
	if (*cpu_count == g.maxcpu) {
		if ( g.warn_row_count < 2) {
			g.msglog(LDMS_LINFO,
				"procstatutil2: unlogged cpu row! max %d\n",
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
				g.msglog(LDMS_LERROR,
				"procstatutil2: extra unnumbered cpu row!\n");
				return EINVAL;
			}
		} else {
			curcpu = g.maxcpu;
		}
		while (curcpu > (*cpu_count)) {
			// we fill a downed cpu line per the prior cpu line.
			for (column = 0; column < *column_count; column++) {
				snprintf(metric_name, PROCSTATUTIL_NAME_MAX,
					default_metric_name_fmt[column],
					*cpu_count);
				rc = mtl_append(lp, metric_name, LDMS_V_U64,
					&meta_sz,&data_sz);
				if (rc)
					return rc;

				(*total_meta_sz) += meta_sz;
				(*total_data_sz) += data_sz;
				(*metric_count)++;
			}
			(*cpu_count)++;
		}
	}
	if (!token) {
		return 0;
	}
	column = 0;
	if (*cpu_count == -1) {
		// overall cpu is always enabled; trim # suffix.
		size_t itmp = strlen(default_metric_name_fmt[column]) 
			- NCORESUFFIX + 1;
		int len = itmp < PROCSTATUTIL_NAME_MAX ?
			itmp : PROCSTATUTIL_NAME_MAX;
		snprintf(metric_name, len, default_metric_name_fmt[column]);
	} else {
		snprintf(metric_name, PROCSTATUTIL_NAME_MAX,
			 default_metric_name_fmt[column], *cpu_count);
	}
	column++;
	rc = mtl_append(lp, metric_name, LDMS_V_U64, &meta_sz, &data_sz);
	if (rc)
		return rc;
	(*total_meta_sz) += meta_sz;
	(*total_data_sz) += data_sz;
	(*metric_count)++;
	for (token = strtok_r(NULL, " \t\n", saveptr);
		token;
		token = strtok_r(NULL, " \t\n", saveptr)) {

		if (column >= MAX_CPU_METRICS) {
			if (g.warn_col_count < 3) {
				g.msglog(LDMS_LERROR,
				"procstatutil2: extra cpu metric found\n");
				g.warn_col_count++;
			}
			break;
		}
		if (*cpu_count == -1) {
			// overall cpu is always enabled; trim # suffix.
			size_t itmp = strlen(default_metric_name_fmt[column]) 
				- NCORESUFFIX + 1;
			int len = itmp < PROCSTATUTIL_NAME_MAX ?
				itmp : PROCSTATUTIL_NAME_MAX;
			snprintf(metric_name, len,
				default_metric_name_fmt[column]);
		} else {
			snprintf(metric_name, PROCSTATUTIL_NAME_MAX,
				 default_metric_name_fmt[column], *cpu_count);
		}
		column++;
		rc = mtl_append(lp, metric_name, LDMS_V_U64, &meta_sz,
			&data_sz);
		if (rc)
			return rc;

		(*total_meta_sz) += meta_sz;
		(*total_data_sz) += data_sz;
		(*metric_count)++;
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
static int create_metric_set(const char *path)
{
	size_t meta_sz, total_meta_sz;
	size_t data_sz, total_data_sz;
	int rc;
	int metric_count;
	int column_count = 0;
	int cpu_count;
	char metric_name[PROCSTATUTIL_NAME_MAX];
	char *saveptr;
	struct metric_tmp_list mtl = { NULL,NULL,0 };

	g.mf = fopen("/proc/stat", "r");
	if (!g.mf) {
		g.msglog(LDMS_LERROR,"Could not open the /proc/stat file.\n");
		return ENOENT;
	}
	total_meta_sz = 0;
	total_data_sz = 0;

	fseek(g.mf, 0, SEEK_SET);

	metric_count = 0;
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
	g.msglog(LDMS_LINFO,"procstatutil2: unexpected %s in /proc/stat names\n",S)

#define STAT_SCALAR(X,Y) \
if (strcmp(token,X)==0) { \
	token = strtok_r(NULL, " \t\n", &saveptr); \
	rc = mtl_append(&mtl, Y, LDMS_V_U64, &meta_sz, &data_sz); \
	if (rc) { \
		g.msglog(LDMS_LERROR,"procstatutil2.c: add " X \
			" failed (finish).\n"); \
		goto err1; \
	} \
	total_meta_sz += meta_sz; \
	total_data_sz += data_sz; \
	metric_count++; \
} else \
	STAT_UNEXPECTED(token)

// cannot be certain of /proc/stat layout evolution, except
// assume cpu lines will come first. so in switch , check this
// for all other cases.
#define FINISH_CPUS \
	if (cpu_count < g.maxcpu) { \
		int errcpu = count_cpu(&mtl,&cpu_count,&metric_count, \
			&column_count, NULL, NULL, \
			metric_name, \
			&total_meta_sz, &total_data_sz); \
		if (errcpu) { \
			g.msglog(LDMS_LERROR,"procstatutil2.c: count_cpu" \
				" failed (finish).\n"); \
			rc = errcpu; \
			goto err1; \
		} \
	}

		switch (token[0]) {
		case 'c':
			if (0 == strncmp(token, "cpu", 3)) {
				int errcpu = count_cpu(&mtl,&cpu_count,&metric_count,
					&column_count, token, &saveptr,
					 metric_name,
					 &total_meta_sz, &total_data_sz);
				if (errcpu) {
					// log something here?
					rc = errcpu;
					goto err1;
				}
			} else {
				STAT_SCALAR("ctxt",CTXT_ALIAS);
			}
			break;
		case 'b':
			FINISH_CPUS;
			// ignore btime constant
			if (strcmp(token,"btime")!=0) {
				STAT_UNEXPECTED("btime");
			}
			break;
		case 'i':
			FINISH_CPUS;
			STAT_SCALAR("intr",INTR_ALIAS);
			break;
		case 's':
			FINISH_CPUS;
			STAT_SCALAR("softirq",SOFTIRQ_ALIAS);
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
				STAT_SCALAR("processes","processes");
				break;
			case 'r':
				STAT_SCALAR("procs_running","procs_running");
				break;
			case 'b':
				STAT_SCALAR("procs_blocked","procs_blocked");
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

#ifdef CHECK_PROCSTATUTIL_TIMING
	rc = mtl_append(&mtl,"procstatutil_tv_sec2", LDMS_V_U64,
			 &meta_sz, &data_sz);
	if (rc) goto err1;
	total_meta_sz += meta_sz;
	total_data_sz += data_sz;

	rc = mtl_append(&mtl,"procstatutil_tv_nsec2", LDMS_V_U64,
			 &meta_sz, &data_sz);

	if (rc) goto err1;
	total_meta_sz += meta_sz;
	total_data_sz += data_sz;

	rc = mtl_append(&mtl,"procstatutil_tv_dnsec2", LDMS_V_U64,
			&meta_sz, &data_sz);
	if (rc) goto err1;
	total_meta_sz += meta_sz;
	total_data_sz += data_sz;
	metric_count += 3;
#endif

	if (metric_count != mtl.size) {
		g.msglog(LDMS_LERROR,"procstatutil2: metric_count bogus \n");
		rc = EINVAL;
		goto err1;
	}
	/* Create a metric set of the required size */
	rc = ldms_create_set(path, total_meta_sz, total_data_sz, &g.set);
	if (rc) {
		g.msglog(LDMS_LERROR,"procstatutil2.c: ldms_create_set"
			" failed.\n");
		goto err1;
	}

	rc = ENOMEM;

	g.metric_table = calloc(metric_count, sizeof(ldms_metric_t));
	if (!g.metric_table) {
		g.msglog(LDMS_LERROR,"procstatutil2.c: alloc metric_table"
			" failed.\n");
		goto err;
	}

	ldms_metric_t m;
	int metric_no = 0;

	struct metric_tmp *mt = mtl.head;
	for ( ; mt != NULL; mt = mt->next) {
		g.msglog(LDMS_LDEBUG,"admet: %s, %"PRIu64"\n",mt->name, mt->type);
		m = ldms_add_metric(g.set, mt->name, mt->type);
		if (!m) {
			g.msglog(LDMS_LERROR,"procstatutil2.c: ldms_add_metric"
				" failed.\n");
			rc = errno;
			goto err;
		}
		ldms_set_user_data(m, g.comp_id);
		g.metric_table[metric_no++] = m;
	}

#ifdef CHECK_PROCSTATUTIL_TIMING
	g.tv_sec_metric_handle2 = g.metric_table[metric_no-3];
	g.tv_nsec_metric_handle2 = g.metric_table[metric_no-2];
	g.tv_dnsec_metric_handle = g.metric_table[metric_no-1];
#endif
	mtl_destroy(&mtl,0);
	return 0;
 err:
	ldms_destroy_set(g.set);
 err1:
	mtl_destroy(&mtl,0);
	if (g.mf)
		fclose(g.mf);
	return rc ;
#undef STAT_UNEXPECTED
#undef STAT_SCALAR
#undef FINISH_CPUS

}

/**
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value, *endp = NULL;
	int rc = EINVAL;
	uint64_t utmp;

	value = av_value(avl, "component_id");
	if (!value) {
		g.msglog(LDMS_LERROR,"procstatutil2: config: component_id value required\n");
		goto out;
	}
	utmp = strtoull(value, &endp, 0);
	if (endp == value) {
		g.msglog(LDMS_LDEBUG,"procstatutil2: config: component_id value bad: %s\n",value);
		goto out;
	}
	g.comp_id = utmp;

	value = av_value(avl,"metrics_type");
	if (value) {
		g.msglog(LDMS_LERROR,"procstatutil2: config: metrics_type obsolete\n");
	}

	value = av_value(avl, "maxcpu");
	if (value) {
		utmp = strtoull(value, &endp, 0);
		if (endp == value) {
			g.msglog(LDMS_LDEBUG,"procstatutil2: config: maxcpu value bad: %s\n",value);
			goto out;
		}
		g.maxcpu = utmp;
	}

	value = av_value(avl, "set");
	if (!value) {
		g.msglog(LDMS_LERROR,"procstatutil2: config: set value required\n");
		goto out;
	}
	rc = create_metric_set(value);
	if (rc) {
		g.msglog(LDMS_LERROR,"procstatutil2 create_metric_set err %d\n",
		rc);
	}

 out:
	return rc;
}

static int sample(void)
{
	int metric_no;
	int rc = 0;
	struct timespec time1;
	char *saveptr = NULL;
	int column_count = 0;
	int cpu_count;
	char metric_name[PROCSTATUTIL_NAME_MAX]; // used per-row for cpu match

#ifdef CHECK_PROCSTATUTIL_TIMING
	uint64_t beg_nsec; //testing
	union ldms_value vv;
#endif

	//	if (!set || !compid_metric_handle ){
	if (!g.set ){
		g.msglog(LDMS_LDEBUG,"procstatutil2: plugin not initialized\n");
		return EINVAL;
	}
	ldms_begin_transaction(g.set);

	clock_gettime(CLOCK_REALTIME, &time1);
#ifdef CHECK_PROCSTATUTIL_TIMING
	beg_nsec = time1.tv_nsec;
#endif


	fseek(g.mf, 0, SEEK_SET);
	metric_no = 0;
	cpu_count = -1;
	do {
		ssize_t nchar = 0;
		nchar = getline(&g.line, &g.line_sz, g.mf);
		if (nchar < 4)
			break;

	// fixme: debug
#define S_STAT_UNEXPECTED(S) \
	g.msglog(LDMS_LINFO,"procstatutil2: unexpected %s in /proc/stat names\n",S)

/* would like to get the check out of here for performance
if we can be sure cpu row count is always handled right.
*/
#define GET_STAT_SCALAR(X) \
	if (strcmp(X,((struct ldms_metric *)(g.metric_table[metric_no]))->desc->name)==0) { \
		token = strtok_r(NULL, " \t\n", &saveptr); \
		char *endp = NULL; \
		uint64_t val = strtoull(token,&endp,10); \
		if (endp == token) { \
			g.msglog(LDMS_LINFO,"procstatutil2: non-int value " \
			"in line %s in /proc/stat: %s\n", X, token); \
		} else { \
			ldms_set_u64(g.metric_table[metric_no], val); \
			metric_no++; \
		} \
	} else { \
		g.msglog(LDMS_LERROR,"procstatutil2: format changed? " \
		"in line %s in /proc/stat: %s\n", X, token); \
		break; \
	}

// cannot be certain of /proc/stat layout evolution, except
// assume cpu lines will come first. so in switch , check this
// for all other cases.
#define S_FINISH_CPUS \
	if (cpu_count < g.maxcpu) { \
		int errcpu = measure_cpu(&cpu_count,&metric_no, \
			&column_count, NULL, NULL, metric_name ); \
		if (errcpu) { \
			rc = errcpu; \
			goto err1; \
		} \
	}

		char *token;
		if ( nchar <4)
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
					&metric_no, &column_count,
					 token, &saveptr,
					 metric_name);
				if (errcpu) {
					// log something here?
					rc = errcpu;
					goto err1;
				}
			} else {
				GET_STAT_SCALAR(CTXT_ALIAS);
			}
			break;
		case 'b':
			S_FINISH_CPUS;
			// ignore btime constant and any other b
			break;
		case 'i':
			S_FINISH_CPUS;
			GET_STAT_SCALAR(INTR_ALIAS);
			break;
		case 's':
			S_FINISH_CPUS;
			GET_STAT_SCALAR(SOFTIRQ_ALIAS);
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
				GET_STAT_SCALAR("processes");
				break;
			case 'r':
				GET_STAT_SCALAR("procs_running");
				break;
			case 'b':
				GET_STAT_SCALAR("procs_blocked");
				break;
			default:
				break;
			}
			break;
		default:
			S_FINISH_CPUS;
		}
#if old
		for (token = strtok_r(NULL, " \t\n", &saveptr); token;
				token = strtok_r(NULL, " \t\n", &saveptr)) {
			uint64_t v = strtoul(token, NULL, 0);
			ldms_set_u64(g.metric_table[metric_no], v);
			metric_no++;
		}
#endif
	} while (1);

err1:
	if (rc) {
		g.msglog(LDMS_LERROR,
			"procstatutil2: incomplete sample call.\n");
	}
#ifdef CHECK_PROCSTATUTIL_TIMING
	clock_gettime(CLOCK_REALTIME, &time1);
	vv.v_u64 = time1.tv_sec;
	ldms_set_metric(g.tv_sec_metric_handle2, &vv);
	vv.v_u64 = time1.tv_nsec;
	ldms_set_metric(g.tv_nsec_metric_handle2, &vv);
	vv.v_u64 = time1.tv_nsec - beg_nsec;
	ldms_set_metric(g.tv_dnsec_metric_handle, &vv);
#endif
	ldms_end_transaction(g.set);
	return rc;
#undef S_STAT_UNEXPECTED
#undef S_FINISH_CPUS
#undef GET_STAT_SCALAR
}

static void term(void)
{
	if (g.set)
		ldms_destroy_set(g.set);
	free(g.metric_table);
	free(g.line);
	g.set = NULL;
	g.metric_table = NULL;
	g.line = NULL;
}


static const char *usage(void)
{
	return  "config name=procstatutil2 component_id=<comp_id> set=<setname> maxcpu=<ncpu>\n"
		"    comp_id     The component id value\n"
		"    setname     The set name\n"
		"    maxcpu      The number of cpus to record.\n"
		"                If fewer exist, report 0s; if more ignore.\n"
	;
}

static struct ldmsd_sampler procstatutil_plugin = {
	.base = {
		.name = "procstatutil2",
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
	return &procstatutil_plugin.base;
}

