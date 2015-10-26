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

#define DEFAULT_NUM_METRICS 10
#define DEFAULT_NUM_SETS 1

static ldms_set_t *set_array;
static ldmsd_msg_log_f msglog;
static char *producer_name;
static ldms_schema_t schema;
static char *default_base_set_name = "set";
static char *base_set_name ;
static int num_sets;
static int num_metrics;
static char *default_schema_name = "test_sampler";

static int create_metric_set(const char *schema_name)
{
	int rc, i, j;
	ldms_set_t set;
	union ldms_value v;
	char *s;
	char metric_name[128];
	char instance_name[128];

	schema = ldms_schema_new(schema_name);
	if (!schema)
		return ENOMEM;

	for (i = 0; i < num_metrics; i++) {
		snprintf(metric_name, 127, "metric_%d", i);
		rc = ldms_schema_metric_add(schema, metric_name, LDMS_V_U64);
		if (rc < 0) {
			rc = ENOMEM;
			goto free_schema;
		}
	}

	set_array = calloc(num_sets, sizeof(*set));
	if (!set_array) {
		rc = ENOMEM;
		goto free_schema;
	}

	for (i = 0; i < num_sets; i++) {
		snprintf(instance_name, 127, "%s_%d", base_set_name, i);
		set = ldms_set_new(instance_name, schema);
		if (!set) {
			rc = ENOMEM;
			goto free_sets;
		}

		for (j = 0; j < num_metrics; j++) {
			v.v_u64 = j;
			ldms_metric_set(set, j, &v);
		}
		set_array[i] = set;
		ldms_set_producer_name_set(set, producer_name);
	}

	return 0;

free_sets:
	for (i = 0; i < num_sets; i++) {
		if (set_array[i]) {
			ldms_set_delete(set_array[i]);
			set_array[i] = NULL;
		}
	}

free_schema:
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	return rc;
}

static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *sname;
	char *s;
	void * arg = NULL;
	int rc;

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, "test_sampler: missing producer\n");
		return ENOENT;
	}

	sname = av_value(avl, "schema");
	if (!sname)
		sname = default_schema_name;
	if (strlen(sname) == 0){
		msglog(LDMSD_LERROR, "test_sampler: schema name invalid.\n");
		return EINVAL;
	}

	base_set_name = av_value(avl, "base");
	if (!base_set_name)
		base_set_name = default_base_set_name;

	s = av_value(avl, "num_sets");
	if (!s)
		num_sets = DEFAULT_NUM_SETS;
	else
		num_sets = atoi(s);

	s = av_value(avl, "num_metrics");
	if (!s)
		num_metrics = DEFAULT_NUM_METRICS;
	else
		num_metrics = atoi(s);

	rc = create_metric_set(sname);
	if (rc) {
		msglog(LDMSD_LERROR, "test_sampler: failed to create metric sets.\n");
		return rc;
	}
	return 0;
}

static ldms_set_t get_set()
{
	assert(0 == "not implemented");
}

static int sample(void)
{
	int rc;
	union ldms_value v;
	ldms_set_t set;

	if (!set_array) {
		msglog(LDMSD_LDEBUG, "test_sampler: plugin not initialized\n");
		return EINVAL;
	}

	int i, j;
	for (i = 0; i < num_sets; i++) {
		set = set_array[i];
		ldms_transaction_begin(set);

		for (j = 0; j < num_metrics; j++) {
			v.v_u64 = ldms_metric_get_u64(set, j);
			v.v_u64++;
			ldms_metric_set(set, j, &v);
		}
		ldms_transaction_end(set);
	}
	return 0;
}

static void term(void)
{
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	int i;
	for (i = 0; i < num_sets; i++) {
		ldms_set_delete(set_array[i]);
		set_array[i] = NULL;
	}
	free(set_array);
}

static const char *usage(void)
{
	return  "config name=test_sampler producer=<prod_name> [base=<base>] [schema=<sname>]\n"
		"	[num_sets=<nsets>] [num_metrics=<nmetrics>]\n"
		"    <prod_name>  The producer name\n"
		"    <base>       The base of set names\n"
		"    <sname>      Optional schema name. Defaults to 'test_sampler'\n"
		"    <nsets>      Number of sets\n"
		"    <nmetrics>   Number of metrics\n";

}

static struct ldmsd_sampler test_sampler_plugin = {
	.base = {
		.name = "test_sampler",
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
	return &test_sampler_plugin.base;
}
