/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2017,2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015-2017,2019 Open Grid Computing, Inc. All rights reserved.
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
 * \file aries_mmr.h
 * \brief Header file exporting aries_mmr plugin functions for extension.
 */

#ifndef __ARIES_MMR_H
#define __ARIES_MMR_H

#define _GNU_SOURCE

#include "gpcd_pub.h"
#include "gpcd_lib.h"
#include "ldmsd.h"
#include "ldmsd_plugin.h"
#include "json/json_util.h"

//list types
enum {REQ_T, RC_T, PTILE_T, NIC_T, OTHER_T, END_T};
//matching string types
enum {REQ_LMT, RSP_LMT, RTR_LMT, PTILE_LMT, NIC_LMT, END_LMT};

struct met { //for the XXX_T
	int metric_index;
	LIST_ENTRY(met) entry;
};

struct mstruct{ //for the XXX_T
	int num_metrics;
	gpcd_context_t* ctx;
	LIST_HEAD(, met) mlist;
};

struct listmatch_t{
	char* header;
	int len;
};

typedef struct aries_mmr_inst_s *aries_mmr_inst_t;
struct aries_mmr_inst_s {
	struct ldmsd_plugin_inst_s base;

	int configured; /* turn to 1 when config success */
	struct mstruct mvals[END_T];
	char** rawlist;
	int numraw;
	char* rtrid;

	int (*filter)(const char *); /* metric filter */
};

int aries_mmr_init(ldmsd_plugin_inst_t pi);
void aries_mmr_del(ldmsd_plugin_inst_t pi);
int aries_mmr_recv(ldmsd_plugin_inst_t pi, ldms_t x, char *msg);
int aries_mmr_config(ldmsd_plugin_inst_t pi, json_entity_t json, char *ebuf, int ebufsz);
int filterKeepAll(const char* name);
int filterKeepNic(const char* name);
int filterKeepRouter(const char* name);
#endif /* __ARIES_MMR_H */
