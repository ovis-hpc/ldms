#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <sys/time.h>
#include <limits.h>
#include "ldms.h"
#include "ldmsd.h"

#define PROC_FILE "/proc/diskstats"
#define SECTOR_SIZE_FILE_FMT "/sys/block/%s/queue/hw_sector_size"

static char *procfile = PROC_FILE;
#define NRAW_FIELD 11
#define NDERIVED_FIELD 2
#define NFIELD (NRAW_FIELD + NDERIVED_FIELD)
static char *fieldname[NFIELD] = {
	/* raw */
	"reads_comp",
	"reads_merg",
	"sect_read",
	"time_read",
	"writes_comp",
	"writes_merg",
	"sect_written",
	"time_write",
	"ios_in_progress",
	"time_ios",
	"weighted_time",

	/* derived */
	"disk.byte_read",	/* number of sectors read * sector_size */
	"disk.byte_written",	/* number of sectors write * sector_size */
};

#define SECT_READ_IDX 2
#define SECT_WRITE_IDX 6

static char **devices;
static int ndevices;
static int monitored_ndevices;

int *sector_sz;

ldms_set_t set;
FILE *mf;
ldms_metric_t *metric_table;
ldms_metric_t *rate_metric_table;
uint64_t *prev_value;
ldmsd_msg_log_f msglog;
uint64_t comp_id;

long USER_HZ; /* initialized in get_plugin() */
struct timeval _tv[2] = {0};
struct timeval *curr_tv = &_tv[0];
struct timeval *prev_tv = &_tv[1];

static int get_device_metrics_size(char *name, size_t *tot_meta_sz,
				size_t *tot_data_sz, int *metric_count)
{
	size_t meta_sz, data_sz;
	char metric_name[128];
	int i, rc;
	for (i = 0; i < NFIELD; i++) {
		snprintf(metric_name, 128,
			"%s#%s", fieldname[i], name);
		if (i < NRAW_FIELD) {
			/* raw metric */
			rc = ldms_get_metric_size(metric_name, LDMS_V_U64,
							&meta_sz, &data_sz);
		} else {
			/* derived */
			rc = ldms_get_metric_size(metric_name, LDMS_V_F,
							&meta_sz, &data_sz);
		}
		if (rc)
			return rc;

		(*tot_meta_sz) += meta_sz;
		(*tot_data_sz) += data_sz;

		/* rate */
		snprintf(metric_name, 128,
			"%s.rate#%s", fieldname[i], name);
		rc = ldms_get_metric_size(metric_name, LDMS_V_F,
						&meta_sz, &data_sz);

		if (rc)
			return rc;

		(*tot_meta_sz) += meta_sz;
		(*tot_data_sz) += data_sz;

		/* count the raw metric and the rate metric as one */
		(*metric_count)++;
	}

	return 0;
}

static int add_device_metrics(char *name, int comp_id, int *_metric_no)
{
	int metric_no = *_metric_no;
	ldms_metric_t m;
	char metric_name[128];
	int i, rc;
	for (i = 0; i < NFIELD; i++) {
		/* raw metric */
		snprintf(metric_name, 128, "%s#%s", fieldname[i], name);
		m = ldms_add_metric(set, metric_name, LDMS_V_U64);
		if (!m)
			return ENOMEM;

		ldms_set_user_data(m, comp_id);
		metric_table[metric_no] = m;

		/* rate metric */
		snprintf(metric_name, 128, "%s.rate#%s", fieldname[i], name);
		m = ldms_add_metric(set, metric_name, LDMS_V_F);
		if (!m)
			return ENOMEM;

		ldms_set_user_data(m, comp_id);
		rate_metric_table[metric_no] = m;
		metric_no++;
	}
	*_metric_no = metric_no;
	return 0;
}

#define DEFAULT_SECTOR_SZ 512
int get_sector_sz(char *device)
{
	int rc = 0;
	int result;
	FILE *f = NULL;
	char filename[FILENAME_MAX];
	sprintf(filename, SECTOR_SIZE_FILE_FMT, device);

	f = fopen(filename, "r");
	if (!f) {
		msglog("Failed to open %s\n", filename);
		return DEFAULT_SECTOR_SZ;
	}

	fseek(f, 0, SEEK_SET);
	char *s;
	do {
		s = fgets(filename, sizeof(filename), f);
		if (!s)
			break;
		rc = sscanf(filename, "%d", &result);

		if (rc != 1) {
			msglog("Failed to get the sector size of %s. "
					"The size is set to 512.\n", device);
			result = DEFAULT_SECTOR_SZ;
		}
	} while (s);
	fclose(f);
	return result;
}

static int create_metric_set(const char *path)
{
	size_t tot_meta_sz;
	size_t tot_data_sz;
	union ldms_value v[NFIELD];
	int rc, metric_count, metric_no;
	char *s;
	char lbuf[256];
	char name[64];
	int i, j, junk1, junk2;
	int nfound_device = 0;
	uint64_t temp_comp_id = comp_id;

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

			monitored_ndevices++;
			for (j = 0; j < ndevices; j++) {
				if (0 == strcmp(devices[j], name)) {
					rc = get_device_metrics_size(name,
						&tot_meta_sz, &tot_data_sz,
						&metric_count);
					if (rc)
						return rc;

					nfound_device++;
					break;
				}
			}
		} else {
			monitored_ndevices++;
			rc = get_device_metrics_size(name, &tot_meta_sz,
						&tot_data_sz, &metric_count);
			if (rc)
				return rc;
		}
		i++;
	} while (s);

	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
		return rc;

	metric_table = calloc(metric_count, sizeof(ldms_metric_t));
	if (!metric_table)
		goto err;

	rate_metric_table = calloc(metric_count, sizeof(ldms_metric_t));
	if (!rate_metric_table)
		goto err1;

	prev_value = calloc(metric_count, sizeof(*prev_value));
	if (!prev_value)
		goto err2;

	sector_sz = malloc(monitored_ndevices * sizeof(int));
	if (!sector_sz)
		goto err3;

	/*
	 * Process the file again to define all the metrics.
	 */
	metric_no = 0;
	nfound_device = 0;
	i = 0;

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
					rc = add_device_metrics(name, temp_comp_id,
								&metric_no);
					if (rc)
						goto err4;
					sector_sz[i] = get_sector_sz(name);
					nfound_device++;
					i++;
					break;
				}
			}
		} else {
			rc = add_device_metrics(name, temp_comp_id, &metric_no);
			if (rc)
				goto err4;
			sector_sz[i] = get_sector_sz(name);
			i++;
		}
		temp_comp_id++;

	} while (s);
	return 0;

err4:
	free(sector_sz);
err3:
	free(prev_value);
err2:
	free(rate_metric_table);
err1:
	free(metric_table);
err:
	ldms_destroy_set(set);
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

	monitored_ndevices = 0;

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

static float calculate_rate(int metric_no, uint64_t curr_v, float dt)
{
	uint64_t dv;
	if ((prev_value[metric_no] == 0) || (prev_value[metric_no] > curr_v))
		dv = 0;
	else
		dv = curr_v - prev_value[metric_no];
	float rate = (dv * 1.0 / USER_HZ) / dt * 100.0;
	return rate;
}

static void set_device_metrics(int *_metric_no, uint64_t *values, float dt,
							int _sector_sz)
{
	float rate;
	uint64_t derived;
	int i, metric_no;
	metric_no = *_metric_no;
	for (i = 0; i < NRAW_FIELD; i++) {
		/* raw */
		ldms_set_u64(metric_table[metric_no], values[i]);

		/* rate */
		rate = calculate_rate(metric_no, values[i], dt);
		ldms_set_float(rate_metric_table[metric_no], rate);

		prev_value[metric_no] = values[i];
		metric_no++;
	}

	/* read_bytes */
	derived = values[SECT_READ_IDX] * _sector_sz;
	ldms_set_float(metric_table[metric_no], derived);
	rate = calculate_rate(metric_no, derived, dt);
	ldms_set_float(rate_metric_table[metric_no], rate);
	prev_value[metric_no] = derived;
	metric_no++;

	/* write bytes */
	derived = values[SECT_WRITE_IDX] * _sector_sz;
	ldms_set_float(metric_table[metric_no], derived);
	rate = calculate_rate(metric_no, derived, dt);
	ldms_set_float(rate_metric_table[metric_no], rate);
	prev_value[metric_no] = derived;
	metric_no++;

	*_metric_no = metric_no;
}

static int sample(void)
{
	int rc, i, j;
	int metric_no;
	char *s;
	char name[64];
	char lbuf[256];
	char metric_name[128];
	int junk1, junk2, nfound_device;
	uint64_t v[NFIELD];
	struct timeval diff_tv;
	struct timeval *tmp_tv;
	float dt;

	rc = 0;

	if (!set) {
		msglog("diskstats: plugin not initialized\n");
		return EINVAL;
	}
	ldms_begin_transaction(set);
	gettimeofday(curr_tv, NULL);
	timersub(curr_tv, prev_tv, &diff_tv);
	dt = diff_tv.tv_sec + diff_tv.tv_usec / 1e06;

	metric_no = 0;
	nfound_device = 0;
	i = 0;
	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%d %d %s %" PRIu64 " %" PRIu64
			 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %"
			 PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
			 " %" PRIu64 " %" PRIu64 "\n", &junk1, &junk2, name,
			 &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
			 &v[6], &v[7], &v[8], &v[9], &v[10]);

		if (rc != (NRAW_FIELD + 3)) { /* + 3 for junk1, junk2 and name */
			rc = EINVAL;
			goto out;
		}

		if (ndevices > 0) {
			if (nfound_device == ndevices)
				/* Found all specified devices */
				continue;

			for (j = 0; j < ndevices; j++) {
				if (0 == strcmp(devices[j], name)) {
					set_device_metrics(&metric_no, v, dt, sector_sz[i]);
					nfound_device++;
					i++;
					break;
				}
			}
		} else {
			set_device_metrics(&metric_no, v, dt, sector_sz[i]);
			i++;
		}
	} while (s);
out:
	tmp_tv = curr_tv;
	curr_tv = prev_tv;
	prev_tv = tmp_tv;
	ldms_end_transaction(set);
	return rc;
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
	USER_HZ = sysconf(_SC_CLK_TCK);
	return &procdiskstats_plugin.base;
}
