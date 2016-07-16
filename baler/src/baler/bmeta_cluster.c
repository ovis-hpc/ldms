/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2016 Sandia Corporation. All rights reserved.
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
/**
 * \file bmeta_cluster.c
 * \author Narate Taerat (narate@ogc.us)
 * \date Mar 20, 2013
 *
 */

/**
 * \page bmeta_cluster Baler CLI for meta-clustering.
 *
 * \section synopsis SYNOPSIS
 * \b bmeta_cluster [\b OPTIONS]
 *
 * \section options OPTIONS
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include "butils.h"
#include "bmeta.h"

const char *short_opts = "s:L:D:R:?p";
struct option long_opts[] = {
	{  "store",             1,  0,  's'  },
	{  "purge",             0,  0,  'p'  },
	{  "looseness",         1,  0,  'L'  },
	{  "diff-ratio",        1,  0,  'D'  },
	{  "refinement-speed",  1,  0,  'R'  },
	{  "help",              0,  0,  '?'  },
	{  0,                   0,  0,  0    }
};

const char *baler_store_path = NULL;
char mptn_store_path[8192];
struct bmeta_cluster_param param = {
	.looseness = .7,
	.diff_ratio = .7,
	.refinement_speed = 2.0,
};
int purge = 0;

struct bmptn_store *mptn_store;

void usage()
{
	printf(
"Usage: bmeta_cluster -s STORE [OPTIONS]\n\
\n\
OPTIONS:\n\
	-s,--store STORE	Path to baler store.\n\
	\n\
	-L,--looseness FLOAT	Looseness parameter. (default: 0.7)\n\
	\n\
	-D,--diff-ratio FLOAT	Difference Ratio parameter. (default 0.7)\n\
	\n\
	-R,--refinement-speed FLOAT \n\
				Refinement Speed parameter. (default 2.0)\n\
	\n\
	-p,--purge		Purge the meta-pattern data in the store\n\
				and do nothing.\n\
"
	);
}

void handle_args(int argc, char **argv)
{
	char c;
	int len;
next:
	c = getopt_long(argc, argv, short_opts, long_opts, NULL);
	switch (c) {
	case -1:
		goto out;
	case 's':
		baler_store_path = optarg;
		break;
	case 'L':
		param.looseness = atof(optarg);
		break;
	case 'D':
		param.diff_ratio = atof(optarg);
		break;
	case 'R':
		param.refinement_speed = atof(optarg);
		break;
	case 'p':
		purge = 1;
		break;
	default:
		usage();
		exit(-1);
	}
	goto next;
out:
	if (!baler_store_path) {
		berr("--store PATH is not specified!");
		exit(-1);
	}
	len = snprintf(mptn_store_path, sizeof(mptn_store_path),
				"%s/mptn_store", baler_store_path);
	if (len >= sizeof(mptn_store_path)) {
		berr("Path too long!");
		exit(-1);
	}
	return;
}

void meta_cluster_routine()
{
	int rc;
	uint32_t class_id, last_class_id;
	mptn_store = bmptn_store_open(mptn_store_path, baler_store_path, 1);
	if (!mptn_store) {
		berr("Cannot open meta-pattern store, errno: %d", errno);
		exit(-1);
	}
	bmptn_store_state_e state = bmptn_store_get_state(mptn_store);
	switch (state) {
	case BMPTN_STORE_STATE_NA:
	case BMPTN_STORE_STATE_ERROR:
	case BMPTN_STORE_STATE_DONE:
		/* In these states, reinit before clustering */
		rc = bmptn_store_reinit(mptn_store);
		if (rc) {
			berr(" bmptn_store_reinit() error, rc: %d\n", rc);
			exit(-1);
		}

		/* intentionally let-through */

	case BMPTN_STORE_STATE_INITIALIZED:
		rc = bmptn_cluster(mptn_store, &param);
		if (rc) {
			berr("bmptn_cluster() error, rc: %d\n", rc);
			exit(-1);
		}
		break;

	default:
		/* this should not happen */
		berr("meta_cluster_work_routine() bad state ...");
		break;
	}
cleanup:
	bmptn_store_close_free(mptn_store);
	return ;
}

void purge_routine()
{
	int rc = bmptn_store_purge(mptn_store_path);
	if (rc) {
		berr("mptn_store_purge() error: %d", rc);
		exit(-1);
	}
}

int main(int argc, char **argv)
{
	handle_args(argc, argv);
	if (purge) {
		purge_routine();
	} else {
		meta_cluster_routine();
	}
}
