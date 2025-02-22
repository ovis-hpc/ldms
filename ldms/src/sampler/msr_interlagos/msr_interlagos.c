/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015-2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file msr_interlagos.c
 * \brief msr data provider for interlagos only
 *
 * Sets/checks/ and reads msr counters for interlagos only
 *
 */
#define _XOPEN_SOURCE 500
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <math.h>
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

#ifndef __linux__ // modern linux provides either bitness in asm/unistd.h as needed
#if defined(__i386__)
#include "/usr/include/asm/unistd.h"
#endif

#if defined(__x86_64__)
#include "/usr/include/asm-x86_64/unistd.h"
#endif
#else // __linux__
#include <asm/unistd.h>
#endif // __linux__


#define MSR_MAXLEN 20LL
#define MSR_HOST 0LL
#define MSR_CNT_MASK 0LL
#define MSR_INV 0LL
#define MSR_EDGE 0LL
#define MSR_ENABLE 1LL
#define MSR_INTT 0LL
#define MSR_TOOMANYMAX 100LL
#define MSR_CONFIGLINE_MAX 1024

typedef enum{CTR_OK, CTR_HALTED, CTR_BROKEN} ctr_state;
typedef enum{CFG_PRE, CFG_DONE_INIT, CFG_IN_FINAL, CFG_FAILED_FINAL, CFG_DONE_FINAL} ctrcfg_state;
typedef enum{CTR_UNCORE, CTR_NUMCORE} ctr_num_values;
typedef enum{MSR_DEFAULT, UNCORE_PER_NUMA} msr_special_cases;

static pthread_mutex_t cfglock;

struct active_counter{
	struct MSRcounter* mctr;
	uint64_t* wctl; //updated: 8/22 size of maxcore (max including padded values)
	int valid; //this is kept track of, but currently unused
	ctr_state state;
	int metric_ctl; // updated: 8/22 array is size of nctl FIXME: will this make everything to hard to parse? Different num vals.
	int metric_ctr; //array is size of ndata
	int metric_name;
	int ndata;
	int nctl; //ndata or 1, as there are unique values
	uint64_t* data;
	pthread_mutex_t lock;
	TAILQ_ENTRY(active_counter) entry;
};

TAILQ_HEAD(, active_counter) counter_list;

struct MSRcounter{
	char* name;
	uint64_t w_reg;
	uint64_t event;
	uint64_t umask;
	uint64_t r_reg;
	uint64_t os_user;
	uint64_t int_core_ena;
	uint64_t int_core_sel;
	msr_special_cases core_flag; // changed on 8/22/16 to handle the more complex uncore case
	ctr_num_values numvalues_type;
	int numcore; /* max legit core vals will consider (was numvals) */
	int offset; /* offsets for core and uncore counters */
	int maxcore; /* allows for extra zeros */
};

/*
Note 1: These now come from a config file which can be modified to change
metric names, what registers are used for them, and what is counted by them.
These names are used during plugin configuration in the selection of what
will be sampled by a particular msr sampler plugin.

Note 2: In the plugin configuration, choosing a second "Name" that re-uses
a register (Write_addr) will fail.

Fields are:
Name, Write_addr, Event, Umask, Read_addr, os_user, core_ena, core_sel, core_flag, ctr_type
##### Core counters ##########
TLB_DM,  0xc0010200, 0x046, 0x07, 0xc0010201, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
TOT_CYC, 0xc0010202, 0x076, 0x00, 0xc0010203, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
L2_DCM,  0xc0010202, 0x043, 0x00, 0xc0010203, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
L1_DCM,  0xc0010204, 0x041, 0x01, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
L1_DCA,  0xc0010204, 0x040, 0x00, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
#LS_DISP,  0xc0010204, 0x029, 0x01, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
#LS_DISP,  0xc0010204, 0x029, 0x02, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
#LS_DISP,  0xc0010204, 0x029, 0x04, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
LS_DISP,  0xc0010204, 0x029, 0x07, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
RETIRED_FLOPS,  0xc0010206, 0x003, 0xFF, 0xc0010207, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
DP_OPS,  0xc0010206, 0x003, 0xF0, 0xc0010207, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
VEC_INS, 0xc0010208, 0x0CB, 0x04, 0xc0010209, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
TOT_INS, 0xc001020A, 0x0C0, 0x00, 0xc001020B, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
##### Uncore counters ##########
L3_CACHE_MISSES, 0xc0010240, 0x4E1, 0xF7, 0xc0010241, 0x0, 0x1, 0x0, MSR_DEFAULT, CTR_UNCORE
RW_DRAM_EXT, 0xc0010242, 0x1E0, 0xF, 0xc0010243, 0x0, 0x1, 0x0, UNCORE_PER_NUMA, CTR_UNCORE
IO_DRAM_INT, 0xc0010242, 0x1E1, 0x0, 0xc0010243, 0x0, 0x1, 0x0, UNCORE_PER_NUMA, CTR_UNCORE
DCT_PREFETCH, 0xc0010242, 0x1F0, 0x64, 0xc0010243, 0x0, 0x1, 0x0, MSR_DEFAULT, CTR_UNCORE
DCT_RD_TOT, 0xc0010244, 0x1F0, 0x62, 0xc0010245, 0x0, 0x1, 0x0, MSR_DEFAULT, CTR_UNCORE
RW_DRAM_INT, 0xc0010246, 0x1E0, 0x0, 0xc0010247, 0x0, 0x1, 0x0, UNCORE_PER_NUMA, CTR_UNCORE
IO_DRAM_EXT, 0xc0010246, 0x1E1, 0xF, 0xc0010247, 0x0, 0x1, 0x0, UNCORE_PER_NUMA, CTR_UNCORE
DCT_WRT, 0xc0010246, 0x1F0, 0x19, 0xc0010247, 0x0, 0x1, 0x0, MSR_DEFAULT, CTR_UNCORE
#
# Note that for the following, CTR_NUMCORE pairs are:
# [0] Control: 0xc0010200 Data: 0xc0010201
# [1] Control: 0xc0010202 Data: 0xc0010203
# [2] Control: 0xc0010204 Data: 0xc0010205
# [3] Control: 0xc0010206 Data: 0xc0010207
# [4] Control: 0xc0010208 Data: 0xc0010209
# [5] Control: 0xc001020A Data: 0xc001020B
# And CTR_UNCORE pairs are:
# [0] Control: 0xc0010240 Data: 0xc0010241
# [1] Control: 0xc0010242 Data: 0xc0010243
# [2] Control: 0xc0010244 Data: 0xc0010245
# [3] Control: 0xc0010246 Data: 0xc0010247
#
The first column below indicates the counters available for a particular
feature. For example [2:0] indicates that the core counters (CTR_NUMCORE)
0, 1, and 2, as indicated above, are available to count TLB_DM.

NOTE: For the UNCORE_PER_NUMA case, use 0x0 to exclude external numa access
and 0xF to exclude local numa access and only count external access.
##### Core counters ##########
#[2:0] TLB_DM,  0xc0010200, 0x046, 0x07, 0xc0010201, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
#[2:0] TOT_CYC, 0xc0010202, 0x076, 0x00, 0xc0010203, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
#[2:0] L2_DCM,  0xc0010202, 0x043, 0x00, 0xc0010203, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
#[5:0] L1_DCM,  0xc0010204, 0x041, 0x01, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
#[5:0] L1_DCA,  0xc0010204, 0x040, 0x00, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
#[5:0] LS_DISP,  0xc0010204, 0x029, 0x01, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
#[5:0] LS_DISP,  0xc0010204, 0x029, 0x02, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
#[5:0] LS_DISP,  0xc0010204, 0x029, 0x04, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
#[5:0] LS_DISP,  0xc0010204, 0x029, 0x07, 0xc0010205, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
#[3] RETIRED_FLOPS,  0xc0010206, 0x003, 0xFF, 0xc0010207, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
#[3] DP_OPS,  0xc0010206, 0x003, 0xF0, 0xc0010207, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
#[5:0] VEC_INS, 0xc0010208, 0x0CB, 0x04, 0xc0010209, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
#[5:0] TOT_INS, 0xc001020A, 0x0C0, 0x00, 0xc001020B, 0x3, 0x0, 0x0, MSR_DEFAULT, CTR_NUMCORE
##### Uncore counters ##########
#[3:0] L3_CACHE_MISSES, 0xc0010240, 0x4E1, 0xF7, 0xc0010241, 0x0, 0x1, 0x0, MSR_DEFAULT, CTR_UNCORE
#[3:0] RW_DRAM_EXT, 0xc0010242, 0x1E0, 0xF, 0xc0010243, 0x0, 0x1, 0x0, UNCORE_PER_NUMA, CTR_UNCORE
#[3:0] IO_DRAM_INT, 0xc0010242, 0x1E1, 0x0, 0xc0010243, 0x0, 0x1, 0x0, UNCORE_PER_NUMA, CTR_UNCORE
#[3:0] DCT_PREFETCH, 0xc0010242, 0x1F0, 0x64, 0xc0010243, 0x0, 0x1, 0x0, MSR_DEFAULT, CTR_UNCORE
#[3:0] DCT_RD_TOT, 0xc0010244, 0x1F0, 0x62, 0xc0010245, 0x0, 0x1, 0x0, MSR_DEFAULT, CTR_UNCORE
#[3:0] RW_DRAM_INT, 0xc0010246, 0x1E0, 0x0, 0xc0010247, 0x0, 0x1, 0x0, UNCORE_PER_NUMA, CTR_UNCORE
#[3:0] IO_DRAM_EXT, 0xc0010246, 0x1E1, 0xF, 0xc0010247, 0x0, 0x1, 0x0, UNCORE_PER_NUMA, CTR_UNCORE
#[3:0] DCT_WRT, 0xc0010246, 0x1F0, 0x19, 0xc0010247, 0x0, 0x1, 0x0, MSR_DEFAULT, CTR_UNCORE
*/

static struct MSRcounter* counter_assignments = NULL;
static char** initnames = NULL;
static int msr_numoptions = 0;
static int numinitnames = 0;
static int numcore = 0;
static int maxcore = 0;


/**
 * NOTE:
 * 1) still subject to race conditions if the user zero's or changes the registers back and forth between our grab
 * 2) since we iterate thru we select a little staggered in time cpu to cpu
 *
 * CONFIGURATION/INTERACTION:
 * a) users must supply the configuration file with the counter assignments.
 *    incompatible pairs are discovered.
 * b) users will add each they want one separately
 * c) users can swap out a var with one of the same size. syntax? name of the one to swap.
 * d) users will also say which one to halt/continue
 *
 * LOCKS:
 * 1) cant call write (change the var or zero the reg) the register while we are reading the register.
 * 2) cant read into the register while we are working with the counter data (print fctn right now.
 *    note we could save them while we are reading them)
 * 3) cant change the intended variable while we are reading or writing the data
 *
 * TODO:
 * 1) could read different ctr independently (threads)
 */

struct kw {
	char *token;
	int (*action)(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg);
};

static int kw_comparator(const void *a, const void *b)
{
	struct kw *_a = (struct kw *)a;
	struct kw *_b = (struct kw *)b;
	return strcmp(_a->token, _b->token);
}

static ovis_log_t mylog;

static ldms_set_t set = NULL;
static char *producer_name = NULL;
static char *instance_name = NULL;
static char *schema_name = NULL;
static ldms_schema_t schema;
static uint64_t comp_id;
#define SAMP "msr_interlagos"
static char *default_schema_name = SAMP;
static base_data_t base;

static ctrcfg_state cfgstate = CFG_PRE;


static const char *usage(struct ldmsd_plugin *self)
{
	return  "    config name=" SAMP " action=initialize producer=<prod_name> instance=<inst_name> [component_id=<comp_id> schema=<sname> with_jobid=<jid>] maxcore=<maxcore> corespernuma=<corespernuma> conffile=<cfile>\n"
		"            - Initialization activities for the set. Does not create it.\n"
		"            producer        - The producer name\n"
		"            instance        - The instance name\n"
		"            component_id    - Optional unique number identifier. Also that for\n"
		"                              the timermetric. Defaults to zero.\n"
		"            maxcore         - max cores that will be reported for all counters.\n"
		"                              If unspecified, it will use the actual number of cores.\n"
		"                              If specified N must be >= actual numcores. This will\n"
		"                              report 0 as values any N > actual numcores\n"
		"            corespernuma    - num cores per numa domain (used for uncore counters)\n"
		"            conffile        - configuration file with the counter assignment options\n"
		"            schema          - Optional schema name. Defaults to '" SAMP "'\n"
		"\n"
		"    config name=msr_interlagos action=add metricname=<name>\n"
		"            - Adds a metric. The order they are issued are the ordered they are added\n"
		"            metricname      - The metric name for the event\n"
		"\n"
		"    config name=msr_interlagos action=finalize\n"
		"            - Creates the set when called after all adds.\n"
		"\n"
		"    config name=msr_interlagos action=ls\n"
		"            - List the currently configured events.\n"
		"\n"
		"    config name=msr_interlagos action=halt metricname=<name>\n"
		"            metricname      - The metric name for the event to halt.\n"
		"                              metricname=all halts all\n"
		"\n"
		"    config name=msr_interlagos action=continue metricname=<name>\n"
		"            metricname      - The metric name for the event to continue (once halted)\n"
		"                              metricname=all\n"
		"\n"
		"    config name=msr_interlagos action=reassign oldmetricname=<oldname> newmetricname=<newname>\n"
		"            oldmetricname   - The metric name for the event to swap out\n"
		"            newmetricname   - The metric name for the event to swap in\n"
		"\n"
		"    config name=msr_interlagos action=rewrite metricname=<name>\n"
		"            metricname      - The metric name for the event to rewrite\n"
		"                              metricname=all rewrites all\n"
		;
}

static int parseConfig(char* fname){
	char name[MSR_CONFIGLINE_MAX];
	uint64_t w_reg;
	uint64_t event;
	uint64_t umask;
	uint64_t r_reg;
	uint64_t os_user;
	uint64_t int_core_ena;
	uint64_t int_core_sel;
	char core_flag[MSR_CONFIGLINE_MAX];
	char temp[MSR_CONFIGLINE_MAX];
	ctr_num_values numvalues_type;
	msr_special_cases msr_special_cases_type;

	char lbuf[MSR_CONFIGLINE_MAX];
	char* s;
	int rc;
	int i, count;

	FILE *fp = fopen(fname, "r");
	if (!fp){
		ovis_log(mylog, OVIS_LERROR, "%s: Cannot open config file <%s>\n",
		       __FILE__, fname);
		return EINVAL;
	}

	count = 0;
	//parse once to count
	do  {

		s = fgets(lbuf, sizeof(lbuf), fp);
		if (!s)
			break;
		if ((strlen(lbuf) > 0)  && (lbuf[0] == '#')){
			ovis_log(mylog, OVIS_LWARNING, "Comment in msr config file <%s>. Skipping\n",
			       lbuf);
			continue;
		}
		rc = sscanf(lbuf, "%[^,],%llx,%llx,%llx,%llx,%llx,%llx,%llx%*[, ]%[^, ]%*[, ]%[^, \n]",
			    name, &w_reg, &event, &umask, &r_reg, &os_user,
			    &int_core_ena, &int_core_sel, core_flag, temp);
		if (rc != 10){
			ovis_log(mylog, OVIS_LWARNING,
			       "Bad format in msr config file <%s>. Skipping\n",
			       lbuf);
			continue;
		}
		ovis_log(mylog, OVIS_LDEBUG, "msr config fields: <%s> <%"PRIu64 "> <%"PRIu64 "> <%"PRIu64 "> <%"PRIu64 "> <%"PRIu64 "> <%"PRIu64 "> <%"PRIu64 "> <%s> <%s>\n",
		       name, w_reg, event, umask, r_reg, os_user, int_core_ena,
		       int_core_sel, core_flag, temp);

		if ((strcmp(core_flag, "MSR_DEFAULT") != 0) &&
		    (strcmp(core_flag, "UNCORE_PER_NUMA") != 0)){
			ovis_log(mylog, OVIS_LDEBUG,
			       "Bad core_flag in msr config file <%s>. Skipping\n",
			       lbuf);
			continue;
		}

		if ((strcmp(temp, "CTR_UNCORE") != 0) &&
		    (strcmp(temp, "CTR_NUMCORE") != 0)){
			ovis_log(mylog, OVIS_LWARNING,
			       "Bad type in msr config file <%s>. Skipping\n",
			       lbuf);
			continue;
		}

		if ((strcmp(temp, "CTR_NUMCORE") == 0) &&
		    (strcmp(core_flag, "UNCORE_PER_NUMA") == 0)){
			ovis_log(mylog, OVIS_LWARNING,
			       "Core flag type mismatch in msr config file <%s>. Skipping\n",
			       lbuf);
			continue;
		}
		count++;
	} while (s);

	counter_assignments =
		(struct MSRcounter*)malloc(count*sizeof(struct MSRcounter));
	if (!counter_assignments){
		fclose(fp);
		return ENOMEM;
	}
	//let the user add up to this many names as well
	initnames = (char**)malloc(count*sizeof(char*));
	if (!initnames){
		free(counter_assignments);
		fclose(fp);
		return ENOMEM;
	}

	//parse again to fill
	fseek(fp, 0, SEEK_SET);
	i = 0;
	do  {

		s = fgets(lbuf, sizeof(lbuf), fp);
		if (!s)
			break;

		if ((strlen(lbuf) > 0)  && (lbuf[0] == '#')){
			ovis_log(mylog, OVIS_LWARNING, "Comment in msr config file <%s>. Skipping\n",
			       lbuf);
			continue;
		}
		rc = sscanf(lbuf, "%[^,],%llx,%llx,%llx,%llx,%llx,%llx,%llx%*[, ]%[^, ]%*[, ]%[^, \n]",
			    name, &w_reg, &event, &umask, &r_reg, &os_user,
			    &int_core_ena, &int_core_sel, core_flag, temp);
		if (rc != 10){
			continue;
		}
		if ((strcmp(core_flag, "MSR_DEFAULT") == 0)) {
			msr_special_cases_type = MSR_DEFAULT;
		} else if ((strcmp(core_flag, "UNCORE_PER_NUMA") == 0)) {
			msr_special_cases_type = UNCORE_PER_NUMA;
		} else {
			continue;
		}
		if ((strcmp(temp, "CTR_UNCORE") == 0)) {
			numvalues_type = CTR_UNCORE;
		} else if ((strcmp(temp, "CTR_NUMCORE") == 0)) {
			numvalues_type = CTR_NUMCORE;
		} else {
			continue;
		}

		if ((strcmp(temp, "CTR_NUMCORE") == 0) &&
		    (strcmp(core_flag, "UNCORE_PER_NUMA") == 0)){
			continue;
		}

		if (i == count){
			ovis_log(mylog, OVIS_LERROR,
			       "Changed number of valid entries from first pass. aborting.\n");
			free(counter_assignments);
			free(initnames);
			return EINVAL;
		}

		counter_assignments[i].name = strdup(name);
		counter_assignments[i].w_reg = w_reg;
		counter_assignments[i].event = event;
		counter_assignments[i].umask = umask;
		counter_assignments[i].r_reg = r_reg;
		counter_assignments[i].os_user = os_user;
		counter_assignments[i].int_core_ena = int_core_ena;
		counter_assignments[i].int_core_sel = int_core_sel;
		counter_assignments[i].core_flag = msr_special_cases_type;
		counter_assignments[i].numvalues_type = numvalues_type;
		counter_assignments[i].numcore = 0; //this will get filled in later
		counter_assignments[i].offset = 0; //this will get filled in later
		counter_assignments[i].maxcore = 0; //this will get filled in later

		i++;
	} while (s);
	fclose(fp);

	msr_numoptions = i;

	return 0;
}

static void _free_names(){
	if (producer_name) free(producer_name);
	producer_name = NULL;
	if (schema_name) free(schema_name);
	schema_name = NULL;
	if (instance_name) free(instance_name);
	instance_name = NULL;
}

//the calling function needs to get the cfg lock
struct active_counter* findactivecounter(char* name){
	struct active_counter* pe;

	if ((name == NULL) || (strlen(name) == 0)){
		return NULL;
	}

	TAILQ_FOREACH(pe, &counter_list, entry){
		if (strcmp(name, pe->mctr->name) == 0){
			return pe;
		}
	}

	return NULL;
}


static int halt(struct attr_value_list *kwl, struct attr_value_list *avl,
		void* arg)
{
	struct active_counter* pe;
	char* value;

	if (cfgstate != CFG_DONE_FINAL){
		ovis_log(mylog, OVIS_LERROR,
		       SAMP ": in wrong state for halting events <%d>\n",
		       cfgstate);
		return -1;
	}

	value = av_value(avl, "metricname");
	if (!value){
		ovis_log(mylog, OVIS_LERROR, SAMP ": no name to halt\n");
		return -1;
	}

	pthread_mutex_lock(&cfglock);
	if (strcmp(value, "all") == 0){
		TAILQ_FOREACH(pe, &counter_list, entry){
			switch(pe->state){
			case CTR_OK:
				//will halt. make everything we have invalid now
				pe->valid = 0;
				pe->state = CTR_HALTED;
				break;
			default:
				//do nothing
				break;
			}
		}
	} else {
		pe = findactivecounter(value);
		if (pe == NULL){
			ovis_log(mylog, OVIS_LERROR,
			       SAMP ": cannot find <%s> to halt\n",
			       value);
			pthread_mutex_unlock(&cfglock);
			return -1;
		}

		switch(pe->state){
		case CTR_OK:
			//will halt. make everything we have invalid now
			pe->valid = 0;
			pe->state = CTR_HALTED;
			break;
		default:
			//do nothing
			break;
		}
	}

	pthread_mutex_unlock(&cfglock);
	return 0;

}


static int cont(struct attr_value_list *kwl, struct attr_value_list *avl,
		void* arg)
{
	struct active_counter* pe;
	char* value;

	if (cfgstate != CFG_DONE_FINAL){
		ovis_log(mylog, OVIS_LERROR,
		       SAMP ": in wrong state for continuing events <%d>\n",
		       cfgstate);
		return -1;
	}

	value = av_value(avl, "metricname");
	if (!value){
		ovis_log(mylog, OVIS_LERROR, SAMP ": no name to continue\n");
		return -1;
	}

	pthread_mutex_lock(&cfglock);
	if (strcmp(value, "all") == 0){
		TAILQ_FOREACH(pe, &counter_list, entry){
			switch(pe->state){
			case CTR_HALTED:
				pe->state = CTR_OK;
				break;
			default:
				//do nothing
				break;
			}
		}
	} else {
		pe = findactivecounter(value);
		if (pe == NULL){
			ovis_log(mylog, OVIS_LERROR,
			       SAMP ": cannot find <%s> to continue\n",
			       value);
			pthread_mutex_unlock(&cfglock);
			return -1;
		}

		switch(pe->state){
		case CTR_HALTED:
			pe->state = CTR_OK;
			break;
		default:
			//do nothing
			break;
		}
	}

	pthread_mutex_unlock(&cfglock);
	return 0;
}


int writeregistercpu(uint64_t x_reg, int cpu, uint64_t val){

	char fname[MSR_MAXLEN];
	uint64_t dat;
	int fd;

	snprintf(fname, MSR_MAXLEN-1, "/dev/cpu/%d/msr", cpu);
	fd = open(fname, O_WRONLY);
	if (fd < 0) {
		int errsv = errno;
		ovis_log(mylog, OVIS_LERROR,
		       SAMP ": writeregistercpu cannot open fd=<%d> for cpu %d errno=<%d>",
		       fd, cpu, errsv);
		return -1;
	}

	dat = val;
	if (pwrite(fd, &dat, sizeof dat, x_reg) != sizeof dat) {
		int errsv = errno;
		ovis_log(mylog, OVIS_LERROR,
		       SAMP ": writeregistercpu cannot pwrite MSR 0x%08" PRIx64
		       " to 0x%016" PRIx64 " for cpu %d errno=<%d>\n",
		       x_reg, dat, cpu, errsv);
		return -1;
	}

	close(fd);

	return 0;
}


int readregistercpu(uint64_t x_reg, int cpu, uint64_t* val){

	char fname[MSR_MAXLEN];
	uint64_t dat;
	int fd;

	snprintf(fname, MSR_MAXLEN-1, "/dev/cpu/%d/msr", cpu);
	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		int errsv = errno;
		ovis_log(mylog, OVIS_LERROR,
		       SAMP ": readregistercpu cannot open fd=<%d> for cpu %d errno=<%d>",
		       fd, cpu, errsv);
		return -1;
	}

	if (pread(fd, &dat, sizeof dat, x_reg) != sizeof dat) {
		int errsv = errno;
		ovis_log(mylog, OVIS_LERROR,
		       SAMP ": readregistercpu cannot pread MSR 0x%08" PRIx64
		       " for cpu %d errno=<%d>\n",
		       x_reg, cpu, errsv);
		close(fd);
		return -1;
	}

	close(fd);
	*val = dat;

	return 0;
}


static int zerometricset( struct active_counter *pe){
	union ldms_value v;
	int i;
	int rc;

	//populate the metrics with zero values
	if (pe == NULL){
		return -1;
	}

	ldms_metric_array_set_str(set, pe->metric_name, "");
	v.v_u64 = 0;
	for (i = 0; i < pe->nctl; i++){
		ldms_metric_array_set_val(set, pe->metric_ctl, i, &v);
	}
	for (i = 0; i < pe->ndata; i++){
		ldms_metric_array_set_val(set, pe->metric_ctr, i, &v);
	}

	pe->valid = 0; //invalidates

	return 0;
}


static int readregisterguts( struct active_counter *pe){
	union ldms_value v;
	int i, j;
	int rc;

	if (pe == NULL){
		return -1;
	}

	j = 0;
	for (i = 0; i < pe->mctr->numcore; i+=pe->mctr->offset){ //will only read what's there (numcore)
		//NOTE: possible race condition if the register changes while reading through.
		rc = readregistercpu(pe->mctr->r_reg, i, &(pe->data[j]));
		if (rc != 0){
			//if any of them fail, invalidate all
			zerometricset(pe);
			return rc;
		}
		j++;
	}

	ldms_metric_array_set_str(set, pe->metric_name, pe->mctr->name);
	if (pe->nctl == 1){ // 1 is the same as all
		v.v_u64 = pe->wctl[0];
		ldms_metric_array_set_val(set, pe->metric_ctl, 0, &v);
	} else {
		j = 0;
		for (i = 0; i < pe->mctr->maxcore; i+=pe->mctr->offset){
                        //set all of them, which will include the padding
			v.v_u64 = pe->wctl[i];
			ldms_metric_array_set_val(set, pe->metric_ctl, j, &v);
			j++;
		}
	}
	for (j = 0; j < pe->ndata; j++){
                //set all of them, which will include the padding.
		v.v_u64 = pe->data[j];
		ldms_metric_array_set_val(set, pe->metric_ctr, j, &v);
	}

	pe->valid = 1;
	return 0;

}


static int checkregister( struct active_counter *pe){

	int i;
	uint64_t val;
	int rc;

	if (pe == NULL){
		return -1;
	}

	//read the val(s) and make sure they match wctl
	for (i = 0; i < pe->mctr->numcore; i+=pe->mctr->offset){
		rc = readregistercpu(pe->mctr->w_reg, i, &val);
		if (rc != 0){
			ovis_log(mylog, OVIS_LERROR,
			       SAMP ": <%s> readregistercpu bad %d\n",
			       pe->mctr->name, rc);
			return rc;
		}
		//              printf("Comparing %llx to %llx\n", val, pe->wctl);
		if (val != pe->wctl[i]){
			ovis_log(mylog, OVIS_LDEBUG,
			       SAMP ": Register changed! read <%llx> want <%llx>\n",
			       val, pe->wctl[i]);
			return -1;
			break;
		}
	}

	return 0;
}


static int readregister(struct active_counter *pe){
	int rc;

	if (pe == NULL){
		return -1;
	}

	switch (pe->state){
	case CTR_HALTED:
		ovis_log(mylog, OVIS_LDEBUG,
		       SAMP ": %s Halted. Register will not be read.\n",
		       pe->mctr->name);
		//invalidate the current vals because this is an invalid read. (but this will have already been done as part of the halt)
		zerometricset(pe); //these sets zero values in the metric set
		rc = 0;
		break;
	case CTR_OK:
		//check all of them first
		rc = checkregister(pe);
		if (rc != 0){
			ovis_log(mylog, OVIS_LDEBUG,
			       SAMP ": Control register for %s has changed. Register will not be read.\n",
			       pe->mctr->name);
			//invalidate the current vals because this is an invalid read.
			zerometricset(pe); //these sets zero values in the metric set
			// we are ok with this
			rc = 0;
		} else {
			//then read all of them
			rc = readregisterguts(pe); //this invalidates if fails. this is an invalid read. this sets values in the metric set
			if (rc != 0){
				ovis_log(mylog, OVIS_LERROR,
				       SAMP ": Read register failed %s\n",
				       pe->mctr->name);
				// we are not ok with this. do not change rc
			}
		}
		break;
	default:
		ovis_log(mylog, OVIS_LDEBUG,
		       SAMP ": register state <%d>. Wont read\n",
		       pe->state);
		rc = 0;
		break;
	}

	return rc;

}


static ctr_state writeregister(struct active_counter *pe){

	int i;
	int rc;

	if (pe == NULL){
		return CTR_BROKEN;
	}

	pe->valid = 0;
	pe->state = CTR_BROKEN;
	//Zero the ctrl register
	for (i = 0; i < pe->mctr->numcore; i+=pe->mctr->offset){
		rc = writeregistercpu(pe->mctr->w_reg, i, 0);
		if (rc != 0){
			return pe->state;
		}
	}
	//Zero the val register
	for (i = 0; i < pe->mctr->numcore; i+=pe->mctr->offset){
		rc = writeregistercpu(pe->mctr->r_reg, i, 0);
		if (rc != 0){
			return pe->state;
		}
	}
	//Write the ctrl register
	for (i = 0; i < pe->mctr->numcore; i+=pe->mctr->offset){
		rc = writeregistercpu(pe->mctr->w_reg, i, pe->wctl[i]);
		if (rc != 0){
			return pe->state;
		}
	}

	pe->state = CTR_OK;
	return pe->state;
}

static int checkreassigncounter(struct active_counter *rpe, int idx){
	struct active_counter* pe;

	if (rpe == NULL) {
		return -1;
	}

	//validity. compare with all the others
	TAILQ_FOREACH(pe, &counter_list, entry){
		if (pe != rpe){
			//duplicates are ok
			if (rpe->mctr->w_reg == pe->mctr->w_reg){
				if (strcmp(rpe->mctr->name, pe->mctr->name) == 0){
					//duplicates are ok
					ovis_log(mylog, OVIS_LINFO,
					       SAMP ": Notify - Duplicate assignments! <%s>\n",
					       rpe->mctr->name);
				} else {
					return -1;
				}
			}
		}
	}

	//size check: can only reassign to a space of the same size
	if (rpe->mctr->numcore != counter_assignments[idx].numcore){
		return -1;
	}
	//offset check: can only reassign to the same offset (for the data array)
	if (rpe->mctr->offset != counter_assignments[idx].offset){
		return -1;
	}
	//wctl size check: can only reassign to the same special case (for the wcl and metric_ctl array)
	if ((rpe->mctr->core_flag != counter_assignments[idx].core_flag) ||
	    (rpe->mctr->numvalues_type !=
	     counter_assignments[idx].numvalues_type)){
		return -1;
	}
	return 0;
}


static int rewrite(struct attr_value_list *kwl, struct attr_value_list *avl,
		   void* arg){
	struct active_counter* pe;
	ctr_state s;
	char* value;

	if (cfgstate != CFG_DONE_FINAL){
		ovis_log(mylog, OVIS_LERROR,
		       SAMP ": in wrong state for rewriting events <%d>\n",
		       cfgstate);
		return -1;
	}

	value = av_value(avl, "metricname");
	if (!value){
		ovis_log(mylog, OVIS_LERROR, SAMP ": no name to rewrite\n");
		return -1;
	}

	pthread_mutex_lock(&cfglock);
	if (strcmp(value, "all") == 0){
		TAILQ_FOREACH(pe, &counter_list, entry){
			s = writeregister(pe);
			if (s != CTR_OK){
				ovis_log(mylog, OVIS_LERROR,
				       SAMP ": cannot rewrite register <%s>\n",
				       value);
				//but will continue;
			}
			pthread_mutex_unlock(&pe->lock);
		}
	} else {
		pe = findactivecounter(value);
		if (pe == NULL){
			ovis_log(mylog, OVIS_LERROR,
			       SAMP ": cannot find <%s> to rewrite\n",
			       value);
			pthread_mutex_unlock(&cfglock);
			return -1;
		}

		pthread_mutex_lock(&pe->lock);
		s = writeregister(pe);
		if (s != CTR_OK){
			ovis_log(mylog, OVIS_LERROR,
			       SAMP ": cannot rewrite register <%s>\n",
			       value);
			//but will continue;
		}
		pthread_mutex_unlock(&pe->lock);
	}

	pthread_mutex_unlock(&cfglock);
	return 0;
}

int assigncounter(struct active_counter* pe, int j);

static struct active_counter* reassigncounter(char* oldname, char* newname) {
	struct active_counter *pe;
	int j;
	int rc;

	if ((oldname == NULL) || (newname == NULL) ||
	    (strlen(oldname) == 0) || (strlen(newname) == 0)){
		ovis_log(mylog, OVIS_LERROR,
		       SAMP ": Invalid args to reassign counter\n");
		return NULL;
	}

	int idx = -1;
	for (j = 0; j < msr_numoptions; j++){
		if (strcmp(newname, counter_assignments[j].name) == 0){
			idx = j;
			break;
		}
	}
	if (idx < 0){
		ovis_log(mylog, OVIS_LERROR,
		       SAMP ": No counter <%s> to reassign to\n",
		       newname);
		return NULL;
	}

	pthread_mutex_lock(&cfglock);
	pe = findactivecounter(oldname);
	if (pe == NULL){
		ovis_log(mylog, OVIS_LERROR,
		       SAMP ": Cannot find counter <%s> to replace\n", oldname);
		pthread_mutex_unlock(&cfglock);
		return NULL;
	} else {
		rc = checkreassigncounter(pe, idx);
		if (rc != 0){
			ovis_log(mylog, OVIS_LERROR,
			       SAMP ": Reassignment of <%s> to <%s> invalid\n",
			       oldname, newname);
			pthread_mutex_unlock(&cfglock);
			return NULL;
		}

		rc = assigncounter(pe, idx);
		if (rc != 0){
			pthread_mutex_unlock(&cfglock);
			return NULL;
		}
	}

	pthread_mutex_unlock(&cfglock);
	return pe;
}


static int reassign(struct attr_value_list *kwl, struct attr_value_list *avl,
		    void *arg){
	struct active_counter* pe;
	ctr_state s;
	char* ovalue;
	char* nvalue;

	if (cfgstate != CFG_DONE_FINAL){
		ovis_log(mylog, OVIS_LERROR,
		       SAMP ": in wrong state for reassigning events <%d>\n",
		       cfgstate);
		return -1;
	}

	ovalue = av_value(avl, "oldmetricname");
	if (!ovalue){
		ovis_log(mylog, OVIS_LERROR, SAMP ": no name to rewrite\n");
		return -1;
	}

	nvalue = av_value(avl, "newmetricname");
	if (!nvalue){
		ovis_log(mylog, OVIS_LERROR, SAMP ": no name to rewrite to\n");
		return -1;
	}

	pthread_mutex_lock(&cfglock);
	pe = reassigncounter(ovalue, nvalue);
	if (pe == NULL){
		ovis_log(mylog, OVIS_LERROR, SAMP ": cannot reassign counter\n");
		pthread_mutex_unlock(&cfglock);
		return -1;
	}

	pthread_mutex_unlock(&cfglock);
	return 0;

}


static int add_event(struct attr_value_list *kwl, struct attr_value_list *avl,
		     void *arg){
	int idx;
	char* nam;
	char* val;
	int i;

	pthread_mutex_lock(&cfglock);
	if (cfgstate != CFG_DONE_INIT){
		ovis_log(mylog, OVIS_LERROR,
		       SAMP ": in wrong state for adding events <%d>\n",
		       cfgstate);
		pthread_mutex_unlock(&cfglock);
		return -1;
	}

	//add an event to the list to be parsed
	if (numinitnames == msr_numoptions){
		ovis_log(mylog, OVIS_LERROR, SAMP ": Trying to add too many events\n");
		pthread_mutex_unlock(&cfglock);
		return -1;
	}

	nam = av_value(avl, "metricname");
	if ((!nam) || (strlen(nam) == 0)){
		ovis_log(mylog, OVIS_LERROR, SAMP ": Invalid event name\n");
		pthread_mutex_unlock(&cfglock);
		return -1;
	}

	idx = -1;
	for (i = 0; i < msr_numoptions; i++){
		if (strcmp(nam, counter_assignments[i].name) == 0){
			idx = i;
			break;
		}
	}
	if (idx < 0){
		ovis_log(mylog, OVIS_LERROR,
		       SAMP ": Non-existent event name <%s>\n", nam);
		pthread_mutex_unlock(&cfglock);
		return -1;
	}

	initnames[numinitnames] = strdup(nam);
	if (!initnames[numinitnames]){
		pthread_mutex_unlock(&cfglock);
		return ENOMEM;
	}

	ovis_log(mylog, OVIS_LDEBUG, SAMP ": Added event name <%s>\n", nam);

	numinitnames++;
	pthread_mutex_unlock(&cfglock);

	return 0;

}

static int checkcountersinit(){ //this will only be called once, from finalize
	//get the lock outside of this
	//check for conflicts. check no conflicting w_reg
	int i, j, ii, jj;

	//Duplicates are OK but warn. this lets you swap.
	for (i = 0; i < numinitnames; i++){
		int imatch = -1;
		for (ii = 0; ii < msr_numoptions; ii++){
			if (strcmp(initnames[i],
				   counter_assignments[ii].name) == 0){
				imatch == ii;
				break;
			}
		}
		if (imatch < 0)
			continue;

		for (j = 0; j < numinitnames; j++){
			int jmatch = -1;
			if (i == j)
				continue;

			for (jj = 0; jj < msr_numoptions; jj++){
				if (strcmp(initnames[j],
					   counter_assignments[jj].name) == 0){
					jmatch == jj;
				}
			}
			if (jmatch < 0)
				continue;

			//do they have the same wregs if they are not the same name?
			if (counter_assignments[imatch].w_reg ==
			    counter_assignments[jmatch].w_reg){
				if (strcmp(initnames[i], initnames[j]) == 0){
					//this is ok
					ovis_log(mylog, OVIS_LINFO,
					       SAMP ": Notify - Duplicate assignments! <%s>\n",
					       initnames[i]);
				} else {
					ovis_log(mylog, OVIS_LERROR,
					       SAMP ": Cannot have conflicting counter assignments <%s> <%s>\n",
					       initnames[i], initnames[j]);
					return -1;
				}
			}
		}
	}

	return 0;
}


static int list(struct attr_value_list *kwl, struct attr_value_list *avl,
		void *arg)
{
	struct active_counter* pe;

	//FIXME: write them all out later
	ovis_log(mylog, OVIS_LINFO,"%-24s %10x %10x %10xs\n");
	ovis_log(mylog, OVIS_LINFO, "Name", "wreg", "wctl[0]", "rreg");
	ovis_log(mylog, OVIS_LINFO,"%-24s %10s %10s %10s\n",
	       "------------------------",
	       "----------", "----------", "----------");
	pthread_mutex_lock(&cfglock);
	TAILQ_FOREACH(pe, &counter_list, entry) {
		ovis_log(mylog, OVIS_LINFO,"%-24s %10x %10x %10x\n",
		       pe->mctr->name, pe->mctr->w_reg, pe->wctl[0],
		       pe->mctr->r_reg);
	}
	pthread_mutex_unlock(&cfglock);
	return 0;
}

static int dfilter(const struct dirent *dp){
	return ((isdigit(dp->d_name[0])) ? 1 : 0);
}

// numcore, maxcore, and corespernuma are all assigned by the end of this
static int init(struct attr_value_list *kwl, struct attr_value_list *avl,
		void *arg)
{
	struct dirent **dlist;
	char* val;
	char* cfile;
	int rc;
	int i;

	if (cfgstate != CFG_PRE){
		ovis_log(mylog, OVIS_LERROR, SAMP ": cannot reinit");
		return -1;
	}

	if (set) {
                ovis_log(mylog, OVIS_LERROR, SAMP ": Set already created.\n");
                return EINVAL;
        }

	cfile = av_value(avl, "conffile");
	if (!cfile){
		ovis_log(mylog, OVIS_LERROR, SAMP ": no config file");
		_free_names();
		rc = EINVAL;
		return rc;
	} else {
		rc = parseConfig(cfile);
		if (rc != 0){
			ovis_log(mylog, OVIS_LERROR,
			       SAMP ": error parsing config file. Aborting\n");
			_free_names();
			return rc;
		}
	}

	//get the actual number of counters = num entries like /dev/cpu/%d
	numcore = scandir("/dev/cpu", &dlist, dfilter, 0);
	if (numcore < 1){
		ovis_log(mylog, OVIS_LERROR, SAMP ": cannot get numcore\n");
		_free_names();
		return -1;
	}
	for (i = 0; i < numcore; i++){
		free(dlist[i]);
	}
	free(dlist);

	pthread_mutex_lock(&cfglock);

	base = base_config(avl, self->inst_name, SAMP, mylog);
        if (!base) {
                rc = errno;
		_free_names();
		pthread_mutex_unlock(&cfglock);
		return rc;
        }

	maxcore = numcore;
	val = av_value(avl, "maxcore");
	if (val){
		maxcore = atoi(val);
		if ((maxcore < numcore) || (maxcore > MSR_TOOMANYMAX)){ //some big number. just a safety check.
			ovis_log(mylog, OVIS_LERROR, SAMP ": maxcore %d invalid\n",
			       maxcore);
			_free_names();
			base_del(base);
			pthread_mutex_unlock(&cfglock);
			return -1;
		}
	}

	int corespernuma = 1;
	val = av_value(avl, "corespernuma");
	if (val){
		corespernuma = atoi(val);
		if ((corespernuma < 1) || (corespernuma > MSR_TOOMANYMAX)){ //some big number. just a safety check.
			ovis_log(mylog, OVIS_LERROR, SAMP ": corespernuma %d invalid\n",
			       maxcore);
			_free_names();
			base_del(base);
			pthread_mutex_unlock(&cfglock);
			return -1;
		}
	} else {
		ovis_log(mylog, OVIS_LERROR, SAMP ": must specify corespernuma\n");
		_free_names();
		base_del(base);
		pthread_mutex_unlock(&cfglock);
		return -1;
	}


	for (i = 0; i < msr_numoptions; i++){
		counter_assignments[i].numcore = numcore;
		counter_assignments[i].maxcore = maxcore;
		if (counter_assignments[i].numvalues_type == CTR_NUMCORE){
			counter_assignments[i].offset = 1;
		} else {
			counter_assignments[i].offset = corespernuma;
		}
		//note alloc for wctl and metric_ctl comes later
	}

	pthread_mutex_unlock(&cfglock);

	numinitnames = 0;
	cfgstate = CFG_DONE_INIT;

	return 0;
}


int assigncounter(struct active_counter* pe, int j){ //includes the write
	uint64_t data_size;
	int i;

	int init = 0;
	if (pe == NULL){ //if already init, dont want to mess up the metric ptrs
		init = 1;
	}

	if (init){
		pe = calloc(1, sizeof *pe);
		if (!pe){
			return ENOMEM;
		}
		pthread_mutex_init(&(pe->lock), NULL);
	}
	pthread_mutex_lock(&(pe->lock));


	pe->mctr = &counter_assignments[j];
	pe->valid = 0;
	if (init){
		int nval = (pe->mctr->maxcore)/(pe->mctr->offset); //unlike v2, array will include the padding
		pe->data = calloc(nval, sizeof(data_size));
		if (!pe->data){
			pthread_mutex_unlock(&(pe->lock));
			return ENOMEM;
		} //wont need to zero out otherwise since valid = 0;
		pe->ndata = nval; //some of these may be padding

		pe->metric_ctl = 0; //this is an array
		//alloc as many wctl as there possible core, including padding
		pe->wctl = calloc(pe->mctr->maxcore, sizeof(data_size));
		if (!pe->wctl){
			free(pe->data);
			pthread_mutex_unlock(&(pe->lock));
			return ENOMEM;
		} //wont need to zero out otherwise since valid = 0;

		pe->metric_ctr = 0; //this is an array, but only make it as big as it needs to be
		if (pe->mctr->core_flag == UNCORE_PER_NUMA){
			pe->nctl = pe->ndata;
		} else {
			pe->nctl = 1;
		}
	}

	//WRITE COMMAND
	uint64_t w_reg = counter_assignments[j].w_reg;
	uint64_t event_hi = counter_assignments[j].event >> 8;
	uint64_t event_low = counter_assignments[j].event & 0xFF;
	uint64_t umask = counter_assignments[j].umask;
	uint64_t umask_tmp = counter_assignments[j].umask;
	uint64_t os_user = counter_assignments[j].os_user;
	uint64_t int_core_ena = counter_assignments[j].int_core_ena;
	uint64_t int_core_sel = counter_assignments[j].int_core_sel;


	if (pe->nctl == 1) { //There is only 1 wctl for all. Assign it to all values
		//pe->wctl = MSR_HOST << 40 | event_hi << 32 | MSR_CNT_MASK << 24 | MSR_INV << 23 | MSR_ENABLE << 22 | MSR_INTT << 20 | MSR_EDGE << 18 | os_user << 16 | umask << 8 | event_low;  very old
		uint64_t temp_wctl =  MSR_HOST << 40 | int_core_sel << 37 | int_core_ena << 36 | event_hi << 32 | MSR_CNT_MASK << 24 | MSR_INV << 23 | MSR_ENABLE << 22 | MSR_INTT << 20 | MSR_EDGE << 18 | os_user << 16 | umask << 8 | event_low;
		//        printf("%s: 0x%llx, 0x%llx, 0x%llx 0x%llx\n", very old
		//               pe->mctr->name, event_hi, event_low, umask, pe->wctl); very old
		for (i = 0; i < pe->mctr->maxcore; i++){
			pe->wctl[i] = temp_wctl;
		}

		//WRITE COMMAND
		ovis_log(mylog, OVIS_LINFO, "WRITECMD: writeregister(0x%llx, %d, 0x%llx)\n",
		       w_reg, (pe->mctr->numcore)/(pe->mctr->offset), pe->wctl[0]);

		//CHECK COMMAND
		ovis_log(mylog, OVIS_LINFO, "CHECKCMD: readregister(0x%llx, %d)\n",
		       w_reg, (pe->mctr->numcore)/(pe->mctr->offset));

		//READ COMMAND
		ovis_log(mylog, OVIS_LINFO, "READCMD: readregister(0x%llx, %d)\n",
		       pe->mctr->r_reg, (pe->mctr->numcore)/(pe->mctr->offset));
	} else {
		//Calculate a different value for each legit one
		for (i = 0; i < pe->mctr->numcore; i+=pe->mctr->offset){
			//do the wctl[i] calc. all others will be zero
			umask_tmp = (umask ^ (1 << (i / pe->mctr->offset)));
			uint64_t temp_wctl =  MSR_HOST << 40 | int_core_sel << 37 | int_core_ena << 36 | event_hi << 32 | MSR_CNT_MASK << 24 | MSR_INV << 23 | MSR_ENABLE << 22 | MSR_INTT << 20 | MSR_EDGE << 18 | os_user << 16 | umask_tmp << 8 | event_low;
			pe->wctl[i] = temp_wctl;

			//WRITE COMMAND
			ovis_log(mylog, OVIS_LINFO, "WRITECMD: writeregister[%d](0x%llx, %d, 0x%llx)\n",
			       i, w_reg, (pe->mctr->numcore)/(pe->mctr->offset), pe->wctl[i]);
		}
		//CHECK COMMAND
		ovis_log(mylog, OVIS_LINFO, "CHECKCMD: readregister(0x%llx, %d)\n",
		       w_reg, (pe->mctr->numcore)/(pe->mctr->offset));

		//READ COMMAND
		ovis_log(mylog, OVIS_LINFO, "READCMD: readregister(0x%llx, %d)\n",
		       pe->mctr->r_reg, (pe->mctr->numcore)/(pe->mctr->offset));
	}

	pe->state = CTR_BROKEN; //until written

	writeregister(pe); //this will reset the state

	if (init){ //cfglock held outside
		TAILQ_INSERT_TAIL(&counter_list, pe, entry);
	}

	pthread_mutex_unlock(&(pe->lock));

	return 0;

}


static int assigncountersinit(){
	int i, j;
	int rc;


	TAILQ_INIT(&counter_list);

	for (i = 0; i < numinitnames; i++){
		int found = -1;
		for (j = 0; j < msr_numoptions; j++){
			if (strcmp(initnames[i],
				   counter_assignments[j].name) == 0){
				rc = assigncounter(NULL, j);
				if (rc != 0){
					return rc;
				}
				found = 1;
				break;
			}
		}
		if (found == -1){
			ovis_log(mylog, OVIS_LERROR,
			       SAMP ": Bad init counter name <%s>\n",
			       initnames[i]);
			return -1;
		}
	}

	return 0;
}


static int finalize(struct attr_value_list *kwl, struct attr_value_list *avl,
		    void *arg)
{

	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	struct active_counter* pe;
	char name[MSR_MAXLEN];
	union ldms_value v;
	int rc;
	int i, j, k;

	pthread_mutex_lock(&cfglock);
	ovis_log(mylog, OVIS_LDEBUG, SAMP ": finalizing\n");

	if (cfgstate != CFG_DONE_INIT){
		_free_names();
		pthread_mutex_unlock(&cfglock);
		ovis_log(mylog, OVIS_LERROR,
		       SAMP ": in wrong state to finalize <%d>", cfgstate);
		return -1;
	}

	cfgstate = CFG_IN_FINAL;

	/* do any checking */
	rc = checkcountersinit();
	if (rc < 0){
		//in theory can do more add's but there is currently no way to remove the conflicting ones
		cfgstate = CFG_FAILED_FINAL;
		_free_names();
		pthread_mutex_unlock(&cfglock);
		return -1;
	}

	//after this point can no longer add more counters
	rc = assigncountersinit();
	if (rc < 0){
		cfgstate = CFG_FAILED_FINAL;
		_free_names();
		pthread_mutex_unlock(&cfglock);
		return -1;
	}

	for (i = 0; i < numinitnames; i++){
		free(initnames[i]);
	}
	numinitnames = 0;

	schema = base_schema_new(base);
	if (!schema) {
                ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
                       __FILE__, base->schema_name, errno);
                goto err;
        }


	/* add the metrics */
	i = 0;
	TAILQ_FOREACH(pe, &counter_list, entry) {
		//new for v3: store the name as well.
		snprintf(name, MSR_MAXLEN, "Ctr%d_name", i);
		rc = ldms_schema_metric_array_add(schema, name,
						  LDMS_V_CHAR_ARRAY,
						  MSR_MAXLEN);
		if (rc < 0) {
			rc = ENOMEM;
			goto err;
		}
		pe->metric_name = rc;

		//now add either 1 or the real and padded wctl. Before 8/22 was Ctr%d and order was different
		if (pe->nctl == 1){
			snprintf(name, MSR_MAXLEN, "Ctr%d_wctl", i);
		} else if (pe->mctr->numvalues_type == CTR_NUMCORE){
			snprintf(name, MSR_MAXLEN, "Ctr%d_wctl_c", i);
		} else {
			snprintf(name, MSR_MAXLEN, "Ctr%d_wctl_n", i);
		}
		rc = ldms_schema_metric_array_add(schema, name,
						  LDMS_V_U64_ARRAY, pe->nctl);
		if (rc < 0) {
			rc = ENOMEM;
			goto err;
		}
		pe->metric_ctl = rc;

		//process the real ones and the padded ones
		if (pe->mctr->numvalues_type == CTR_NUMCORE){
			snprintf(name, MSR_MAXLEN, "Ctr%d_c", i);
		} else {
			snprintf(name, MSR_MAXLEN, "Ctr%d_n", i);
		}
		rc = ldms_schema_metric_array_add(schema, name,
						  LDMS_V_U64_ARRAY, pe->ndata);
		if (rc < 0) {
			rc = ENOMEM;
			goto err;
		}
		pe->metric_ctr = rc;
		i++;
	}

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		goto err;
	}

	_free_names();
	cfgstate = CFG_DONE_FINAL;
	pthread_mutex_unlock(&cfglock);
	return 0;

 err:
	ovis_log(mylog, OVIS_LERROR, SAMP ": failed finalize\n");
	cfgstate = CFG_FAILED_FINAL;
	_free_names();
	if (schema) ldms_schema_delete(schema);
	schema = NULL;
	pthread_mutex_unlock(&cfglock);
	if (set)
		ldms_set_delete(set);
	set = NULL;
	return rc;

}


struct kw kw_tbl[] = {
	{ "add", add_event },
	{ "continue", cont },
	{ "finalize", finalize },
	{ "halt", halt },
	{ "initialize", init },
	{ "ls", list },
	{ "reassign", reassign },
	{ "rewrite", rewrite },
};


static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl,
		  struct attr_value_list *avl)
{
	struct kw *kw;
	struct kw key;
	int rc;


	char *action = av_value(avl, "action");

	if (!action)
		goto err0;

	key.token = action;
	kw = bsearch(&key, kw_tbl, ARRAY_SIZE(kw_tbl),
		     sizeof(*kw), kw_comparator);
	if (!kw)
		goto err1;

	rc = kw->action(kwl, avl, NULL);
	if (rc)
		goto err2;
	return 0;
 err0:
	ovis_log(mylog, OVIS_LDEBUG,usage(self));
	goto err2;
 err1:
	ovis_log(mylog, OVIS_LDEBUG,"Invalid configuration keyword '%s'\n", action);
 err2:
	return 0;
}


void fincounter(struct active_counter *pe){

	if (pe == NULL){
		return;
	}

	pe->mctr = NULL;
	free(pe->wctl);
	pe->wctl = 0;

	pe->state = CTR_BROKEN;
	free(pe->data);
	pe->data = NULL;
	pe->ndata = 0;

	pthread_mutex_destroy(&pe->lock);
}


void fin(){
	struct active_counter *pe;

	pthread_mutex_lock(&cfglock);
	TAILQ_FOREACH(pe, &counter_list, entry){
		fincounter(pe);
	}

	free(pe); //FIXME: does this work?
	pthread_mutex_unlock(&cfglock);

};

static int sample(struct ldmsd_sampler *self)
{
	struct active_counter* pe;

	if (cfgstate != CFG_DONE_FINAL){
		ovis_log(mylog, OVIS_LERROR,
		       SAMP ": in wrong state for sampling <%d>\n", cfgstate);
		return -1;
	}

	base_sample_begin(base);

	TAILQ_FOREACH(pe, &counter_list, entry) {
		pthread_mutex_lock(&pe->lock);
		readregister(pe);
		pthread_mutex_unlock(&pe->lock);
	}

	base_sample_end(base);
	return 0;
}

static void term(struct ldmsd_plugin *self)
{
	int i;

	_free_names();

	for (i = 0; i < numinitnames; i++){
		free(initnames[i]);
		initnames[i] = NULL;
	}
	numinitnames = 0;

	//8/22 FIXME: have to free the wctl. from where
	for (i = 0; i < msr_numoptions; i++){
		free(counter_assignments[i].name);
		counter_assignments[i].name = NULL;
		counter_assignments[i].core_flag = 0;
	}
	msr_numoptions = 0;

	//should also free the counter list FIXME -- what does this mean?

	if (base)
		base_del(base);
	base = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
	if (mylog)
		ovis_log_destroy(mylog);
}

static struct ldmsd_sampler msr_interlagos_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.sample = sample,
};

struct ldmsd_plugin *get_plugin()
{
	int rc;
	mylog = ovis_log_register("sampler."SAMP, "The log subsystem of the " SAMP " plugin");
	if (!mylog) {
		rc = errno;
		ovis_log(NULL, OVIS_LWARN, "Failed to create the subsystem "
				"of '" SAMP "' plugin. Error %d\n", rc);
	}
	set = NULL;
	return &msr_interlagos_plugin.base;
}
