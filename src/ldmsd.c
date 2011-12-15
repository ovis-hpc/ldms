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
#include <unistd.h>
#include <inttypes.h>
#include <stdarg.h>
#include <getopt.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/un.h>
#include <ctype.h>
#include <netdb.h>
#include <dlfcn.h>
#include "event.h"
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_xprt.h"
#include "un.h"

#define LDMSD_SOCKNAME "/var/run/ldmsd/metric_socket"
#define LDMSD_SETFILE "/proc/sys/kldms/set_list"
#define LDMSD_LOGFILE "/var/log/ldmsd.log"
#define MAXSETS 512
#define MAXMETRICS 2048
#define MAXMETRICNAME 128

#define FMT "H:i:C:l:S:s:x:z:T:M:t:vFk"
char myhostname[80];
int setno;
int foreground;
int bind_succeeded;
pthread_t un_thread = (pthread_t)-1;
pthread_t event_thread = (pthread_t)-1;
pthread_t relay_thread = (pthread_t)-1;
char *test_set_name;
int test_set_count=1;
int test_metric_count=1;
int muxr_s = -1;
char *logfile;
char *sockname = NULL;
ldms_t ldms;
FILE *log_fp;
struct set_ref {
	ldms_set_t set;
	int metric_count;
	ldms_metric_t metrics[MAXMETRICS];
};
struct set_ref *sets[MAXSETS];
int max_set = 0;
int passive = 0;
int quiet = 1;
void ldms_log(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(log_fp, fmt, ap);
	fflush(log_fp);
}

void __release_set_ref(struct set_ref *ref)
{
	int m_no;

	/* Release all the metric references */
	for (m_no = 0; m_no < ref->metric_count; m_no++)
		ldms_release_metric(ref->metrics[m_no]);

	/* Destroy the metric set */
	ldms_destroy_set(ref->set);

	/* Free the set reference */
	free(ref);
}

void cleanup(int x)
{
	int set_no;

	ldms_log("LDMS Daemon exiting...status %d\n", x);
	if (un_thread != (pthread_t)-1) {
		void *dontcare;
		pthread_cancel(un_thread);
		pthread_join(un_thread, &dontcare);
	}

	if (muxr_s >= 0)
		close(muxr_s);
	if (sockname && bind_succeeded)
		unlink(sockname);
	if (ldms)
		ldms_release_xprt(ldms);

	for (set_no = 0; set_no < max_set; set_no++) {
		if (!sets[set_no])
			continue;
		__release_set_ref(sets[set_no]);
	}
	exit(x);
}

void usage(char *argv[])
{
	printf("%s: [%s]\n", argv[0], FMT);
	printf("    -i             Metric set sample interval.\n");
	printf("    -k             Publish publish kernel metrics.\n");
	printf("    -s setfile     Text file containing kernel metric sets to publish.\n"
	       "                   [" LDMSD_SETFILE "]\n");
	printf("    -S sockname    Specifies the name of a local metric publishing socket.\n");
	printf("    -C cfg_file    Specifies the name of the ldms config file.\n");
	printf("    -x xprt:port   Specifies the transport type to listen on. May be specified\n"
	       "                   more than once for multiple transports. The transport string\n"
	       "                   is one of 'rdma', or 'sock'. A transport specific port number\n"
	       "                   is optionally specified following a ':', e.g. rdma:50000.\n");
	printf("    -l log_file    The path to the log file for status messages.\n"
	       "                   [" LDMSD_LOGFILE "]\n");
	printf("    -v             Verbose mode, i.e. print requests as they are processed.\n"
	       "                   [false].\n");
	printf("    -F             Foreground mode, don't daemonize the program [false].\n");
	printf("    -z set_list_sz Set the size of the set list query buffer.\n");
	printf("    -T set_name    Create a test set with the specified name.\n");
	printf("    -t set_count   Create set_count instances of set_name.\n");
	cleanup(1);
}

/*
 * This function opens the device file specified by 'devname' and
 * mmaps the metric set 'set_no'.
 */
int map_fd;
ldms_set_t map_set;
int publish_kernel(const char *setfile)
{
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
			sprintf(sh->name, "%s%s", myhostname, set_name);
		else
			sprintf(sh->name, "%s/%s", myhostname, set_name);
		data_addr = mmap((void *)0, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, map_fd,
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
		ldms_log("name: '%s'\n", sh->name);
		ldms_log("size: %d\n", sh->meta_size);
	}
	return 0;
}


static int is_set_valid(int set_no)
{
	return (set_no < max_set && sets[set_no] != 0);
}

char *skip_space(char *s)
{
	while (*s != '\0' && isspace(*s)) s++;
	if (*s == '\0')
		return NULL;
	return s;
}

static char replybuf[4096];
int send_reply(int sock, struct sockaddr *sa, ssize_t sa_len,
	       char *msg, ssize_t msg_len)
{
	struct msghdr reply;
	struct iovec iov;

	reply.msg_name = sa;
	reply.msg_namelen = sa_len;
	iov.iov_base = msg;
	iov.iov_len = msg_len;
	reply.msg_iov = &iov;
	reply.msg_iovlen = 1;
	reply.msg_control = NULL;
	reply.msg_controllen = 0;
	reply.msg_flags = 0;
	sendmsg(sock, &reply, 0);
	return 0;
}

int process_set_metric(int fd,
		       struct sockaddr *sa, ssize_t sa_len,
		       char *command)
{
	struct set_ref *sr;
	int rc;
	int set_no;
	int metric_no;
	uint64_t metric_value;
	struct ldms_metric *m;

	rc = sscanf(command, "%d %d %" PRIu64 "\n", &set_no, &metric_no, &metric_value);
	if (rc != 3) {
		ldms_log("Illegal record format '%s'\n", command);
		goto err_out;
	}
	if (!is_set_valid(set_no)) {
		ldms_log("Invalid set number %d\n", set_no);
		goto err_out;
	}
	sr = sets[set_no];
	if (metric_no >= sr->metric_count) {
		ldms_log("Invalid metric number %d\n", metric_no);
		goto err_out;
	}
	m  = sr->metrics[metric_no];
	ldms_set_u64(m, metric_value);
	sprintf(replybuf, "GN %" PRIu64 "\n", ldms_get_meta_gn(sr->set));
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf)+1);
	return 0;

 err_out:
	send_reply(fd, sa, sa_len, "NM -1\n", 7);
	/* TODO */
	return -1;
}

char *type_names[] = {
	[LDMS_V_NONE] = "NONE",
	[LDMS_V_U8] = "U8",
	[LDMS_V_S8] = "S8",
	[LDMS_V_U16] = "U16",
	[LDMS_V_S16] = "S16",
	[LDMS_V_U32] = "U32",
	[LDMS_V_S32] = "S32",
	[LDMS_V_U64] = "U64",
	[LDMS_V_S64] = "S64",
};

/*
 * DM <set-idx> <metric-type> <metric-name>
 */
int process_define_metric(int fd,
			  struct sockaddr *sa, ssize_t sa_len,
			  char *command)
{
	char metric_type[16];
	char metric_name[MAXMETRICNAME+1];
	struct set_ref *sr;
	int rc;
	int set_no;
	int metric_no;
	enum ldms_value_type t;
	struct ldms_metric *m;

	rc = sscanf(command, "%d %s %s\n", &set_no, metric_type, metric_name);
	if (rc != 3) {
		ldms_log("Illegal record format '%s'\n", command);
		rc = -EINVAL;
		goto err_out;
	}
	if (!is_set_valid(set_no)) {
		ldms_log("Invalid set number %d\n", set_no);
		rc = -EINVAL;
		goto err_out;
	}
	sr = sets[set_no];
	for (metric_no = 0; metric_no < sr->metric_count; metric_no++) {
		if (0 == strcmp(ldms_get_metric_name(sr->metrics[metric_no]), metric_name))
			goto out;
	}
	t = ldms_str_to_type(metric_type);
	if (t == LDMS_V_NONE) {
		ldms_log("Invalid type name '%s' specified.\n", metric_type);
		rc = -EINVAL;
		goto err_out;
	}
	m = ldms_add_metric(sr->set, metric_name, t);
	if (!m) {
		ldms_log("The metric '%s' could not be created.\n", metric_name);
		rc = -ENOMEM;
		goto err_out;
	}
	metric_no = sr->metric_count;
	sr->metric_count++;
	sr->metrics[metric_no] = m;
	ldms_log("Created metric '%s' no %d\n", metric_name, metric_no);
 out:
	sprintf(replybuf, "NM %d\n", metric_no);
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf)+1);
	return 0;

 err_out:
	sprintf(replybuf, "NM %d\n", rc);
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf)+1);
	/* TODO */
	return -1;
}

#define MAX_SET_NAME_SIZE 127
const char *set_name_fixup(const char *set_name)
{
	static char hset_name[MAX_SET_NAME_SIZE+1];
	if (set_name[0] == '/')
		sprintf(hset_name, "%s%s", myhostname, set_name);
	else
		sprintf(hset_name, "%s/%s", myhostname, set_name);
	return hset_name;
}

struct plugin {
	char *name;
	char *libpath;
	enum {
		PLUGIN_IDLE,
		PLUGIN_INIT,
		PLUGIN_STARTED
	} state;
	unsigned long sample_interval_us;
	struct ldms_plugin *plugin;
	struct timeval timeout;
	struct event event;
	LIST_ENTRY(plugin) entry;
};
LIST_HEAD(plugin_list, plugin) plugin_list;

struct plugin *get_plugin(char *name)
{
	struct plugin *p;
	LIST_FOREACH(p, &plugin_list, entry) {
		if (0 == strcmp(p->name, name))
			return p;
	}
	return NULL;
}

static char msg_buf[4096];
void msg_logger(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsprintf(msg_buf, fmt, ap);
	ldms_log(msg_buf);
}

static char library_name[PATH_MAX];
struct plugin *new_plugin(char *plugin_name, char *err_str)
{
	struct ldms_plugin *lpi;
	struct plugin *pi = NULL;
	char *path = getenv("LDMS_PLUGIN_LIBPATH");
	if (!path)
		path = "/usr/lib64";

	sprintf(library_name, "%s/lib%s.so", path, plugin_name);
	void *d = dlopen(library_name, RTLD_NOW);
	if (!d) {
		sprintf(err_str, "dlerror %s", dlerror());
		goto err;
	}
	ldmsd_plugin_get_f pget = dlsym(d, "get_plugin");
	lpi = pget(msg_logger);
	if (!lpi)
		goto err;

	pi = calloc(1, sizeof *pi);
	if (!pi)
		goto enomem;
	pi->name = strdup(plugin_name);
	if (!pi->name)
		goto enomem;
	pi->libpath = strdup(library_name);
	if (!pi->libpath)
		goto enomem;
	pi->plugin = lpi;
	pi->sample_interval_us = 1000000;
	LIST_INSERT_HEAD(&plugin_list, pi, entry);
	return pi;
 enomem:
	sprintf(err_str, "No memory");
 err:
	if (pi) {
		if (pi->name)
			free(pi->name);
		if (pi->libpath)
			free(pi->libpath);
		free(pi);
	}
	return NULL;
}

/*
 * Load a plugin
 *
 * PL <plugin_name>
 */
int process_load_plugin(int fd,
			struct sockaddr *sa, ssize_t sa_len,
			char *command)
{
	char plugin_name[128];
	char err_str[128];
	char reply[128];
	int rc;

	err_str[0] = '\0';

	rc = sscanf(command, "%s\n", plugin_name);
	if (rc != 1) {
		sprintf(err_str, "Bad request syntax\n");
		rc = EINVAL;
		goto out;
	}
	struct plugin *pi = get_plugin(plugin_name);
	if (pi) {
		sprintf(err_str, "Plugin already loaded");
		rc = EEXIST;
		goto out;
	}
	pi = new_plugin(plugin_name, err_str);
 out:
	sprintf(reply, "PR %d %s\n", rc, err_str);
	send_reply(fd, sa, sa_len, reply, strlen(reply)+1);
	return 0;
}

/*
 * Initialize a plugin
 *
 * PI <plugin_name> <set_name>
 */
int process_init_plugin(int fd,
			struct sockaddr *sa, ssize_t sa_len,
			char *command)
{
	char plugin_name[LDMS_MAX_PLUGIN_NAME_LEN];
	char set_name[MAX_SET_NAME_SIZE+1];
	char hset_name[MAX_SET_NAME_SIZE+1];
	char *err_str = "";
	int rc = 0;
	struct plugin *pi;

	rc = sscanf(command, "%s %s\n", plugin_name, set_name);
	if (rc != 2) {
		err_str = "Invalid request syntax";
		rc = EINVAL;
		goto out;
	}

	pi = get_plugin(plugin_name);
	if (!pi) {
		rc = ENOENT;
		err_str = "Plugin not found.";
		goto out;
	}
	switch (pi->state) {
	case PLUGIN_IDLE:
		break;
	default:
		rc = EBUSY;
		err_str = "Plugin already has a metric set assigned";
		goto out;
	}
	pi->state = PLUGIN_INIT;
	if (set_name[0] == '/')
		sprintf(hset_name, "%s%s", myhostname, set_name);
	else
		sprintf(hset_name, "%s/%s", myhostname, set_name);
	rc = pi->plugin->init(hset_name);
	if (rc) {
		err_str = "Failed to create set";
		goto out;
	}
 out:
	sprintf(replybuf, "PR %d %s", rc, err_str);
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf)+1);
	return 0;
}

/*
 * Configure a plugin
 *
 * PC <plugin_name> <config_string>
 */
char config_str[LDMS_MAX_CONFIG_STR_LEN];
int process_config_plugin(int fd,
			  struct sockaddr *sa, ssize_t sa_len,
			  char *command)
{
	char plugin_name[LDMS_MAX_PLUGIN_NAME_LEN];
	char *err_str = "";
	int rc = 0;
	struct plugin *pi;

	rc = sscanf(command, "%s %[^\n]", plugin_name, config_str);
	if (rc != 2) {
		err_str = "Invalid request syntax";
		rc = EINVAL;
		goto out;
	}

	pi = get_plugin(plugin_name);
	if (!pi) {
		rc = ENOENT;
		err_str = "Plugin not found.";
		goto out;
	}
	rc = pi->plugin->config(config_str);
	if (rc) {
		err_str = "Plugin configuration error";
		goto out;
	}

 out:
	sprintf(replybuf, "PR %d %s", rc, err_str);
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf)+1);
	return 0;
}

void plugin_sampler_cb(int fd, short sig, void *arg)
{
	struct plugin *pi = arg;
	int rc;
	pi->plugin->sample();
	evtimer_set(&pi->event, plugin_sampler_cb, pi);
	rc = evtimer_add(&pi->event, &pi->timeout);
}

/*
 * Start the sampler
 *
 * PS <plugin_name> <sample_interval>
 */
int process_start_plugin(int fd,
			 struct sockaddr *sa, ssize_t sa_len,
			 char *command)
{
	char plugin_name[LDMS_MAX_PLUGIN_NAME_LEN];
	unsigned long sample_interval;
	char *err_str = "";
	int rc = 0;
	struct plugin *pi;

	rc = sscanf(command, "%s %ld\n", plugin_name, &sample_interval);
	if (rc != 2) {
		err_str = "Invalid request syntax";
		rc = EINVAL;
		goto out;
	}
	pi = get_plugin(plugin_name);
	if (!pi) {
		rc = ENOENT;
		err_str = "Plugin not found.";
		goto out;
	}
	switch (pi->state) {
	case PLUGIN_IDLE:
		rc = ENOENT;
		err_str = "Use the 'init' command to assign a metric set to the plugin.";
		goto out;
	case PLUGIN_INIT:
		break;
	default:
		rc = EBUSY;
		err_str = "Plugin is already sampling";
		goto out;
	}
	pi->state = PLUGIN_STARTED;
	memset(&pi->event, 0, sizeof pi->event);
	pi->timeout.tv_sec = sample_interval / 1000000;
	pi->timeout.tv_usec = sample_interval % 1000000;
	evtimer_set(&pi->event, plugin_sampler_cb, pi);
	rc = evtimer_add(&pi->event, &pi->timeout);
 out:
	sprintf(replybuf, "PR %d %s", rc, err_str);
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf)+1);
	return 0;
}

/*
 * Stop the sampler
 *
 * PX <plugin_name>
 */
int process_stop_plugin(int fd,
			struct sockaddr *sa, ssize_t sa_len,
			char *command)
{
	char plugin_name[LDMS_MAX_PLUGIN_NAME_LEN];
	char *err_str = "";
	int rc = 0;
	struct plugin *pi;

	rc = sscanf(command, "%s", plugin_name);
	if (rc != 1) {
		err_str = "Invalid request syntax";
		rc = EINVAL;
		goto out;
	}

	pi = get_plugin(plugin_name);
	if (!pi) {
		rc = ENOENT;
		err_str = "Plugin not found.";
		goto out;
	}
	evtimer_del(&pi->event);
	pi->state = PLUGIN_INIT;
 out:
	sprintf(replybuf, "PR %d %s", rc, err_str);
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf)+1);
	return 0;
}

/*
 * DS <set_name> <size>
 */
int process_define_set(int fd,
		       struct sockaddr *sa, ssize_t sa_len,
		       char *command)
{
	struct set_ref *sr = 0;
	ldms_set_t set;
	size_t set_size;
	char set_name[MAX_SET_NAME_SIZE+1];
	char hset_name[MAX_SET_NAME_SIZE+1];
	int rc;
	int set_no;

	rc = sscanf(command, "%s %zu\n", set_name, &set_size);
	if (rc != 2) {
		ldms_log("Illegal record format '%s'\n", command);
		goto err_out;
	}

	/* check to see if this set is already defined */
	for (set_no = 0; set_no < max_set; set_no++) {
		if (!sets[set_no])
			continue;
		if (0 == strcmp(ldms_get_set_name(sets[set_no]->set), set_name))
			goto out;
	}
	sr = calloc(1, sizeof *sr);
	if (!sr) {
		ldms_log("Could not allocate set data.\n");
		return -1;
	}

	if (set_name[0] == '/')
		sprintf(hset_name, "%s%s", myhostname, set_name);
	else
		sprintf(hset_name, "%s/%s", myhostname, set_name);
	rc = ldms_create_set(hset_name, set_size, set_size, &set);
	if (rc && rc != -EEXIST) {
		ldms_log("Error %d creating set '%s' with size %zu\n", rc, hset_name, set_size);
		goto err_out;
	}
	sr->set = set;

	/* Find an empty slot, or allocate a new one */
	for (set_no = 0; set_no < max_set && sets[set_no]; set_no++);
	if (set_no >= max_set) {
		set_no = max_set;
		max_set++;
	}
	sets[set_no] = sr;
	ldms_log("Created metric set '%s' set_no %d size %d\n", hset_name, set_no, set_size);
 out:
	sprintf(replybuf, "NS %d\n", set_no);
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf)+1);
	return 0;

 err_out:
	if (sr)
		free(sr);
	send_reply(fd, sa, sa_len, "NS -1\n", 7);
	return -1;
}

int process_remove_set(int fd,
		       struct sockaddr *sa, ssize_t sa_len,
		       char *command)
{
	int rc;
	int set_no;

	rc = sscanf(command, "%d\n", &set_no);
	if (rc != 1) {
		ldms_log("Illegal record format '%s'\n", command);
		goto err_out;
	}
	if (!is_set_valid(set_no)) {
		ldms_log("Invalid set number %d\n", set_no);
		goto err_out;
	}
	ldms_log("Removing metric set '%s'\n", ldms_get_set_name(sets[set_no]->set));

	if (sets[set_no])
		__release_set_ref(sets[set_no]);

	/* Reset slot */
	sets[set_no] = 0;

	sprintf(replybuf, "NS %d\n", 0);
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf)+1);
	return 0;
 err_out:
	send_reply(fd, sa, sa_len, "NS -1\n", 7);
	return -1;
}


int process_record(int fd,
		   struct sockaddr *sa, ssize_t sa_len,
		   char *command, ssize_t cmd_len)
{
	char *s;

	/* Skip whitespace to start */
	s = skip_space(command);

	/* Handle Set Metric command */
	if (0 == strncasecmp(s, "SM", 2)) {
		s += 2;
		return process_set_metric(fd, sa, sa_len, s);
	}

	/* Handle Define Metric command */
	if (0 == strncasecmp(s, "DM", 2)) {
		s += 2;
		return process_define_metric(fd, sa, sa_len, s);
	}

	/* Handle Define Set command */
	if (0 == strncasecmp(s, "DS", 2)) {
		s += 2;
		return process_define_set(fd, sa, sa_len, s);
	}

	/* Handle Remove Set command */
	if (0 == strncasecmp(s, "RS", 2)) {
		s += 2;
		return process_remove_set(fd, sa, sa_len, s);
	}

	/* Handle Load Plugin command */
	if (0 == strncasecmp(s, "PL", 2)) {
		s += 2;
		return process_load_plugin(fd, sa, sa_len, s);
	}
	/* Handle Init Plugin command */
	if (0 == strncasecmp(s, "PI", 2)) {
		s += 2;
		return process_init_plugin(fd, sa, sa_len, s);
	}
	/* Handle Start Plugin command */
	if (0 == strncasecmp(s, "PS", 2)) {
		s += 2;
		return process_start_plugin(fd, sa, sa_len, s);
	}
	/* Handle Stop Plugin command */
	if (0 == strncasecmp(s, "PX", 2)) {
		s += 2;
		return process_stop_plugin(fd, sa, sa_len, s);
	}
	/* Handle Stop Plugin command */
	if (0 == strncasecmp(s, "PC", 2)) {
		s += 2;
		return process_config_plugin(fd, sa, sa_len, s);
	}
	ldms_log("Unrecognized request '%s'\n", command);
	s[2] = '\0';
	sprintf(replybuf, "%s -1\n", s);
	send_reply(fd, sa, sa_len, "%s -1\n", strlen(replybuf)+1);
	return -1;
}

void do_reset_host_(struct hostspec *hs)
{
	int i;
	printf("Resetting host %s\n", hs->hostname); 
	ldms_xprt_close(hs->x);
	ldms_release_xprt(hs->x);
	hs->x = 0;
	for (i = 0; i < hs->set_count; i++)
		ldms_destroy_set(hs->sets[i]);
	if (hs->sets)
		free(hs->sets);
	hs->sets = NULL;
	hs->set_count = 0;
}

void do_reset_bridges(void)
{
	struct hostspec *hs;
	for (hs = host_first(); hs; hs = host_next(hs)) {
		if (hs->type == BRIDGING &&
		    hs->x && ldms_xprt_connected(hs->x))
			do_reset_host_(hs);
	}
}

void do_reset_host(struct hostspec *hs)
{
	do_reset_host_(hs);
	do_reset_bridges();
}

int sample_interval = 2000000;
size_t set_list_sz = 0;
char *set_list = NULL;

int do_connect(struct hostspec *hs)
{
	int ret;

	if (!hs->x) {
		hs->x = ldms_create_xprt(hs->xprt_name);
		if (hs->x)
			/* Take a reference since we've caching the handle */
			ldms_xprt_get(hs->x);
	}
	if (!hs->x) {
		printf("Error creating transport '%s'.\n", hs->xprt_name);
		return -1;
	}
	printf("Connecting to host %s\n", hs->hostname);
	ret  = ldms_connect(hs->x, (struct sockaddr *)&hs->sin,
			    sizeof(hs->sin));
	if (ret) {
		printf("Could not connect to host %s:%d\n",
		       hs->hostname, ntohs(hs->sin.sin_port));
		return -1;
	}
	if (hs->type == ACTIVE)
		/* Reset any bridging hosts so they will see the new active host */
		do_reset_bridges();
	return 0;
}

void lookup_cb(ldms_t t, int status, ldms_set_t s, void *arg)
{
	ldms_set_t *sp = arg;
	if (status)
		*sp = NULL;
	else
		*sp = s;
}

void dir_cb(ldms_t t, int status, ldms_dir_t dir, void *arg)
{
	int i;
	struct hostspec *hs = arg;
	if (status) {
		printf("Error %d in lookup on host %s.\n",
		       status, hs->hostname);
		return;
	}
	hs->dir = dir;
	if (!hs->dir || !hs->dir->set_count)
		return;
	if (hs->set_count) {
		int i;
		for (i = 0; i < hs->set_count; i++)
			if (hs->sets[i])
				ldms_set_release(hs->sets[i]);
		free(hs->sets);
	}
	hs->set_count = dir->set_count;
	hs->sets = calloc(hs->dir->set_count, sizeof(ldms_set_t));
	if (!hs->sets) {
		printf("Memory allocation failure updating "
		       "meta data for host '%s'.\n",
		       hs->hostname);
		return;
	}
	for (i = 0; i < hs->set_count; i++) {
		int ret = ldms_lookup(hs->x, hs->dir->set_names[i],
				      lookup_cb, &hs->sets[i], 0);
		if (ret) {
			printf("Synchronous error %d looking up set '%s' "
			       "on host '%s'.\n", ret, hs->dir->set_names[i],
			       hs->hostname);
		}
	}
}

int update_meta_data(struct hostspec *hs)
{
	int ret;

	if (hs->dir) {
		ldms_dir_release(hs->x, hs->dir);
		hs->dir = NULL;
	}
	ret = ldms_dir(hs->x, dir_cb, hs, 0);
	if (ret) {
		printf("Synchronous error %d from ldms_dir(%s) in %s\n",
		       ret, hs->hostname, __FUNCTION__);
		return -1;
	}
	return 0;
}

void update_data_done(ldms_t t, ldms_set_t s, int rc, void *arg)
{
	printf("Update to %p complete.\n", s);
}

int update_data(struct hostspec *hs)
{
	int i, ret;

	if (!hs->x || !hs->set_count)
		return 0;

	for (i = 0; i < hs->set_count; i++) {
		if (!hs->sets[i]) {
			printf("Metric set for host %s not found.\n", hs->hostname);
			continue;
		}
		ret = ldms_update(hs->sets[i], update_data_done, NULL);
		if (ret) {
			printf("Error %d updating metric set "
			       "on host '%s'.\n", ret, hs->hostname);
			return -1;
		}
	}
	return 0;
}

void do_active(struct hostspec *hs)
{
	if (!hs->x && do_connect(hs))
		return;

	if (!ldms_xprt_connected(hs->x))
		if (do_connect(hs))
			return;
	if (!hs->set_count)
		update_meta_data(hs);

	update_data(hs);
}

int do_passive_connect(struct hostspec *hs)
{
	ldms_t l = ldms_xprt_find(&hs->sin);
	if (!l)
		return -1;

	hs->x = l;
	return 0;
}

void do_passive(struct hostspec *hs)
{
	if (!hs->x && do_passive_connect(hs))
		return;
	if (!ldms_xprt_connected(hs->x))
		return;
	if (!hs->set_count)
		update_meta_data(hs);
	update_data(hs);
}

void do_bridging(struct hostspec *hs)
{
	if (!hs->x)
		do_connect(hs);
	if (!ldms_xprt_connected(hs->x))
		do_connect(hs);
}

int process_message(int sock, struct msghdr *msg, ssize_t msglen)
{
	return process_record(sock,
			      msg->msg_name, msg->msg_namelen,
			      msg->msg_iov->iov_base, msglen);
}

struct event keepalive;
struct timeval keepalive_to;
void keepalive_cb(int fd, short sig, void *arg)
{
	evtimer_set(&keepalive, keepalive_cb, NULL);
	keepalive_to.tv_sec = 10;
	keepalive_to.tv_usec = 0;
	evtimer_add(&keepalive, &keepalive_to);
}

void *event_proc(void *v)
{
	struct event_base *eb = v;
	event_base_loop(eb, 0);
	return NULL;
}

void *un_thread_proc(void *v)
{
	struct msghdr msg;
	struct iovec iov;
	static unsigned char lbuf[256];
	struct sockaddr_storage ss;
	iov.iov_base = lbuf;
	do {
		ssize_t msglen;
		ss.ss_family = AF_UNIX;
		msg.msg_name = &ss;
		msg.msg_namelen = sizeof(ss);
		iov.iov_len = sizeof(lbuf);
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_flags = 0;
		msglen = recvmsg(muxr_s, &msg, 0);
		if (msglen <= 0)
			break;
		process_message(muxr_s, &msg, msglen);
	} while (1);
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

	l = ldms_create_xprt(name);
	if (!l) {
		ldms_log("The transport specified, '%s', is invalid.\n", name);
		cleanup(6);
	}
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = 0;
	sin.sin_port = htons(port_no);
	ret = ldms_listen(l, (struct sockaddr *)&sin, sizeof(sin));
	if (ret) {
		ldms_log("Error %d listening on the '%s' transport.\n",
			 ret, name);
		cleanup(7);
	}
}

int main(int argc, char *argv[])
{
	int do_kernel = 0;
	int ret;
	int op;
	ldms_set_t test_set;
	struct sockaddr_un sun;
	char *setfile = NULL;
	log_fp = stdout;
	char *cfg_file = NULL;

	signal(SIGHUP, cleanup);
	signal(SIGINT, cleanup);
	signal(SIGTERM, cleanup);

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
			listen_on_transport(optarg);
			break;
		case 'S':
			/* Set the SOCKNAME to listen on */
			sockname = strdup(optarg);
			break;
		case 'l':
			logfile = strdup(optarg);
			break;
		case 's':
			setfile = strdup(optarg);
			break;
		case 'v':
			quiet = 0;
			break;
		case 'C':
			cfg_file = strdup(optarg);
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
		case 'M':
			test_metric_count = atoi(optarg);
			break;
		case 'z':
			set_list_sz = atoi(optarg);
			break;
		default:
			usage(argv);
		}
	}
	struct event_base *eb = event_init();
	evtimer_set(&keepalive, keepalive_cb, NULL);
	keepalive_to.tv_sec = 10;
	keepalive_to.tv_usec = 0;
	evtimer_add(&keepalive, &keepalive_to);
	ret = pthread_create(&event_thread, NULL, event_proc, eb);
	if (ret) {
		ldms_log("Error %d creating the event thread.\n", ret);
		cleanup(6);
	}
	if (logfile) {
		log_fp = fopen(logfile, "a");
		if (!log_fp) {
			log_fp = stdout;
			ldms_log("Could not open the log file named '%s'\n", logfile);
			exit(1);
		}
		stdout = log_fp;
	}
	set_list = malloc(set_list_sz);
	if (!set_list) {
		ldms_log("Memory allocation failure creating "
			 "%d byte set query list.\n", set_list_sz);
		exit(1);
	}
	ret = parse_cfg(cfg_file);
	if (ret) {
		ldms_log("Configuration file error %d\n", ret);
		cleanup(8);
	}

	if (myhostname[0] == '\0') {
		ret = gethostname(myhostname, sizeof(myhostname));
		if (ret)
			myhostname[0] = '\0';
	}

	if (!foreground) {
		if (daemon(1, 1)) {
			perror("ldmsd: ");
			exit(1);
		}
	}

	if (test_set_name) {
		int set_no;
		char test_set_name_no[1024];
		for (set_no = 1; set_no <= test_set_count; set_no++) {
			int j;
			ldms_metric_t m;
			char metric_name[32];
			sprintf(test_set_name_no, "%s_%d", test_set_name, set_no);
			ldms_create_set(test_set_name_no, 2048, 2048, &test_set);
			if (test_metric_count > 0){
				m = ldms_add_metric(test_set, "component_id",
						    LDMS_V_U64);
				ldms_set_u64(m, (uint64_t)1);
			}
			for (j = 1; j <= test_metric_count; j++) {
				sprintf(metric_name, "metric_no_%d", j);
				m = ldms_add_metric(test_set, metric_name,
						    LDMS_V_U64);
				ldms_set_u64(m, (uint64_t)(set_no * j));
			}
		}
	}

	if (!setfile)
		setfile = LDMSD_SETFILE;

	if (!logfile)
		logfile = LDMSD_LOGFILE;

	ldms_log("Started LDMS Daemon version 1.0\n");
	if (do_kernel && publish_kernel(setfile))
		cleanup(3);
	if (sockname) {
		memset(&sun, 0, sizeof(sun));
		sun.sun_family = AF_UNIX;
		strncpy(sun.sun_path, sockname,
			sizeof(struct sockaddr_un) - sizeof(short));
		/* Create listener */
		muxr_s = socket(AF_UNIX, SOCK_DGRAM, 0);
		if (muxr_s < 0) {
			ldms_log("Error %d creating muxr socket.\n", muxr_s);
			cleanup(4);
		}
		/* Bind to our public name */
		ret = bind(muxr_s, (struct sockaddr *)&sun, sizeof(struct sockaddr_un));
		if (ret < 0) {
			ldms_log("Error %d binding to socket named '%s'.\n", ret, sockname);
			cleanup(5);
		}
		bind_succeeded = 1;

		ret = pthread_create(&un_thread, NULL, un_thread_proc, 0);
		if (ret) {
			ldms_log("Error %d creating the socket named '%s'.\n", ret, sockname);
			cleanup(6);
		}
	}
	do {
		struct hostspec *hs;
		for (hs = host_first(); hs; hs = host_next(hs)) {
			if (hs->x && ldms_xprt_closed(hs->x)) {
				do_reset_host(hs);
			}
			switch (hs->type) {
			case ACTIVE:
				do_active(hs);
				break;
			case PASSIVE:
				do_passive(hs);
				break;
			case BRIDGING:
				do_bridging(hs);
				break;
			}
		}
		usleep(sample_interval);
	} while (1);

	cleanup(0);
	return 0;
}
