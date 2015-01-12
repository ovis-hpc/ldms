#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include "ldms.h"
#include "ldmsd.h"



#define PROC_FILE "/proc/diskstats"

static char *procfile = PROC_FILE;
#define NFIELD 11
static char *fieldname[NFIELD] =
	{"reads_comp", "reads_merg", "sect_read", "time_read",
	"writes_comp","writes_merg", "sect_written", "time_write",
	"ios_in_progress", "time_ios", "weighted_time"};

static ldms_set_t set;
static FILE *mf;
static ldms_metric_t *metric_table;
static ldmsd_msg_log_f msglog;
static uint64_t comp_id;

static int create_metric_set(const char *path)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	union ldms_value v[NFIELD];
	int rc, metric_count, metric_no;
	char *s;
	char lbuf[256];
	char metric_name[128];
	char name[64];
	int i, junk1, junk2;
	int num_device = 0;
	ldms_metric_t m;

	mf = fopen(procfile, "r");

	if(!mf) {
		msglog(LDMS_LDEBUG,"Could not open the diskstats file '%s'...exiting\n",
			 procfile);
		return ENOENT;
	}

	metric_count = 0;
	tot_meta_sz = 0;
	tot_data_sz = 0;

	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%d %d %s %" PRIu64 " %" PRIu64
			" %" PRIu64 " %" PRIu64 " %" PRIu64 " %"
			PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
			" %" PRIu64 " %" PRIu64 "\n", &junk1, &junk2, name,
			&v[0].v_u64, &v[1].v_u64, &v[2].v_u64, &v[3].v_u64,
			&v[4].v_u64, &v[5].v_u64, &v[6].v_u64, &v[7].v_u64,
			&v[8].v_u64, &v[9].v_u64, &v[10].v_u64);

		if (rc != 14)
			break;

		for (i = 0; i < NFIELD; i++) {
			snprintf(metric_name, 128, "%s#%s", fieldname[i], name);
			rc = ldms_get_metric_size(metric_name,
					LDMS_V_U64, &meta_sz, &data_sz);

			if (rc)
				return rc;

			tot_meta_sz += meta_sz;
			tot_data_sz += data_sz;
			metric_count++;
		}
	} while (s);

	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
		return rc;

	metric_table = calloc(metric_count, sizeof(ldms_metric_t));
	if (!metric_table)
		goto err;

	/*
	 * Process the file again to define all the metrics.
	 */
	metric_no = 0;

	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if(!s)
			break;
		rc = sscanf(lbuf, "%d %d %s %" PRIu64 " %" PRIu64
			" %" PRIu64 " %" PRIu64 " %" PRIu64 " %"
			PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
			" %" PRIu64 " %" PRIu64 "\n", &junk1, &junk2, name,
			&v[0].v_u64, &v[1].v_u64, &v[2].v_u64, &v[3].v_u64,
			&v[4].v_u64, &v[5].v_u64, &v[6].v_u64, &v[7].v_u64,
			&v[8].v_u64, &v[9].v_u64, &v[10].v_u64);
		if (rc != 14)
			break;

		for (i = 0; i < NFIELD; i++) {
			snprintf(metric_name, 128, "%s#%s", fieldname[i], name);
			m = ldms_add_metric(set, metric_name, LDMS_V_U64);
			if (!m) {
				rc = ENOMEM;
				goto err;
			}
			ldms_set_user_data(m, comp_id);
			metric_table[metric_no++] = m;
		}

	} while (s);
	return 0;

 err:
	ldms_destroy_set(set);
	return rc;

}


static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
        char *value;

        value = av_value(avl, "component_id");
        if (value)
                comp_id = strtol(value, NULL, 0);

        value = av_value(avl, "set");
        if (value)
                create_metric_set(value);

        return 0;
}


static int sample(void)
{
	int rc, i;
	int metric_no;
	char *s;
	char name[64];
	char lbuf[256];
	char metric_name[128];
	int junk1, junk2;
	union ldms_value v[NFIELD];

	if (!set) {
		msglog(LDMS_LDEBUG,"diskstats: plugin not initialized\n");
		return EINVAL;
	}
	ldms_begin_transaction(set);
	metric_no = 0;
	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%d %d %s %" PRIu64 " %" PRIu64
			 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %"
			 PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
			 " %" PRIu64 " %" PRIu64 "\n", &junk1, &junk2, name,
			 &v[0].v_u64, &v[1].v_u64, &v[2].v_u64,
			 &v[3].v_u64, &v[4].v_u64, &v[5].v_u64,
			 &v[6].v_u64, &v[7].v_u64, &v[8].v_u64,
			 &v[9].v_u64, &v[10].v_u64);
		if (rc != 14) {
			rc = EINVAL;
			goto out;
		}

		for (i = 0; i < NFIELD; i++)
			ldms_set_metric(metric_table[metric_no++], &v[i]);

	} while (s);

out:
	ldms_end_transaction(set);
	return 0;

}

static ldms_set_t get_set()
{
	return set;
}

static void term(void)
{
	if (mf)
		fclose(mf);
	mf = 0;

	if (set)
		ldms_destroy_set(set);
	set = NULL;
}

static const char *usage(void)
{
        return  "config name=procdiskstats component_id=<comp_id> set=<setname>\n"
                "    comp_id     The component id value.\n"
                "    setname     The set name.\n";
}

static struct ldmsd_sampler procdiskstats_plugin = {
        .base = {
                .name = "procdiskstats",
                .term = term,
                .config = config,
                .usage = usage,
        },
        .get_set = get_set,
        .sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &procdiskstats_plugin.base;
}

