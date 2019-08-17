#ifndef _SLURM_SAMPLER_H_
#define _SLURM_SAMPLER_H_

enum slurm_job_state {
	JOB_FREE = 0,
	JOB_STARTING = 1,
	JOB_RUNNING = 2,
	JOB_STOPPING = 3,
	JOB_COMPLETE = 4
};

#define COMPONENT_ID_MID(s)	0
#define JOB_ID_MID(s)		1
#define APP_ID_MID(s)		2
#define JOB_SLOT_LIST_TAIL_MID(s) 3
#define JOB_SLOT_LIST_MID(s)	4
#define JOB_STATE_MID(s)	5
#define JOB_SIZE_MID(s)		6
#define JOB_UID_MID(s)		7
#define JOB_GID_MID(s)		8
#define JOB_START_MID(s)	9
#define JOB_END_MID(s)		10
#define NODE_COUNT_MID(s)	11
#define TASK_COUNT_MID(s)	12
#define TASK_PID_MID(s)		13
#define TASK_RANK_MID(s)	(TASK_PID_MID(s) + ldms_metric_array_get_len(s, JOB_ID_MID(s)))
#define TASK_EXIT_STATUS_MID(s)	(TASK_PID_MID(s) + (2 * ldms_metric_array_get_len(s, JOB_ID_MID(s))))
#define JOB_USER_MID(s)	(TASK_PID_MID(s) + (3 * ldms_metric_array_get_len(s, JOB_ID_MID(s))))
#define JOB_NAME_MID(s)	(TASK_PID_MID(s) + (4 * ldms_metric_array_get_len(s, JOB_ID_MID(s))))
#define JOB_TAG_MID(s) (TASK_PID_MID(s) + (5 * ldms_metric_array_get_len(s, JOB_ID_MID(s))))

#endif
