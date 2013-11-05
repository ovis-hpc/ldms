/*
 * service_paser.h
 *
 *  Created on: Oct 24, 2013
 *      Author: nichamon
 */

#ifndef SERVICE_PASER_H_
#define SERVICE_PASER_H_

#include <sys/queue.h>
#include <stddef.h>

enum ovis_service {
	OVIS_LDMSD_SAMPLER = 0,
	OVIS_LDMSD_AGG,
	OVIS_BALER,
	OVIS_ME,
	OVIS_KOMONDOR,
	OVIS_NUM_SERVICES
};

static enum ovis_service str_to_enum_ovis_service(char *s)
{
	if (strcmp(s, "ldmsd_sampler") == 0)
		return OVIS_LDMSD_SAMPLER;
	else if (strcmp(s, "ldmsd_aggregator") == 0)
		return OVIS_LDMSD_AGG;
	else if (strcmp(s, "balerd") == 0)
		return OVIS_BALER;
	else if (strcmp(s, "me") == 0)
		return OVIS_ME;
	else if (strcmp(s, "komondor") == 0)
		return OVIS_KOMONDOR;
	else
		return -1;
}

static char *enum_ovis_service_to_str(enum ovis_service s)
{
	switch (s) {
	case OVIS_LDMSD_SAMPLER:
		return "ldmsd_sampler";
	case OVIS_LDMSD_AGG:
		return "ldmsd_aggregator";
	case OVIS_BALER:
		return "balerd";
	case OVIS_KOMONDOR:
		return "komondor";
	case OVIS_ME:
		return "me";
	default:
		return NULL;
	}
}

struct oparser_host_services {
	struct oparser_cmd_queue queue[OVIS_NUM_SERVICES];
};

void oparser_service_conf_init(sqlite3 *_db);
void oparser_service_conf_parser(FILE *_conf);
void oparser_services_to_sqlite(sqlite3 *db);

#endif /* SERVICE_PASER_H_ */
