/*
 * Copyright (c) 2011 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2011 Sandia Corporation. All rights reserved.
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
#include <netinet/in.h>
#include <sys/un.h>
#include <libgen.h>
#include <signal.h>
#include <search.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "ldms.h"
#include "ldmsd.h"

#define FMT "S:"
#define ARRAY_SIZE(a)  (sizeof(a) / sizeof(a[0]))

struct attr_value_list *av_list, *kw_list;

void usage(char *argv[])
{
	printf("%s: [%s]\n"
               "    -S <socket>     The UNIX socket that the ldms daemon is listening on.\n"
               "                    [" LDMSD_CONTROL_SOCKNAME "].\n",
               argv[0], FMT);
	exit(1);
}

int handle_help(char *kw, char *err_str)
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
	       "start name=<name> interval=<interval>\n"
	       "   - Begins calling the sampler's 'sample' method at the\n"
	       "     sample interval.\n"
	       "     <name>       The sampler name.\n"
	       "     <interval>   The sample interval in microseconds.\n"
	       "\n"
	       "stop name=<name>\n"
	       "   - Cancels sampling on the specified plugin.\n"
	       "     <name>       The sampler name.\n"
	       "\n"
	       "add host=<host> type=<type> [ interval=<interval> ]\n"
	       "                [ xprt=<xprt> ] [ port=<port> ]\n"
	       "   - Adds a host to the list of hosts monitored by this ldmsd.\n"
	       "     <host>       The hostname. This can be an IP address or DNS\n"
	       "                  hostname.\n"
	       "     <type>       One of the following host types: \n"
	       "         active   An connection is initiated with the peer and\n"
	       "                  it's metric sets will be periodically queried.\n"
	       "         passive  A connect request is expected from the specified host.\n"
	       "                  After this request is received, the peer's metric sets\n"
	       "                  will be queried periodically.\n"
	       "         bridging A connect request is initated to the remote peer,\n"
	       "                  but it's metric sets are not queried. This is the active\n"
	       "                  sive of the passive host above.\n"
	       "     <interval>   An optional sampling interval in microseconds,\n"
	       "                  defaults to 1000000.\n"
	       "     <xprt>       The transport type, defaults to 'sock'\n"
	       "         sock     The sockets transport.\n"
	       "         rdma     The OFA Verbs Transport for Infiniband or iWARP.\n"
	       "         ugni     The Cray Gemini transport.\n"
	       "     <port>       The port number to connect on, defaults to 50000.\n"
	       "\n"
	       "store name=<store> hosts=<hosts> set=<set> metric=<metric>\n"
	       "   - Saves a set from one or more hosts to a persistent object store.\n"
	       "     <store>      The name of the storage plugin.\n"
	       "     <comp_type>  The component type.\n"
	       "     <set>        The set whose data will be saved. Data is saved\n"
	       "                  when update completes if the generation number has\n"
	       "                  changed.\n"
	       "     <metrics>    A list of metric names in the specified set. If not\n"
	       "                  specified, all metrics will be saved.\n"
	       "     <hosts>      A list of hosts to whose set data will be saved.\n"
	       "                  If not specified, all hosts that have this set will\n"
	       "                  have their data saved.\n"
	       "\n"
	       "info\n"
	       "   - Causes the ldmsd to dump out information about plugins,\n"
	       "     work queue utilization, hosts and object stores.\n"
	       "\n"
	       "quit\n"
	       "   - Exit.\n");
	return 0;
}

char err_str[8192];
char linebuf[8192];
char *sockname = LDMSD_CONTROL_SOCKNAME;
struct ctrlsock *ctrl_sock;

int handle_usage(char *kw, char *err_str)
{
	return ctrl_request(ctrl_sock, LDMSCTL_LIST_PLUGINS, av_list, err_str);
}

int handle_plugin_load(char *kw, char *err_str)
{
	return ctrl_request(ctrl_sock, LDMSCTL_LOAD_PLUGIN, av_list, err_str);
}

int handle_plugin_term(char *kw, char *err_str)
{
	return ctrl_request(ctrl_sock, LDMSCTL_TERM_PLUGIN, av_list, err_str);
}

int handle_plugin_config(char *kw, char *err_str)
{
	return ctrl_request(ctrl_sock, LDMSCTL_CFG_PLUGIN, av_list, err_str);
}

int handle_sampler_start(char *kw, char *err_str)
{
	return ctrl_request(ctrl_sock, LDMSCTL_START_SAMPLER, av_list, err_str);
}

int handle_sampler_stop(char *kw, char *err_str)
{
	return ctrl_request(ctrl_sock, LDMSCTL_STOP_SAMPLER, av_list, err_str);
}

int handle_host_add(char *kw, char *err_str)
{
	return ctrl_request(ctrl_sock, LDMSCTL_ADD_HOST, av_list, err_str);
}

int handle_store(char *kw, char *err_str)
{
	return ctrl_request(ctrl_sock, LDMSCTL_STORE, av_list, err_str);
}

int handle_info(char *kw, char *err_str)
{
	return ctrl_request(ctrl_sock, LDMSCTL_INFO_DAEMON, av_list, err_str);
}

int handle_quit(char *kw, char *err_str)
{
	exit(0);
	return 0;
}

struct kw {
	char *token;
	int (*action)(char *kw, char *err_str);
};

int handle_nxt_token(char *kw, char *err_str);
struct kw keyword_tbl[] = {
	{ "?", handle_help },
	{ "add", handle_host_add },
	{ "config", handle_plugin_config },
	{ "help", handle_help },
	{ "info", handle_info },
	{ "load", handle_plugin_load },
	{ "quit", handle_quit },
	{ "start", handle_sampler_start },
	{ "stop", handle_sampler_stop },
	{ "store", handle_store },
	{ "term", handle_plugin_term },
	{ "usage", handle_usage },
};

static int kw_comparator(const void *a, const void *b)
{
	struct kw *_a = (struct kw *)a;
	struct kw *_b = (struct kw *)b;
	return strcmp(_a->token, _b->token);
}

int nxt_kw;
int handle_nxt_token(char *word, char *err_str)
{
	struct kw key;
	struct kw *kw;

	key.token = av_name(kw_list, nxt_kw);
	kw = bsearch(&key, keyword_tbl, ARRAY_SIZE(keyword_tbl),
		     sizeof(*kw), kw_comparator);
	if (kw) {
		nxt_kw++;
		return kw->action(key.token, err_str);
	}
	printf("Uncrecognized keyword '%s'.", key.token);
	return EINVAL;
}

int main(int argc, char *argv[])
{
	int op;
	char *s;
	int rc;

	opterr = 0;
	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'S':
			sockname = strdup(optarg);
			break;
		default:
			usage(argv);
		}
	}
	av_list = av_new(128);
	kw_list = av_new(128);

	ctrl_sock = ctrl_connect(basename(argv[0]), sockname);
	if (!ctrl_sock) {
		printf("Error setting up connection with ldmsd.\n");
		exit(1);
	}
	do {
		if (isatty(0))
			s = readline("ldmsctl> ");
		else
			s = fgets(linebuf, sizeof linebuf, stdin);
		if (!s)
			break;
		add_history(s);
		err_str[0] = '\0';
		rc = tokenize(s, kw_list, av_list);
		if (rc) {
			sprintf(err_str, "Memory allocation failure.");
			continue;
		}

		if (!kw_list->count)
			continue;

		struct kw key;
		struct kw *kw;

		nxt_kw = 0;
		key.token = av_name(kw_list, nxt_kw);
		kw = bsearch(&key, keyword_tbl, ARRAY_SIZE(keyword_tbl),
			     sizeof(*kw), kw_comparator);
		if (kw)
			(void)kw->action(key.token, err_str);
		else
			printf("Uncrecognized keyword '%s'.\n", key.token);
		if (err_str[0] != '\0')
			printf("%s\n", err_str);
	} while (s);
 	return 0;
}
