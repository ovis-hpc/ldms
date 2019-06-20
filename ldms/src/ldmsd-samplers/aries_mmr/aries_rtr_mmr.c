/**
 * \file aries_rtr_mmr.c
 * \brief aries_rtr_mmr is an aries_mmr with metric filter for router.
 */
#include "ldmsd.h"
#include "aries_mmr.h"

static
const char *aries_rtr_mmr_desc(ldmsd_plugin_inst_t pi)
{
	return "aries_rtr_mmr - aries network metric provider (reads gpcd mmr) "
	       "for router";
}

static
char *_help = "\
aries_rtr_mmr configuration synopsis:\n\
    config name=INST [COMMON_OPTIONS] file=<PATH> aries_rtr_id=<STR>\n\
\n\
Option descriptions:\n\
    file          A path to metric config file.\n\
    aries_rtr_id  A string identifying router ID.\n\
";

static
const char *aries_rtr_mmr_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
struct aries_mmr_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "aries_rtr_mmr",

		.desc   = aries_rtr_mmr_desc,
		.help   = aries_rtr_mmr_help,

		/* Use `aries_mmr` routines */
		.init   = aries_mmr_init,
		.del    = aries_mmr_del,
		.config = aries_mmr_config,
	},
	/* filter for router metrics */
	.filter = filterKeepRouter,
};

ldmsd_plugin_inst_t new()
{
	aries_mmr_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
