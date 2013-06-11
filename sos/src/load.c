/*
 * Copyright (c) 2012 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012 Sandia Corporation. All rights reserved.
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
 * Author: Tom Tucker tom at ogc dot us
 */

#include <sys/time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "sds.h"
#include "idx.h"
#include "oidx.h"

idx_t comp_type_db;
idx_t comp_type_metric_db;

struct metric_s {
	sds_t sds;
	char *metric_name;
	char *path;
};

int main(int argc, char *argv[])
{
	char *s;
	static char buf[128];
	static char comp_type[12];
	static char metric_name[12];
	static char comp_type_metric_key[32];
	uint64_t comp_id, metric_value, secs, msecs;
	ssize_t cnt = 0;
	struct timeval tv0;
	struct timeval tv1;
	struct timeval tvres;
	struct timeval tvsum = { 0, 0 };
	static int yes=1;
	struct metric_s *m;

	/*
	 * ComponentType IDX keeps track of component type
	 * subdirectories we've already created
	 */
	comp_type_db = idx_create();

	/*
	 * ComponentType-Metric IDX to keep track of databases we've
	 * already created
	 */
	comp_type_metric_db = idx_create();

	while ((s = fgets(buf, sizeof(buf), stdin)) != NULL) {
		sscanf(buf, "%[^,],%[^,],%ld,%ld,%ld,%ld",
		       comp_type, metric_name,
		       &comp_id, &metric_value, &secs, &msecs);

		/* Add a component type directory if one does not
		 * already exist
		 */
		if (!idx_find(comp_type_db, &comp_type, 2)) {
			mkdir(comp_type, 0777);
			idx_add(comp_type_db, &comp_type, 2, strdup(comp_type));
		}

		sprintf(comp_type_metric_key, "%s-%s", comp_type, metric_name);
		m = idx_find(comp_type_metric_db,
			     comp_type_metric_key,
			     strlen(comp_type_metric_key));
		if (!m) {
			m = malloc(sizeof *m);
			m->metric_name = strdup(metric_name);
			char *s = malloc(strlen(metric_name) + 1 + 2);
			sprintf(s, "%s/%s", comp_type, metric_name);
			m->path = s;
#if 0
			m->sds = sds_open(m->path,
					  SDS_COMP_IDX | SDS_TIME_IDX,
					  O_CREAT | O_RDWR, 0660);
#endif
			idx_add(comp_type_metric_db,
				comp_type_metric_key, strlen(comp_type_metric_key),
				m);
		}
		struct sds_tuple_s tuple;
		tuple.value = metric_value;
		tuple.timestamp = msecs;
		tuple.comp_id = comp_id;
		gettimeofday(&tv0, NULL);
#if 0
		if (sds_add(m->sds, &tuple))
			goto err;
#endif
		gettimeofday(&tv1, NULL);
		timersub(&tv1, &tv0, &tvres);
		timeradd(&tvsum, &tvres, &tvsum);
		cnt++;
	}
 out:
	printf("cnt %d seconds %ld msecs %ld\n", cnt, tvsum.tv_sec, tvsum.tv_usec);
	sds_term();
	return 0;
 err:
	printf("Exiting due to sds_add error\n");
	return 1;
}
