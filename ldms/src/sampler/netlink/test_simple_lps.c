/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2021 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2021 Open Grid Computing, Inc. All rights reserved.
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
//#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
//#include <pwd.h>
#include <strings.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <linux/limits.h>
#include <sys/types.h>
//#include <sys/socket.h>
//#include <sys/wait.h>
//#include <netinet/in.h>
//#include <semaphore.h>
#include <pthread.h>
#include "simple_lps.h"

/*** the stuff in this section is just to have a log function.
 * the application's own log function can be used instead or
 * application can pass NULL to skip logging.
 */
#define LDMSD_STR_WRAP(NAME) #NAME
#define OVIS_LWRAP(NAME) LDMSD_L ## NAME

#define LOGLEVELS(WRAP) \
	WRAP (DEBUG), \
	WRAP (INFO), \
	WRAP (WARNING), \
	WRAP (ERROR), \
	WRAP (CRITICAL), \
	WRAP (ALL), \
	WRAP (LASTLEVEL),

enum ldmsd_loglevel {
	OVIS_LNONE = -1,
	LOGLEVELS(OVIS_LWRAP)
};

const char* ldmsd_loglevel_names[] = {
	LOGLEVELS(LDMSD_STR_WRAP)
	NULL
};


static int log_time_sec = -1;
int log_level_thr = 0;
int quiet = 0;
pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
FILE *log_fp;
static void __ldmsd_log(enum ldmsd_loglevel level, const char *fmt, va_list ap)
{
	log_fp = stdout;
	if ((level != LDMSD_LALL) &&
			(quiet || ((0 <= level) && (level < log_level_thr))))
		return;
	char dtsz[200];

	pthread_mutex_lock(&log_lock);
	if (!log_fp) {
		pthread_mutex_unlock(&log_lock);
		return;
	}
	if (log_time_sec == -1) {
		char * lt = getenv("OVIS_LOG_TIME_SEC");
		if (lt)
			log_time_sec = 1;
		else
			log_time_sec = 0;
	}
	if (log_time_sec) {
		struct timeval tv;
		gettimeofday(&tv, NULL);
		fprintf(log_fp, "%lu.%06lu: ", tv.tv_sec, tv.tv_usec);
	} else {
		time_t t;
		t = time(NULL);
		struct tm tm;
		localtime_r(&t, &tm);
		if (strftime(dtsz, sizeof(dtsz), "%a %b %d %H:%M:%S %Y", &tm))
			fprintf(log_fp, "%s: ", dtsz);
	}

	if (level < LDMSD_LALL) {
		fprintf(log_fp, "%-10s: ", ldmsd_loglevel_names[level]);
	}

	vfprintf(log_fp, fmt, ap);
	fflush(log_fp);
	pthread_mutex_unlock(&log_lock);
}

static void ldmsd_log(enum ldmsd_loglevel level, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	__ldmsd_log(level, fmt, ap);
	va_end(ap);
}

static char *get_arg_value(const char *arg)
{
        char *s = strstr(arg, "=");
        if (s) {
                s++;
                return s;
        }
        return NULL;
}
/***  end of stuff for log function. */

#define TSTR "big string"

/* demos of how simple simple_lps is.
 * init/final and jb are handled by main
 */

static const char *auth_type;

/* create a single target publisher following C directions */
int test_slps_create(int argc, const char * argv[], jbuf_t jb)
{
	struct slps *slps;
	struct slps_send_result r, r2;
	fprintf(stdout, "%s: blocking to 411\n", __FUNCTION__);

	slps = slps_create("kokkos-sampler", SLPS_BLOCKING,
				ldmsd_log, SLPS_CONN_DEFAULTS);
	if (!slps)
		return errno;

	r = slps_send_event(slps, jb);
	fprintf(stdout, "blocking send_event result: pub=%d rc=%d\n",
		r.publish_count, r.rc);

	slps_destroy(slps);
	slps = NULL;

	sleep(2);
	fprintf(stdout, "%s: nonblocking to 10444\n", __FUNCTION__);
	/* usleep here simulates whatever other work an app does.
	slps_destroy is called when publishing messages is finished.
	When non-blocking and robust to remote listener changes,
	events sent before the initial connection has completed
	may get dropped.
	 */
	slps = slps_create("teststream", SLPS_NONBLOCKING,
				ldmsd_log, "sock",
				"localhost", 10444,
				auth_type, 1, 2, "test_slps.send.log");
	if (!slps)
		return errno;
	usleep(100000);

	r2 = slps_send_event(slps, jb);
	fprintf(stdout, " non-blocking send_event result: pub=%d rc=%d\n",
		r2.publish_count, r2.rc);

	usleep(100000);
	slps_destroy(slps);
	slps = NULL;
	sleep(2);
	return r.rc + r2.rc;
}

/* create a single target publisher following KOKKOS_LDMS_* env vars */
int test_slps_create_from_env(int argc, const char * argv[], jbuf_t jb)
{
	struct slps *slps;
	struct slps_send_result r, r2;
	fprintf(stdout, "%s: nonblocking (default on port 412 may"
		" be overriden)\n", __FUNCTION__);

	slps = slps_create_from_env("KOKKOS", "kokkos-sampler",
					SLPS_NONBLOCKING, ldmsd_log,
					SLPS_CONN_DEFAULTS2);
	if (!slps)
		return errno;
	/* usleep here simulates whatever other work an app does.
	slps_destroy is called when publishing messages is finished.
	When non-blocking and robust to remote listener changes,
	events sent before the initial connection has completed
	may get dropped.
	 */
	usleep(100000);

	r = slps_send_event(slps, jb);
	fprintf(stdout, "send_event result: pub=%d rc=%d\n",
		r.publish_count, r.rc);

	r2 = slps_send_string(slps, strlen(TSTR)+1, TSTR);
	fprintf(stdout, "send_string result: pub=%d rc=%d\n",
		r2.publish_count, r2.rc);

	/* usleep here simulates whatever other work an app does.
	slps_destroy is called when you're done publishing messages.
	 */
	usleep(100000);
	slps_destroy(slps);
	slps = NULL;
	return r.rc + r2.rc;
}

/* create a publisher following keyword=value pairs in an argv
This allows for multiple delivery target daemons on
a single message send by repeating target= arguments.
*/
int test_slps_create_from_argv(int argc, const char * argv[], jbuf_t jb)
{
	struct slps *slps;
	struct slps_send_result r, r2;
	fprintf(stdout, "%s:\nargv: ", __FUNCTION__);
	int i;
	for (i = 1; i< argc; i++) {
		fprintf(stdout,"'%s' ", argv[i]);
	}
	fprintf(stdout,"\n");
	slps = slps_create_from_argv(argc, argv, ldmsd_log);
	if (!slps)
		return errno;
	/* usleep here simulates whatever other work an app does.
	slps_destroy is called when publishing messages is finished.
	When non-blocking and robust to remote listener changes,
	events sent before the initial connection has completed
	may get dropped.
	 */
	r = slps_send_event(slps, jb);
	fprintf(stdout, "send_event result: pub=%d rc=%d\n",
		r.publish_count, r.rc);

	r2 = slps_send_string(slps, strlen(TSTR)+1, TSTR);
	fprintf(stdout, "send_string result: pub=%d rc=%d\n",
		r2.publish_count, r2.rc);

	slps_destroy(slps);
	return r.rc + r2.rc;
}

int main(int argc, const char * argv[])
{
	/* this big just for our logging implementation. */
	int i, rc = 0, rc_env = 0, rc_argv = 0;
	for (i = 1; i < argc; i++) {
		if (0 == strncasecmp(argv[i], "debug_level", 11)) {
			char *ll = get_arg_value(argv[i]);
			if (ll) {
				log_level_thr = atoi(ll);
			}
			fprintf(stdout, "log_level=%d\n", log_level_thr);
		}
	}
	auth_type = getenv("LDMS_AUTH");
	if (!auth_type || !strlen(auth_type))
		auth_type = "none";

	jbuf_t jb, jb2;
	jb = jbuf_new();
	if (!jb) {
		rc = ENOMEM;
		goto nomem;
	}
	jb2 = jbuf_append_str(jb,
		"{\"foo\":\"bar\", \"data\": {\"foo\":\"baz\"}}");
	if (!jb2) {
		rc = ENOMEM;
		goto nomem;
	}

	/* begin slps demonstrations  */
	slps_init();
	rc = test_slps_create(argc, argv, jb2);
	sleep(4);
	rc_env = test_slps_create_from_env(argc, argv, jb2);
	sleep(4);
	rc_argv = test_slps_create_from_argv(argc, argv, jb2);
	slps_finalize();
 nomem:
	jbuf_free(jb);
	fprintf(stdout, "test_slps_create returned %d\n", rc);
	fprintf(stdout, "test_slps_create_from_env returned %d\n", rc_env);
	fprintf(stdout, "test_slps_create_from_argv returned %d\n", rc_argv);
	return rc + rc_env + rc_argv;
}
