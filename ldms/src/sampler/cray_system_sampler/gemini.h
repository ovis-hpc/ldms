/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2011-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2011-2015 Sandia Corporation. All rights reserved.
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
 * Link aggregation methodlogy from gpcd counters based on Kevin Pedretti's
 * (Sandia National Laboratories) gemini performance counter interface and
 * link aggregation library. It has been augmented with pattern analysis
 * of the interconnect file.
 */


#ifndef _GEMINI_H_
#define _GEMINI_H_

#include <stdint.h>

// Constants
#define GEMINI_NUM_TILES                48
#define GEMINI_NUM_TILE_ROWS             6
#define GEMINI_NUM_TILE_COLUMNS          8
#define GEMINI_NUM_NET_TILES            40
#define GEMINI_NUM_HOST_TILES            8
#define GEMINI_NUM_LOGICAL_LINKS         7  // x+, x-, y+, y-, z+, z-, host
#define GEMINI_NUM_NIC_COUNTERS         12
#define GEMINI_NUM_TILE_COUNTERS         6
#define GEMINI_NUM_LINK_STATS            5

// Link types
#define GEMINI_LINK_TYPE_INVALID        -1
#define GEMINI_LINK_TYPE_MEZZANINE       0
#define GEMINI_LINK_TYPE_BACKPLANE       1
#define GEMINI_LINK_TYPE_CABLE           2
#define GEMINI_LINK_TYPE_NIC             3

// Link type per-tile bandwidths, in Gbytes/s
#define GEMINI_MEZZANINE_TILE_BW        (6.250 * 3.0 / 8.0)
#define GEMINI_BACKPLANE_TILE_BW        (5.000 * 3.0 / 8.0)
#define GEMINI_CABLE_TILE_BW            (3.125 * 3.0 / 8.0)
#define GEMINI_NIC_TILE_BW              (3.467 * 3.0 / 8.0)

// Link directions
#define GEMINI_LINK_DIR_INVALID         -1
#define GEMINI_LINK_DIR_X_PLUS           0
#define GEMINI_LINK_DIR_X_MINUS          1
#define GEMINI_LINK_DIR_Y_PLUS           2
#define GEMINI_LINK_DIR_Y_MINUS          3
#define GEMINI_LINK_DIR_Z_PLUS           4
#define GEMINI_LINK_DIR_Z_MINUS          5
#define GEMINI_LINK_DIR_HOST             6

static char* gemini_linkdir_name[] = {
	"X+", "X-", "Y+", "Y-", "Z+", "Z-", "HH"};

// Gemini fixed tile counter names
#define GEMINI_TCTR_VC0_INPUT_PHITS      0
#define GEMINI_TCTR_VC1_INPUT_PHITS      1
#define GEMINI_TCTR_VC0_INPUT_PACKETS    2
#define GEMINI_TCTR_VC1_INPUT_PACKETS    3
#define GEMINI_TCTR_INPUT_STALLS         4
#define GEMINI_TCTR_OUTPUT_STALLS        5

typedef struct {
	int       type;  // type of the logical link the tile is a part of
	int       dir;   // direction of logical link the tile is a part of
} gemini_tile_t;

typedef struct {
	gemini_tile_t     tile[GEMINI_NUM_TILES];
} gemini_state_t;

#endif
