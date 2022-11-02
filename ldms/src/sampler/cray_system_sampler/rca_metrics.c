/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2016,2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2016,2018 Open Grid Computing, Inc. All rights reserved.
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


/**
 * \file rca_metrics.c
 * \brief Functions used in the cray_system_sampler that are particular to
 * rca (mesh coord).
 */

#include "rca_metrics.h"

nettopo_coord_t nettopo_coord;
int* nettopo_metric_table;

/** These only work for gemini */
#define RCAHELPER_BIN "/opt/cray/rca/default/bin/rca-helper"

/* Defined in cray_sampler_base.c */
extern ovis_log_t __cray_sampler_log;

int nettopo_setup()
{
	uint16_t nid;
	FILE* pipe;
	char cmdbuffer[128];
	char buffer[128];


	snprintf(cmdbuffer, 127, "%s %s", RCAHELPER_BIN, "-i");
	pipe = popen(cmdbuffer, "r");
	if (!pipe)
		goto err;

	if (!feof(pipe)){
		if (fgets(buffer, 128, pipe) != NULL){
			/* FIXME: better check here.... */
			nid = (uint16_t)(atoi(buffer));
		} else {
			goto err;
		}
	} else {
		goto err;
	}

	pclose(pipe);

	snprintf(cmdbuffer, 127, "%s %s %d", RCAHELPER_BIN, "-C", (int)nid);
	pipe = popen(cmdbuffer, "r");
	if (!pipe)
		goto err;

	if (!feof(pipe)){
		if (fgets(buffer, 128, pipe) != NULL){
			/* FIXME: better check here.... */
			int rc = sscanf(buffer, "%d %d %d\n",
					&nettopo_coord.x,
					&nettopo_coord.y,
					&nettopo_coord.z);
			if (rc != 3)
				goto err;
		}
	} else {
		goto err;
	}

	pclose(pipe);
	return 0;

	err:
	if (pipe)
		pclose(pipe);

	pipe = 0;
	ovis_log(__cray_sampler_log, OVIS_LERROR,
	       "rca_metrics: rca-helper fail. node and coords will be invalid\n");
	nid = 0;
	nettopo_coord.x = 6666;
	nettopo_coord.y = 6666;
	nettopo_coord.z = 6666;

	return 0; /* even though it fails */
}

int sample_metrics_nettopo(ldms_set_t set)
{

	union ldms_value v;

	/*  Fill in mesh coords (this is static and should be moved) */
	/* will want these 3 to be LDMS_V_U8 */
	v.v_u64 = (uint64_t) nettopo_coord.x;
	ldms_metric_set(set, nettopo_metric_table[0], &v);
	v.v_u64 = (uint64_t) nettopo_coord.y;
	ldms_metric_set(set, nettopo_metric_table[1], &v);
	v.v_u64 = (uint64_t) nettopo_coord.z;
	ldms_metric_set(set, nettopo_metric_table[2], &v);

	return 0;
}
