/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2022 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2022 Open Grid Computing, Inc. All rights reserved.
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
 * \file procstat2.c
 * /proc/stat data provider utilizing LIST and RECORD.
 *
 * According to \c linux/fs/proc/stat.c from https://github.com/torvalds/linux
 * tags v5.16, v4.20, v3.19, and v2.6.39, \c proc/stat has the following data:
 *
 * - sum cpu time (u64):
 *   - user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice
 * - individual cpu time (u64):
 *   - user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice
 * - intr (u64):
 *   - sum, irq0, irq1, ...
 * - ctxt (u64)
 * - btime (u64)
 * - processes (u64)
 * - procs_running (u64)
 * - procs_blocked (u64)
 * - softirq (u64):
 *   - sum, softirq[0], softirq[1], ...
 *   - the number of softirqs is static (see NR_SOFTIRQS)
 *
 * The \c page, \c swap, \c disk_io that appeared in the \c proc(5) manpage do
 * not appear in the \c linux/fs/proc/stat.c source code.
 *
 * Note about the number of intr
 * -----------------------------
 * The number of intr is dynamic if the kernel has \c CONFIG_SPARSE_IRQ. For a
 * reference, Ubuntu 20.04 and Centos 7.5 both have \c CONFIG_SPARSE_IRQ=y. The
 * number of intr (\c nr_irqs) can be increased by calling \c irq_alloc_descs()
 * (or \c __irq_alloc_descs()).  The max of \c nr_irqs is limited by
 * \c IRQ_BITMAP_BITS (see \c kernel/irq/irqdesc.c:irq_expand_nr_irqs()) which
 * is actually just \c NR_IRQS in the case of \c CONFIG_SPARSE_IRQ=y. The
 * \c NR_IRQS varies by architecture, core count, etc. For example, the
 * \c NR_IRQS for x86 is determined as follows (from
 * \c arch/x86/include/asm/irq_vectors.h):
 * \code
 * #define NR_IRQS_LEGACY                  16
 *
 * #define CPU_VECTOR_LIMIT                (64 * NR_CPUS)
 * #define IO_APIC_VECTOR_LIMIT            (32 * MAX_IO_APICS)
 *
 * #if defined(CONFIG_X86_IO_APIC) && defined(CONFIG_PCI_MSI)
 * #define NR_IRQS                                         \
 *         (CPU_VECTOR_LIMIT > IO_APIC_VECTOR_LIMIT ?      \
 *                 (NR_VECTORS + CPU_VECTOR_LIMIT)  :      \
 *                 (NR_VECTORS + IO_APIC_VECTOR_LIMIT))
 * #elif defined(CONFIG_X86_IO_APIC)
 * #define NR_IRQS                         (NR_VECTORS + IO_APIC_VECTOR_LIMIT)
 * #elif defined(CONFIG_PCI_MSI)
 * #define NR_IRQS                         (NR_VECTORS + CPU_VECTOR_LIMIT)
 * #else
 * #define NR_IRQS                         NR_IRQS_LEGACY
 * #endif
 * \endcode
 *
 *
 * Note about softirq
 * ------------------
 * The number of softirq is static (dictated by \c NR_SOFTIRQS in
 * \c include/linux/interrupt.h). The names of the softirqs are:
 * "HI", "TIMER", "NET_TX", "NET_RX", "BLOCK", "IRQ_POLL",
 * "TASKLET", "SCHED", "HRTIMER", "RCU" (from \c kernel/softirq.c).
 * (Shall we name them as regular metrics or keep them as list?).
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
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include "../sampler_base.h"
#define PROC_FILE "/proc/stat"

static char *procfile = PROC_FILE;
static int collect_intr = 0;
static int collect_softirq = 0;
static ldms_set_t set = NULL;
static ldms_set_t intr_set = NULL;
static ldms_set_t softirq_set = NULL;
static FILE *mf = 0;
#define SAMP "procstat2"
static int metric_offset;
static base_data_t base;
static base_data_t intr_base;
static base_data_t softirq_base;
static size_t incr_heap_sz;

static ovis_log_t mylog;

#ifndef ARRAY_LEN
#define ARRAY_LEN(A) ( sizeof(A) / sizeof(A[0]) )
#endif

static struct ldms_metric_template_s cpu_metrics[] = {
	{       "name",        0,   LDMS_V_CHAR_ARRAY,        "", 8 },
	{       "user",        0,   LDMS_V_U64, "jiffies", 1 },
	{       "nice",        0,   LDMS_V_U64, "jiffies", 1 },
	{     "system",        0,   LDMS_V_U64, "jiffies", 1 },
	{       "idle",        0,   LDMS_V_U64, "jiffies", 1 },
	{     "iowait",        0,   LDMS_V_U64, "jiffies", 1 },
	{        "irq",        0,   LDMS_V_U64, "jiffies", 1 },
	{    "softirq",        0,   LDMS_V_U64, "jiffies", 1 },
	{      "steal",        0,   LDMS_V_U64, "jiffies", 1 },
	{      "guest",        0,   LDMS_V_U64, "jiffies", 1 },
	{ "guest_nice",        0,   LDMS_V_U64, "jiffies", 1 },
	{0},
};
static int cpu_metric_ids[ARRAY_LEN(cpu_metrics)];

/* metric templates for the set schema */
static struct ldms_metric_template_s sch_metrics[] = {
	{       "cpu_rec",        0, LDMS_V_RECORD_TYPE,        "", /* set rec_def later */ },
	{      "cpu_list",        0, LDMS_V_LIST,        "", /* set heap_sz later */ },
//	{     "intr_list",        0, LDMS_V_LIST,        "", /* set heap_sz later */ },
	{          "ctxt",        0, LDMS_V_U64,        "",                       1 },
	{         "btime",        0, LDMS_V_U64, "seconds",                       1 },
	{     "processes",        0, LDMS_V_U64,        "",                       1 },
	{ "procs_running",        0, LDMS_V_U64,        "",                       1 },
	{ "procs_blocked",        0, LDMS_V_U64,        "",                       1 },
//	{  "softirq_list",        0, LDMS_V_LIST,        "", /* set heap_sz later */ },
	{0},
};
static int sch_metric_ids[ARRAY_LEN(sch_metrics)];

enum stat_row {
	STAT_CPU = 1, /* so that sch_metric_ids[STAT_XXX] also works */
	STAT_CTXT,
	STAT_BTIME,
	STAT_PROCESSES,
	STAT_PROCS_RUNNING,
	STAT_PROCS_BLOCKED,
	STAT_INTR,
	STAT_SOFTIRQ,
};


static int intr_max = -1; /* determine from current intr */

/* big lbuf for (potentially) big "intr" line */
#define LBUFSZ 65536
static char lbuf[LBUFSZ];

static int create_metric_sets()
{
	ldms_schema_t core_schema = NULL;
	ldms_schema_t intr_schema = NULL;
	ldms_schema_t softirq_schema = NULL;
	int rc;
	char *s;
	ldms_record_t rec_def;
	int n_cpu;
	size_t sz;
	int nr_irqs;
	int nr_softirqs;

	rec_def = ldms_record_from_template(sch_metrics[0].name, cpu_metrics, cpu_metric_ids);
	if (!rec_def)
		return errno;

	mf = fopen(procfile, "r");
	if (!mf) {
		ovis_log(mylog, OVIS_LERROR, "Could not open the " SAMP " file "
				"'%s'...exiting sampler\n", procfile);
		rc = ENOENT;
		goto err;
	}

	core_schema = base_schema_new(base);
	if (!core_schema) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		rc = errno;
		goto err;
	}

	if (collect_intr) {
		intr_schema = base_schema_new(intr_base);
		if (!intr_schema) {
			ovis_log(mylog, OVIS_LERROR,
				"The schema '%s' could not be created, errno=%d.\n",
				intr_base->schema_name, errno);
			rc = errno;
			goto err;
		}
	}

	if (collect_softirq) {
		softirq_schema = base_schema_new(softirq_base);
		if (!softirq_schema) {
			ovis_log(mylog, OVIS_LERROR,
				"The schema '%s' could not be created, errno=%d\n",
				softirq_base->schema_name, errno);
		}
	}

	/* Location of first metric from proc/stat file */
	metric_offset = ldms_schema_metric_count_get(core_schema);

	/* Get the number of cpu records in /proc/stat */
	n_cpu = 0;
	nr_irqs = 0;
	nr_softirqs = 0;
	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		if (0 == strncmp(s, "cpu", 3)) {
			n_cpu++;
		} else if (0 == strncmp(s, "intr", 4)) {
			for (; *s; s++)
				nr_irqs += (*s == ' ');
		} else if (0 == strncmp(s, "softirq", 7)) {
			for (; *s; s++)
				nr_softirqs += (*s == ' ');
		}
	} while (s);

	if (intr_max < 0) {
		intr_max = nr_irqs;
	} else if (intr_max < nr_irqs) {
		ovis_log(mylog, OVIS_LWARNING,
		       "intr_max(%d) too small (nr_irqs = %d), "
		            "the data will be truncated\n",
		       intr_max, nr_irqs);
	}

	/* "cpu_rec" rec_def */
	sch_metrics[0].rec_def = rec_def;

	/* "cpu_list" heap size */
	sz = ldms_record_heap_size_get(rec_def);
	sch_metrics[1].len = n_cpu * sz;

	incr_heap_sz += ldms_record_heap_size_get(rec_def);

	rc = ldms_schema_metric_add_template(core_schema, sch_metrics, sch_metric_ids);
	if (rc < 0)
		goto err;

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		goto err;
	}

	/* interrupt schema & set */
	if (collect_intr) {
		sz = ldms_list_heap_size_get(LDMS_V_U64, intr_max, 1);
		ldms_schema_metric_list_add(intr_schema, "intr_list", "", sz);
		intr_set = base_set_new(intr_base);
		if (!intr_set) {
			rc = errno;
			goto err;
		}
	}

	if (collect_softirq) {
		sz = ldms_list_heap_size_get(LDMS_V_U64, intr_max, 1);
		ldms_schema_metric_list_add(softirq_schema, "softirq_list", "", sz);
		softirq_set = base_set_new(softirq_base);
		if (!softirq_set) {
			rc = errno;
			goto err;
		}
	}

	return 0;

 err:
	if (mf)
		fclose(mf);
	if (rec_def) {
		ldms_record_delete(rec_def);
		rec_def = NULL;
	}
	if (rc < 0)
		rc = -rc;
	if (set)
		ldms_set_delete(set);
	if (intr_set)
		ldms_set_delete(intr_set);
	if (softirq_set)
		ldms_set_delete(softirq_set);
	mf = NULL;
	return rc;
}

/**
 * check for invalid flags, with particular emphasis on warning the user about
 */
static int config_check(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	char *value;
	int i;

	char* deprecated[]={"set"};

	for (i = 0; i < (sizeof(deprecated)/sizeof(deprecated[0])); i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			ovis_log(mylog, OVIS_LERROR, "config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return	"config name=" SAMP " " BASE_CONFIG_SYNOPSIS
		"       [interrupt=<intr>] [soft_interrupt=<softirq>]\n"
		"       [intr_max=<int>]\n"
		BASE_CONFIG_DESC
		"    intr         <true|false> True to create a set to collect interrupt data. The default is false.\n"
		"    softirq      <true|false> True to create a set to collect soft interrupt data. The default is false.\n"
		"    intr_max     The maximum number of interrupt numbers supported in interrupt and soft interrupt lists.\n"
		"                 If not specified, intr_max will be the current number of\n"
		"                 interrupts in the intr list. The attribute is ignored if\n"
		"                 both 'intr' and 'softirq' are false or not given.\n"
	;
}

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc;
	char *val, *end;

	if (set) {
		ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
		return EINVAL;
	}

	rc = config_check(kwl, avl, NULL);
	if (rc != 0){
		return rc;
	}

	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, mylog);
	if (!base) {
		rc = errno;
		goto err;
	}

	val = av_value(avl, "interrupt");
	if (val && (0 == strcasecmp(val, "true"))) {
		collect_intr = 1;
		intr_base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP"_intr", mylog);
		if (!intr_base) {
			rc = errno;
			goto err;
		}
		/* Rename the instance name of the interrupt set */
		free(intr_base->instance_name);
		rc = asprintf(&intr_base->instance_name,
				"%s_intr", base->instance_name);
		if (rc < 0) {
			intr_base->instance_name = NULL;
			rc = ENOMEM;
			goto err;
		}
	}

	val = av_value(avl, "soft_interrupt");
	if (val && (0 == strcasecmp(val, "true"))) {
		collect_softirq = 1;
		softirq_base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP"_softirq", mylog);
		if (!softirq_base) {
			rc = errno;
			goto err;
		}
		/* Rename the instance name of the soft interrupt set */
		free(softirq_base->instance_name);
		rc = asprintf(&softirq_base->instance_name,
				"%s_softirq", base->instance_name);
		if (rc < 0) {
			softirq_base->instance_name = NULL;
			rc = ENOMEM;
			goto err;
		}
	}

	val = av_value(avl, "intr_max");
	if (val) {
		intr_max = strtol(val, &end, 10);
		if (*end) {
			ovis_log(mylog, OVIS_LERROR, "intr_max must be a decimal number.\n");
			rc = EINVAL;
			goto err;
		}
	}

	rc = create_metric_sets();
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "failed to create a metric set.\n");
		goto err;
	}

	return 0;
 err:
	base_del(base);
	base_del(intr_base);
	base_del(softirq_base);
	return rc;
}

struct stat_row_ent {
	const char *prefix;
	enum stat_row type;
};

struct stat_row_ent stat_row_ents[] = {
	/* sorted by prefix for bsearch */
	{ "btime",         STAT_BTIME         },
	{ "cpu",           STAT_CPU           },
	{ "ctxt",          STAT_CTXT          },
	{ "intr",          STAT_INTR          },
	{ "processes",     STAT_PROCESSES     },
	{ "procs_blocked", STAT_PROCS_BLOCKED },
	{ "procs_running", STAT_PROCS_RUNNING },
	{ "softirq",       STAT_SOFTIRQ       },
};

int stat_row_cmp(const void *key, const void *_ent)
{
	const struct stat_row_ent *ent = _ent;
	return strncmp(key, ent->prefix, strlen(ent->prefix));
}

static ldms_set_t __resize_set(base_data_t b, size_t incr)
{
	int rc;
	ldms_set_t s;
	size_t heap_sz = ldms_set_heap_size_get(b->set) + incr;
	base_set_delete(b);
	s = base_set_new_heap(b, heap_sz);
	if (!s) {
		rc = errno;
		ovis_log(mylog, OVIS_LCRITICAL, "Failed to create set '%s' "
				"with a bigger heap. Error %d.\n",
				ldms_set_instance_name_get(b->set),
				rc);
	}
	return s;
}

static int sample(ldmsd_plug_handle_t handle)
{
	int i, rc;
	char tok[128];
	int n;
	struct stat_row_ent *ent;
	uint64_t u64, data[16];
	ldms_mval_t cpu_rec, cpu_list, mval, lh, nxt_mval;
	size_t heap_sz;

	if (!set) {
		ovis_log(mylog, OVIS_LDEBUG, "plugin not initialized\n");
		return EINVAL;
	}

	base_sample_begin(base);
	if (collect_intr)
		base_sample_begin(intr_base);
	if (collect_softirq)
		base_sample_begin(softirq_base);

begin:
	cpu_list = ldms_metric_get(set, sch_metric_ids[STAT_CPU]);
	assert(cpu_list >= 0);
	cpu_rec = ldms_list_first(set, cpu_list, NULL, NULL);
	fseek(mf, 0, SEEK_SET);
	while (1 == fscanf(mf, "%s", tok)) {
		ent = bsearch(tok, stat_row_ents, ARRAY_LEN(stat_row_ents),
				sizeof(stat_row_ents[0]), stat_row_cmp);
		if (!ent) {
			rc = ENOENT;
			ovis_log(mylog, OVIS_LDEBUG, "unknown key: %s\n", tok);
			goto out;
		}
		switch (ent->type) {
		case STAT_CPU:
			if (!cpu_rec) {
				cpu_rec = ldms_record_alloc(set, sch_metric_ids[0]);
				if (!cpu_rec) {
					set = __resize_set(base, 2 * incr_heap_sz);
					if (!set) {
						rc = errno;
						goto out;
					}
					goto begin;
				}
				ldms_list_append_record(set, cpu_list, cpu_rec);
			}
			n = fscanf(mf,  " %"PRIu64 " %"PRIu64 " %"PRIu64
					" %"PRIu64 " %"PRIu64 " %"PRIu64
					" %"PRIu64 " %"PRIu64 " %"PRIu64
					" %"PRIu64,
				&data[0], &data[1], &data[2], &data[3],
				&data[4], &data[5], &data[6], &data[7],
				&data[8], &data[9]);
			if (n != 10) {
				rc = EINVAL;
				goto out;
			}
			/* cpu name */
			mval = ldms_record_metric_get(cpu_rec, cpu_metric_ids[0]);
			snprintf(mval->a_char, 8, "%.7s", tok);
			/* cpu stats */
			for (i = 0; i < 10; i++) {
				ldms_record_set_u64(cpu_rec, cpu_metric_ids[i+1], data[i]);
			}
			cpu_rec = ldms_list_next(set, cpu_rec, NULL, NULL);
			break;
		case STAT_INTR:
			/* interrupt set */
			if (!collect_intr) {
				/* Read the whole interrupt line */
				while (1 == fscanf(mf, "%"PRIu64, &u64))
					continue;
				/* do nothing */
				break;
			}
			lh = ldms_metric_get(intr_set, metric_offset);
			mval = ldms_list_first(intr_set, lh, NULL, NULL);
			while (1 == fscanf(mf, "%"PRIu64, &u64)) {
				if (!mval) {
					mval = ldms_list_append_item(intr_set, lh, LDMS_V_U64, 1);
					if (!mval) {
						heap_sz = ldms_list_heap_size_get(LDMS_V_U64, 2, 1);
						intr_set = __resize_set(intr_base, heap_sz);
						if (!intr_set) {
							rc = errno;
							goto out;
						}
						goto begin;
					}
				}
				mval->v_u64 = htole64(u64);
				mval = ldms_list_next(intr_set, mval, NULL, NULL);
			}
			/* Remove the excess elements */
			while (mval) {
				nxt_mval = ldms_list_next(intr_set, mval, NULL, NULL);
				ldms_list_remove_item(intr_set, lh, mval);
				mval = nxt_mval;
			}
			break;
		case STAT_SOFTIRQ:
			/* soft interrupt set */
			/* Read the whole soft interrupt line. */
			n = fscanf(mf,  " %"PRIu64 " %"PRIu64 " %"PRIu64
					" %"PRIu64 " %"PRIu64 " %"PRIu64
					" %"PRIu64 " %"PRIu64 " %"PRIu64
					" %"PRIu64 " %"PRIu64,
				&data[0], &data[1], &data[2], &data[3],
				&data[4], &data[5], &data[6], &data[7],
				&data[8], &data[9], &data[10]);
			if (!collect_softirq) {
				/* Do nothing */
				break;
			}
			lh = ldms_metric_get(softirq_set, metric_offset);
			mval = ldms_list_first(softirq_set, lh, NULL, NULL);
			if (n != 11) {
				rc = EINVAL;
				goto out;
			}
			for (i = 0; i < n; i++) {
				if (!mval) {
					mval = ldms_list_append_item(softirq_set, lh, LDMS_V_U64, 1);
					if (!mval) {
						heap_sz = ldms_list_heap_size_get(LDMS_V_U64, 2, 1);
						softirq_set = __resize_set(softirq_base, heap_sz);
						if (!softirq_set) {
							rc = errno;
							goto out;
						}
						goto begin;
					}
				}
				mval->v_u64 = htole64(data[i]);
				mval = ldms_list_next(softirq_set, mval, NULL, NULL);
			}
			break;
		case STAT_CTXT:
		case STAT_BTIME:
		case STAT_PROCESSES:
		case STAT_PROCS_RUNNING:
		case STAT_PROCS_BLOCKED:
			n = fscanf(mf, "%"PRIu64 , &u64);
			if (n != 1) {
				rc = ENODATA;
				goto out;
			}
			ldms_metric_set_u64(set, sch_metric_ids[ent->type], u64);
			break;
		default:
			rc = EINVAL;
			goto out;
		}
	}
	rc = 0;
 out:
	base_sample_end(base);
	if (collect_intr)
		base_sample_end(intr_base);
	if (collect_softirq)
		base_sample_end(softirq_base);
	return rc;
}

static void term(ldmsd_plug_handle_t handle)
{
	if (mf) {
		fclose(mf);
		mf = NULL;
	}
	if (base) {
		base_del(base);
		base = NULL;
	}
	if (intr_base) {
		base_del(intr_base);
		intr_base = NULL;
	}
	if (softirq_base) {
		base_del(softirq_base);
		softirq_base = NULL;
	}
	if (set) {
		ldms_set_delete(set);
		set = NULL;
	}
	if (intr_set) {
		ldms_set_delete(intr_set);
		intr_set = NULL;
	}
	if (softirq_set) {
		ldms_set_delete(softirq_set);
		softirq_set = NULL;
	}
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);
	set = NULL;

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.type = LDMSD_PLUGIN_SAMPLER,
		.config = config,
		.usage = usage,
		.term = term,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};
