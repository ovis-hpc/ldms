/*
 * ldmsd_translator.c
 *
 *  Created on: Jun 2, 2020
 *      Author: nnich
 */

#include "ldmsd.h"
#include "ldmsd_plugin.h"
#include "ldmsd_translator.h"

static
const char *translator_desc(ldmsd_plugin_inst_t inst)
{
	return "Base implementation of all translator.";
}

static
const char *translator_help(ldmsd_plugin_inst_t inst)
{
	return "...";
}

static
int translator_init(ldmsd_plugin_inst_t inst)
{
	ldmsd_translator_type_t translator = (void*)inst->base;

	pthread_mutex_init(&translator->lock, NULL);

	return 0;
}

static
void translator_del(ldmsd_plugin_inst_t inst)
{
	ldmsd_translator_type_t translator = (void*)inst->base;

	ldmsd_linfo("Plugin %s: Deleting ...\n", inst->inst_name);

	pthread_mutex_destroy(&translator->lock);
}

void *new()
{
	ldmsd_translator_type_t translator;
	translator = calloc(1, sizeof(*translator));
	if (!translator)
		return translator;

	translator->base.type_name = LDMSD_TRANSLATOR_TYPENAME;
	LDMSD_PLUGIN_VERSION_INIT(&translator->base.version);

	/* setup plugin base interface */
	translator->base.desc = translator_desc;
	translator->base.help = translator_help;
	translator->base.init = translator_init;
	translator->base.del = translator_del;
	translator->base.config = NULL;
	translator->base.query = NULL;

	/* setup translator interface */

	return translator;
}
