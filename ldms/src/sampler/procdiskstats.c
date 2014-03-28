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

static char **devices;
static int ndevices;

ldms_set_t set;
FILE *mf;
ldms_metric_t *metric_table;
ldmsd_msg_log_f msglog;
uint64_t comp_id;

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
	int i, j, junk1, junk2;
	int nfound_device = 0;
	uint64_t temp_comp_id = comp_id;
	ldms_metric_t m;

	mf = fopen(procfile, "r");

	if(!mf) {
		msglog("Could not open the diskstats file '%s'...exiting\n",
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

		if (ndevices > 0) {

			if (nfound_device == ndevices)
				/* Found all specified devices */
				continue;

			for (j = 0; j < ndevices; j++) {
				if (0 == strcmp(devices[j], name)) {
					for (i = 0; i < NFIELD; i++) {
						snprintf(metric_name, 128,
							"%s#%s", fieldname[i],
									name);
						rc = ldms_get_metric_size(
								metric_name,
								LDMS_V_U64,
								&meta_sz,
								&data_sz);

						if (rc)
							return rc;

						tot_meta_sz += meta_sz;
						tot_data_sz += data_sz;
						metric_count++;
					}
					nfound_device++;
					break;
				}
			}
		} else {
			for (i = 0; i < NFIELD; i++) {
				snprintf(metric_name, 128, "%s#%s",
						fieldname[i], name);
				rc = ldms_get_metric_size(metric_name,
						LDMS_V_U64, &meta_sz,
							&data_sz);

				if (rc)
					return rc;

				tot_meta_sz += meta_sz;
				tot_data_sz += data_sz;
				metric_count++;
			}
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
	nfound_device = 0;

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

		if (ndevices > 0) {
			if (nfound_device == ndevices)
				/* Found all specified devices */
				continue;

			for (j = 0; j < ndevices; j++) {
				if (0 == strcmp(devices[j], name)) {
					for (i = 0; i < NFIELD; i++) {
						snprintf(metric_name, 128,
							"%s#%s", fieldname[i],
									name);
						m = ldms_add_metric(set,
								metric_name,
								LDMS_V_U64);
						if (!m) {
							rc = ENOMEM;
							goto err;
						}
						ldms_set_user_data(m,
								temp_comp_id);
						metric_table[metric_no++] = m;
					}
					nfound_device++;
					break;
				}
			}
		} else {
			for (i = 0; i < NFIELD; i++) {
				snprintf(metric_name, 128, "%s#%s",
						fieldname[i], name);
				m = ldms_add_metric(set, metric_name,
							LDMS_V_U64);
				if (!m) {
					rc = ENOMEM;
					goto err;
				}
				ldms_set_user_data(m, temp_comp_id);
				metric_table[metric_no++] = m;
			}
		}
		temp_comp_id++;

	} while (s);
	return 0;

 err:
	ldms_set_release(set);
	return rc;

}

static int add_device(struct attr_value_list *avl)
{
	char *value = av_value(avl, "device");

	if (!value) {
		/*
		 * If no devices are give, collect all devices.
		 */
		ndevices = -1;
		devices = NULL;
		return 0;
	}

	char *value_tmp = strdup(value);
	if (!value_tmp)
		goto enomem;

	char *ptr, *token;
	token = strtok_r(value_tmp, ",", &ptr);
	while (token) {
		ndevices++;
		token = strtok_r(NULL, ",", &ptr);
	}
	free(value_tmp);

	devices = malloc(ndevices * sizeof(char *));
	if (!devices)
		goto enomem;

	int i, j;
	token = strtok_r(value, ",", &ptr);
	devices[0] = strdup(token);
	if (!devices[0])
		goto free_devices;

	for (i = 1; i < ndevices; i++) {
		token = strtok_r(NULL, ",", &ptr);
		devices[i] = strdup(token);
		if (!devices[i])
			goto free_devices;
	}

	return 0;

free_devices:
	for (j = 0; j < i; j++)
		free(devices[j]);
	free(devices);
enomem:
	msglog("procdiskstat: add_device: Out of memory.\n");
	return ENOMEM;

}

static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *attr;
	int rc;

	rc = add_device(avl);
	if (rc)
		return rc;

	attr = "component_id";
	value = av_value(avl, attr);
	if (value)
		comp_id = strtol(value, NULL, 0);
	else
		goto enoent;

	attr = "set";
	value = av_value(avl, attr);
	if (value)
		create_metric_set(value);
	else
		goto enoent;

	return 0;
enoent:
	msglog("procdiskstat: requires '%s'\n", attr);
	return ENOENT;
}


static int sample(void)
{
	int rc, i, j;
	int index;
	char *s;
	char name[64];
	char lbuf[256];
	char metric_name[128];
	int junk1, junk2, nfound_device;
	union ldms_value v[NFIELD];

	if (!set) {
		msglog("diskstats: plugin not initialized\n");
		return EINVAL;
	}
	ldms_begin_transaction(set);
	index = 0;
	nfound_device = 0;
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

		if (ndevices > 0) {
			if (nfound_device == ndevices)
				/* Found all specified devices */
				continue;

			for (j = 0; j < ndevices; j++) {
				if (0 == strcmp(devices[j], name)) {
					for (i = 0; i < NFIELD; i++)
						ldms_set_metric(
							metric_table[index++],
								&v[i]);
					nfound_device++;
					break;
				}
			}
		} else {
			for (i = 0; i < NFIELD; i++)
				ldms_set_metric(metric_table[index++], &v[i]);
		}
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
        return  "config name=procdiskstats component_id=<comp_id> set=<setname> device=<device>\n"
                "    comp_id     The component id value.\n"
                "    setname     The set name.\n"
                "    device	 The comma-separated list of devices\n";
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

