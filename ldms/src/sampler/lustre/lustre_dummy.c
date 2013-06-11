/**
 * \file lustre_dummy.c
 * \brief Lustre dummy data sampler.
 *
 * This is only a dummy sampler which produces only counter and comp_id metrics.
 * The sole purpose of this sampler is for testing the plugin when the plugin
 * got compiled outside src/.
 *
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
#include "ldms.h"
#include "ldmsd.h"

#include "str_map.h"
#include "lustre_sampler.h"

static uint64_t counter;
ldms_set_t set;
FILE *mf;
ldmsd_msg_log_f msglog;
union ldms_value comp_id;
ldms_metric_t compid_metric_handle;
ldms_metric_t counter_metric_handle;

static int create_metric_set(const char *path)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc;

	/*
	 * Process the file once first to determine the metric set size.
	 */
	rc = ldms_get_metric_size("component_id", LDMS_V_U64,
				  &tot_meta_sz, &tot_data_sz);


	rc = ldms_get_metric_size("meminfo_counter", LDMS_V_U64, &meta_sz, &data_sz);
	tot_meta_sz += meta_sz;
	tot_data_sz += data_sz;

	/* Create the metric set */
	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
		return rc;

	compid_metric_handle = ldms_add_metric(set, "component_id", LDMS_V_U64);
	if (!compid_metric_handle)
		goto err;

	counter_metric_handle = ldms_add_metric(set, "meminfo_counter", LDMS_V_U64);
	if (!counter_metric_handle)
		goto err;

	return 0;

 err:
	ldms_set_release(set);
	return rc;
}

/**
 * \brief Configuration
 *
 * config name=meminfo component_id=<comp_id> set=<setname>
 *     comp_id     The component id value.
 *     setname     The set name.
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;

	value = av_value(avl, "component_id");
	if (value)
		comp_id.v_u64 = strtol(value, NULL, 0);

	value = av_value(avl, "set");
	if (value)
		create_metric_set(value);

	return 0;
}

static ldms_set_t get_set()
{
	return set;
}

static int sample(void)
{

	if (!set) {
		msglog("meminfo: plugin not initialized\n");
		return EINVAL;
	}
	ldms_begin_transaction(set);
	ldms_set_metric(compid_metric_handle, &comp_id);

	/* Set the counter */
	union ldms_value v = { .v_u64 = ++counter};
	ldms_set_metric(counter_metric_handle, &v);

 out:
	ldms_end_transaction(set);
	return 0;
}

static void term(void)
{
	if (set)
		ldms_destroy_set(set);
	set = NULL;
}

static const char *usage(void)
{
	return  "config name=meminfo component_id=<comp_id> set=<setname>\n"
		"    comp_id     The component id value.\n"
		"    setname     The set name.\n";
}

static struct ldmsd_sampler meminfo_plugin = {
	.base = {
		.name = "meminfo",
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
	return &meminfo_plugin.base;
}
