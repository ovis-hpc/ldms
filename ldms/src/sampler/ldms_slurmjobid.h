#ifndef slurmjobid_h_seen
#define slurmjobid_h_seen

#include <stdbool.h>
/* name all samplers will used for slurm jobid */
#define SLURM_JOBID_METRIC_NAME "slurm.jobid"

/*
 * A set of macros for inserting jobid column in most samplers.
 * Currently default is not to have jobids.  In v3, we should switch
 * this default to true.
 */

#define LDMS_JOBID_GLOBALS \
static bool with_jobid = false; \
static struct resource_info * slurmjobid_ri = NULL


/* create_metric_set part 1 */
#define LDMS_SIZE_JOBID_METRIC(setname,ms, tms, ds, tds, cnt, rc, l) \
if (with_jobid) { \
	resource_info_manager rim = ldms_get_rim(); \
	slurmjobid_ri = get_resource_info(rim, SLURM_JOBID_METRIC_NAME); \
	if (! slurmjobid_ri) { \
		l(LDMS_LERROR, #setname " requested slurm jobid, " \
			"but slurmjobid not configured/loaded\n"); \
		return ENOENT; \
	} \
	l(LDMS_LDEBUG, #setname " got slurm jobid\n"); \
	rc = ldms_get_metric_size(SLURM_JOBID_METRIC_NAME, LDMS_V_U64, \
		&ms, &ds); \
	if (rc) \
		return rc; \
	tms += ms; \
	tds += ds; \
	cnt++; \
} else  \
	l(LDMS_LDEBUG, #setname " config without jobid\n")

/* create_metric_set part 2 */
#define LDMS_ADD_JOBID_METRIC(table,mno,set,rc,errlabel,cid) \
        if (with_jobid && slurmjobid_ri) { \
                table[mno] = ldms_add_metric(set, SLURM_JOBID_METRIC_NAME, \
                                 LDMS_V_U64); \
                if (!table[mno]) { \
                        rc = ENOMEM; \
                        goto errlabel; \
                } \
                ldms_set_user_data(table[mno], cid); \
                mno++; \
        }

/* add with_jobid config flag */
#define LDMS_CONFIG_JOBID_METRIC(value,avl) \
        value = av_value(avl, "with_jobid"); \
        if (value && strcmp(value,"1")==0) \
                with_jobid = true

/* get the most recently updated value from the other plugin. */
#define LDMS_JOBID_SAMPLE(lv,table,mno) \
        if (with_jobid && slurmjobid_ri) { \
                update_resource_info(slurmjobid_ri); \
                lv.v_u64 = slurmjobid_ri->v.u64; \
                ldms_set_metric(table[mno], &lv); \
                mno++; \
        }

/* clean up reference counted connection to other sampler data */
#define LDMS_JOBID_TERM \
	release_resource_info(slurmjobid_ri); \
        slurmjobid_ri = NULL

#define LDMS_JOBID_DESC \
	"    id          0/1 [0] 1:use jobid metric.\n"












#endif
