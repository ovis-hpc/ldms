/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2011-2017 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2011-2017 Open Grid Computing, Inc. All rights reserved.
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
#include <sys/socket.h>
#include <sys/un.h>
#include <libgen.h>
#include <signal.h>
#include <search.h>
#include <semaphore.h>
#include <assert.h>
#include "config.h"

#define _GNU_SOURCE

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

#include "ldmsd.h"
#include "ldmsd_request.h"

#define FMT "h:p:a:S:"
#define ARRAY_SIZE(a)  (sizeof(a) / sizeof(a[0]))

#define LDMSD_SOCKPATH_ENV "LDMSD_SOCKPATH"

static char *linebuf;
static size_t linebuf_len;
static char *buffer;
static size_t buffer_len;

struct ctrl {
	int sock;
	struct sockaddr *sa;
	size_t sa_len;
};

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

#define LDMSCTL_HELP LDMSD_NOTSUPPORT_REQ + 1
#define LDMSCTL_QUIT LDMSD_NOTSUPPORT_REQ + 2

static void usage(char *argv[])
{
	printf("%s: [%s]\n"
               "    -S <socket>     The UNIX socket that the ldms daemon is listening on.\n"
               "                    [" LDMSD_CONTROL_SOCKNAME "].\n",
               argv[0], FMT);
	exit(1);
}

static int handle_quit(char *kw)
{
	printf("bye ... :)\n");
	exit(0);
	return 0;
}

static void help_usage()
{
	printf( "usage\n"
		"   - Show loaded plugin usage information.\n"
		"Parameters:\n"
		"    [name=]     A loaded plugin name\n");
}

static void help_load()
{
	printf(	"\nLoads the specified plugin. The library that implements\n"
		"the plugin should be in the directory specified by the\n"
		"LDMSD_PLUGIN_LIBPATH environment variable.\n\n"
		"Parameters:\n"
		"     name=       The plugin name, this is used to locate a loadable\n"
		"                 library named \"lib<name>.so\"\n");
}

static void help_term()
{
	printf(	"\nUnload the specified plugin.\n\n"
		"Parameters:\n"
		"     name=       The plugin name.\n");
}

static void help_config()
{
	printf(	"Provides a mechanism to specify configuration options\n\n"
		"Parameters:\n"
		"     name=       The plugin name.\n"
		"     <attr>=     Plugin specific attr=value tuples.\n");
}

static void help_start()
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

static void help_stop()
{
	printf( "\nCancels sampling on the specified plugin.\n\n"
		"Parameters:\n"
		"     name=       The sampler name.\n");
}

static void help_daemon_status()
{
	printf( "\nCauses the ldmsd to dump out information about its internal state.\n");
}

static void help_udata()
{
	printf( "\nSet the user data of the specified metric in the given set\n\n"
		"Parameters:\n"
		"     set=           The metric set name\n"
		"     metric_name=   The metric name\n"
		"     user_data=     The user data value\n");
}

static void help_udata_regex()
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

static void help_oneshot()
{
	printf( "\nSchedule a one-shot sample event\n\n"
		"Parameters:\n"
		"     name=       The sampler name.\n"
		"     time=       A Unix timestamp or a special keyword 'now+<second>'\n"
		"                 The sample will occur at the specified timestamp or at\n"
		"                 the second= from now.\n");
}

static void help_loglevel()
{
	printf( "\nChange the verbosity level of ldmsd\n\n"
		"Parameters:\n"
		"	level=	levels [DEBUG, INFO, ERROR, CRITICAL, QUIET]\n");
}

static void help_quit()
{
	printf( "\nquit\n"
		"   - Exit.\n");
}

static void help_prdcr_add()
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

static void help_prdcr_del()
{
	printf( "\nDelete an LDMS Producer from the Aggregator. The producer\n"
		"cannot be in use or running.\n\n"
		"Parameters:\n"
		"     name=    The Producer name\n");
}

static void help_prdcr_start()
{
	printf( "\nStart the specified producer.\n\n"
		"Parameters:\n"
		"     name=       The name of the producer\n"
		"     [interval=] The connection retry interval in micro-seconds.\n"
		"                 If this is not specified, the previously\n"
		"                 configured value will be used.\n");
}

static void help_prdcr_stop()
{
	printf( "\nStop the specified producer.\n\n"
		"Parameters:\n"
		"     name=     THe producer name\n");
}

static void help_prdcr_start_regex()
{
	printf( "\nStart all producers matching a regular expression.\n\n"
		"Parameters:\n\n"
		"     regex=        A regular expression\n"
		"     [interval=]   The connection retry interval in micro-seconds.\n"
		"                   If this is not specified, the previously configured\n"
		"                   value will be used.\n");
}

static void help_prdcr_stop_regex()
{
	printf( "\nStop all producers matching a regular expression.\n\n"
		"Parameters:\n"
		"     regex=        A regular expression\n");
}

static void help_updtr_add()
{
	printf( "\nAdd an updater process that will periodically sample\n"
		"Producer metric sets\n\n"
		"Parameters:\n"
		"     name=       The update policy name\n"
		"     interval=   The update/collect interval\n"
		"     [offset=]   Offset for synchronized aggregation\n");
}

static void help_updtr_del()
{
	printf( "\nRemove an updater from the configuration\n\n"
		"Parameters:\n"
		"     name=     The update policy name\n");
}

static void help_updtr_match_add()
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

static void help_updtr_match_del()
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

static void help_updtr_prdcr_add()
{
	printf( "\nAdd matching Producers to an Updater policy.\n\n"
		"Parameters:\n"
		"     name=   The update policy name\n"
		"     regex=  A regular expression matching zero or more producers\n");
}

static void help_updtr_prdcr_del()
{
	printf( "\nRemove matching Producers from an Updater policy.\n\n"
		"Parameters:\n"
		"     name=   The update policy name\n"
		"     regex=  A regular expression matching zero or more producers\n");
}

static void help_updtr_start()
{
	printf( "\nStart an update policy.\n\n"
		"Parameters:\n"
		"     name=       The update policy name\n"
		"     [interval=] The update interval in micro-seconds.\n"
		"                 If this is not specified, the previously\n"
		"                 configured value will be used.\n"
		"     [offset=]   Offset for synchronization\n");
}

static void help_updtr_stop()
{
	printf( "\nStop an update policy. The Updater must be stopped in order to\n"
		"change it's configuration.\n\n"
		"Parameters:\n"
		"     name=   The update policy name\n");
}

static void help_strgp_add()
{
	printf( "\nCreate a Storage Policy and open/create the storage instance.\n"
		"The store plugin must be configured via the command 'config'\n\n"
		"Parameters:\n"
		"     name=        The unique storage policy name.\n"
		"     plugin=      The name of the storage backend.\n"
		"     container=   The storage backend container name.\n"
		"     schema=      The schema name of the metric set to store.\n");
}

static void help_strgp_del()
{
	printf( "\nRemove a Storage Policy. All updaters must be stopped in order for\n"
		"a storage policy to be deleted.\n\n"
		"Parameters:\n"
		"     name=   The storage policy name\n");
}

static void help_strgp_prdcr_add()
{
	printf( "\nAdd a regular expression used to identify the producers this\n"
		"storage policy will apply to.\n\n"
		"Parameters:\n"
		"     name=   The storage policy name\n"
		"     regex=  A regular expression matching metric set producers\n");
}

static void help_strgp_prdcr_del()
{
	printf( "\nRemove a regular expression from the producer match list.\n\n"
		"Parameters:\n"
		"     name=   The storage policy name\n"
		"     regex=  The regular expression to remove\n");
}

static void help_strgp_metric_add()
{
	printf( "\nAdd the name of a metric to store. If the metric list is NULL,\n"
		"all metrics in the metric set will be stored.\n\n"
		"Parameters:\n"
		"     name=   The storage policy name\n"
		"     metric= The metric name\n");
}

static void help_strgp_metric_del()
{
	printf( "\nRemove a metric from the set of stored metrics\n\n"
		"Parameters:\n"
		"     name=   The storage policy name\n"
		"     metric= The metric name\n");
}

static void help_strgp_start()
{
	printf( "\nStart an storage policy\n\n"
		"Parameters:\n"
		"     name=   The storage policy name\n");
}

static void help_strgp_stop()
{
	printf( "\nStop an storage policy. A storage policy must be stopped\n"
		"in order to change its configuration.\n\n"
		"Parameters:\n"
		"     name=   The storage policy name\n");
}

static void help_version()
{
	printf( "\nGet the LDMS version.\n");
}

int handle_help(char *args);

static struct command action_tbl[] = {
		{ "?", LDMSCTL_HELP, handle_help, NULL },
		{ "help", LDMSCTL_HELP, handle_help, NULL },
		{ "quit", LDMSCTL_QUIT, handle_quit, help_quit },
};

static struct command help_tbl[] = {
	{ "config", LDMSD_PLUGN_CONFIG_REQ, NULL, help_config },
	{ "daemon_status", LDMSD_DAEMON_STATUS_REQ, NULL, help_daemon_status },
	{ "load", LDMSD_PLUGN_LOAD_REQ, NULL, help_load },
	{ "loglevel", LDMSD_VERBOSE_REQ, NULL, help_loglevel },
	{ "oneshot", LDMSD_ONESHOT_REQ, NULL, help_oneshot },
	{ "prdcr_add", LDMSD_PRDCR_ADD_REQ, NULL, help_prdcr_add },
	{ "prdcr_del", LDMSD_PRDCR_DEL_REQ, NULL, help_prdcr_del },
	{ "prdcr_start", LDMSD_PRDCR_START_REQ, NULL, help_prdcr_start },
	{ "prdcr_start_regex", LDMSD_PRDCR_START_REGEX_REQ, NULL, help_prdcr_start_regex },
	{ "prdcr_stop", LDMSD_PRDCR_STOP_REQ, NULL, help_prdcr_stop },
	{ "prdcr_stop_regex", LDMSD_PRDCR_STOP_REGEX_REQ, NULL, help_prdcr_stop_regex },
	{ "quit", LDMSCTL_QUIT, handle_quit, help_quit },
	{ "start", LDMSD_PLUGN_START_REQ, NULL, help_start },
	{ "stop", LDMSD_PLUGN_STOP_REQ, NULL, help_stop },
	{ "strgp_add", LDMSD_STRGP_ADD_REQ, NULL, help_strgp_add },
	{ "strgp_del", LDMSD_STRGP_DEL_REQ, NULL, help_strgp_del },
	{ "strgp_metric_add", LDMSD_STRGP_METRIC_ADD_REQ, NULL, help_strgp_metric_add },
	{ "strgp_metric_del", LDMSD_STRGP_METRIC_DEL_REQ, NULL, help_strgp_metric_del },
	{ "strgp_prdcr_add", LDMSD_STRGP_PRDCR_ADD_REQ, NULL, help_strgp_prdcr_add },
	{ "strgp_prdcr_del", LDMSD_STRGP_PRDCR_DEL_REQ, NULL, help_strgp_prdcr_del },
	{ "strgp_start", LDMSD_STRGP_START_REQ, NULL, help_strgp_start },
	{ "strgp_stop", LDMSD_STRGP_STOP_REQ, NULL, help_strgp_stop },
	{ "term", LDMSD_PLUGN_TERM_REQ, NULL, help_term },
	{ "udata", LDMSD_SET_UDATA_REQ, NULL, help_udata },
	{ "udata_regex", LDMSD_SET_UDATA_REGEX_REQ, NULL, help_udata_regex },
	{ "updtr_add", LDMSD_UPDTR_ADD_REQ, NULL, help_updtr_add },
	{ "updtr_del", LDMSD_UPDTR_DEL_REQ, NULL, help_updtr_del },
	{ "updtr_match_add", LDMSD_UPDTR_MATCH_ADD_REQ, NULL, help_updtr_match_add },
	{ "updtr_match_del", LDMSD_UPDTR_DEL_REQ, NULL, help_updtr_match_del },
	{ "updtr_prdcr_add", LDMSD_UPDTR_PRDCR_ADD_REQ, NULL, help_updtr_prdcr_add },
	{ "updtr_prdcr_del", LDMSD_UPDTR_PRDCR_DEL_REQ, NULL, help_updtr_prdcr_del },
	{ "updtr_start", LDMSD_UPDTR_START_REQ, NULL, help_updtr_start },
	{ "updtr_stop", LDMSD_UPDTR_STOP_REQ, NULL, help_updtr_stop },
	{ "usage", LDMSD_PLUGN_LIST_REQ, NULL, help_usage },
	{ "version", LDMSD_VERSION_REQ, NULL, help_version },
};

void __print_all_command()
{
	printf( "The available commands are as follows. To see help for\n"
		"a command, do 'help <command>'\n\n");
	size_t tbl_len = sizeof(help_tbl)/sizeof(help_tbl[0]);

	int max_width = 20;
	int i = 0;
	printf("%-*s", max_width, help_tbl[i].token);
	for (i = 1; i < tbl_len; i++) {
		printf("%-*s", max_width, help_tbl[i].token);
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

		struct command *help_cmd;
		help_cmd = bsearch(&_args, help_tbl, ARRAY_SIZE(help_tbl),
			     sizeof(*help_cmd), command_comparator);
		if (!help_cmd) {
			printf("Unrecognized command '%s'.\n", _args);
			return EINVAL;
		}
		if (help_cmd->help) {
			help_cmd->help();
		} else {
			printf("No help found for the command '%s'.\n",
					help_cmd->token);
		}
		return 0;
	}

	return 0;
}

static int __send_cmd(struct ctrl *ctrl, ldmsd_req_hdr_t req,
					char *data, size_t data_len)
{
	struct msghdr reply;
	struct iovec iov[2];

	reply.msg_name = 0;
	reply.msg_namelen = 0;
	iov[0].iov_base = req;
	iov[0].iov_len = sizeof(*req);
	iov[1].iov_base = data;
	iov[1].iov_len = data_len;
	reply.msg_iov = iov;
	reply.msg_iovlen = 2;
	reply.msg_control = NULL;
	reply.msg_controllen = 0;
	reply.msg_flags = 0;

	if (sendmsg(ctrl->sock, &reply, 0) < 0)
		return errno;
	return 0;
}

static char * __recv_resp(struct ctrl *ctrl)
{
	struct msghdr msg;
	struct iovec iov;
	struct sockaddr_storage ss;

	struct ldmsd_req_hdr_s req_resp;
	ssize_t msglen;
	ss.ss_family = AF_UNIX;
	msg.msg_name = &ss;
	msg.msg_namelen = sizeof(ss);
	iov.iov_base = &req_resp;
	iov.iov_len = sizeof(req_resp);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;
	msglen = recvmsg(ctrl->sock, &msg, MSG_PEEK);
	if (msglen <= 0)
		return NULL;

	if (buffer_len < req_resp.rec_len) {
		free(buffer);
		buffer = malloc(req_resp.rec_len);
		if (!buffer) {
			printf("Out of memory\n");
			exit(ENOMEM);

		}
		buffer_len = req_resp.rec_len;
	}
	memset(buffer, 0, buffer_len);
	iov.iov_base = buffer;
	iov.iov_len = req_resp.rec_len;

	msglen = recvmsg(ctrl->sock, &msg, MSG_WAITALL);
	if (msglen < req_resp.rec_len)
		return NULL;

	return buffer;
}

static int __handle_cmd(char *cmd, struct ctrl *ctrl)
{
	static int msg_no = 0;
	struct ldmsd_req_hdr_s request, response;
	int rc;

	struct command key, *action_cmd;
	char *ptr, *args, *dummy;
	int cmd_id;

	/* Strip the new-line character */
	char *newline = strrchr(cmd, '\n');
	if (newline)
		*newline = '\0';

	dummy = strdup(cmd);
	if (!dummy) {
		printf("Out of memory\n");
		exit(ENOMEM);
	}

	key.token = strtok_r(dummy, " \t\n", &ptr);
	args = strtok_r(NULL, "\n", &ptr);
	action_cmd = bsearch(&key, action_tbl, ARRAY_SIZE(action_tbl),
			sizeof(struct command), command_comparator);
	if (action_cmd) {
		(void)action_cmd->action(args);
		free(dummy);
		return 0;
	}
	free(dummy);

	size_t buffer_offset = 0;
	memset(buffer, 0, buffer_len);
	rc = ldmsd_process_cfg_str(&request, cmd, &buffer, &buffer_offset,
							&buffer_len);
	if (rc) {
		if (rc == ENOMEM) {
			printf("Out of memory\n");
			return rc; /* Return the error to exit the program */
		} else if (rc == ENOSYS) {
			printf("Command not found\n");
		} else {
			printf("Invalid configuration\n");
		}
		return 0;
	}
	request.flags = LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F;
	request.msg_no = msg_no;
	msg_no++;

	rc = __send_cmd(ctrl, &request, buffer, buffer_offset);
	if (rc) {
		printf("Failed to send data to ldmsd. %s\n", strerror(errno));
		return rc;
	}
	char *resp = __recv_resp(ctrl);
	if (!resp) {
		printf("Failed to receive the response\n");
		return -1;
	}
	memcpy(&response, resp, sizeof(response));
	char *msg = resp + sizeof(response);

	if (response.code != 0) {
		printf("%s\n", msg);
	} else {
		if (request.code == LDMSD_PRDCR_STATUS_REQ ||
				request.code == LDMSD_DAEMON_STATUS_REQ ||
				request.code == LDMSD_UPDTR_STATUS_REQ ||
				request.code == LDMSD_STRGP_STATUS_REQ) {
			printf("%s\n", msg);
		}
	}
	return 0;
}

struct ctrl *__unix_domain_ctrl(char *my_name, char *sockname)
{
	int rc;
	struct sockaddr_un my_un, lcl_addr, rem_addr;
	char *mn;
	char *sockpath;
	char *_sockname = NULL;
	struct ctrl *ctrl;

	ctrl = calloc(1, sizeof *ctrl);
	if (!ctrl)
		return NULL;

	if (sockname[0] == '/') {
		_sockname = strdup(sockname);
		if (!_sockname)
			goto err;
		sockpath = dirname(_sockname);
		strcpy(my_un.sun_path, sockname);
	} else {
		sockpath = getenv(LDMSD_SOCKPATH_ENV);
		if (!sockpath)
			sockpath = "/var/run";
		sprintf(my_un.sun_path, "%s/%s", sockpath, sockname);
	}

	rem_addr.sun_family = AF_UNIX;
	strncpy(rem_addr.sun_path, my_un.sun_path,
		sizeof(struct sockaddr_un) - sizeof(short));

	/* Create control socket */
	ctrl->sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (ctrl->sock < 0)
		goto err;

	pid_t pid = getpid();
	lcl_addr.sun_family = AF_UNIX;

	mn = strdup(my_name);
	if (!mn)
		goto err;
	sprintf(my_un.sun_path, "%s/%s", sockpath, basename(mn));
	free(mn);

	rc = mkdir(my_un.sun_path, 0755);
	if (rc < 0) {
		/* Ignore the error if the path already exists */
		if (errno != EEXIST) {
			printf("Error creating '%s: %s\n", my_un.sun_path,
							strerror(errno));
			close(ctrl->sock);
			goto err;
		}
	}

	sprintf(lcl_addr.sun_path, "%s/%d", my_un.sun_path, pid);

	rc = connect(ctrl->sock, (struct sockaddr *)&rem_addr,
			  sizeof(struct sockaddr_un));
	if (rc < 0) {
		printf("Error creating '%s'\n", rem_addr.sun_path);
		close(ctrl->sock);
		goto err;
	}
	ctrl->sa = (struct sockaddr *)&rem_addr;
	ctrl->sa_len = sizeof(rem_addr);
	if (_sockname)
		free(_sockname);
	return ctrl;
 err:
	free(ctrl);
	if (_sockname)
		free(_sockname);
	return NULL;
}

int __inet_ctrl(const char *host, const char *port, const char *secretword_path)
{
	return ENOSYS;
}

int main(int argc, char *argv[])
{
	int op;
	char *host, *port, *secretword_path, *sockname, *env, *s;
	host = port = secretword_path = sockname = s = NULL;
	int rc, is_inet = 0;

	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'S':
			sockname = strdup(optarg);
			break;
		default:
			usage(argv);
		}
	}

	if (!sockname) {
		printf("Please provide the UNIX Domain socket path ('-S')\n");
		usage(argv);
		return 0;
	}

	env = getenv("LDMSD_MAX_CONFIG_STR_LEN");
	if (env)
		linebuf_len = buffer_len = strtol(env, NULL, 0);
	else
		linebuf_len = buffer_len = LDMSD_MAX_CONFIG_STR_LEN;
	buffer = malloc(buffer_len);
	linebuf = malloc(buffer_len);
	if (!buffer || !linebuf) {
		printf("Out of memory\n");
		exit(ENOMEM);
	}
	struct ctrl *ctrl;
	if (sockname) {
		ctrl = __unix_domain_ctrl(basename(argv[0]), sockname);
		if (!ctrl) {
			printf("Failed to connect to ldmsd.\n");
			exit(-1);
		}
	} else {
		if (!host) {
			printf("Please give '-h'.\n");
			usage(argv);
			return 0;
		}
		if (!port) {
			printf("please give '-p'.\n");
			usage(argv);
			return 0;
		}
		is_inet = 1;
		rc = __inet_ctrl(host, port, secretword_path);
		if (rc) {
			printf("Failed to connect to ldmsd.\n");
			exit(rc);
		}
	}

	do {
#ifdef HAVE_LIBREADLINE
#ifndef HAVE_READLINE_HISTORY
		if (s != NULL)
			free(s); /* previous readline output must be freed if not in history */
#endif /* HAVE_READLINE_HISTORY */
		if (isatty(0))
			s = readline("ldmsctl> ");
		else
			s = fgets(linebuf, linebuf_len, stdin);
#else /* HAVE_LIBREADLINE */
		if (isatty(0)) {
			fputs("ldmsctl> ", stdout);
		}
		s = fgets(linebuf, linebuf_len, stdin);
#endif /* HAVE_LIBREADLINE */
		if (!s)
			break;
		if (s[0] == '\0')
			continue;
#ifdef HAVE_READLINE_HISTORY
		add_history(s);
#endif /* HAVE_READLINE_HISTORY */

		rc = __handle_cmd(s, ctrl);
		if (rc)
			break;
	} while (s);
	return 0;
}
