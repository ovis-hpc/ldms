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


/**
 * \file general_metrics.h non-HSN metrics
 */

#ifndef __GENERAL_METRICS_H_
#define __GENERAL_METRICS_H_

#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <ctype.h>
#include <wordexp.h>
//have it for the logfile and set
#include "ldmsd.h"
#include "ldms.h"


  /* Lustre specific vars */
  /**                                                                                                         
   * str<->idx in LUSTRE_METRICS.                                                                             
   */
extern struct lustre_svc_stats_head lustre_svc_head;
extern struct str_map *lustre_idx_map;

/** get metric size */
int get_metric_size_lustre(size_t *m_sz, size_t *d_sz,
                           ldmsd_msg_log_f msglog);


/** add metrics */
int add_metrics_lustre(ldms_set_t set, int comp_id,
		       ldmsd_msg_log_f msglog);

/** helpers */
int handle_llite(const char *llite);
int procnetdev_setup(ldmsd_msg_log_f msglog);


/** sample */
int sample_metrics_vmstat(ldmsd_msg_log_f msglog);
int sample_metrics_vmcf(ldmsd_msg_log_f msglog);
int sample_metrics_kgnilnd(ldmsd_msg_log_f msglog);
int sample_metrics_current_freemem(ldmsd_msg_log_f msglog);
int sample_metrics_loadavg(ldmsd_msg_log_f msglog);
int sample_metrics_procnetdev(ldmsd_msg_log_f msglog);
int sample_metrics_lustre(ldmsd_msg_log_f msglog);
	
#endif
