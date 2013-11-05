/*
 * store_example.c
 *
 *  Created on: Apr 10, 2013
 *      Author: nichamon
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ovis_util/util.h>

#include "me.h"
#include "me_interface.h"

static pthread_mutex_t cfg_lock;
static char *path = "output";
static me_log_fn msglog;

struct me_store_example {
	struct me_store base;
	char *filename;
	FILE *f;
};

static const char *usage(void)
{
	return "	config name=store_example path=<path>\n"
		"	   - Set the file path\n"
		"	   <path>	The file path\n";
}

static int flush_store_ex(struct me_store *strg)
{
	struct me_store_example *strg_ex = (struct me_store_example *)strg;
	fflush(strg_ex->f);
	return 0;
}

static void print_output(struct me_store *strg, me_output_t output)
{
	struct me_store_example *strg_ex = (struct me_store_example *)strg;
	static count = 0;
	fprintf(strg_ex->f, "%j" PRIu32 "\t%s\t%" PRIu32 "\t%" PRIu8 "\n",
			output->ts.tv_sec, output->model_name,
			output->model_id, output->level);
	count++;
	if (count % 10 == 0)
		flush_store_ex(strg);
}

static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	value = av_value(avl, "path");
	if (!value)
		return EINVAL;

	pthread_mutex_lock(&cfg_lock);
	path = strdup(value);
	pthread_mutex_unlock(&cfg_lock);
	return 0;
}

static void term()
{
	pthread_mutex_lock(&cfg_lock);
	free(path);
	pthread_mutex_unlock(&cfg_lock);
}

static void destroy_store_ex(struct me_store *store)
{
	struct me_store_example *store_ex = (struct me_store_example *)store;
	fflush(store_ex->f);
	fclose(store_ex->f);
	free(store_ex);
}

static void *get_instance(struct attr_value_list *avlist,
		struct me_interface_plugin *pi)
{
	struct me_store_example *store = malloc(sizeof(*store));
	store->base.intf_base = pi;
	store->f = fopen(path, "w");
	if (!store->f)
		return NULL;
	fprintf(store->f, "timestamp\tmodel_name\tmodel_id\tcomp_id\tlevel\n");
	store->base.store = print_output;
	store->base.flush_store = flush_store_ex;
	store->base.destroy_store = destroy_store_ex;
	return (void *) store;
}

static struct me_interface_plugin store_ex = {
	.base = {
			.name = "store_example",
			.type = ME_INTERFACE_PLUGIN,
			.usage = usage,
			.config = config,
			.term = term
	},
	.type = ME_STORE,
	.get_instance = get_instance,
};

struct me_plugin *get_plugin(me_log_fn log_fn)
{

	store_ex.base.intf_pi = &store_ex;
	msglog = log_fn;
	return &store_ex.base;
}

static void __attribute__ ((constructor)) store_example_init();
static void store_example_init()
{
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) store_example_fini(void);
static void store_example_fini()
{
	pthread_mutex_destroy(&cfg_lock);
}
