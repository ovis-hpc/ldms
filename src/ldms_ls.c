/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
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
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
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
 *
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */
#include <getopt.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <signal.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <inttypes.h>
#include <fcntl.h>
#include <sys/queue.h>
#include "ldms.h"
#include "ldms_xprt.h"

static pthread_mutex_t dir_lock;
static pthread_cond_t dir_cv;
static int dir_count;
static int dir_needed;

static pthread_mutex_t lu_lock;
static pthread_cond_t lu_cv;
static int lu_count;
static int lu_needed;

static pthread_mutex_t upd_lock;
static pthread_cond_t upd_cv;
static int upd_count;

struct ls_set {
	ldms_set_t set;
	LIST_ENTRY(ls_set) entry;
};
LIST_HEAD(set_list, ls_set) set_list;

#define FMT "h:p:i:x:lv"
void usage(char *argv[])
{
	printf("%s:\n"
	       "    -h <hostname>    The name of the host to query. Default is localhost.\n"
	       "    -p <port_num>    The port number. The default is 50000.\n"
	       "    -l               Show the values of the metrics in each metric set.\n"
	       "    -i <interval>    When used with the -l option, the metric values will \n"
	       "                     be queried and displayed every <interval> microseconds.\n"
	       "    -x <name>        The transport name: sock, rdma, or local. Default is\n"
	       "                     localhost unless -h is specified in which case it is sock.\n"
	       "    -v               Show detail information about the metric set. Specifying\n"
	       "                     this option multiple times increases the verbosity.\n",
	       argv[0]);
	exit(1);
}

void metric_printer(struct ldms_value_desc *vd, union ldms_value *v, void *arg)
{
	char value_str[64];
	printf("%4s ", ldms_type_to_str(vd->type));

	switch (vd->type) {
	case LDMS_V_U8:
		sprintf(value_str, "%hhu", v->v_u8);
		break;
	case LDMS_V_S8:
		sprintf(value_str, "%hhd", v->v_s8);
		break;
	case LDMS_V_U16:
		sprintf(value_str, "%hu", v->v_u16);
		break;
	case LDMS_V_S16:
		sprintf(value_str, "%hd", v->v_s16);
		break;
	case LDMS_V_U32:
		sprintf(value_str, "%8u", v->v_u32);
		break;
	case LDMS_V_S32:
		sprintf(value_str, "%d", v->v_s32);
		break;
	case LDMS_V_U64:
		sprintf(value_str, "%" PRIu64, v->v_u64);
		break;
	case LDMS_V_S64:
		sprintf(value_str, "%" PRId64, v->v_s64);
		break;
	}
	printf("%-16s %s\n", value_str, vd->name);
}
void print_detail(ldms_set_t s)
{
	struct ldms_set_desc *sd = s;

	printf("  METADATA --------\n");
	printf("             Size : %" PRIu32 "\n", sd->set->meta->meta_size);
	printf("            Inuse : %" PRIu32 "\n", sd->set->meta->tail_off);
	printf("     Metric Count : %" PRIu32 "\n", sd->set->meta->card);
	printf("               GN : %" PRIu64 "\n", sd->set->meta->meta_gn);
	printf("  DATA ------------\n");
	printf("             Size : %" PRIu32 "\n", sd->set->meta->data_size);
	printf("            Inuse : %" PRIu32 "\n", sd->set->data->tail_off);
	printf("               GN : %" PRIu64 "\n", sd->set->data->gn);
	printf("  -----------------\n");
}

int verbose = 0;
int long_format = 0;

void print_cb(ldms_t t, ldms_set_t s, int rc, void *arg)
{
	printf("%s\n", ldms_get_set_name(s));
	if (verbose)
		print_detail(s);
	if (long_format)
		ldms_visit_metrics(s, metric_printer, NULL);
	printf("\n");

	pthread_mutex_lock(&upd_lock);
	upd_count++;
	if (upd_count >= lu_needed)
		pthread_cond_signal(&upd_cv);
	pthread_mutex_unlock(&upd_lock);
}

void lookup_cb(ldms_t t, int status, ldms_set_t s, void *arg)
{
	struct ls_set *lss;
	if (status)
		return;

	lss = calloc(1, sizeof(struct ls_set));
	if (!lss)
		return;

	pthread_mutex_lock(&dir_lock);
	lss->set = s;
	LIST_INSERT_HEAD(&set_list, lss, entry);
	pthread_mutex_unlock(&dir_lock);

	pthread_mutex_lock(&lu_lock);
	lu_count++;
	if (lu_count == lu_needed)
		pthread_cond_signal(&lu_cv);
	pthread_mutex_unlock(&lu_lock);
}

void add_set_list(ldms_t t, ldms_dir_t _dir)
{
	int i;
	pthread_mutex_lock(&lu_lock);
	lu_count = 0;
	lu_needed = _dir->set_count;
	pthread_mutex_unlock(&lu_lock);

	dir_count = 1;
	pthread_cond_signal(&dir_cv);
	for (i = 0; i < _dir->set_count; i++)
		if (!verbose && !long_format)
			printf("%s\n", _dir->set_names[i]);
		else
			ldms_lookup(t, _dir->set_names[i], lookup_cb, 0);
}

static void _del_set(ldms_t t, const char *set_name)
{
	struct ls_set *lss;
	LIST_FOREACH(lss, &set_list, entry) {
		if (0 == strcmp(set_name, ldms_get_set_name(lss->set))) {
			ldms_set_release(lss->set);
			LIST_REMOVE(lss, entry);
			free(lss);
			break;
		}
	}
}

void del_set(ldms_t t, ldms_dir_t _dir)
{
	int i;
	for (i = 0; i < _dir->set_count; i++) {
		_del_set(t, _dir->set_names[i]);
	}
}

void add_set(ldms_t t, ldms_dir_t _dir)
{
	int i;
	for (i = 0; i < _dir->set_count; i++) {

		/* In case we already have it, delete it first */
		_del_set(t, _dir->set_names[i]);

		ldms_lookup(t, _dir->set_names[i], lookup_cb, 0);
	}
}

void dir_cb(ldms_t t, int status, ldms_dir_t _dir, void *cb_arg)
{
	if (status)
		return;

	pthread_mutex_lock(&dir_lock);
	switch (_dir->type) {
	case LDMS_DIR_LIST:
		add_set_list(t, _dir);
		break;
	case LDMS_DIR_DEL:
		del_set(t, _dir);
		break;
	case LDMS_DIR_ADD:
		add_set(t, _dir);
		break;
	}
	pthread_mutex_unlock(&dir_lock);
	ldms_dir_release(t, _dir);
}

int main(int argc, char *argv[])
{
	struct sockaddr_in sin;
	ldms_t ldms;
	int ret;
	struct hostent *h;
	u_char *oc;
	char *hostname = "localhost";
	short port_no = LDMS_DEFAULT_PORT;
	int op;
	int i;
	long sample_interval = -1;
	char *xprt = "local";

	opterr = 0;
	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'h':
			hostname = strdup(optarg);
			break;
		case 'p':
			port_no = atoi(optarg);
			break;
		case 'l':
			long_format = 1;
			break;
		case 'i':
			sample_interval = strtol(optarg, NULL, 0);
			break;
		case 'v':
			verbose++;
			break;
		case 'x':
			xprt = strdup(optarg);
			break;
		default:
			usage(argv);
		}
	}
	if ((sample_interval >= 0) && !(verbose || long_format))
		usage(argv);

	/* If they specify a host name change the default transport to socket */
	if (0 != strcmp(hostname, "localhost") && strcmp(xprt, "local"))
		xprt = "sock";
	h = gethostbyname(hostname);
	if (!h) {
		herror(argv[0]);
		usage(argv);
	}

	if (h->h_addrtype != AF_INET)
		usage(argv);
	oc = (u_char *)h->h_addr_list[0];

	ldms = ldms_create_xprt(xprt);
	if (!ldms) {
		printf("Error creating transport.\n");
		exit(1);
	}

	memset(&sin, 0, sizeof sin);
	sin.sin_addr.s_addr = *(unsigned int *)(h->h_addr_list[0]);
	sin.sin_family = h->h_addrtype;
	sin.sin_port = htons(port_no);
	if (verbose > 1) {
		printf("Hostname    : %s\n", hostname);
		printf("IP Address  : %s\n", inet_ntoa(sin.sin_addr));
		printf("Port        : %hu\n", port_no);
		printf("Transport   : %s\n", xprt);
	}
	ret  = ldms_connect(ldms, (struct sockaddr *)&sin, sizeof(sin));
	if (ret) {
		perror("ldms_ls");
		exit(1);
	}

	pthread_mutex_init(&dir_lock, 0);
	pthread_cond_init(&dir_cv, NULL);
	pthread_mutex_init(&lu_lock, 0);
	pthread_cond_init(&lu_cv, NULL);
	pthread_mutex_init(&upd_lock, 0);
	pthread_cond_init(&upd_cv, NULL);

	if (optind == argc) {
		dir_needed = 1;
		ret = ldms_dir(ldms, dir_cb, NULL, 1);
		if (ret) {
			printf("ldms_dir returned synchronous error %d\n",
			      ret);
			exit(1);
		}
	} else {
		if (!verbose && !long_format)
			usage(argv);

		/* Set list specified on the command line */
		for (i = optind; i < argc; i++)
			ldms_lookup(ldms, argv[i], lookup_cb, NULL);
	}

	pthread_mutex_lock(&dir_lock);
	while (dir_needed != dir_count)
		pthread_cond_wait(&dir_cv, &dir_lock);
	pthread_mutex_unlock(&dir_lock);

	/* If we're not monitoring the peer, wait for all lookups to complete */
	if ((verbose || long_format) && sample_interval < 0) {
		pthread_mutex_lock(&lu_lock);
		while (lu_needed != lu_count)
			pthread_cond_wait(&lu_cv, &lu_lock);
		pthread_mutex_unlock(&lu_lock);
	}
		
	do {
		struct ls_set *lss;
		pthread_mutex_lock(&dir_lock);
		LIST_FOREACH(lss, &set_list, entry) {
			ret = ldms_update(lss->set, print_cb, NULL);
		}
		pthread_mutex_unlock(&dir_lock);
		if (sample_interval > 0)
			usleep(sample_interval);
	} while (sample_interval >= 0);

	if ((verbose || long_format) && sample_interval < 0) {
		pthread_mutex_lock(&upd_lock);
		while (upd_count < lu_needed)
			pthread_cond_wait(&upd_cv, &upd_lock);
		pthread_mutex_unlock(&upd_lock);
	}

	exit(0);
}
