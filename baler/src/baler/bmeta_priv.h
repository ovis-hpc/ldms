/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015 Sandia Corporation. All rights reserved.
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
 * \file bmeta_priv.h
 * \author Narate Taerat (narate at ogc dot us)
 * \brief Private data structures for Baler meta pattern.
 */
#ifndef __BMETA_PRIV_H
#define __BMETA_PRIV_H
#include "bmeta.h"

#include "bmlist.h"
#include "btypes.h"
#include "bmvec.h"
#include "bmapper.h"
#include "bmhash.h"
#include "bptn.h"

#define BMPTN_STORE_VER "BMPTN_STORE_VER_1.0"

/**
 * Baler Meta Cluster Handle.
 *
 * This structure store an offset to ::bmstr in bmptn_store::mem that represent
 * the name of the group, and another offset to the list in bmptn_store::mem
 * that describe the cluster.
 */
struct bmc_handle {

	/**
	 * Average distance of this cluster.
	 */
	float avg_dist;

	/**
	 * Distance threshold used to create this cluster.
	 */
	float dist_thr;

	/**
	 * Offset to the name of the cluster in bmptn_store::mem.
	 */
	uint64_t bmstr_off;

	/**
	 * Offset to the ::bmptn_node that is the head of the list representing
	 * the cluster in bmptn_store::nodes.
	 */
	uint64_t bmlist_off;
};

/**
 * Node structure (reside in bmtpn_store::nodes).
 */
struct bmptn_node {
	/**
	 * Pattern ID.
	 */
	uint32_t ptn_id;

	/**
	 * Node label.
	 */
	uint32_t label;

	/**
	 * Link to the next node in the same cluster.
	 */
	struct bmlist_link link;
};

struct bmptn_store_header {
	union {
		struct {
			char version[64];
			uint32_t last_ptn_id;
			float diff_ratio;
			float looseness;
			float refinement_speed;
			uint32_t last_cls_id;
			bmptn_store_state_e state;
			uint32_t working_cls_id;
			uint32_t percent;
		};
		/**
		 * Reserving 4KB for the header.
		 */
		char __space[4096];
	};
};

/**
 * Structure for Baler meta-pattern store.
 */
struct bmptn_store {
	/**
	 * Store path.
	 */
	char path[PATH_MAX];

	/**
	 * General mutex for the store.
	 */
	pthread_mutex_t mutex;

	/**
	 * MISC memory for the store.
	 *
	 * Now contains:
	 *  - cluster name
	 *  - Eng signature
	 */
	struct bmem *mem;

	/**
	 * Vector of ::bmc_handle.
	 */
	struct bmvec_char *bmc_handle_vec;

	/**
	 * Nodes of patterns.
	 */
	struct bmvec_char *nodes;

	/**
	 * Distance between pattern IDs.
	 */
	struct bmhash *dist;

	/**
	 * Handle of the pattern store.
	 *
	 * \note ::bmptn_store does not own bmptn_store::ptn_store. The caller
	 *       need to handle ptn_store separately.
	 */
	struct bptn_store *ptn_store;

	/**
	 * Reference to the token store.
	 */
	struct btkn_store *tkn_store;

	/**
	 * English signature hash
	 */
	struct bhash *engsig_hash;

	/**
	 * English signature array.
	 * \note engsig_array[class_id] :--> bhash_entry in ::engsig_hash.
	 */
	struct bhash_entry **engsig_array;
};

struct bmptn_cluster_name {
	uint32_t alloc_len;
	struct bstr bstr;
};

#endif /* __BMETA_PRIV_H */
