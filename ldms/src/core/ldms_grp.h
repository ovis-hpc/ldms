/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2020 Open Grid Computing, Inc. All rights reserved.
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
#ifndef __LDMS_GRP_H__
#define __LDMS_GRP_H__

#include <stdint.h>
#define LDMS_GRP_SCHEMA "!!LDMS_GRP!!"

#define LDMS_GRP_NAME_MAX 127

/* max child-per-node */
#define LDMS_GRP_NCHILD 3

typedef struct ldms_grp_node_s *ldms_grp_node_t;
typedef struct ldms_grp_rec_s   *ldms_grp_rec_t;
typedef struct ldms_grp_priv_s  *ldms_grp_priv_t;

#pragma pack(push, 1)
struct ldms_grp_rec_s {
	char name[LDMS_GRP_NAME_MAX + 1];
	/* all numbers are in little-endian */
	uint16_t prev;
	uint16_t next;
	uint16_t id;
	char pad[2];
};

struct ldms_grp_node_entry_s {
	/* all numbers are in little-endian */
	uint16_t rec_id;
	uint16_t child;
};

struct ldms_grp_node_s {
	/* all numbers are in little-endian */
	uint8_t  n; /* the number of entries in this node */
	uint8_t  is_leaf;
	uint16_t id;
	uint16_t parent; /* refers to parent when in b-tree */
	uint16_t next;   /* next free entry when in free list */
	struct ldms_grp_node_entry_s ent[LDMS_GRP_NCHILD];
	/* NOTE: ent[i].child is the left child
	 *       ent[n].child is the right child of ent[n-1]
	 *       ent[n].rec_id is unused.
	 */
};

/* The `priv` structure managing ldms_grp. */
struct ldms_grp_priv_s {
	/* all numbers are in little-endian */
	char zero[2]; /* leading '\0' to prevent bad ldms_ls printing */
	uint16_t node_root;      /* node[node_root] is the root of b-tree */
	uint16_t node_free_list; /* node[node_free_list] is the first free node entry */
	uint16_t rec_list;      /* refers to the first group member */

	uint16_t rec_free_list; /* refers to the first free record */
	uint16_t m_max; /* maximum number of members */
	char pad[4]; /* so that node is 8-byte aligned */

	struct ldms_grp_node_s node[0];
};
#pragma pack(pop)

#define LDMS_GRP_PRIV(grp) ((ldms_grp_priv_t)ldms_metric_get(&((grp)->set), 0))

#define LDMS_GRP_NODE(grp, node_id) ((node_id)?&(LDMS_GRP_PRIV(grp)->node[(node_id)]):NULL)

#define LDMS_GRP_REC(grp, rec_id) ((rec_id)?((ldms_grp_rec_t)ldms_metric_get(&((grp)->set), rec_id)):(NULL))

#endif
