/**
 * Copyright (c) 2018-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018-2019 Open Grid Computing, Inc. All rights reserved.
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

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <getopt.h>
#include <inttypes.h>
#include <coll/map.h>

#define MAP_NEW		0x0001
#define MAP_OPEN	0x0002
#define MAP_INVERSE	0x0010
#define MAP_TRANSFORM	0x0020
#define MAP_ENTRY_NEW	0x0100
#define MAP_LIST_GET	0x0200
#define MAP_CONTAINER	0x0400

const char *short_options = "C:ce:lm:noi:t:";
struct option long_options[] = {
	{"container",			required_argument,0,'C'},
	{"map_container_create",	no_argument,0,'c'},
	{"map_list_get",		no_argument,0,'l'},
	{"map_name",			required_argument,0,'m'},
	{"map_new",			no_argument,0,'n'},
	{"map_entry_add",		required_argument,0,'e'},
	{"map_open",			no_argument,0,'o'},
	{"map_inverse",			required_argument,0,'i'},
	{"map_transform",		required_argument,0,'t'},
	{"help",			no_argument,0,'?'}
};

void usage(int argc, char *argv[])
{
	printf("map_cmd { -e | -n | -o | -i | -t } -C <path> -m <map_name>\n");
	printf("	-C <path>		The path to the container. Required for all options\n");
	printf("	-c			Create Map Container\n");
	printf("	-l			Get a list of available maps\n");
	printf("	-m <map_name>		The name of the Map. Required for all options\n"
		"				except map_list_get and map_container_create\n");
	printf("	-n			Create a new map with name specified with -m\n");
	printf("	-e <obj_cols>		Create a new entry to the map with the format\n"
	       "				[transform, inverse]\n");
	printf("	-t <transform_val>	Query the map for the inverse value that corresponds\n"
		"				with the transform value provided\n");
	printf("	-i <inverse_val>	Query the map for the transform value that corresponds\n"
		"				with the inverse value provided\n");
}

int map_container_create(const char *path)
{
	int rc;

	rc = map_container_new(path);
	if (rc) {
		goto err0;
	}
	return rc;
err0:
	printf("Failed to create container");
	return 0;
}

int map_inverse_query(map_t map, uint64_t match_val)
{
	uint64_t retval;

	(void)map_inverse(map, match_val, &retval);
	if (!retval) {
		printf("Could not match inverse value %" PRIu64 " with a transform value\n", match_val);
		return -1;
	}
	printf("Transform obj %" PRIu64 "\n", retval);
	return 0;
}

int map_transform_query(map_t map, uint64_t match_val)
{
	uint64_t retval;

	(void)map_transform(map, match_val, &retval);
	if (!retval) {
		printf("Could not match transform value %" PRIu64 " with an inverse value\n", match_val);
		return -1;
	}
	printf("Inverse obj %" PRIu64 "\n", retval);
	return 0;
}

int main(int argc, char **argv)
{
	int o, rc = 0;
	int action = 0;
	char *obj_cols;
	uint64_t match_val;
	char *path = NULL;
	char *name;
	char **map_list;
	map_t map;
	while(0 < (o = getopt_long(argc, argv, short_options, long_options, NULL))) {
		switch(o) {
		case 'C':
			path = strdup(optarg);
			break;
		case 'm':
			/* fix map_name byte check */
			name = strdup(optarg);
			if (strlen(name) > 56)
				printf("Map name cannot exceed 56 characters");
			break;
		case 'c':
			action = MAP_CONTAINER;
			break;
		case 'e':
			obj_cols = strdup(optarg);
			action = MAP_ENTRY_NEW;
			break;
		case 'n':
			action = MAP_NEW;
			break;
		case 'l':
			action = MAP_LIST_GET;
			break;
		case 't':
			match_val = strtoul(optarg, NULL, 0);
			action = MAP_TRANSFORM;
			break;
		case 'i':
			match_val = strtoul(optarg, NULL, 0);
			action = MAP_INVERSE;
			break;
		case '?':
		default:
			usage(argc, argv);
			break;
		}
	}
	if (!path) {
		printf("You must specify a container path\n");
		usage(argc, argv);
		return 0;
	}
	if (action & (MAP_LIST_GET | MAP_CONTAINER)) {
		if (action & MAP_LIST_GET) {
			map_list = map_list_get(path);
			int i =0;
			for (i=0; map_list[i]; i++) {
				printf("%s\n", map_list[i]);
			}
			return 0;
		} else if (action & MAP_CONTAINER) {
			rc = map_container_create(path);
		}
	} else if (!name) {
		printf("You must specify a map\n");
		usage(argc, argv);
		return 0;
	}
	if (!action) {
		printf("No action requested\n");
		usage(argc, argv);
		return 0;
	}
	if (action & MAP_NEW) {
		rc = map_new(path, name);
	}
	if (action & (MAP_TRANSFORM | MAP_INVERSE | MAP_ENTRY_NEW)) {
		map = map_open(path, name);
		if (action & MAP_TRANSFORM) {
			rc = map_transform_query(map, match_val);
			return rc;
		} else if (action & MAP_INVERSE) {
			rc = map_inverse_query(map, match_val);
			return rc;
		} else if (action & MAP_ENTRY_NEW) {
			rc = map_entry_new(map, obj_cols);
		}
	}
	return rc;
}
