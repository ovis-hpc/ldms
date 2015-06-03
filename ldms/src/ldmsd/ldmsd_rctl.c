/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015 Sandia Corporation. All rights reserved.
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
#include <inttypes.h>
#include <unistd.h>
#include <stdarg.h>
#include <getopt.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <libgen.h>
#include <signal.h>
#include <search.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <semaphore.h>
#include <assert.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ocm/ocm.h"
#include "ovis_rctrl/rctrl.h"

#ifdef ENABLE_AUTH
#include "ovis_auth/auth.h"
#define LDMSD_RCTRL_AUTH_ENV "LDMSD_RCTRL_AUTH_FILE"
#endif /* ENABLE_AUTH */

#define FMT "h:p:a:"
#define ARRAY_SIZE(a)  (sizeof(a) / sizeof(a[0]))

static char linebuf[8192];
static char *buff = 0;
static struct ocm_cfg_buff *cfg = 0;
static sem_t conn_sem;
static sem_t recv_sem;
static char *host = 0;
static char *port = 0;
static char *secretword_path = 0;

void cleanup()
{
	if (buff)
		free(buff);
	if (cfg)
		ocm_cfg_buff_free(cfg);
}

struct kw {
	char *token;
	int cmd_id;
	int (*action)(char *kw);
};

#define LDMSCTL_HELP LDMSCTL_LAST_COMMAND + 1
#define LDMSCTL_QUIT LDMSCTL_LAST_COMMAND + 2

void usage(char *argv[])
{
	printf("%s: [%s]\n"
	       "    -h <hostname>   The hostname of the ldmsd.\n"
	       "    -p <port>       The listener port of ldmsd.\n",
	       argv[0], FMT);
#ifdef ENABLE_AUTH
	printf("    -a <path>       The full Path to the file containing the shared secret word.\n"
	       "		    Set the environment variable %s to the full path\n"
	       "		    to avoid giving it at command-line every time.\n",
	       LDMSD_RCTRL_AUTH_ENV);
#endif /* ENABLE_AUTH */
	exit(1);
}

int handle_help(char *kw)
{
	printf("help\n"
	       "   - Print this menu.\n"
	       "\n"
	       "usage\n"
	       "   - Show loaded plugin usage information.\n"
	       "\n"
	       "load name=<name>\n"
	       "   - Loads the specified plugin. The library that implements\n"
	       "     the plugin should be in the directory specified by the\n"
	       "     LDMSD_PLUGIN_LIBPATH environment variable.\n"
	       "     <name>       The plugin name, this is used to locate a loadable\n"
	       "                  library named \"lib<name>.so\"\n"
	       "\n"
	       "config name=<name> [ <attr>=<value> ... ]\n"
	       "   - Provides a mechanism to specify configuration options\n"
	       "     <name>       The plugin name.\n"
	       "     <attr>       An attribute name.\n"
	       "     <value>      An attribute value.\n"
	       "\n"
	       "udata set=<set_name> metric=<metric_name> udata=<user_data>\n"
	       "   - Set the user data of the specified metric in the given set\n"
	       "     <set_name>      The metric set name\n"
	       "     <metric_name>   The metric name\n"
	       "     <user_data>     The user data value\n"
	       "\n"
	       "start name=<name> interval=<interval> [ offset=<offset>]\n"
	       "   - Begins calling the sampler's 'sample' method at the\n"
	       "     sample interval.\n"
	       "     <name>       The sampler name.\n"
	       "     <interval>   The sample interval in microseconds.\n"
	       "     <offset>     Optional offset (shift) from the sample mark\n"
	       "                  in microseconds. Offset can be positive or\n"
	       "                  negative with magnitude up to 1/2 the sample interval.\n"
	       "                  If this offset is specified, including 0, \n"
	       "                  collection will be synchronous; if the offset\n"
	       "                  is not specified, collection will be asychronous.\n"
	       "\n"
	       "stop name=<name>\n"
	       "   - Cancels sampling on the specified plugin.\n"
	       "     <name>       The sampler name.\n"
	       "\n"
	       "add host=<host> type=<type> sets=<set names>\n"
	       "                [ interval=<interval> ] [ offset=<offset>]\n"
	       "                [ xprt=<xprt> ] [ port=<port> ]\n"
	       "                [ standby=<agg_no> ]\n"
	       "   - Adds a host to the list of hosts monitored by this ldmsd.\n"
	       "     <host>       The hostname. This can be an IP address or DNS\n"
	       "                  hostname.\n"
	       "     <type>       One of the following host types: \n"
	       "         active   An connection is initiated with the peer and\n"
	       "                  it's metric sets will be periodically queried.\n"
	       "         passive  A connect request is expected from the specified host.\n"
	       "                  After this request is received, the peer's metric sets\n"
	       "                  will be queried periodically.\n"
	       "         bridging A connect request is initiated to the remote peer,\n"
	       "                  but it's metric sets are not queried. This is the active\n"
	       "                  side of the passive host above.\n"
	       "         local    The to-be-added host is the local host. The given\n"
	       "                  set name(s) must be the name(s) of local set(s).\n"
	       "                  This option is used so that ldmsd can store\n"
	       "                  the given local set(s) if it is configured to do so.\n"
	       "     <set names>  The list of metric set names to be queried.\n"
	       "		  The list is comma separated.\n"
	       "     <interval>   An optional sampling interval in microseconds,\n"
	       "                  defaults to 1000000.\n"
	       "     <offset>     An optional offset (shift) from the sample mark\n"
	       "                  in microseconds. If this offset is specified,\n "
	       "                  including 0, the collection will be synchronous;\n"
	       "                  if the offset is not specified, the collection\n"
	       "                  will be asychronous\n"
	       "     <xprt>       The transport type, defaults to 'sock'\n"
	       "         sock     The sockets transport.\n"
	       "         rdma     The OFA Verbs Transport for Infiniband or iWARP.\n"
	       "         ugni     The Cray Gemini transport.\n"
	       "     <port>       The port number to connect on, defaults to 50000.\n"
	       "     <agg_no>     The number of the aggregator that this is standby for.\n"
	       "                  Defaults to 0 which means this is an active aggregator.\n"
	       "\n"
	       "store name=<plugin> policy=<policy> container=<container> schema=<schema>\n"
	       "      [hosts=<hosts>] [metric=<metric>,<metric>,...]\n"
	       "   - Saves a metrics from one or more hosts to persistent storage.\n"
	       "      <policy>      The storage policy name. This must be unique.\n"
	       "      <container>   The container name used by the plugin to name data.\n"
	       "      <schema>      A name used to name the set of metrics stored together.\n"
	       "      <metrics>     A comma separated list of metric names. If not specified,\n"
	       "                    all metrics in the metric set will be saved.\n"
	       "      <hosts>       The set of hosts whose data will be stored. If hosts is not\n"
	       "                    specified, the metric set will be saved for all hosts. If\n"
	       "                    specified, the value should be a comma separated list of\n"
	       "                    host names.\n"
	       "\n"
	       "standby agg_no=<agg_no> state=<0/1>\n"
	       "   - ldmsd will update the standby state (standby/active) of\n"
	       "     the given aggregator number.\n"
	       "    <agg_no>    Unique integer id for an aggregator from 1 to 64\n"
	       "    <state>     0/1 - standby/active\n"
	       "\n"
	       "oneshot name=<name> time=<time>\n"
	       "   - Schedule a one-shot sample event\n"
	       "     <name>       The sampler name.\n"
	       "     <time>       A Unix timestamp or a special keyword 'now+<second>'\n"
	       "                  The sample will occur at the specified timestamp or at\n"
	       "                  the <second> from now.\n"
	       "\n"
	       "info\n"
	       "   - Causes the ldmsd to dump out information about plugins,\n"
	       "     work queue utilization, hosts and object stores.\n"
	       "\n"
	       "quit\n"
	       "   - Exit.\n");
	return 0;
}

int handle_quit(char *kw)
{
	printf("bye ... :)\n");
	exit(0);
	return 0;
}

struct kw keyword_tbl[] = {
	{ "?", LDMSCTL_HELP, handle_help },
	{ "add", LDMSCTL_ADD_HOST, NULL },
	{ "config", LDMSCTL_CFG_PLUGIN, NULL },
	{ "help", LDMSCTL_HELP, handle_help },
	{ "info", LDMSCTL_INFO_DAEMON, NULL },
	{ "load", LDMSCTL_LOAD_PLUGIN, NULL },
	{ "oneshot", LDMSCTL_ONESHOT_SAMPLE, NULL },
	{ "quit", LDMSCTL_QUIT, handle_quit },
	{ "standby", LDMSCTL_UPDATE_STANDBY, NULL },
	{ "start", LDMSCTL_START_SAMPLER, NULL },
	{ "stop", LDMSCTL_STOP_SAMPLER, NULL },
	{ "store", LDMSCTL_STORE, NULL },
	{ "term", LDMSCTL_TERM_PLUGIN, NULL },
	{ "udata", LDMSCTL_SET_UDATA, NULL },
	{ "usage", LDMSCTL_LIST_PLUGINS, NULL },
};

static int kw_comparator(const void *a, const void *b)
{
	struct kw *_a = (struct kw *)a;
	struct kw *_b = (struct kw *)b;
	return strcmp(_a->token, _b->token);
}

int connected = 0;

void __recv_resp(rctrl_t ctrl)
{
	ocm_cfg_t cfg = ctrl->cfg;
	struct ocm_cfg_cmd_iter cmd_iter;
	ocm_cfg_cmd_iter_init(&cmd_iter, cfg);
	ocm_cfg_cmd_t cmd;
	while (0 == ocm_cfg_cmd_iter_next(&cmd_iter, &cmd)) {
		const struct ocm_value *v;
		v = ocm_av_get_value(cmd, "msg");
		int errcode;
		int cnt;
		sscanf(v->s.str, "%d%n", &errcode, &cnt);
		if (0 != strlen(&v->s.str[cnt]))
			printf("%s\n", &v->s.str[cnt]);
	}
	sem_post(&recv_sem);
}

void rctrl_cb(enum rctrl_event ev, rctrl_t ctrl)
{
	switch (ev) {
	case RCTRL_EV_CONNECTED:
		sem_post(&conn_sem);
		break;
	case RCTRL_EV_REJECTED:
		printf("Connection to %s at %s is rejected.\n",
							host, port);
		exit(1);
		break;
	case RCTRL_EV_DISCONNECTED:
	case RCTRL_EV_CONN_ERROR:
		printf("Connection to %s at %s is disconnected/error.\n",
							host, port);
		exit(1);
		break;
	case RCTRL_EV_RECV_COMPLETE:
		__recv_resp(ctrl);
		break;
	default:
		assert(0 == "unrecognized rctrl event");
		break;
	}
}

void __handle_cmd(char *cmd, rctrl_t ctrl)
{
	struct ocm_value *v = (void *)buff;
	ocm_cfg_buff_reset(cfg, "");
	ocm_cfg_buff_add_verb(cfg, "");
	ocm_value_set_s(v, cmd);
	ocm_cfg_buff_add_av(cfg, "cmd", v);
	char *ptr;
	struct kw key;
	struct kw *kw;

	key.token = strtok_r(cmd, " \t\n", &ptr);
	kw = bsearch(&key, keyword_tbl, ARRAY_SIZE(keyword_tbl),
		     sizeof(*kw), kw_comparator);
	if (kw) {
		ocm_value_set(v, OCM_VALUE_INT32, kw->cmd_id);
		ocm_cfg_buff_add_av(cfg, "cmd_id", v);
		if (kw->action) {
			(void)kw->action(key.token);
		} else {
			rctrl_send_request(ctrl, cfg);
			sem_wait(&recv_sem);
		}
	} else {
		printf("Unrecognized keyword '%s'.\n", key.token);
	}

}

static void null_log(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	fflush(stdout);
}

#ifdef ENABLE_AUTH

#include "ovis_auth/auth.h"

const char *ldmsd_rctrl_get_secretword(const char *path)
{
	int rc;
	if (!path) {
		path = getenv(LDMSD_RCTRL_AUTH_ENV);
		if (!path)
			return NULL;
	}
	return ovis_auth_get_secretword(path, null_log);
}
#endif /* ENABLE_AUTH */

int main(int argc, char *argv[])
{
	int op;
	char *s;
	int rc;

	opterr = 0;
	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'h':
			host = strdup(optarg);
			break;
		case 'p':
			port = strdup(optarg);
			break;
		case 'a':
			secretword_path = strdup(optarg);
			break;
		default:
			usage(argv);
		}
	}

	if (!host) {
		printf("hostname is required.\n");
		usage(argv);
	}

	if (!port) {
		printf("port is required.\n");
		usage(argv);
	}

	rc = sem_init(&conn_sem, 0, 0);
	if (rc) {
		printf("Failed to initialize the connection semaphore.\n");
		exit(rc);
	}

	rc = sem_init(&recv_sem, 0, 0);
	if (rc) {
		printf("Failed to initialize the receive semaphore.\n");
		exit(rc);
	}

	const char *word = 0;
#ifdef ENABLE_AUTH
	word = ldmsd_rctrl_get_secretword(secretword_path);
#endif /* ENABLE_AUTH */

	rctrl_t ctrl = rctrl_setup_controller("sock", rctrl_cb, word, null_log);
	if (!ctrl) {
		printf("Error setting up connection with ldmsd.\n");
		exit(1);
	}
	if (word)
		free((void *)word);
	printf("Connecting to %s at %s\n", host, port);
	rc = rctrl_connect(host, port, ctrl);
	if (rc)
		exit(rc);

	sem_wait(&conn_sem);

	buff = malloc(1024 * 512);
	if (!buff) {
		printf("Out of memory\n");
		exit(ENOMEM);
	}
	cfg = ocm_cfg_buff_new(1024 * 1024, "");
	if (!cfg) {
		printf("Out of memory\n");
		exit(ENOMEM);
	}
	atexit(cleanup);
	do {
		if (isatty(0))
			s = readline("ldmsd_rctl> ");
		else
			s = fgets(linebuf, sizeof linebuf, stdin);
		if (!s)
			break;
		add_history(s);

		__handle_cmd(s, ctrl);
	} while (s);
	return 0;
}
