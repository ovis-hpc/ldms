/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-15 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010-15 Sandia Corporation. All rights reserved.
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

#include <unistd.h>
#include <inttypes.h>
#include <stdarg.h>
#include <getopt.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/un.h>
#include <ctype.h>
#include <netdb.h>
#include <dlfcn.h>
#include <assert.h>
#include <libgen.h>
#include <time.h>
#include <event2/thread.h>
#include <coll/rbt.h>
#include <coll/str_map.h>
#include "event.h"
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_xprt.h"
#include "config.h"

#ifdef ENABLE_OCM
#include <ocm/ocm.h>
#include <coll/str_map.h>
const char *ldmsd_svc_type = "ldmsd_sampler";
uint16_t ocm_port = OCM_DEFAULT_PORT;
int ldmsd_ocm_init(const char *svc_type, uint16_t port);
#endif

#ifdef ENABLE_AUTH
#include "ovis_auth/auth.h"
#endif /* ENABLE_AUTH */

#define LDMSD_AUTH_ENV "LDMSD_AUTH_FILE"

#define LDMSD_SETFILE "/proc/sys/kldms/set_list"
#define LDMSD_LOGFILE "/var/log/ldmsd.log"
#define FMT "H:i:l:S:s:x:T:M:t:P:m:FkNf:D:qz:o:r:p:a"

#define LDMSD_MEM_SIZE_DEFAULT 512 * 1024

int flush_N = 2; /* The number of flush threads */
char myhostname[80];
char ldmstype[20];
int foreground;
pthread_t event_thread = (pthread_t)-1;
char *test_set_name;
int test_set_count=1;
int test_metric_count=1;
int notify=0;
char *logfile;
const char *secretword = 0;
int authenticate;
pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
size_t max_mem_size = LDMSD_MEM_SIZE_DEFAULT;

extern unsigned long saggs_mask;
ldms_t ldms;
FILE *log_fp;

/* dirty_threshold defined in ldmsd_store.c */
extern int dirty_threshold;
extern size_t calculate_total_dirty_threshold(size_t mem_total,
					      size_t dirty_ratio);
void do_connect(struct hostspec *hs);
int update_data(struct hostspec *hs);
void reset_hostspec(struct hostspec *hs);

int do_kernel = 0;
char *setfile = NULL;
char *listen_arg = NULL;

extern pthread_mutex_t host_list_lock;
extern LIST_HEAD(host_list_s, hostspec) host_list;
extern LIST_HEAD(ldmsd_store_policy_list, ldmsd_store_policy) sp_list;
extern pthread_mutex_t sp_list_lock;

int passive = 0;
int quiet = 0; /* by default ldmsd should not be quiet */
void ldms_log(const char *fmt, ...)
{
	if (quiet) /* Don't say a word when quiet */
		return;
	va_list ap;
	time_t t;
	struct tm *tm;
	char dtsz[200];

	pthread_mutex_lock(&log_lock);
	t = time(NULL);
	tm = localtime(&t);
	if (strftime(dtsz, sizeof(dtsz), "%a %b %d %H:%M:%S %Y", tm))
		fprintf(log_fp, "%s: ", dtsz);
	va_start(ap, fmt);
	vfprintf(log_fp, fmt, ap);
	fflush(log_fp);
	pthread_mutex_unlock(&log_lock);
}

void cleanup(int x)
{
	ldms_log("LDMS Daemon exiting...status %d\n", x);
	ldmsd_config_cleanup();
	if (ldms) {
		ldms_xprt_close(ldms);
		ldms = NULL;
	}

	/* Destroy all store instances */
	struct ldmsd_store_policy *sp;
	pthread_mutex_lock(&sp_list_lock);
	LIST_FOREACH(sp, &sp_list, link) {
		if (sp->si) {
			sp->si->plugin->close(sp->si->store_handle);
			sp->si = NULL;
		}
	}
	pthread_mutex_unlock(&sp_list_lock);
	exit(x);
}

FILE *ldmsd_open_log()
{
	FILE *f;
	f = fopen(logfile, "a");
	if (!f) {
		ldms_log("Could not open the log file named '%s'\n",
							logfile);
		cleanup(9);
	} else {
		int fd = fileno(f);
		if (dup2(fd, 1) < 0) {
			ldms_log("Cannot redirect log to %s\n",
							logfile);
			cleanup(10);
		}
		if (dup2(fd, 2) < 0) {
			ldms_log("Cannot redirect log to %s\n",
							logfile);
			cleanup(11);
		}
		stdout = f;
		stderr = f;
	}
	return f;
}

void ldmsd_logrotate(int x) {
	if (logfile) {
		/*
		 * Close after open the new log file
		 * to reserve the file descriptors 1 and 2.
		 */
		pthread_mutex_lock(&log_lock);
		FILE *new_log = ldmsd_open_log();
		fflush(log_fp);
		fclose(log_fp);
		log_fp = new_log;
		pthread_mutex_unlock(&log_lock);
	}
}

void cleanup_sa(int signal, siginfo_t *info, void *arg)
{
	printf("signo : %d\n", info->si_signo);
	printf("si_pid: %d\n", info->si_pid);
	cleanup(100);
}

void usage(char *argv[])
{
	printf("%s: [%s]\n", argv[0], FMT);
	printf("    -P thr_count   Count of event threads to start.\n");
	printf("    -i             Test metric set sample interval.\n");
	printf("    -k             Publish publish kernel metrics.\n");
	printf("    -s setfile     Text file containing kernel metric sets to publish.\n"
	       "                   [" LDMSD_SETFILE "]\n");
	printf("    -S sockname    Specifies the unix domain socket name to\n"
	       "                   use for ldmsctl access.\n");
	printf("    -x xprt:port   Specifies the transport type to listen on. May be specified\n"
	       "                   more than once for multiple transports. The transport string\n"
	       "                   is one of 'rdma', or 'sock'. A transport specific port number\n"
	       "                   is optionally specified following a ':', e.g. rdma:50000.\n");
	printf("    -l log_file    The path to the log file for status messages.\n"
	       "                   [" LDMSD_LOGFILE "]\n");
	printf("    -q             Quiet mode. All the logging messages will be suppressed.\n"
	       "                   [false].\n");
	printf("    -F             Foreground mode, don't daemonize the program [false].\n");
	printf("    -t count       Number of test sets to create.\n");
	printf("    -T set_name    Test set prefix.\n");
	printf("    -N             Notify registered monitors of the test metric sets\n");
	printf("    -t set_count   Create set_count instances of set_name.\n");
	printf("    -m memory size   Maximum size of pre-allocated memory for metric sets.\n"
	       "                     The given size must be less than 1 petabytes.\n"
	       "                     For example, 20M or 20mb are 20 megabytes.\n");
	printf("    -f count       The number of flush threads.\n");
	printf("    -D num         The dirty threshold.\n");
#ifdef ENABLE_OCM
	printf("    -o ocm_port    The OCM port (default: %hu).\n", ocm_port);
	printf("    -z ldmsd_mode  ldmsd mode (either 'ldmsd_sampler' or 'ldmsd_aggregator'\n");
#endif
#ifdef ENABLE_AUTH
	printf("    -a		   Authentication is required. The environment variable\n"
	       "		   %s must be set to the full path to the file storing\n"
	       "		   the shared secret word, e.g., secretword=<word>, where\n"
	       "		   %d < word length < %d\n", LDMSD_AUTH_ENV,
				   MIN_SECRET_WORD_LEN, MAX_SECRET_WORD_LEN);
#endif /* ENABLE_AUTH */
#ifdef ENABLE_LDMSD_TEST
	printf("    -p port        The listener port for receiving configuration.\n"
	       "                   via socket\n");
#endif /* ENABLE_LDMSD_TEST */
#ifdef ENABLE_LDMSD_RCTRL
	printf("    -r port        The listener port for receiving configuration\n"
	       "                   from the ldmsd_rctl program\n");
#endif
	cleanup(1);
}

int ev_thread_count = 1;
struct event_base **ev_base;	/* event bases */
pthread_t *ev_thread;		/* sampler threads */
int *ev_count;			/* number of hosts/samplers assigned to each thread */

int find_least_busy_thread()
{
	int i;
	int idx = 0;
	int count = ev_count[0];
	for (i = 1; i < ev_thread_count; i++) {
		if (ev_count[i] < count) {
			idx = i;
			count = ev_count[i];
		}
	}
	return idx;
}

struct event_base *get_ev_base(int idx)
{
	ev_count[idx] = ev_count[idx] + 1;
	return ev_base[idx];
}

void release_ev_base(int idx)
{
	ev_count[idx] = ev_count[idx] - 1;
}

pthread_t get_thread(int idx)
{
	return ev_thread[idx];
}

/*
 * This function opens the device file specified by 'devname' and
 * mmaps the metric set 'set_no'.
 */
int map_fd;
ldms_set_t map_set;
int publish_kernel(const char *setfile)
{
#if 1
	return ENOSYS;
#else
	int rc;
	int i, j;
	void *meta_addr;
	void *data_addr;
	int set_no;
	int set_size;
	struct ldms_set_hdr *sh;
	unsigned char *p;
	char set_name[80];
	FILE *fp;

	fp = fopen(setfile, "r");
	if (!fp) {
		ldms_log("The specified kernel metric set file '%s' could not be opened.\n",
			 setfile);
		return 0;
	}

	map_fd = open("/dev/kldms0", O_RDWR);
	if (map_fd < 0) {
		ldms_log("Error %d opening the KLDMS device file '/dev/kldms0'\n", map_fd);
		return map_fd;
	}

	while (3 == fscanf(fp, "%d %d %s", &set_no, &set_size, set_name)) {
		int id = set_no << 13;
		ldms_log("Mapping set %d name %s\n", set_no, set_name);
		meta_addr = mmap((void *)0, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, map_fd, id);
		if (meta_addr == MAP_FAILED)
			return -ENOMEM;
		sh = meta_addr;
		if (set_name[0] == '/')
			sprintf(sh->instance_name, "%s%s", myhostname, set_name);
		else
			sprintf(sh->instance_name, "%s/%s", myhostname, set_name);
		data_addr = mmap((void *)0, 8192, PROT_READ|PROT_WRITE,
				 MAP_SHARED, map_fd,
				 id | LDMS_SET_ID_DATA);
		if (data_addr == MAP_FAILED) {
			munmap(meta_addr, 8192);
			return -ENOMEM;
		}
		rc = ldms_mmap_set(meta_addr, data_addr, &map_set);
		if (rc) {
			ldms_log("Error encountered mmaping the set '%s', rc %d\n",
				 set_name, rc);
			return rc;
		}
		sh = meta_addr;
		p = meta_addr;
		ldms_log("addr: %p\n", meta_addr);
		for (i = 0; i < 256; i = i + j) {
			for (j = 0; j < 16; j++)
				ldms_log("%02x ", p[i+j]);
			ldms_log("\n");
			for (j = 0; j < 16; j++) {
				if (isalnum(p[i+j]))
					ldms_log("%2c ", p[i+j]);
				else
					ldms_log("%2s ", ".");
			}
			ldms_log("\n");
		}
		ldms_log("name: '%s'\n", sh->instance_name);
		ldms_log("size: %d\n", sh->meta_sz);
	}
	return 0;
#endif
}


char *skip_space(char *s)
{
	while (*s != '\0' && isspace(*s)) s++;
	if (*s == '\0')
		return NULL;
	return s;
}

static char msg_buf[4096];
void ldmsd_msg_logger(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg_buf, sizeof(msg_buf), fmt, ap);
	ldms_log(msg_buf);
}

int calculate_timeout(int thread_id, unsigned long interval_us,
			     long offset_us, struct timeval* tv){

	struct timeval new_tv;
	long int adj_interval;
	long int epoch_us;

	if (thread_id < 0){
		/* get real time of day */
		gettimeofday(&new_tv, NULL);
	} else {
		/* NOTE: this uses libevent's cached time for the callback.
		      By the time we add the event we will be at least off by
			 the amount of time it takes to do the sample call. We
			 deem this accepable. */
		event_base_gettimeofday_cached(get_ev_base(thread_id), &new_tv);
	}

	epoch_us = (1000000 * (long int)new_tv.tv_sec) +
		(long int)new_tv.tv_usec;
	adj_interval = interval_us - (epoch_us % interval_us) + offset_us;
	/* Could happen initially, and later depending on when the event
	   actually occurs. However the max negative this can be, based on
	   the restrictions put in is (-0.5*interval+ 1us). Skip this next
	   point and go on to the next one */
	if (adj_interval <= 0)
		adj_interval += interval_us; /* Guaranteed to be positive */

	tv->tv_sec = adj_interval/1000000;
	tv->tv_usec = adj_interval % 1000000;
	return 0;
}

static void stop_sampler(struct ldmsd_plugin_cfg *pi)
{
	evtimer_del(pi->event);
	event_free(pi->event);
	pi->event = NULL;
	release_ev_base(pi->thread_id);
	pi->thread_id = -1;
	pi->ref_count--;
}

void plugin_sampler_cb(int fd, short sig, void *arg)
{
	struct timeval tv;
	struct ldmsd_plugin_cfg *pi = arg;
	pthread_mutex_lock(&pi->lock);
	assert(pi->plugin->type == LDMSD_PLUGIN_SAMPLER);
	if (pi->synchronous){
		calculate_timeout(pi->thread_id, pi->sample_interval_us,
				  pi->sample_offset_us, &pi->timeout);
	}
	int rc = pi->sampler->sample();
	if (!rc) {
		(void)evtimer_add(pi->event, &pi->timeout);
	} else {
		/*
		 * If the sampler reports an error don't reschedule
		 * the timeout. This is an indication of a configuration
		 * error that needs to be corrected.
		*/
		ldms_log("'%s': failed to sample. Stopping the plug-in.\n",
				pi->name);
		stop_sampler(pi);
	}
	pthread_mutex_unlock(&pi->lock);
}

/*
 * Start the sampler
 */
int ldmsd_start_sampler(char *plugin_name, char *interval, char *offset,
			char err_str[LEN_ERRSTR])
{
	char *attr, *endptr;
	int rc = 0;
	unsigned long sample_interval;
	long sample_offset = 0;
	int synchronous = 0;
	struct ldmsd_plugin_cfg *pi;
	err_str[0] = '\0';

	sample_interval = strtoul(interval, &endptr, 0);
	if (endptr[0] != '\0') {
		snprintf(err_str, LEN_ERRSTR, "interval '%s' invalid", interval);
		return EINVAL;
	}

	pi = ldmsd_get_plugin((char *)plugin_name);
	if (!pi) {
		rc = ENOENT;
		snprintf(err_str, LEN_ERRSTR, "Sampler not found.");
		return rc;
	}
	pthread_mutex_lock(&pi->lock);
	if (pi->plugin->type != LDMSD_PLUGIN_SAMPLER) {
		rc = EINVAL;
		snprintf(err_str, LEN_ERRSTR,
				"The specified plugin is not a sampler.");
		goto out;
	}
	if (pi->thread_id >= 0) {
		rc = EBUSY;
		snprintf(err_str, LEN_ERRSTR, "Sampler is already running.");
		goto out;
	}

	if (offset) {
		sample_offset = strtol(offset, NULL, 0);
		if ( !((sample_interval >= 10) &&
		       (sample_interval >= labs(sample_offset)*2)) ){
			snprintf(err_str, LEN_ERRSTR, "Sampler parameters "
				"interval and offset are incompatible.");
			goto out;
		}
		synchronous = 1;
	}

	pi->sample_interval_us = sample_interval;
	pi->sample_offset_us = sample_offset;
	pi->synchronous = synchronous;

	pi->ref_count++;

	pi->thread_id = find_least_busy_thread();
	pi->event = evtimer_new(get_ev_base(pi->thread_id), plugin_sampler_cb, pi);
	if (pi->synchronous){
		calculate_timeout(-1, pi->sample_interval_us,
				  pi->sample_offset_us, &pi->timeout);
	} else {
		pi->timeout.tv_sec = sample_interval / 1000000;
		pi->timeout.tv_usec = sample_interval % 1000000;
	}
	rc = evtimer_add(pi->event, &pi->timeout);
out:
	pthread_mutex_unlock(&pi->lock);
	return rc;
}

struct oneshot {
	struct ldmsd_plugin_cfg *pi;
	struct event *event;
};

void oneshot_sample_cb(int fd, short sig, void *arg)
{
	struct timeval tv;
	struct oneshot *os = arg;
	struct ldmsd_plugin_cfg *pi = os->pi;
	pthread_mutex_lock(&pi->lock);
	assert(pi->plugin->type == LDMSD_PLUGIN_SAMPLER);
	pi->sampler->sample();
	pi->ref_count--;
	evtimer_del(os->event);
	free(os);
	release_ev_base(pi->thread_id);
	pthread_mutex_unlock(&pi->lock);
}

int ldmsd_oneshot_sample(char *plugin_name, char *ts, char err_str[LEN_ERRSTR])
{
	char *attr, *endptr;
	int rc = 0;
	struct ldmsd_plugin_cfg *pi;
	err_str[0] = '\0';
	time_t now, sched;
	struct timeval tv;

	if (0 == strncmp(ts, "now", 3)) {
		ts = ts + 4;
		tv.tv_sec = strtoul(ts, NULL, 10);
	} else {
		sched = strtoul(ts, NULL, 10);
		now = time(NULL);
		if (now < 0) {
			snprintf(err_str, LEN_ERRSTR, "Failed to get "
						"the current time.");
			rc = errno;
			return rc;
		}
		double diff = difftime(sched, now);
		if (diff < 0) {
			snprintf(err_str, LEN_ERRSTR, "The schedule time '%s' "
				 "is ahead of the current time %jd",
				 ts, (intmax_t)now);
			rc = EINVAL;
			return rc;
		}
		tv.tv_sec = diff;
	}
	tv.tv_usec = 0;

	struct oneshot *ossample = malloc(sizeof(ossample));
	if (!ossample) {
		snprintf(err_str, LEN_ERRSTR, "Out of Memory");
		rc = ENOMEM;
		return rc;
	}

	pi = ldmsd_get_plugin((char *)plugin_name);
	if (!pi) {
		rc = ENOENT;
		snprintf(err_str, LEN_ERRSTR, "Sampler not found.");
		free(ossample);
		return rc;
	}
	pthread_mutex_lock(&pi->lock);
	if (pi->plugin->type != LDMSD_PLUGIN_SAMPLER) {
		rc = EINVAL;
		snprintf(err_str, LEN_ERRSTR,
				"The specified plugin is not a sampler.");
		goto err;
	}
	pi->ref_count++;
	ossample->pi = pi;
	if (pi->thread_id < 0) {
		snprintf(err_str, LEN_ERRSTR, "Sampler '%s' not started yet.",
								plugin_name);
		rc = EPERM;
		goto err;
	}
	ossample->event = evtimer_new(get_ev_base(pi->thread_id),
				      oneshot_sample_cb, ossample);

	rc = evtimer_add(ossample->event, &tv);
	if (rc)
		goto err;
	goto out;
err:
	free(ossample);
out:
	pthread_mutex_unlock(&pi->lock);
	return rc;
}

/*
 * Stop the sampler
 */
int ldmsd_stop_sampler(char *plugin_name, char err_str[LEN_ERRSTR])
{
	int rc = 0;
	struct ldmsd_plugin_cfg *pi;
	err_str[0] = '\0';

	pi = ldmsd_get_plugin(plugin_name);
	if (!pi) {
		rc = ENOENT;
		snprintf(err_str, LEN_ERRSTR, "Sampler not found.");
		goto out_nolock;
	}
	pthread_mutex_lock(&pi->lock);
	/* Ensure this is a sampler */
	if (pi->plugin->type != LDMSD_PLUGIN_SAMPLER) {
		rc = EINVAL;
		snprintf(err_str, LEN_ERRSTR,
				"The specified plugin is not a sampler.");
		goto out;
	}
	if (pi->event) {
		evtimer_del(pi->event);
		event_free(pi->event);
		pi->event = NULL;
		release_ev_base(pi->thread_id);
		pi->thread_id = -1;
		pi->ref_count--;
	} else {
		rc = EINVAL;
		snprintf(err_str, LEN_ERRSTR, "The sampler is not running.");
	}
out:
	pthread_mutex_unlock(&pi->lock);
out_nolock:
	return rc;
}

void ldmsd_host_sampler_cb(int fd, short sig, void *arg)
{
	struct hostspec *hs = arg;
	int rc;

	pthread_mutex_lock(&hs->conn_state_lock);
	switch (hs->conn_state) {
	case HOST_DISCONNECTED:
		do_connect(hs);
		break;
	case HOST_CONNECTED:
		if (update_data(hs))
			hs->conn_state = HOST_DISCONNECTED;
		break;
	case HOST_CONNECTING:
		ldms_log("Connection stall on '%s[%s]'.\n", hs->hostname, hs->xprt_name);
		break;
	case HOST_DISABLED:
		ldms_log("Host %s[%s] is disabled.\n", hs->hostname, hs->xprt_name);
		break;
	default:
		ldms_log("Host connection state '%d' is invalid.\n",
			 hs->conn_state);
		assert(0);
	}
	pthread_mutex_unlock(&hs->conn_state_lock);
}

/*
 * Release the ldms set, metrics and storage policy from a hostset record.
 */
void reset_hostset(struct hostset *hset)
{
	struct ldmsd_store_policy_ref *ref;
	struct hset_metric *hsm;
	if (hset->set) {
		ldms_set_delete(hset->set);
		hset->set = NULL;
	}
	while (!LIST_EMPTY(&hset->lsp_list)) {
		ref = LIST_FIRST(&hset->lsp_list);
		LIST_REMOVE(ref, entry);
		free(ref);
	}
}

/*
 * Host Type Descriptions:
 *
 * 'active' -
 *    - ldms_xprt_connect() to a specified peer
 *    - ldms_xprt_lookup() the peer's metric sets
 *    - periodically performs an ldms_update of the peer's metric data
 *
 * 'bridging' - Designed to 'hop over' fire walls by initiating the connection
 *    - ldms_xprt_connect to a specified peer
 *
 * 'passive' - Designed as target side of 'bridging' host
 *    - searches list of incoming connections (connections it
 *      ldms_accepted) to find the matching peer (the bridging host
 *      that connected to it)
 *    - ldms_lookup of the peer's metric data
 *    - periodically performs an ldms_update of the peer's metric data
 */

int sample_interval = 2000000;
void lookup_cb(ldms_t t, enum ldms_lookup_status status, int more, ldms_set_t s,
		void *arg)
{
	int rc;
	struct hostset *hset = arg;

	pthread_mutex_lock(&hset->state_lock);
	if (status != LDMS_LOOKUP_OK){
		ldms_log("Error doing lookup for set '%s'\n",
				hset->name);
		hset->set = NULL;
		goto err;
	}
	hset->set = s;
	rc = apply_store_policies(hset);
	if (rc)
		goto err;
	hset->state = LDMSD_SET_READY;
 out:
	pthread_mutex_unlock(&hset->state_lock);
	return;
 err:
	reset_hostset(hset);
	hset->state = LDMSD_SET_CONFIGURED;
	pthread_mutex_unlock(&hset->state_lock);
	hset_ref_put(hset);
}

/*
 * Must be called with the hostpec conn_state_lock held.
 *
 * Closes the transport, cleans up all hostset state.
 */
void reset_hostspec(struct hostspec *hs)
{
	struct hostset *hset;

	hs->x = NULL;
	hs->conn_state = HOST_DISCONNECTED;

	LIST_FOREACH(hset, &hs->set_list, entry) {
		pthread_mutex_lock(&hset->state_lock);
		reset_hostset(hset);
		/*
		 * Do the lookup again after the reconnection is successful.
		 */
		hset->state = LDMSD_SET_CONFIGURED;
		pthread_mutex_unlock(&hset->state_lock);
	}
}

#if 0
void _add_cb(ldms_t t, struct hostspec *hs, const char *set_name)
{
	struct hostset *hset;
	int rc;

	ldms_log("Adding the metric set '%s'\n", set_name);

	/* Check to see if it's already there */
	hset = find_host_set(hs, set_name);
	if (!hset) {
		hset = hset_new();
		if (!hset) {
			ldms_log("Memory allocation failure in %s for set_name %s\n",
				 __FUNCTION__, set_name);
			return;
		}
		hset->name = strdup(set_name);
		hset->host = hs;

		pthread_mutex_lock(&hs->set_list_lock);
		LIST_INSERT_HEAD(&hs->set_list, hset, entry);
		pthread_mutex_unlock(&hs->set_list_lock);

		/* Take a lookup reference. Find takes one for us. */
		hset_ref_get(hset);
	}

	/* Refresh the set with a lookup */
	rc = ldms_lookup(hs->x, set_name, lookup_cb, hset);
	if (rc)
		ldms_log("Synchronous error %d from ldms_lookup\n", rc);
}

/*
 * Destroy the set and metrics associated with the set named in the
 * directory update.
 */
void _dir_cb_del(ldms_t t, struct hostspec *hs, const char *set_name)
{
	struct hostset *hset = find_host_set(hs, set_name);
	ldms_log("%s removing set '%s'\n", __FUNCTION__, set_name);
	if (hset) {
		reset_hostset(hset);
		hset_ref_put(hset);
	}
}

/*
 * Process the directory list and add or restore specified sets.
 */
void dir_cb_list(ldms_t t, ldms_dir_t dir, void *arg)
{
	struct hostspec *hs = arg;
	int i;

	for (i = 0; i < dir->set_count; i++)
		_add_cb(t, hs, dir->set_names[i]);
}

/*
 * Process the directory list and add or restore specified sets.
 */
void dir_cb_add(ldms_t t, ldms_dir_t dir, void *arg)
{
	struct hostspec *hs = arg;
	int i;
	for (i = 0; i < dir->set_count; i++)
		_add_cb(t, hs, dir->set_names[i]);
}

/*
 * Process the directory list and release the sets and metrics
 * associated with the specified sets.
 */
void dir_cb_del(ldms_t t, ldms_dir_t dir, void *arg)
{
	struct hostspec *hs = arg;
	int i;

	for (i = 0; i < dir->set_count; i++)
		_dir_cb_del(t, hs, dir->set_names[i]);
}

/*
 * The ldms_dir has completed. Decode the directory type and call the
 * appropriate handler function.
 */
void dir_cb(ldms_t t, int status, ldms_dir_t dir, void *arg)
{
	struct hostspec *hs = arg;
	if (status) {
		ldms_log("Error %d in lookup on host %s.\n",
		       status, hs->hostname);
		return;
	}
	switch (dir->type) {
	case LDMS_DIR_LIST:
		dir_cb_list(t, dir, hs);
		break;
	case LDMS_DIR_ADD:
		dir_cb_add(t, dir, hs);
		break;
	case LDMS_DIR_DEL:
		dir_cb_del(t, dir, hs);
		break;
	}
	ldms_xprt_dir_free(t, dir);
}
#endif

void __ldms_connect_cb(ldms_t x, ldms_conn_event_t e, void *cb_arg)
{
	struct hostspec *hs = cb_arg;
	switch (e) {
	case LDMS_CONN_EVENT_CONNECTED:
		hs->conn_state = HOST_CONNECTED;
		if (hs->synchronous){
			calculate_timeout(hs->thread_id, hs->sample_interval,
					  hs->sample_offset, &hs->timeout);
		} else {
			hs->timeout.tv_sec = hs->sample_interval / 1000000;
			hs->timeout.tv_usec = hs->sample_interval % 1000000;
		}
		evtimer_add(hs->event, &hs->timeout);
		break;
	case LDMS_CONN_EVENT_REJECTED:
	case LDMS_CONN_EVENT_DISCONNECTED:
		/* Destroy the ldms xprt. */
		ldms_xprt_delete(hs->x);
		goto schedule_reconnect;
		break;
	case LDMS_CONN_EVENT_ERROR:
		/* Disconnect the connection */
		ldms_xprt_close(hs->x);
		goto schedule_reconnect;
		break;
	default:
		assert(0);
	}
	return;
schedule_reconnect:
	reset_hostspec(hs);
	hs->timeout.tv_sec = hs->connect_interval / 1000000;
	hs->timeout.tv_usec = hs->connect_interval % 1000000;
	evtimer_add(hs->event, &hs->timeout);
}

void ldms_connect_cb(ldms_t x, ldms_conn_event_t e, void *cb_arg)
{
	struct hostspec *hs = cb_arg;
	pthread_mutex_lock(&hs->conn_state_lock);
	__ldms_connect_cb(x, e, cb_arg);
	pthread_mutex_unlock(&hs->conn_state_lock);
}

void do_connect(struct hostspec *hs)
{
	int ret;

	assert(hs->x == NULL);
	switch (hs->type) {
	case ACTIVE:
	case BRIDGING:
		hs->x = ldms_xprt_new(hs->xprt_name, ldms_log, secretword);
		if (hs->x) {
			ret  = ldms_xprt_connect(hs->x, (struct sockaddr *)&hs->sin,
						 sizeof(hs->sin), ldms_connect_cb, hs);
			if (ret) {
				ldms_xprt_put(hs->x);
				hs->x = NULL;
			} else
				hs->conn_state = HOST_CONNECTING;
		} else {
			ldms_log("%s Error creating endpoint on transport '%s'.\n",
				 __func__, hs->xprt_name);
			hs->conn_state = HOST_DISABLED;
		}
		break;
	case PASSIVE:
		hs->x = ldms_xprt_by_remote_sin(&hs->sin);
		/* Call connect callback to advance state and update timers*/
		if (hs->x) {
			__ldms_connect_cb(hs->x, LDMS_CONN_EVENT_CONNECTED, hs);
		} else {
			hs->timeout.tv_sec = hs->connect_interval / 1000000;
			hs->timeout.tv_usec = hs->connect_interval % 1000000;
			evtimer_add(hs->event, &hs->timeout);
		}
		break;
	case LOCAL:
		assert(0);
	}
}

void update_complete_cb(ldms_t t, ldms_set_t s, int status, void *arg)
{
	struct hostset *hset = arg;
	uint64_t gn;
	pthread_mutex_lock(&hset->state_lock);
	if (status) {
		reset_hostset(hset);
		hset->state = LDMSD_SET_CONFIGURED;
		goto out1;
	}

	gn = ldms_set_data_gn_get(hset->set);
	if (hset->gn == gn) {
		ldms_log("Over-sampled set %s with generation# %d.\n",
			 hset->name, hset->gn);
		goto out;
	}

	if (!ldms_set_is_consistent(hset->set)) {
		ldms_log("Set %s with generation# %d is inconsistent.\n",
			 hset->name, hset->gn);
		goto out;
	}
	hset->gn = gn;

	struct ldmsd_store_policy_ref *lsp_ref;
	LIST_FOREACH(lsp_ref, &hset->lsp_list, entry) {
		struct ldmsd_store_policy *lsp = lsp_ref->lsp;

		pthread_mutex_lock(&lsp->cfg_lock);
		switch (lsp->state) {
		case STORE_POLICY_CONFIGURING:
			if (update_policy_metrics(lsp, hset))
				break;
			/* fall through to add data */
		default:
			ldmsd_store_data_add(lsp, hset->set);
		}
		pthread_mutex_unlock(&lsp->cfg_lock);
	}
 out:
	hset->state = LDMSD_SET_READY;
 out1:
	pthread_mutex_unlock(&hset->state_lock);
	/* Put the reference taken at the call to ldms_update() */
	hset_ref_put(hset);
}

int do_lookup(struct hostspec *hs, struct hostset *hset)
{
	if (hs->type != LOCAL)
		return ldms_xprt_lookup(hs->x, hset->name,
					LDMS_LOOKUP_BY_INSTANCE,
					lookup_cb, hset);

	/* local host */
	int status = LDMS_LOOKUP_OK;
	ldms_set_t set = ldms_set_by_name(hset->name);
	if (!set)
		status = LDMS_LOOKUP_ERROR;
	pthread_mutex_unlock(&hset->state_lock);
	lookup_cb(NULL, status, 0, set, hset);
	/* To match the unlock() in update_data */
	pthread_mutex_lock(&hset->state_lock);
	return 0;
}

int do_update(struct hostspec *hs, struct hostset *hset)
{
	if (hs->type != LOCAL)
		return ldms_xprt_update(hset->set, update_complete_cb, hset);

	/* local host */
	int status = 0;
	hset->set = ldms_set_by_name(hset->name);
	if (!hset->set)
		status = ENOENT;
	pthread_mutex_unlock(&hset->state_lock);
	update_complete_cb(NULL, hset->set, status, hset);
	/* To match the unlock() in update_data */
	pthread_mutex_lock(&hset->state_lock);
	return 0;
}

/*
 * hostspec conn_state_lock must be held.
 */
int update_data(struct hostspec *hs)
{
	int ret;
	struct hostset *hset;
	int host_error = 0;

	if (hs->type == LOCAL) {
		ldms_log("Sample callback on local host %s.\n", hs->hostname);
		assert(NULL == hs->x);
		return 0;
	}
	if (hs->type == BRIDGING) {
		ldms_log("Sample callback on host %s in bridging mode.\n", hs->hostname);
		return 0;
	}

	if (hs->standby && (0 == (hs->standby & saggs_mask))) {
		ldms_log("Sample callback on unowned failover host %s.\n", hs->hostname);
		return 0;
	}
	/* Take the host lock to protect the set_list */
	pthread_mutex_lock(&hs->set_list_lock);
	LIST_FOREACH(hset, &hs->set_list, entry) {
		pthread_mutex_lock(&hset->state_lock);
		switch (hset->state) {
		case LDMSD_SET_CONFIGURED:
			hset->state = LDMSD_SET_LOOKUP;
			/* Get a lookup reference */
			hset_ref_get(hset);
			ret = do_lookup(hs, hset);
			if (ret) {
				hset->state = LDMSD_SET_CONFIGURED;
				host_error = 1;
				ldms_log("Synchronous error %d "
					"from ldms_lookup\n", ret);
				hset_ref_put(hset);
			}
			break;
		case LDMSD_SET_READY:
			hset->state = LDMSD_SET_BUSY;
			if (hset->curr_busy_count) {
				hset->total_busy_count += hset->curr_busy_count;
				hset->curr_busy_count = 0;
			}
			/* Get reference for update */
			hset_ref_get(hset);
			ret = do_update(hs, hset);
			if (ret) {
				hset->state = LDMSD_SET_CONFIGURED;
				host_error = 1;
				ldms_log("Error %d updating metric set "
					"on host %s:%d[%s].\n", ret,
					hs->hostname, ntohs(hs->sin.sin_port),
					hs->xprt_name);
				hset_ref_put(hset);
			}
			break;
		case LDMSD_SET_LOOKUP:
			/* do nothing */
			break;
		case LDMSD_SET_BUSY:
			hset->curr_busy_count++;
			break;
		default:
			ldms_log("Invalid hostset state '%d'\n", hset->state);
			assert(0);
			break;
		}
		pthread_mutex_unlock(&hset->state_lock);
		if (host_error)
			break;
	}
	if (host_error) {
		reset_hostspec(hs);
		hs->timeout.tv_sec = hs->connect_interval / 1000000;
		hs->timeout.tv_usec = hs->connect_interval % 1000000;
	} else {
		if (hs->synchronous){
			calculate_timeout(hs->thread_id, hs->sample_interval,
					  hs->sample_offset, &hs->timeout);
		} else {
			hs->timeout.tv_sec = hs->sample_interval / 1000000;
			hs->timeout.tv_usec = hs->sample_interval % 1000000;
		}
	}
	evtimer_add(hs->event, &hs->timeout);
	pthread_mutex_unlock(&hs->set_list_lock);
	return host_error;
}

void keepalive_cb(int fd, short sig, void *arg)
{
	struct event *keepalive = arg;
	struct timeval keepalive_to;

	keepalive_to.tv_sec = 10;
	keepalive_to.tv_usec = 0;
	evtimer_add(keepalive, &keepalive_to);
}

void *event_proc(void *v)
{
	struct event_base *sampler_base = v;
	struct timeval keepalive_to;
	struct event *keepalive;
	keepalive = evtimer_new(sampler_base, keepalive_cb, NULL);
	keepalive_to.tv_sec = 10;
	keepalive_to.tv_usec = 0;
	evtimer_assign(keepalive, sampler_base, keepalive_cb, keepalive);
	evtimer_add(keepalive, &keepalive_to);
	event_base_loop(sampler_base, 0);
	ldms_log("Exiting the sampler thread.\n");
	return NULL;
}

void listen_on_transport(char *transport_str)
{
	char *name;
	char *port_s;
	int port_no;
	ldms_t l;
	int ret;
	struct sockaddr_in sin;

	ldms_log("Listening on transport %s\n", transport_str);
	name = strtok(transport_str, ":");
	port_s = strtok(NULL, ":");
	if (!port_s)
		port_no = LDMS_DEFAULT_PORT;
	else
		port_no = atoi(port_s);

	l = ldms_xprt_new(name, ldms_log, secretword);
	if (!l) {
		ldms_log("The transport specified, '%s', is invalid.\n", name);
		cleanup(6);
	}
	ldms = l;
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = 0;
	sin.sin_port = htons(port_no);
	ret = ldms_xprt_listen(l, (struct sockaddr *)&sin, sizeof(sin));
	if (ret) {
		ldms_log("Error %d listening on the '%s' transport.\n",
			 ret, name);
		cleanup(7);
	}
}

void ev_log_cb(int sev, const char *msg)
{
	const char *sev_s[] = {
		"EV_DEBUG",
		"EV_MSG",
		"EV_WARN",
		"EV_ERR"
	};
	ldms_log("%s: %s\n", sev_s[sev], msg);
}

#ifdef ENABLE_AUTH
int ldmsd_get_secretword()
{
	int rc;

	/* Get path from the environment variable */
	char *path = getenv(LDMSD_AUTH_ENV);
	if (!path) {
		ldms_log("ldmsd auth: Failed to get the auth file path "
				"from %s.\n", LDMSD_AUTH_ENV);
		return EINVAL;
	}
	secretword = ovis_auth_get_secretword(path, ldms_log);
	if (!secretword) {
		rc = errno;
		return rc;
	}
	return 0;
}
#endif /* ENABLE_AUTH */

int main(int argc, char *argv[])
{
	char *sockname = NULL;
#ifdef ENABLE_LDMSD_TEST
	char *config_port = NULL;
#endif /* ENABLE_LDMSD_TEST */
#ifdef ENABLE_LDMSD_RCTRL
	char *rctrl_port = NULL;
#endif /* ENABLE_LDMSD_CTRL */
	int ret;
	int op;
	ldms_set_t test_set;
	log_fp = stdout;
	char *cfg_file = NULL;
	struct sigaction action, logrotate_act;
	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGUSR1);

	memset(&action, 0, sizeof(action));
	action.sa_sigaction = cleanup_sa;
	action.sa_flags = SA_SIGINFO;
	action.sa_mask = sigset;

	sigaction(SIGHUP, &action, NULL);
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGABRT, &action, NULL);

	sigaddset(&sigset, SIGHUP);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGTERM);
	sigaddset(&sigset, SIGABRT);
	memset(&logrotate_act, 0, sizeof(logrotate_act));
	logrotate_act.sa_handler = ldmsd_logrotate;
	logrotate_act.sa_mask = sigset;
	sigaction(SIGUSR1, &logrotate_act, NULL);

	opterr = 0;
	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'H':
			strcpy(myhostname, optarg);
			break;
		case 'i':
			sample_interval = atoi(optarg);
			break;
		case 'k':
			do_kernel = 1;
			break;
		case 'x':
			listen_arg = strdup(optarg);
			break;
		case 'S':
			/* Set the SOCKNAME to listen on */
			sockname = strdup(optarg);
			break;
#ifdef ENABLE_LDMSD_TEST
		case 'p':
			/* Set the port to listen on configuration */
			config_port = strdup(optarg);
			break;
#endif /* ENABLE_LDMSD_TEST */
#ifdef ENABLE_LDMSD_RCTRL
		case 'r':
			rctrl_port = strdup(optarg);
			break;
#endif /* ENABLE_LDMSD_RCTRL */
		case 'l':
			logfile = strdup(optarg);
			break;
		case 's':
			setfile = strdup(optarg);
			break;
		case 'q':
			quiet = 1;
			break;
		case 'F':
			foreground = 1;
			break;
		case 'T':
			test_set_name = strdup(optarg);
			break;
		case 't':
			test_set_count = atoi(optarg);
			break;
		case 'P':
			ev_thread_count = atoi(optarg);
			break;
		case 'N':
			notify = 1;
			break;
		case 'M':
			test_metric_count = atoi(optarg);
			break;
		case 'm':
			if ((max_mem_size = ovis_get_mem_size(optarg)) == 0) {
				printf("Invalid memory size '%s'\n", optarg);
				usage(argv);
			}
			break;
		case 'f':
			flush_N = atoi(optarg);
			break;
		case 'D':
			dirty_threshold = atoi(optarg);
			break;
		case 'z':
#ifdef ENABLE_OCM
			if (strcmp(optarg, "ldmsd_sampler")
					&& strcmp(optarg, "ldmsd_aggregator")) {
				printf("Invalid ldmsd type '%s', ldmsd type can"
					" only be 'ldmsd_sampler' or "
					"'ldmsd_aggregator'\n", optarg);
				cleanup(-1);
			}
			ldmsd_svc_type = optarg;
#else
			printf("Error: -z options requires OCM support.\n");
#endif
			break;
		case 'o':
#ifdef ENABLE_OCM
			ocm_port = atoi(optarg);
#else
			printf("Error: -o options requires OCM support.\n");
#endif
			break;
#ifdef ENABLE_AUTH
		case 'a':
			authenticate = 1;
			break;
#endif /* ENABLE_AUTH */
		case '?':
			printf("Error: unknown argument: %c\n", optopt);
		default:
			usage(argv);
		}
	}

	if (!dirty_threshold)
		/* default total dirty threshold is calculated based on popular
		 * 4 GB RAM setting with Linux's default 10% dirty_ratio */
		dirty_threshold = calculate_total_dirty_threshold(1ULL<<32, 10);

	/* Make dirty_threshold to be per thread */
	dirty_threshold /= flush_N;

	if (logfile)
		log_fp = ldmsd_open_log();

	if (!foreground) {
		if (daemon(1, 1)) {
			perror("ldmsd: ");
			cleanup(8);
		}
	}

	/* Initialize LDMS */
	umask(0);
	if (ldms_init(max_mem_size)) {
		ldms_log("LDMS could not pre-allocate the memory of size %lu.\n",
								max_mem_size);
		exit(1);
	}

	evthread_use_pthreads();
	event_set_log_callback(ev_log_cb);

	ev_count = calloc(ev_thread_count, sizeof(int));
	if (!ev_count) {
		ldms_log("Memory allocation failure.\n");
		exit(1);
	}
	ev_base = calloc(ev_thread_count, sizeof(struct event_base *));
	if (!ev_base) {
		ldms_log("Memory allocation failure.\n");
		exit(1);
	}
	ev_thread = calloc(ev_thread_count, sizeof(pthread_t));
	if (!ev_thread) {
		ldms_log("Memory allocation failure.\n");
		exit(1);
	}
	for (op = 0; op < ev_thread_count; op++) {
		ev_base[op] = event_init();
		if (!ev_base[op]) {
			ldms_log("Error creating an event base.\n");
			cleanup(6);
		}
		ret = pthread_create(&ev_thread[op], NULL, event_proc, ev_base[op]);
		if (ret) {
			ldms_log("Error %d creating the event thread.\n", ret);
			cleanup(7);
		}
	}

	if (myhostname[0] == '\0') {
		ret = gethostname(myhostname, sizeof(myhostname));
		if (ret)
			myhostname[0] = '\0';
	}

	/* Create the test sets */
	ldms_set_t *test_sets = calloc(test_set_count, sizeof(ldms_set_t));
	if (test_set_name) {
		int rc, set_no, j;
		char metric_name[32];
		static char test_set_name_no[1024];
		ldms_schema_t schema = ldms_schema_new("test_set");
		if (!schema)
			cleanup(11);
		rc = ldms_schema_metric_add(schema, "component_id", LDMS_V_U64);
		if (rc < 0)
			cleanup(12);
		for (j = 1; j <= test_metric_count; j++) {
			sprintf(metric_name, "metric_no_%d", j);
			rc = ldms_schema_metric_add(schema, metric_name, LDMS_V_U64);
			if (rc < 0)
				cleanup(13);
		}
		for (set_no = 1; set_no <= test_set_count; set_no++) {
			sprintf(test_set_name_no, "%s/%s_%d",
				myhostname, test_set_name, set_no);
			test_set = ldms_set_new(test_set_name_no, schema);
			if (!test_set)
				cleanup(14);
			test_sets[set_no-1] = test_set;

		}
	} else
		test_set_count = 0;

	if (!setfile)
		setfile = LDMSD_SETFILE;

	if (!logfile)
		logfile = LDMSD_LOGFILE;

	ldms_log("Started LDMS Daemon version " VERSION "\n");

#ifdef ENABLE_AUTH
	if (authenticate) {
		secretword = NULL;
		if (ldmsd_get_secretword())
			cleanup(15);
	}

#endif /* ENABLE_AUTH */
	if (do_kernel && publish_kernel(setfile))
		cleanup(3);

	if (ldmsd_config_init(sockname))
		cleanup(4);

#ifdef ENABLE_LDMSD_TEST
	if (config_port)
		if (ldmsd_inet_config_init(config_port))
			cleanup(4);
#endif /* ENABLE_LDMSD_TEST */

#ifdef ENABLE_LDMSD_RCTRL
	if (rctrl_port)
		if (ldmsd_rctrl_init(rctrl_port, secretword))
			cleanup(4);
#endif /* ENABLE_LDMSD_RCTRL */
	if (ldmsd_store_init(flush_N)) {
		ldms_log("Could not initialize the storage subsystem.\n");
		cleanup(7);
	}

	if (listen_arg)
		listen_on_transport(listen_arg);

#ifdef ENABLE_OCM
	int ocm_rc = ldmsd_ocm_init(ldmsd_svc_type, ocm_port);
	if (ocm_rc) {
		ldms_log("Error: cannot initialize OCM, rc: %d\n",
				ocm_rc);
		cleanup(ocm_rc);
	}
#endif

	uint64_t count = 1;
	do {
		int set_no;
		for (set_no = 0; set_no < test_set_count; set_no++) {
			ldms_transaction_begin(test_sets[set_no]);
			ldms_metric_set_u64(test_sets[set_no], 0, count);
			ldms_transaction_end(test_sets[set_no]);
			if (notify) {
				struct ldms_notify_event_s event;
				ldms_init_notify_modified(&event);
				ldms_notify(test_sets[set_no], &event);
			}
		}
		count++;
		usleep(sample_interval);
	} while (1);

	cleanup(0);
	return 0;
}
