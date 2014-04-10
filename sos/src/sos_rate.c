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
 * Author: Narate Taerat (narate@ogc.us)
 */

/*
 * This is a utility to generate *rate* data from a given SOS.
 * The output is also written in SOS (with the default name <ORIGINAL>_rate).
 * The rate is defined as (value@t_1 - value@t_2) / (t_2 - t_1) (obviously
 * the values must be of the same component).
 */

#include <stdio.h>
#include <unistd.h>
#include <linux/limits.h>
#include <fcntl.h>
#include <stdlib.h>

#include "sos.h"
#include "mds.h"

#define SR_OPTS "i:o:h"

#define eprintf(...) fprintf(stderr, __VA_ARGS__)

#define MAX_COMP 1048576

void usage()
{
	printf(
"Usage: sos_rate -i INPUT_SOS [OPTIONS]\n"
"\n"
"OPTIONS:\n"
"    -h        Print this help message.\n"
"    -i ISOS   Specify input SOS.\n"
"    -o OSOS   Specify output SOS (the default is <ISOS>_rate).\n"
	);
	exit(0);
}

/**
 * \param sec1
 * \param sec2
 * \param usec1
 * \param usec2
 * \returns sec1.usec1 - sec2.usec2
 */
inline
double diff_time(uint32_t sec1, uint32_t usec1,
		uint32_t sec2, uint32_t usec2)
{
	return (0.0 + sec1 - sec2) + (0.0 + usec1 - usec2)*(1e-6);
}

int main(int argc, char **argv)
{
	char o;
	char *isos_path = NULL;
	char *osos_path = NULL;
	char osos_path_tmp[PATH_MAX];

	while ( (o = getopt(argc, argv, SR_OPTS)) != -1 ) {
		switch (o) {
		case 'i':
			isos_path = optarg;
			break;
		case 'o':
			osos_path = optarg;
			break;
		case 'h':
		default:
			usage();
		}
	}

	if (!isos_path) {
		printf("An input is needed ...");
		usage();
	}

	if (!osos_path) {
		sprintf(osos_path_tmp, "%s_rate", isos_path);
		osos_path = osos_path_tmp;
	}

	sos_t isos = sos_open(isos_path, O_RDWR);
	if (!isos) {
		perror("ERROR:");
		eprintf("ERROR: Cannot open INPUT sos: %s\n", isos_path);
		exit(-1);
	}

	sos_t osos = sos_open(osos_path, O_RDWR | O_CREAT, 0660,
			&ovis_metric_class);
	if (!osos) {
		perror("ERROR:");
		eprintf("ERROR: Cannot open OUTPUT sos: %s\n", osos_path);
		exit(-1);
	}

	ovis_record_s *prev_rec = calloc(MAX_COMP, sizeof(ovis_record_s));

	sos_iter_t iter = sos_iter_new(isos, MDS_TV_SEC);
	sos_obj_t obj;

	ovis_record_s rec;

	int rc;
	for (rc = sos_iter_begin(iter); !rc; rc = sos_iter_next(iter)) {
		obj = sos_iter_obj(iter);
		OBJ2OVISREC_S(isos, obj, rec);
		uint32_t comp_id = rec.comp_id;
		ovis_record_t p = prev_rec + comp_id;
		if (p->sec) {
			// if it is 0, this record
			// will be the first record. Thus, no need to do anything except
			// update the last seen record.
			//
			// If it is not 0, then we need to calculate the rate and store it :D
			/*
			 * NOTE: I abusively use 8 bytes of uint64_t to store double
			 *       from the calculation.
			 */
			ovis_record_s rec2;
			double dv, dt, rate;
			rec2 = rec;
			if (rec.value >= p->value)
				dv = rec.value - p->value;
			else // if counter overflow
				dv = UINT64_MAX - p->value + rec.value;

			dt = diff_time(rec.sec, rec.usec,
					p->sec, p->usec);

			if (dt > 1e-07) { // if dt > 0, store it. Otherwise, do nothing
				rate = dv / dt;
				rec2.value = *(uint64_t*)&rate;
				ovis_rec_store(osos, &rec2);
			}
		}
		// Update the last seen record on the component
		*p = rec;
	}

	sos_commit(osos, ODS_COMMIT_ASYNC);
	return 0;
}
