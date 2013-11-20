/*
 * consumer_intf_ex_a.c
 *
 *  Created on: Apr 11, 2013
 *      Author: nichamon
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "me.h"
#include "me_interface.h"

struct csm_output {
	int time;
	int level;
	char m_name[64];
};

static struct csm_output *csm_o;

static void *send_output(me_output_t output,
				void **o, size_t *o_sz)
{
	csm_o->time = output->ts.tv_sec;
	csm_o->level = output->level;
	strcpy(csm_o->m_name, output->model_cfg->name);
	*o = csm_o;
	*o_sz = sizeof(*csm_o);
	return NULL;
}

static void term()
{
	free(csm_o);
}

static struct me_interface_plugin csm_ex = {
	.base = {
			.name = "consumer_intf_ex",
			.type = ME_INTERFACE_PLUGIN,
			.term = term
	},
	.type = ME_CONSUMER,
	.format_input_data = NULL,
	.format_output_data = send_output
};

struct me_plugin *get_plugin()
{
	csm_o = malloc(sizeof(*csm_o));
	csm_ex.base.intf_pi = &csm_ex;
	return &csm_ex.base;
}

