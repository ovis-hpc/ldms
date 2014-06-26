/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of Sandia nor the names of any contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
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

void oparser_service_conf_init(FILE *log_file, sqlite3 *_db, char *read_buf,
							char *value_buf);
void oparser_service_conf_parser(FILE *_conf);
void oparser_services_to_sqlite(sqlite3 *db);

#endif /* SERVICE_PASER_H_ */
