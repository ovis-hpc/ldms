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
#include <semaphore.h>
#include <assert.h>
#include "config.h"

#ifdef HAVE_LIBREADLINE
#  if defined(HAVE_READLINE_READLINE_H)
#    include <readline/readline.h>
#  elif defined(HAVE_READLINE_H)
#    include <readline.h>
#  else /* !defined(HAVE_READLINE_H) */
extern char *readline ();
#  endif /* !defined(HAVE_READLINE_H) */
#else /* !defined(HAVE_READLINE_READLINE_H) */
  /* no readline */
#endif /* HAVE_LIBREADLINE */

#ifdef HAVE_READLINE_HISTORY
#  if defined(HAVE_READLINE_HISTORY_H)
#    include <readline/history.h>
#  elif defined(HAVE_HISTORY_H)
#    include <history.h>
#  else /* !defined(HAVE_HISTORY_H) */
extern void add_history ();
extern int write_history ();
extern int read_history ();
#  endif /* defined(HAVE_READLINE_HISTORY_H) */
  /* no history */
#endif /* HAVE_READLINE_HISTORY */

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

struct command {
	char *token;
	int cmd_id;
	int (*action)(char *args);
	void (*help)();
};

static int command_comparator(const void *a, const void *b)
{
	struct command *_a = (struct command *)a;
	struct command *_b = (struct command *)b;
	return strcmp(_a->token, _b->token);
}

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

int handle_quit(char *kw)
{
	printf("bye ... :)\n");
	exit(0);
	return 0;
}

void help_usage()
{
	printf( "usage\n"
		"   - Show loaded plugin usage information.\n");
}

void help_load()
{
	printf(	"\nLoads the specified plugin. The library that implements\n"
		"the plugin should be in the directory specified by the\n"
		"LDMSD_PLUGIN_LIBPATH environment variable.\n\n"
		"Parameters:\n"
		"     name=       The plugin name, this is used to locate a loadable\n"
		"                 library named \"lib<name>.so\"\n");
}

void help_term()
{
	printf(	"\nUnload the specified plugin.\n\n"
		"Parameters:\n"
		"     name=       The plugin name.\n");
}

void help_config()
{
	printf(	"Provides a mechanism to specify configuration options\n\n"
		"Parameters:\n"
		"     name=       The plugin name.\n"
		"     <attr>=     Plugin specific attr=value tuples.\n");
}

void help_start()
{
	printf(	"Begins calling the sampler's 'sample' method at the\n"
		"sample interval.\n\n"
		"Parameters:\n"
		"     name=       The sampler name.\n"
		"     interval=   The sample interval in microseconds.\n"
		"     [offset=]     Optional offset (shift) from the sample mark\n"
		"                 in microseconds. Offset can be positive or\n"
		"                 negative with magnitude up to 1/2 the sample interval.\n"
		"                 If this offset is specified, including 0, \n"
		"                 collection will be synchronous; if the offset\n"
		"                 is not specified, collection will be asychronous.\n");
}

void help_stop()
{
	printf( "\nCancels sampling on the specified plugin.\n\n"
		"Parameters:\n"
		"     name=       The sampler name.\n");
}

void help_add()
{
	printf( "\nAdds a host to the list of hosts monitored by this ldmsd.\n\n"
		"Parameters:\n"
		"     host=        The hostname. This can be an IP address or DNS\n"
		"                  hostname.\n"
		"     type=        One of the following host types: \n"
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
		"     sets=        The list of metric set names to be queried.\n"
		"		   The list is comma separated. If the type is bridging\n"
		"                  no set names should be specified\n"
		"     [xprt=]      The transport type, defaults to 'sock'\n"
		"         sock     The sockets transport.\n"
		"         rdma     The OFA Verbs Transport for Infiniband or iWARP.\n"
		"         ugni     The Cray Gemini transport.\n"
		"     [port=]        The port number to connect on, defaults to 50000.\n"
		"     [interval=]  An optional sampling interval in microseconds,\n"
		"                  defaults to 1000000.\n"
		"     [offset=]    An optional offset (shift) from the sample mark\n"
		"                  in microseconds. If this offset is specified,\n "
		"                  including 0, the collection will be synchronous;\n"
		"                  if the offset is not specified, the collection\n"
		"                  will be asychronous\n"
		"     [agg_no=]    The number of the aggregator that this is standby for.\n"
		"                  Defaults to 0 which means this is an active aggregator.\n");
}

void help_store()
{
	printf( "\nSaves metrics from one or more hosts to persistent storage.\n\n"
		"Parameters:\n"
		"      policy=      The storage policy name. This must be unique.\n"
		"      container=   The container name used by the plugin to name data.\n"
		"      schema=      A name used to name the set of metrics stored together.\n"
		"      [metrics=]   A comma separated list of metric names. If not specified,\n"
		"                   all metrics in the metric set will be saved.\n"
		"      [hosts=]     The set of hosts whose data will be stored. If hosts is not\n"
		"                   specified, the metric set will be saved for all hosts. If\n"
		"                   specified, the value should be a comma separated list of\n"
		"                   host names.\n");
}

void help_info()
{
	printf( "\nCauses the ldmsd to dump out information about its internal state.\n"
		"Parameters:\n"
		"      [name=]     Dump the specified objects. The choices are\n"
		"                  prdcr, updtr and strgp.\n"
		"                     prdcr: List the states of all producers\n"
		"                     updtr: List the states of all update policies\n"
		"                     strgp: List the states of all storage policies.\n");
}

void help_udata()
{
	printf( "\nSet the user data of the specified metric in the given set\n\n"
		"Parameters:\n"
		"     set=           The metric set name\n"
		"     metric_name=   The metric name\n"
		"     user_data=     The user data value\n");
}

void help_udata_regex()
{
	printf( "\nSet the user data of multiple metrics using regular expression.\n"
		"The user data of the first matched metric is set to the base value.\n"
		"The base value is incremented by the given 'incr' value and then\n"
		"sets to the user data of the consecutive matched metric and so on.\n"
		"Parameters:\n"
		"     set=           The metric set name\n"
		"     regex=         A regular expression to match metric names to be set\n"
		"     base=          The base value of user data (uint64).\n"
		"     [incr=]        Increment value (int). The default is 0. If incr is 0,\n"
		"                    the user data of all matched metrics are set\n"
		"                    to the base value.\n");
}

void help_standby()
{
	printf( "\nldmsd will update the standby state (standby/active) of\n"
		"the given aggregator number.\n\n"
		"Parameters:\n"
		"    agg_no=    Unique integer id for an aggregator from 1 to 64\n"
		"    state=     0/1 - standby/active\n");
}

void help_oneshot()
{
	printf( "\nSchedule a one-shot sample event\n\n"
		"Parameters:\n"
		"     name=       The sampler name.\n"
		"     time=       A Unix timestamp or a special keyword 'now+<second>'\n"
		"                 The sample will occur at the specified timestamp or at\n"
		"                 the second= from now.\n");
}

void help_quit()
{
	printf( "\nquit\n"
		"   - Exit.\n");
}

void help_prdcr_add()
{
	printf( "\nAdd an LDMS Producer to the Aggregator\n\n"
		"Parameters:\n"
		"     name=     A unique name for this Producer\n"
		"     xprt=     The transport name [sock, rdma, ugni]\n"
		"     host=     The hostname of the host\n"
		"     port=     The port number on which the LDMS is listening\n"
		"     type=     The connection type [active, passive]\n"
		"     interval= The connection retry interval (us)\n");
}

void help_prdcr_del()
{
	printf( "\nDelete an LDMS Producer from the Aggregator. The producer\n"
		"cannot be in use or running.\n\n"
		"Parameters:\n"
		"     name=    The Producer name\n");
}

void help_prdcr_start()
{
	printf( "\nStart the specified producer.\n\n"
		"Parameters:\n"
		"     name=       The name of the producer\n"
		"     [interval=] The connection retry interval in micro-seconds.\n"
		"                 If this is not specified, the previously\n"
		"                 configured value will be used.\n");
}

void help_prdcr_stop()
{
	printf( "\nStop the specified producer.\n\n"
		"Parameters:\n"
		"     name=     THe producer name\n");
}

void help_prdcr_start_regex()
{
	printf( "\nStart all producers matching a regular expression.\n\n"
		"Parameters:\n\n"
		"     regex=        A regular expression\n"
		"     [interval=]   The connection retry interval in micro-seconds.\n"
		"                   If this is not specified, the previously configured\n"
		"                   value will be used.\n");
}

void help_prdcr_stop_regex()
{
	printf( "\nStop all producers matching a regular expression.\n\n"
		"Parameters:\n"
		"     regex=        A regular expression\n");
}

void help_updtr_add()
{
	printf( "\nAdd an updater process that will periodically sample\n"
		"Producer metric sets\n\n"
		"Parameters:\n"
		"     name=       The update policy name\n"
		"     interval=   The update/collect interval\n"
		"     [offset=]   Offset for synchronized aggregation\n");
}

void help_updtr_del()
{
	printf( "\nRemove an updater from the configuration\n\n"
		"Parameters:\n"
		"     name=     The update policy name\n");
}

void help_updtr_match_add()
{
	printf( "\nAdd a match condition that specifies the sets to an Updater policy.\n\n"
		"Parameters:\n"
		"     name=   The update policy name\n"
		"     regex=  The regular expression string\n"
		"     match=  The value with which to compare; if match=inst,\n"
		"     	      the expression will match the set's instance name, if\n"
		"     	      match=schema, the expression will match the set's\n"
		"     	      schema name.\n");
}

void help_updtr_match_del()
{
	printf( "\nRemove a match condition that specifies the sets from an Updater policy.\n\n"
		"Parameters:\n"
		"     name=   The update policy name\n"
		"     regex=  The regular expression string\n"
		"     match=  The value with which to compare; if match=inst,\n"
		"     	      the expression will match the set's instance name, if\n"
		"     	      match=schema, the expression will match the set's\n"
		"     	      schema name.\n");
}

void help_updtr_prdcr_add()
{
	printf( "\nAdd matching Producers to an Updater policy.\n\n"
		"Parameters:\n"
		"     name=   The update policy name\n"
		"     regex=  A regular expression matching zero or more producers\n");
}

void help_updtr_prdcr_del()
{
	printf( "\nRemove matching Producers from an Updater policy.\n\n"
		"Parameters:\n"
		"     name=   The update policy name\n"
		"     regex=  A regular expression matching zero or more producers\n");
}

void help_updtr_start()
{
	printf( "\nStart an update policy.\n\n"
		"Parameters:\n"
		"     name=       The update policy name\n"
		"     [interval=] The update interval in micro-seconds.\n"
		"                 If this is not specified, the previously\n"
		"                 configured value will be used.\n"
		"     [offset=]   Offset for synchronization\n");
}

void help_updtr_stop()
{
	printf( "\nStop an update policy. The Updater must be stopped in order to\n"
		"change it's configuration.\n\n"
		"Parameters:\n"
		"     name=   The update policy name\n");
}

void help_strgp_add()
{
	printf( "\nCreate a Storage Policy and open/create the storage instance.\n"
		"The store plugin must be configured via the command 'config'\n\n"
		"Parameters:\n"
		"     name=        The unique storage policy name.\n"
		"     plugin=      The name of the storage backend.\n"
		"     container=   The storage backend container name.\n"
		"     schema=      The schema name of the metric set to store.\n");
}

void help_strgp_del()
{
	printf( "\nRemove a Storage Policy. All updaters must be stopped in order for\n"
		"a storage policy to be deleted.\n\n"
		"Parameters:\n"
		"     name=   The storage policy name\n");
}

void help_strgp_prdcr_add()
{
	printf( "\nAdd a regular expression used to identify the producers this\n"
		"storage policy will apply to.\n\n"
		"Parameters:\n"
		"     name=   The storage policy name\n"
		"     regex=  A regular expression matching metric set producers\n");
}

void help_strgp_prdcr_del()
{
	printf( "\nRemove a regular expression from the producer match list.\n\n"
		"Parameters:\n"
		"     name=   The storage policy name\n"
		"     regex=  The regular expression to remove\n");
}

void help_strgp_metric_add()
{
	printf( "\nAdd the name of a metric to store. If the metric list is NULL,\n"
		"all metrics in the metric set will be stored.\n\n"
		"Parameters:\n"
		"     name=   The storage policy name\n"
		"     metric= The metric name\n");
}

void help_strgp_metric_del()
{
	printf( "\nRemove a metric from the set of stored metrics\n\n"
		"Parameters:\n"
		"     name=   The storage policy name\n"
		"     metric= The metric name\n");
}

void help_strgp_start()
{
	printf( "\nStart an storage policy\n\n"
		"Parameters:\n"
		"     name=   The storage policy name\n");
}

void help_strgp_stop()
{
	printf( "\nStop an storage policy. A storage policy must be stopped\n"
		"in order to change its configuration.\n\n"
		"Parameters:\n"
		"     name=   The storage policy name\n");
}

void help_version()
{
	printf( "\nGet the LDMS version.\n");
}

int handle_help(char *args);

static struct command command_tbl[] = {
	{ "?", LDMSCTL_HELP, handle_help, NULL },
	{ "add", LDMSCTL_ADD_HOST, NULL, help_add },
	{ "config", LDMSCTL_CFG_PLUGIN, NULL, help_config },
	{ "help", LDMSCTL_HELP, handle_help, NULL },
	{ "info", LDMSCTL_INFO_DAEMON, NULL, help_info },
	{ "load", LDMSCTL_LOAD_PLUGIN, NULL, help_load },
	{ "oneshot", LDMSCTL_ONESHOT_SAMPLE, NULL, help_oneshot },
	{ "prdcr_add", LDMSCTL_PRDCR_ADD, NULL, help_prdcr_add },
	{ "prdcr_del", LDMSCTL_PRDCR_DEL, NULL, help_prdcr_del },
	{ "prdcr_start", LDMSCTL_PRDCR_START, NULL, help_prdcr_start },
	{ "prdcr_start_regex", LDMSCTL_PRDCR_START_REGEX, NULL, help_prdcr_start_regex },
	{ "prdcr_stop", LDMSCTL_PRDCR_STOP, NULL, help_prdcr_stop },
	{ "prdcr_stop_regex", LDMSCTL_PRDCR_STOP_REGEX, NULL, help_prdcr_stop_regex },
	{ "quit", LDMSCTL_QUIT, handle_quit, help_quit },
	{ "standby", LDMSCTL_UPDATE_STANDBY, NULL, help_standby },
	{ "start", LDMSCTL_START_SAMPLER, NULL, help_start },
	{ "stop", LDMSCTL_STOP_SAMPLER, NULL, help_stop },
	{ "store", LDMSCTL_STORE, NULL, help_store },
	{ "strgp_add", LDMSCTL_STRGP_ADD, NULL, help_strgp_add },
	{ "strgp_del", LDMSCTL_STRGP_DEL, NULL, help_strgp_del },
	{ "strgp_metric_add", LDMSCTL_STRGP_METRIC_ADD, NULL, help_strgp_metric_add },
	{ "strgp_metric_del", LDMSCTL_STRGP_METRIC_DEL, NULL, help_strgp_metric_del },
	{ "strgp_prdcr_add", LDMSCTL_STRGP_PRDCR_ADD, NULL, help_strgp_prdcr_add },
	{ "strgp_prdcr_del", LDMSCTL_STRGP_PRDCR_DEL, NULL, help_strgp_prdcr_del },
	{ "strgp_start", LDMSCTL_STRGP_START, NULL, help_strgp_start },
	{ "strgp_stop", LDMSCTL_STRGP_STOP, NULL, help_strgp_stop },
	{ "term", LDMSCTL_TERM_PLUGIN, NULL, help_term },
	{ "udata", LDMSCTL_SET_UDATA, NULL, help_udata },
	{ "udata_regex", LDMSCTL_SET_UDATA_REGEX, NULL, help_udata_regex },
	{ "updtr_add", LDMSCTL_UPDTR_ADD, NULL, help_updtr_add },
	{ "updtr_del", LDMSCTL_UPDTR_DEL, NULL, help_updtr_del },
	{ "updtr_match_add", LDMSCTL_UPDTR_MATCH_ADD, NULL, help_updtr_match_add },
	{ "updtr_match_del", LDMSCTL_UPDTR_MATCH_DEL, NULL, help_updtr_match_del },
	{ "updtr_prdcr_add", LDMSCTL_UPDTR_PRDCR_ADD, NULL, help_updtr_prdcr_add },
	{ "updtr_prdcr_del", LDMSCTL_UPDTR_PRDCR_DEL, NULL, help_updtr_prdcr_del },
	{ "updtr_start", LDMSCTL_UPDTR_START, NULL, help_updtr_start },
	{ "updtr_stop", LDMSCTL_UPDTR_STOP, NULL, help_updtr_stop },
	{ "usage", LDMSCTL_LIST_PLUGINS, NULL, help_usage },
	{ "version", LDMSCTL_VERSION },
};

void __print_all_command()
{
	printf( "The available commands are as follows. To see help for\n"
		"a command, do 'help <command>'\n\n");
	size_t tbl_len = sizeof(command_tbl)/sizeof(command_tbl[0]);

	int max_width = 20;
	int i = 0;
	printf("%-*s", max_width, command_tbl[i].token);
	for (i = 1; i < tbl_len; i++) {
		printf("%-*s", max_width, command_tbl[i].token);
		if (i % 5 == 4)
			printf("\n");
	}
	printf("\n");
}

int handle_help(char *args)
{
	if (!args) {
		__print_all_command();
	} else {
		char *_args, *ptr;
		_args = strtok_r(args, " \t\n", &ptr);
		if (!_args) {
			__print_all_command();
			return 0;
		}

		struct command *command;
		command = bsearch(&_args, command_tbl, ARRAY_SIZE(command_tbl),
			     sizeof(*command), command_comparator);
		if (!command) {
			printf("Unrecognized command '%s'.\n", _args);
			return EINVAL;
		}
		if (command->help) {
			command->help();
		} else {
			printf("No help found for the command '%s'.\n",
					command->token);
		}
		return 0;
	}

	return 0;
}

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
	char *args, *ptr;
	struct command key;
	struct command *command;

	key.token = strtok_r(cmd, " \t\n", &ptr);
	args = strtok_r(NULL, "\n", &ptr);
	command = bsearch(&key, command_tbl, ARRAY_SIZE(command_tbl),
		     sizeof(struct command), command_comparator);
	if (command) {
		ocm_value_set(v, OCM_VALUE_INT32, command->cmd_id);
		ocm_cfg_buff_add_av(cfg, "cmd_id", v);
		if (command->action) {
			(void)command->action(args);
		} else {
			rctrl_send(ctrl, cfg);
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
#ifdef HAVE_LIBREADLINE
#ifndef HAVE_READLINE_HISTORY
		if (s != NULL)
			free(s); /* previous readline output must be freed if not in history */
#endif /* HAVE_READLINE_HISTORY */
		if (isatty(0))
			s = readline("ldmsd_rctl> ");
		else
			s = fgets(linebuf, sizeof linebuf, stdin);
#else /* HAVE_LIBREADLINE */
		s = fgets(linebuf, sizeof linebuf, stdin);
#endif /* HAVE_LIBREADLINE */
		if (!s)
			break;
#ifdef HAVE_READLINE_HISTORY
		add_history(s);
#endif /* HAVE_READLINE_HISTORY */

		__handle_cmd(s, ctrl);
	} while (s);
	return 0;
}
