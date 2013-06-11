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

#include <stdio.h>
#include <unistd.h>
#include <linux/limits.h>
#include <fcntl.h>
#include <stdlib.h>
#include <float.h>

#include "sos.h"
#include "mds.h"

#define SS_OPTS "i:h"

void usage()
{
	printf( "Usage: sos_stat -i <SOS_PATH>\n");
	exit(0);
}

#define MAX_COMP_ID 1048576

#define AS_TS(_s, _u) ( ((uint64_t) _s << 32 ) | _u )
#define ARRAY_INIT(_a, _len, _v) do { \
	int __i; \
	for (__i=0; __i < _len; __i++) { \
		_a[__i] = _v; \
	} \
} while(0);

#define UPDATE_MIN(_min, _a) do { \
	if (_a < _min) { \
		_min = _a; \
	} \
} while(0);

#define UPDATE_MAX(_max, _a) do { \
	if (_a > _max) { \
		_max = _a; \
	} \
} while(0);

#define AS_DOUBLE(_a) ( *(double *)&_a )

const char* ts_str(uint64_t ts)
{
	static char str[BUFSIZ];
	uint32_t sec = ts >> 32;
	uint32_t usec = ts & 0xFFFFFFFF;
	sprintf(str, "%u.%u", sec, usec);
	return str;
}

int main(int argc, char **argv)
{
	if (argc < 2)
		usage();
	
	char opt;
	char *sos_path = NULL;

	while ((opt = getopt(argc, argv, SS_OPTS)) != -1 ) {
		switch (opt) {
		case 'i':
			sos_path = optarg; 
			break;
		case 'h':
		default:
			usage();
		}
	}

	if (!sos_path) {
		printf("ERROR: -i is needed");
		usage();
	}

	sos_t sos = sos_open(sos_path, O_RDWR);
	if (!sos) {
		perror("ERROR");
		printf("ERROR: Cannot open sos: %s\n", sos_path);
		exit(-1);
	}

	sos_iter_t iter = sos_iter_new(sos, MDS_TV_SEC);
	sos_obj_t obj = NULL;
	
	ovis_record_s rec;
	ovis_record_t r = &rec;

	ovis_record_s *prev_rec;
	prev_rec = calloc(MAX_COMP_ID, sizeof(ovis_record_s));

	uint32_t max_comp_id = 0;
	uint32_t min_comp_id = UINT32_MAX;

	uint64_t *max_ivalue = calloc(MAX_COMP_ID, sizeof(uint64_t));
	uint64_t *min_ivalue = malloc(MAX_COMP_ID * sizeof(uint64_t));
	ARRAY_INIT(min_ivalue, MAX_COMP_ID, UINT64_MAX);

	double *max_dvalue = malloc(MAX_COMP_ID * sizeof(double));
	ARRAY_INIT(max_dvalue, MAX_COMP_ID, DBL_MIN);
	double *min_dvalue = malloc(MAX_COMP_ID * sizeof(double));
	ARRAY_INIT(min_dvalue, MAX_COMP_ID, DBL_MAX);

	// ts := (sec << 32) | usec
	uint64_t *max_ts = calloc(MAX_COMP_ID, sizeof(uint64_t));
	uint64_t *min_ts = malloc(MAX_COMP_ID * sizeof(uint64_t));
	ARRAY_INIT(min_ts, MAX_COMP_ID, UINT64_MAX);

	uint32_t *count_vup = calloc(MAX_COMP_ID, sizeof(uint32_t));
	uint32_t *count_vdown = calloc(MAX_COMP_ID, sizeof(uint32_t));
	uint32_t *count_veq = calloc(MAX_COMP_ID, sizeof(uint32_t));

	while (obj = sos_iter_next(iter)) {
		// r is the current record
		// p is the previous record of the same comp_id;
		OBJ2OVISREC_T(sos, obj, r);
		ovis_record_t p = prev_rec + r->comp_id;
		if (p->sec) {
			uint32_t c = r->comp_id;
			UPDATE_MIN(min_comp_id, c);
			UPDATE_MAX(max_comp_id, c);
			UPDATE_MIN(min_ivalue[c], r->value);
			UPDATE_MAX(max_ivalue[c], r->value);
			UPDATE_MIN(min_dvalue[c], AS_DOUBLE(r->value));
			UPDATE_MAX(max_dvalue[c], AS_DOUBLE(r->value));
			UPDATE_MIN(min_ts[c], AS_TS(r->sec, r->usec));
			UPDATE_MAX(max_ts[c], AS_TS(r->sec, r->usec));

			if ( p->value < r->value ) {
				count_vup[c]++;
			} else if (p->value > r->value) {
				count_vdown[c]++;
			} else {
				count_veq[c]++;
			}
		}
		// update prev rec
		*p = *r;
	}
	
	uint64_t MIN_TS = UINT64_MAX;
	uint64_t MAX_TS = 0;
	uint64_t MIN_IVALUE = UINT64_MAX;
	uint64_t MAX_IVALUE = DBL_MIN;
	double MIN_DVALUE = DBL_MAX;
	double MAX_DVALUE = DBL_MIN;
	uint64_t COUNT_VUP = 0;
	uint64_t COUNT_VEQ = 0;
	uint64_t COUNT_DOWN = 0;

	// report statistics locally (for each comp_id)
	int c;
	printf("comp_id min_ts max_ts min_ivalue max_ivalue min_dvalue max_dvalue count_vup count_veq count_vdown\n");
	for (c = min_comp_id; c <= max_comp_id; c++) {
		printf("%d %s %s %lu %lu %lf %lf %d %d %d\n", c, 
				ts_str(min_ts[c]), ts_str(max_ts[c]),
				min_ivalue[c], max_ivalue[c],
				min_dvalue[c], max_dvalue[c],
				count_vup[c], count_veq[c], count_vdown[c]
		);
		UPDATE_MIN(MIN_TS, min_ts[c]);
		UPDATE_MAX(MAX_TS, max_ts[c]);
		UPDATE_MIN(MIN_IVALUE, min_ivalue[c]);
		UPDATE_MAX(MAX_IVALUE, max_ivalue[c]);
		UPDATE_MIN(MIN_DVALUE, min_dvalue[c]);
		UPDATE_MAX(MAX_DVALUE, max_dvalue[c]);
		COUNT_VUP += count_vup[c];
		COUNT_VEQ += count_veq[c];
		COUNT_DOWN += count_vdown[c];
	}
	// then report the summary
	printf("GLOBAL %s %s %lu %lu %lf %lf %ld %ld %ld\n",
			ts_str(MIN_TS), ts_str(MAX_TS),
			MIN_IVALUE, MAX_IVALUE,
			MIN_DVALUE, MAX_DVALUE,
			COUNT_VUP, COUNT_VEQ, COUNT_DOWN
	);
	
	return 0;
}
//EOF
