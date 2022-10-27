/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2017-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2017-2018 Open Grid Computing, Inc. All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <pthread.h>
#include <errno.h>
#include <ovis_util/util.h>
#include "ldms.h"

#define FMT "x:p:a:A:s:i:"

#define METRIC_NAME_PREFIX "metric_"
#define MARRAY_NUM_ELE 5

static char *xprt;
static int port;

static char *auth_name = "none";
static struct attr_value_list *auth_opt;

struct test_schema {
	const char *name;
	ldms_schema_t schema;
	LIST_ENTRY(test_schema) entry;
};
LIST_HEAD(test_schema_list, test_schema) schema_list;

static int num_schema;

struct test_set {
	const char *name;
	ldms_set_t set;
	struct test_schema *tschema;
	LIST_ENTRY(test_set) entry;
};
LIST_HEAD(test_set_list, test_set) set_list;

static int num_set;
struct metric_info {
	char *type_str;
	enum ldms_value_type type;
	int num_ele;
};

static struct metric_info metric_info_list[] = {
	{"char", LDMS_V_CHAR, 1},
	{"u8", LDMS_V_U8, 1},
	{"s8", LDMS_V_S8, 1},
	{"u16", LDMS_V_U16, 1},
	{"s16", LDMS_V_S16, 1},
	{"u32", LDMS_V_U32, 1},
	{"s32", LDMS_V_S32, 1},
	{"u64", LDMS_V_U64, 1},
	{"s64", LDMS_V_S64, 1},
	{"f32", LDMS_V_F32, 1},
	{"d32", LDMS_V_D64, 1},
	{"char_array", LDMS_V_CHAR_ARRAY, MARRAY_NUM_ELE},
	{"u8_array", LDMS_V_U8_ARRAY, MARRAY_NUM_ELE},
	{"s8_array", LDMS_V_S8_ARRAY, MARRAY_NUM_ELE},
	{"u16_array", LDMS_V_U16_ARRAY, MARRAY_NUM_ELE},
	{"s16_array", LDMS_V_S16_ARRAY, MARRAY_NUM_ELE},
	{"u32_array", LDMS_V_U32_ARRAY, MARRAY_NUM_ELE},
	{"s32_array", LDMS_V_S32_ARRAY, MARRAY_NUM_ELE},
	{"u64_array", LDMS_V_U64_ARRAY, MARRAY_NUM_ELE},
	{"s64_array", LDMS_V_S64_ARRAY, MARRAY_NUM_ELE},
	{"float_array", LDMS_V_F32_ARRAY, MARRAY_NUM_ELE},
	{"double_array", LDMS_V_D64_ARRAY, MARRAY_NUM_ELE},
	{NULL, LDMS_V_NONE, MARRAY_NUM_ELE},
};

struct conn {
	struct sockaddr_in sin;
	ldms_t ldms;
	pthread_mutex_t state_lock;
	enum connect_state {
		INIT = 0,
		CONNECTING,
		CONNECTED,
		DISCONNECTED,
	} state;
};

struct conn *conn_list = 0;

static struct test_schema *__schema_new(const char *name) {
	printf("Creating schema %s\n", name);
	struct test_schema *tschema = malloc(sizeof(*tschema));
	if (!tschema) {
		exit(ENOMEM);
	}
	struct metric_info *minfo;
	int rc;

	tschema->name = strdup(name);
	if (!tschema->name) {
		printf("out of memory\n");
		exit(ENOMEM);
	}

	tschema->schema = ldms_schema_new(name);
	if (!tschema->schema) {
		printf("ldms_schema_new error: %s\n", name);
		pthread_exit(NULL);
	}

	int j = 0;
	char metric_name[64];
	minfo = &metric_info_list[j];
	while (minfo->type != LDMS_V_NONE) {
		sprintf(metric_name, "%s%s", METRIC_NAME_PREFIX, minfo->type_str);
		if (ldms_type_is_array(minfo->type)) {
			rc = ldms_schema_metric_array_add(tschema->schema, metric_name,
							minfo->type, minfo->num_ele);
		} else {
			rc = ldms_schema_metric_add(tschema->schema,
					metric_name, minfo->type);
		}
		if (rc < 0) {
			printf("ldms_schema_metric_add error %d: schema %s, metric ID %d, "
					"metric type %s\n",
					rc, name, j, minfo->type_str);
			pthread_exit(NULL);
		}
		j++;
		minfo = &metric_info_list[j];
	}
	LIST_INSERT_HEAD(&schema_list, tschema, entry);
	num_schema++;
	return tschema;
}

static struct test_schema *__find_test_schema(const char *name)
{
	struct test_schema *tschema;
	LIST_FOREACH(tschema, &schema_list, entry) {
		if (0 == strcmp(tschema->name, name))
			return tschema;
	}
	return NULL;
}

static struct test_set *__set_new(struct test_schema *tschema, const char *name,
							const char *published)
{
	int rc;
	printf("Creating set %s\n", name);
	struct test_set *tset = malloc(sizeof(*tset));
	if (!tset) {
		printf("Out of memory\n");
		exit(ENOMEM);
	}

	tset->name = strdup(name);
	if (!tset->name) {
		printf("Out of memory\n");
		exit(ENOMEM);
	}

	tset->set = ldms_set_new(name, tschema->schema);
	if (!tset->set) {
		printf("ldms_set_new failed: set %s, schema %s\n", name, tschema->name);
		exit(1);
	}
	if (0 == strcasecmp(published, "true")) {
		rc = ldms_set_publish(tset->set);
		if (rc) {
			printf("Failed to publish the set '%s'\n", name);
			exit(rc);
		}
	}

	tset->tschema = tschema;
	LIST_INSERT_HEAD(&set_list, tset, entry);
	num_set++;
	return tset;
}

void *__sample_sets(void *arg)
{
	struct test_set *tset;
	ldms_set_t set;
	struct metric_info *minfo;
	union ldms_value v;
	int value = 0;
	int i, j;
	while (1) {
		value++;
		sleep(2);
		LIST_FOREACH(tset, &set_list, entry) {
			set = tset->set;
			ldms_transaction_begin(set);

			i = 0;
			minfo = &metric_info_list[i];
			while (minfo->type != LDMS_V_NONE) {
				for (j = 0; j < minfo->num_ele; j++) {
					switch (minfo->type) {
					case LDMS_V_CHAR:
					case LDMS_V_CHAR_ARRAY:
						v.v_s8 = (value % 26) + 97;
						break;
					case LDMS_V_S8:
					case LDMS_V_S8_ARRAY:
						v.v_s8 = value;
						break;
					case LDMS_V_U8:
					case LDMS_V_U8_ARRAY:
						v.v_u8 = value;
						break;
					case LDMS_V_S16:
					case LDMS_V_S16_ARRAY:
						v.v_s16 = value;
						break;
					case LDMS_V_U16:
					case LDMS_V_U16_ARRAY:
						v.v_u16 = value;
						break;
					case LDMS_V_S32:
					case LDMS_V_S32_ARRAY:
						v.v_s32 = value;
						break;
					case LDMS_V_U32:
					case LDMS_V_U32_ARRAY:
						v.v_u32 = value;
						break;
					case LDMS_V_S64:
					case LDMS_V_S64_ARRAY:
						v.v_s64 = value;
						break;
					case LDMS_V_U64:
					case LDMS_V_U64_ARRAY:
						v.v_u64 = value;
						break;
					case LDMS_V_F32:
					case LDMS_V_F32_ARRAY:
						v.v_f = value;
						break;
					case LDMS_V_D64:
					case LDMS_V_D64_ARRAY:
						v.v_d = value;
						break;
					default:
						printf("Invalid metric type: %s\n",
								minfo->type_str);
						pthread_exit(NULL);
					}
					if (ldms_type_is_array(minfo->type)) {
						ldms_metric_array_set_val(set, i, j, &v);
					} else {
						ldms_metric_set(set, i, &v);
					}
				}
				i++;
				minfo = &metric_info_list[i];
			}
			ldms_transaction_end(set);
		}
	}
}

void do_server(struct sockaddr_in *sin)
{
	int rc;
	ldms_t ldms;

	ldms = ldms_xprt_new_with_auth(xprt, auth_name, auth_opt);
	if (!ldms) {
		printf("ldms_xprt_new_with_auth error\n");
		exit(errno);
	}

	rc = ldms_xprt_listen(ldms, (void *)sin, sizeof(*sin), NULL, NULL);
	if (rc) {
		printf("ldms_xprt_listen: %d\n", rc);
		exit(rc);
	}

	printf("Listening on port %d\n", port);

	pthread_t t;
	rc = pthread_create(&t, NULL, __sample_sets, (void *)ldms);
	if (rc) {
		printf("pthread_create error %d\n", rc);
		exit(rc);
	}
	pthread_join(t, NULL);
}

void usage()
{
	printf("\ntest_ldms usage\n");
	printf("	-a auth_name	the same list as for ldmsd\n");
	printf("	-A auht_arg	the arguments for the authentication\n");
	printf("	-p port		listener port\n");
	printf("	-x xprt		sock, rdma, or ugni\n");
	printf("	-s name		Create a schema with the given name.\n"
	       "			This might be given multiple time.\n");
	printf("	-i schema:name:[true|false]	Create a set with the schema.\n"
	       "					The set is published if 'true' is given.\n"
	       "					This might be given multiple time.\n");
}

static size_t AUTH_OPT_MAX = 10;

static void process_auth_arg(char *arg)
{
	char *key, *value;
	if (!auth_opt) {
		auth_opt = av_new(AUTH_OPT_MAX);
		if (!auth_opt) {
			printf("Out of memory\n");
			exit(ENOMEM);
		}
	}
	if (auth_opt->count == auth_opt->size) {
		printf("Too many auth arguments");
		exit(EINVAL);
	}

	key = strtok(arg, "=");
	value = strtok(NULL, "=");
	auth_opt->list[auth_opt->count].name = key;
	auth_opt->list[auth_opt->count].value = value;
	auth_opt->count++;
}

void process_arg(int argc, char **argv)
{
	struct test_schema *tschema;
	char *dummy, *inst_name, *delim;
	char op;


	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'x':
			xprt = strdup(optarg);
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case 'a':
			auth_name = strdup(optarg);
			break;
		case 'A':
			process_auth_arg(optarg);
			break;
		case 's':
			(void)__schema_new(optarg);
			break;
		case 'i':
			dummy = strdup(optarg);
			delim = strchr(dummy, ':');
			if (!delim) {
				printf("Wrong -i value format\n");
				usage();
				exit(EINVAL);
			}
			*delim = '\0';
			inst_name = delim + 1;
			delim = strchr(inst_name, ':');
			if (!delim) {
				printf("Wrong -i value format\n");
				usage();
				exit(EINVAL);
			}
			*delim = '\0';
			tschema = __find_test_schema(dummy);
			if (!tschema) {
				printf("Failed to create set '%s' "
					"due to schema %s not found\n",
					delim+1, dummy);
				exit(ENOENT);
			}
			(void)__set_new(tschema, inst_name, delim + 1);
			free(dummy);
			break;
		case '?':
			usage();
			exit(0);
		default:
			printf("Unrecognized argument '%c'\n", op);
			exit(1);
			break;
		}
	}

	if (!port) {
		printf("Please specified the listener port with the -p option\n");
		usage();
		exit(1);
	}

	if (!xprt) {
		printf("Please specified the transport with the -x option\n");
		usage();
		exit(1);
	}
}

int main(int argc, char **argv) {
	ldms_init(512 * 1024);
	process_arg(argc, argv);
	struct sockaddr_in sin = {0};
	sin.sin_port = htons(port);
	sin.sin_family = AF_INET;

	do_server(&sin);
	sleep(1);
	printf("DONE\n");
	return 0;
}
