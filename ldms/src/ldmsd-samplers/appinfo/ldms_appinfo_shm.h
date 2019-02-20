/* -*- c-basic-offset: 8 -*-
  * Copyright (c) 2018 National Technology & Engineering Solutions
  * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
  * NTESS, the U.S. Government retains certain rights in this software.
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
#include "ldms.h"

/*
 * LDMS Appinfo shared memory type definitions, shared between
 * sampler code and application library code
 * - Jonathan Cook, 1-30-2018
 */

/***
 * New Design: Jonathan Cook : begun 1-30-2018
 * req: need to support multiple processes (ranks) per node, meaning
 *	multiple copies of the data structure for each process and for
 *	each sampler.
 * currently: the LDMS sampler shares two shared memory segments with
 *	the app: a meta segment for metadata, and a data segment for
 *	actual metric data
 * idea: create only one shared memory segment:
 *  - header portion will be a shared structure
 *  - after header will be N copies of the per-process data
 *  - each process will use a library call to request reservation
 *	of a data block, and will then use that block only
 * Q: should sampler round-robin the process data blocks, or should
 *	there be an N-process schema in LDMS?
 *	- easier to do just one schema, since then the rank can be an index
 * Q: Still allow sampler loading to define the appinfo schema? But must
 *	be able to be renamed -- cannot always be "appinfo"
 * Q: Should this code be configurable to support Kokkos? probably
 ***/

#define MAX_PROCESSES 16
#define MAX_METRICS 128
/**
 * Shared memory header. Fields marked (SS) are set by the sampler.
 * Others marked (AP) are set by the application processes.
 **/
typedef struct shmem_header_s {
	int max_processes;  /* max # of separate process data structs (SS) */
	int num_registered; /* current # of process structs allocated (AP) */
	int metric_count;  /* number of metrics each process data struct (SS) */
	int data_block_size; /* size (bytes) of each process data struct (SS) */
	int sample_offset;  /* offset to struct to sample this time (AP?) */
	struct {
		enum {AVAILABLE=0, ALLOCATED, NOTREADY, NEEDSAMPLED} status;
		unsigned int app_id; /* application ID */
		unsigned int job_id; /* job ID */
		int process_id; /* OS PID */
		int rank_id; /* MPI rank ID (or other library rank id) */
		int start_offset; /* starting address offset of data struct */
	} proc_metadata[MAX_PROCESSES]; /* table for data struct allocation */
	unsigned int metric_offset[MAX_METRICS]; /* offsets for metrics (SS) */
} shmem_header_t;

/**
 * Each process metric data block is an array of these.
 * metric values are set by the application library side, and
 * then read and copied into an LDMS schema on the sampler side.
 * BUT TODO: Omar's code has another array of sizes because these
 * can be variable size -- "value" can be a 256-byte array for strings.
 **/
typedef struct app_metric_s {
	int status; /* not sure how to use this, was used as 1==updated value */
	char name[64];
	int size;
	enum ldms_value_type vtype;
	union ldms_value value;
} app_metric_t;
