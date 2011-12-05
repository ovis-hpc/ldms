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
#include <byteswap.h>
#include <semaphore.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <inttypes.h>
#include "ldms.h"
#include "ldms_xprt.h"

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
volatile int io_outstanding;
void print_cb(ldms_t t, ldms_set_t s, int rc, void *arg)
{
	printf("%s\n", ldms_get_set_name(s));
	if (verbose)
		print_detail(s);
	if (long_format)
		ldms_visit_metrics(s, metric_printer, NULL);
	io_outstanding --;
	printf("\n");
}

struct io_status {
	sem_t sem;
	int rc;
	union {
		ldms_dir_t dir;
		ldms_set_t s;
	};
};

pthread_spinlock_t dir_lock;
ldms_dir_t dir;
ldms_set_t *sets;
sem_t cb2_sem;

void lookup_cb2(ldms_t t, int status, ldms_set_t s, void *arg)
{
	int set_no = (int)(unsigned long)arg;
	pthread_spin_lock(&dir_lock);
	if (status)
		goto out;
	sets[set_no] = s;
 out:
	pthread_spin_unlock(&dir_lock);
	if (set_no == dir->set_count - 1)
		sem_post(&cb2_sem);
}
void dir_cb2(ldms_t t, int status, ldms_dir_t _dir, void *cb_arg)
{
	int i;
	pthread_spin_lock(&dir_lock);
	if (!status) {
		if (sets && dir) {
			for (i = 0; i < dir->set_count; i++)
				ldms_set_release(sets[i]);
			free(sets);
		}
		if (_dir && _dir->set_count)
			sets = calloc(_dir->set_count, sizeof(ldms_set_t));
	}
	if (dir)
		ldms_dir_release(t, dir);
	dir = _dir;
	pthread_spin_unlock(&dir_lock);
	for (i = 0; i < dir->set_count; i++)
		ldms_lookup(t, dir->set_names[i], lookup_cb2,
			    (void *)(unsigned long)i, 1);
}

void dir_cb(ldms_t t, int status, ldms_dir_t dir, void *cb_arg)
{
	struct io_status *ios = cb_arg;
	ios->rc = status;
	ios->dir = dir;
	sem_post(&ios->sem);
}

void lookup_cb(ldms_t t, int status, ldms_set_t s, void *arg)
{
	struct io_status *ios = arg;
	ios->rc = status;
	ios->s = s;
	sem_post(&ios->sem);
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
	int set_count = 0;
	int i;
	long sample_interval = -1;
	char *xprt = "local";
	struct io_status io_status;

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
		printf("Transport   : %s", xprt);
	}
	ret  = ldms_connect(ldms, (struct sockaddr *)&sin, sizeof(sin));
	if (ret) {
		perror("ldms_ls");
		exit(1);
	}
	sem_init(&io_status.sem, 0, 0);
	if (optind == argc) {
		ret = ldms_dir(ldms, dir_cb, &io_status, 0);
		if (ret) {
			printf("ldms_dir returned synchronous error %d\n",
			      ret);
			exit(1);
		}

		sem_wait(&io_status.sem);
		if (io_status.dir == NULL || io_status.dir->set_count == 0)
			exit(0);
		dir = io_status.dir;
	} else {
		/* Set list specified on the command line */
		set_count = argc - optind;
		dir = malloc(sizeof *dir + (set_count * sizeof(char *)));
		if (!dir) {
			printf("Memory allocation failure allocating directory.\n");
			exit(ENOMEM);
		}
		dir->set_count = set_count;
		for (i = 0; i < set_count; i++) {
			dir->set_names[i] = strdup(argv[optind+i]);
		}
	}
	for (i = 0; i < dir->set_count; i++) {
		if (!long_format && !verbose)
			printf("%s\n", dir->set_names[i]);
	}
	if (ret)
		goto out;
	if (!long_format && !verbose)
		goto out;

	/* Long format and periodic query */
	pthread_spin_init(&dir_lock, 0);
	ldms_dir_release(ldms, dir);
	dir = NULL;
	/* Do another dir and ask for a coherent directory */
	sem_init(&cb2_sem, 0, 0);
	ldms_dir(ldms, dir_cb2, NULL, 1);

	/* Wait until we have at least one update */
	sem_wait(&cb2_sem);
	do {
		pthread_spin_lock(&dir_lock);
		for (i = 0; sets && dir && i < dir->set_count; i++) {
			io_outstanding++;
			ret = ldms_update(sets[i], print_cb, NULL);
		}
		pthread_spin_unlock(&dir_lock);
		if (sample_interval > 0)
			usleep(sample_interval);
	} while (sample_interval >= 0);
	while(io_outstanding)
		usleep(0);

 out:
	exit(0);
}
