/*
 * Copyright (c) 2013-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2015 Sandia Corporation. All rights reserved.
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
 * \file rtr_util.c
 * \brief Utilities reading and aggregating the gemini_performance counters.
 */

/**
 * Link aggregation methodlogy from gpcd counters based on Kevin Pedretti's
 * (Sandia National Laboratories) gemini performance counter interface and
 * link aggregation library. It has been augmented with pattern analysis
 * of the interconnect file.
 *
 * NOTE: link aggregation has been deprecated in v3.
 */


#define _GNU_SOURCE

#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <limits.h>
#include "rtr_util.h"
#include "ldmsd.h"
#include "ldms.h"

/* Defined in the plugins */
extern ovis_log_t mylog;

/**
 * Converts a Gemini tile ID to tile (\c row, \c col) coordinate.
 *
 * \returns 0 on success.
 * \returns -1 on failure.
 */
static int tid_to_tcoord(int tid, int *row, int *col)
{
	if (tid >= GEMINI_NUM_TILES)
	  return -1;

	*row = tid / GEMINI_NUM_TILE_COLUMNS;
	*col = tid % GEMINI_NUM_TILE_COLUMNS;

	return 0;
}

/**
 * Converts a Gemini tile (row, col) coordinate to tile ID.
 *
 * \returns 0 on success, -1 on failure.
 */
int tcoord_to_tid(int row, int col, int *tid)
{
	if ((row >= GEMINI_NUM_TILE_ROWS) || (col >= GEMINI_NUM_TILE_COLUMNS))
	  return EINVAL;

	*tid = row * GEMINI_NUM_TILE_COLUMNS + col;

	return 0;
}


static int get_my_pattern(int *pattern, int* zind)
{
	int cabrow;
	int cabcol;
	int chassis;
	int slot;
	int node;
	int gem;
	int mapnum;
	FILE *fd;

	/* Read our cname and convert to pattern
	 * Patterns of form [e/o c#-][e/o -#][e/o c#][e/0 g#][row][column]
	 * where e/o will be 0 if even and 1 if odd */
	fd = fopen("/proc/cray_xt/cname", "r");
	if (!fd) {
		ovis_log(mylog, OVIS_LERROR, "Could not open cnameprocfile\n");
		return ENOENT;
	}
	fseek(fd, 0, SEEK_SET);
	fscanf(fd, "c%d-%dc%ds%dn%d\n",
	 &cabrow, &cabcol, &chassis, &slot, &node);
	fclose (fd);


	 if ( cabrow % 2 == 0)
		mapnum = 0;
	 else
		mapnum = 1;

	 if ( cabcol % 2 == 0)
		 mapnum = (mapnum *10) + 0;
	 else
		 mapnum = (mapnum *10) + 1;

	 if ( chassis % 2 == 0)
		 mapnum = (mapnum *10) + 0;
	 else
		 mapnum = (mapnum *10) + 1;

	 if ( node == 0 || node == 1 )
		 mapnum = (mapnum *10) + 0;
	 else
		 mapnum = (mapnum *10) + 1;

	 *pattern = mapnum;
	 *zind = slot;

	 return 0;
}


/**
 * Converts the input link direction string (e.g., "X+") into a
 * link direction integer value, representing the direction of the link.
 */
static int str_to_linkdir(char *str)
{
	int dir = GEMINI_LINK_DIR_INVALID;

	if      (strcmp(str, "X+")   == 0)
		dir = GEMINI_LINK_DIR_X_PLUS;
	else if (strcmp(str, "X-")   == 0)
		dir = GEMINI_LINK_DIR_X_MINUS;
	else if (strcmp(str, "Y+")   == 0)
		dir = GEMINI_LINK_DIR_Y_PLUS;
	else if (strcmp(str, "Y-")   == 0)
		dir = GEMINI_LINK_DIR_Y_MINUS;
	else if (strcmp(str, "Z+")   == 0)
		dir = GEMINI_LINK_DIR_Z_PLUS;
	else if (strcmp(str, "Z-")   == 0)
		dir = GEMINI_LINK_DIR_Z_MINUS;
	else if (strcmp(str, "HOST") == 0)
		dir = GEMINI_LINK_DIR_HOST;
	else
		dir = GEMINI_LINK_DIR_INVALID;
		/*printf("unknown link direction %s", str); */

	return dir;
}


static int tile_to_linkdir(int my_pattern,
		    int my_z_pattern, char *link_file, gemini_tile_t *tile)
{
	char lbuf[64];
	char type[32];
	char dir_str[8];
	int file_pattern = 0;
	int found = 0;
	int file_z_pattern = 0;
	int file_tile_type;
	char *s;
	FILE *fd;
	int rc;

	// Search linkfile for our pattern, linkdir, and type
	fd = fopen(link_file, "r");
	if (!fd) {
		ovis_log(mylog, OVIS_LERROR, "Could not open %s for read\n",
					link_file);
		return ENOENT;
	}
	fseek(fd, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), fd);
		if (!s)
			break;
		rc = sscanf(lbuf, "%d %s %d %d\n", &file_pattern, dir_str,
			    &file_tile_type, &file_z_pattern);
		if (rc < 3){
			ovis_log(mylog, OVIS_LERROR, "Failure reading line in "
						"linkfile %s\n", link_file);
			fclose(fd);
			return EINVAL;
		}
		if ( my_pattern == file_pattern ){
			if ((strcmp(dir_str,"Z+")!= 0) &&
			    (strcmp(dir_str,"Z-") != 0)){
				found = 1;
				break;
			} else {
				if (rc != 4){
					fclose(fd);
					return EINVAL;
				}
				if (my_z_pattern == file_z_pattern){
					found = 1;
					break;
			       }
		       }
		}
	} while (s);
	fclose (fd);

	if ( !found ) {
	       ovis_log(mylog, OVIS_LINFO, "rtr_util: Pattern %d not found in "
				       "linkfile %s (this may be ok)\n",
				       my_pattern, link_file);
	       tile->type = GEMINI_LINK_TYPE_INVALID;
		tile->dir ==  GEMINI_LINK_DIR_INVALID;
	       return 0;
	}

	tile->dir = str_to_linkdir(dir_str);
	if (tile->dir ==  GEMINI_LINK_DIR_INVALID) {
		ovis_log(mylog, OVIS_LERROR, "str_to_linkdir failed on %s",
				     tile->dir);
	      return EINVAL;
	}
	tile->type = file_tile_type;
	return 0;
}


/**
 * Converts the input link type string (e.g., "mezzanine") into a
 * link type integer value, representing the type of the link.
 */
static int str_to_linktype(char *str)
{
	if (strcmp(str,  "backplane") == 0)
		return GEMINI_LINK_TYPE_BACKPLANE;
	if (strcmp(str,  "mezzanine") == 0)
		return GEMINI_LINK_TYPE_MEZZANINE;
	if (strncmp(str, "cable", 5)  == 0)
		return GEMINI_LINK_TYPE_CABLE;
	if (strcmp(str,  "nic")       == 0)
		return GEMINI_LINK_TYPE_NIC;

	/* printf("unknown link type %s", str); */
	return GEMINI_LINK_TYPE_INVALID;
}

/**
 * Return link bandwidth based on type. This is specified in ldms_gemini.h
 */
static double tile_to_bw(int tile_type)
{
	switch (tile_type) {
	case GEMINI_LINK_TYPE_MEZZANINE:
		return GEMINI_MEZZANINE_TILE_BW;
	case GEMINI_LINK_TYPE_BACKPLANE:
		return GEMINI_BACKPLANE_TILE_BW;
	case GEMINI_LINK_TYPE_CABLE:
		return GEMINI_CABLE_TILE_BW;
	case GEMINI_LINK_TYPE_NIC:
		return GEMINI_NIC_TILE_BW;
	default:
		ovis_log(mylog, OVIS_LERROR, "invalid tile type (%d)", tile_type);
	}

	return -1.0;
}

/**
 * Parses rtr tool interconnect dump file to determine each gemini's
 * logical links and which gemini tiles are associated with each
 * logical link.  This is a collective call. Only rank 0 actually
 * reads interconnect.txt.
 *
 * \returns 0 on success.
 * \returns -1 on failure.
 */
int gem_link_perf_parse_interconnect_file(char *filename,
					  gemini_tile_t *tile,
					  double (*max_link_bw)[],
					  int (*tiles_per_dir)[])
{
	FILE *fd;
	int tid;
	int my_tiles = 0;
	int my_pattern = 0;
	int my_z_pattern = 0;
	int my_tmp_pattern = 0;
	double lbw = 0.0;

	int row, col;
	int i, n;
	int rc;


	/* Zero max_link_bw */
	for (i = 0; i < GEMINI_NUM_LOGICAL_LINKS; i++) {
		(*max_link_bw)[i] = 0.0;
		(*tiles_per_dir)[i] = 0;
	}

	// Get my pattern
	rc = get_my_pattern(&my_pattern, &my_z_pattern);
	if (rc != 0)
	  return rc;

	/*  Initialize tile parameters */
	for (i = 0; i < GEMINI_NUM_TILES; i++) {
		tile[i].type = GEMINI_LINK_TYPE_INVALID;
		tile[i].dir  = GEMINI_LINK_DIR_INVALID;
	}

	for (row=0; row<GEMINI_NUM_TILE_ROWS; row++) {
		for (col=0; col<GEMINI_NUM_TILE_COLUMNS; col++) {

			/*  Convert the tile's (row,col) to a linear tile ID */
			tid = -1;
			if (tcoord_to_tid(row, col, &tid) != 0) {
				ovis_log(mylog, OVIS_LERROR,
						"tcoord_to_tid(%u,%u) failed",
						row, col);
				return EINVAL;
			}
			if (tid == -1) {
				ovis_log(mylog, OVIS_LERROR,
						"tcoord_to_tid failed on row %d,"
						" column %d", row, col);
				return EINVAL;
			}
			my_tmp_pattern = (my_pattern * 100) + ( row * 10) + col;
			rc = tile_to_linkdir(my_tmp_pattern, my_z_pattern,
					     filename, &tile[tid]);
			if ( rc ) {
				ovis_log(mylog, OVIS_LERROR,
						"tile_to_linkdir failed on "
						"%d, %d, %s, %d",
						my_tmp_pattern, my_z_pattern,
						filename, tid);
				return EINVAL;
			}
			if (tile[tid].type != GEMINI_LINK_TYPE_INVALID){
				/* NOTE: some tiles not in the interconnect */
				lbw = tile_to_bw(tile[tid].type);
				if (lbw > 0)
				      (*max_link_bw)[tile[tid].dir] += lbw;
				(*tiles_per_dir)[tile[tid].dir]++;
				my_tiles++;
			}
		}

	}

	if (my_tiles != GEMINI_NUM_NET_TILES) {
		ovis_log(mylog, OVIS_LERROR, "src (%d,%d,%d) found %d tiles in "
			       " interconnect file, expected %d",
			       my_tiles, GEMINI_NUM_NET_TILES);
		return EINVAL;
	}

	/*  Manually add in the Gemini to NIC/host links */
	tile[19].dir = GEMINI_LINK_DIR_HOST;
	tile[20].dir = GEMINI_LINK_DIR_HOST;
	tile[27].dir = GEMINI_LINK_DIR_HOST;
	tile[28].dir = GEMINI_LINK_DIR_HOST;
	tile[35].dir = GEMINI_LINK_DIR_HOST;
	tile[36].dir = GEMINI_LINK_DIR_HOST;
	tile[43].dir = GEMINI_LINK_DIR_HOST;
	tile[44].dir = GEMINI_LINK_DIR_HOST;

	tile[19].type = GEMINI_LINK_TYPE_NIC;
	tile[20].type = GEMINI_LINK_TYPE_NIC;
	tile[27].type = GEMINI_LINK_TYPE_NIC;
	tile[28].type = GEMINI_LINK_TYPE_NIC;
	tile[35].type = GEMINI_LINK_TYPE_NIC;
	tile[36].type = GEMINI_LINK_TYPE_NIC;
	tile[43].type = GEMINI_LINK_TYPE_NIC;
	tile[44].type = GEMINI_LINK_TYPE_NIC;

	/* Manually update the host tiles and bw */
	(*tiles_per_dir)[GEMINI_LINK_DIR_HOST] = 8;
	(*max_link_bw)[GEMINI_LINK_DIR_HOST] = GEMINI_NIC_TILE_BW * 8.0;

	return 0;
}
