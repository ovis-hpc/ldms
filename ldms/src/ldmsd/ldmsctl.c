/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2011-2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2011-2020 Open Grid Computing, Inc. All rights reserved.
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
#include <netdb.h>
#include <time.h>
#include <ctype.h>
#include "json/json_util.h"
#include "ldms.h"
#include "ldmsd_request.h"
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

#define FMT "h:p:a:A:S:x:s:X:i"
#define ARRAY_SIZE(a)  (sizeof(a) / sizeof(a[0]))

#define LDMSD_SOCKPATH_ENV "LDMSD_SOCKPATH"

static char *linebuf;
static size_t linebuf_len;
static char *buffer;
static size_t buffer_len;

struct ldmsctl_ctrl {
	ldms_t x;

	uint32_t msg_no;
	ldmsd_req_buf_t send_buf;
	ldmsd_req_buf_t recv_buf;

	json_entity_t resp_obj;

	sem_t connected_sem;
	sem_t recv_sem;
};

struct command {
	char *token;
	int (*action)(struct ldmsctl_ctrl *ctrl, char *args);
	json_entity_t (*req_get)(char *args);
	void (*help)();
	void (*resp)(json_entity_t resp_obj);
};

static int command_comparator(const void *a, const void *b)
{
	struct command *_a = (struct command *)a;
	struct command *_b = (struct command *)b;
	return strcmp(_a->token, _b->token);
}

static void usage(char *argv[])
{
	printf("%s: [%s]\n"
	       "    -h <host>       Hostname of ldmsd to connect to.\n"
	       "    -p <port>       LDMS daemon listener port to connect to.\n"
	       "    -x <xprt>       Transports one of sock, ugni, and rdma.\n"
	       "    -a              Authentication plugin (default: 'none').\n"
	       "    -A <K>=<VAL>    Authentication plugin options (repeatable).\n"
	       "    -s <source>     Path to a configuration file\n"
	       "    -X <script>     Path to a script file that generates a configuration file\n"
	       "DEPRECATED OPTIONS:\n"
	       "    -S <socket>     **DEPRECATED** The UNIX socket that the ldms daemon is listening on.\n"
	       "                    [" LDMSD_CONTROL_SOCKNAME "].\n"
	       "    -i              **DEPRECATED** Specify to connect to the data channel\n"
	       ,
	       argv[0], FMT);
	exit(0);
}

/* Caller must free the returned string. */
char *ldmsctl_ts_str(uint32_t sec, uint32_t usec)
{
	struct tm *tm;
	char dtsz[200];
	char *str = malloc(200);
	if (!str)
		return NULL;
	time_t t = sec;
	tm = localtime(&t);
	strftime(dtsz, sizeof(dtsz), "%D %H:%M:%S %z", tm);
	snprintf(str, 200, "%s [%dus]", dtsz, usec);
	return str;
}

static void ldmsctl_ctrl_free(struct ldmsctl_ctrl *ctrl)
{
	if (ctrl->send_buf)
		ldmsd_req_buf_free(ctrl->send_buf);
	if (ctrl->recv_buf)
		ldmsd_req_buf_free(ctrl->recv_buf);
	sem_destroy(&ctrl->connected_sem);
	free(ctrl);
}

static int handle_record(struct ldmsctl_ctrl *ctrl, ldmsd_rec_hdr_t rec)
{
	char *str = (char *)(rec + 1);
	size_t str_len;
	ldmsd_ntoh_rec_hdr(rec);

	/*
	 * ldmsctl does not handle any requests.
	 */
	assert(LDMSD_MSG_TYPE_RESP == rec->type);
	/*
	 * ldmsctl sends requests one-by-one. It won't send another request
	 * until it has received the response of the outstanding request.
	 */
	assert(ctrl->msg_no == rec->msg_no);

	str_len = rec->rec_len - sizeof(*rec);
	if (ctrl->recv_buf->len - ctrl->recv_buf->off < str_len) {
		ctrl->recv_buf = ldmsd_req_buf_realloc(ctrl->recv_buf,
					(ctrl->recv_buf->len * 2) + str_len);
		if (!ctrl->recv_buf) {
			printf("Out of memory\n");
			return ENOMEM;
		}
	}
	memcpy(&ctrl->recv_buf->buf[ctrl->recv_buf->off], str, str_len);
	ctrl->recv_buf->off += str_len;

	if (!(rec->flags & LDMSD_REC_EOM_F))
		return 0;

	json_parser_t parser;
	int rc;
	parser = json_parser_new(0);
	if (!parser) {
		printf("Out of memory\n");
		return ENOMEM;
	}
	rc = json_parse_buffer(parser, ctrl->recv_buf->buf,
				ctrl->recv_buf->off, &ctrl->resp_obj);
	json_parser_free(parser);
	if (rc) {
		printf("Failed to parse the response of the request.\n");
		return rc;
	}
	return 0;
}

static void __ldms_event_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	ldmsd_rec_hdr_t rec;
	struct ldmsctl_ctrl *ctrl = cb_arg;
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		sem_post(&ctrl->connected_sem);
		break;
	case LDMS_XPRT_EVENT_REJECTED:
		printf("The connected request is rejected.\n");
		ldms_xprt_put(ctrl->x);
		exit(0);
	case LDMS_XPRT_EVENT_DISCONNECTED:
		ldms_xprt_put(ctrl->x);
		ldmsctl_ctrl_free(ctrl);
		printf("The connection is disconnected.\n");
		exit(0);
	case LDMS_XPRT_EVENT_ERROR:
		printf("Connection error\n");
		exit(0);
		break;
	case LDMS_XPRT_EVENT_RECV:
		rec = (ldmsd_rec_hdr_t)e->data;
		handle_record(ctrl, rec);
		if (LDMSD_REC_EOM_F & rec->flags)
			sem_post(&ctrl->recv_sem);
		break;
	default:
		assert(0);
	}
}

static struct ldmsctl_ctrl *ldmsctl_ctrl_new(const char *host, const char *port,
			const char *xprt, const char *auth,
			struct attr_value_list *auth_opt)
{
	struct ldmsctl_ctrl *ctrl;
	int rc;

	ctrl = calloc(1, sizeof(*ctrl));
	if (!ctrl)
		goto oom;
	ctrl->msg_no = 1;

	sem_init(&ctrl->connected_sem, 0, 0);
	sem_init(&ctrl->recv_sem, 0, 0);

	ctrl->x = ldms_xprt_new_with_auth(xprt, NULL, auth, auth_opt);
	if (!ctrl->x) {
		printf("Failed to create an ldms transport. %s\n",
						strerror(errno));
		return NULL;
	}

	ctrl->send_buf = ldmsd_req_buf_alloc(ldms_xprt_msg_max(ctrl->x));
	if (!ctrl->send_buf)
		goto oom;
	ctrl->recv_buf = ldmsd_req_buf_alloc(ldms_xprt_msg_max(ctrl->x));
	if (!ctrl->recv_buf)
		goto oom;

	rc = ldms_xprt_connect_by_name(ctrl->x, host, port, __ldms_event_cb, ctrl);
	if (rc) {
		ldms_xprt_put(ctrl->x);
		goto err;
	}

	sem_wait(&ctrl->connected_sem);
	return ctrl;
oom:
	printf("Out of memory\n");
	errno = ENOMEM;
err:
	if (ctrl)
		ldmsctl_ctrl_free(ctrl);
	return NULL;
}

static int ldmsctl_ctrl_send(void *xprt, char *data, size_t data_len)
{
	return ldms_xprt_send((ldms_t)xprt, data, data_len);
}

static int
ldmsctl_send_request(struct ldmsctl_ctrl *ctrl, uint32_t msg_no,
			int msg_type, char *data, size_t data_len)
{
	int rc;
	struct ldmsd_msg_key msg_key = {.msg_no = msg_no};

	rc = ldmsd_append_msg_buffer((void *)ctrl->x,
					ldms_xprt_msg_max(ctrl->x),
					&msg_key, ldmsctl_ctrl_send, ctrl->send_buf,
					LDMSD_REC_SOM_F | LDMSD_REC_EOM_F,
					msg_type, data, data_len);
	return rc;
}

static int __handle_cmd(struct ldmsctl_ctrl *ctrl, char *cmd_str);

static json_entity_t json_req(char *args)
{
	json_parser_t p;
	json_entity_t j;
	int rc;

	p = json_parser_new(0);
	if (!p)
		return NULL;
	rc = json_parse_buffer(p, args, strlen(args), &j);
	json_parser_free(p);
	if (rc) {
		errno = rc;
		return NULL;
	}
	return j;
}

static void help_json()
{
	printf( "\nSend the string of a JSON object\n\n"
		"Example\n"
		"   json {\"request\" : \"query\", \"id\" : 1, \"schema\" : \"daemon\" }\n");
}

static void resp_json(json_entity_t resp_obj)
{
	jbuf_t jb;
	jb = json_entity_dump(NULL, resp_obj);
	if (!jb) {
		printf("Failed to dump the response JSON object\n");
	} else {
		printf("%s\n", jb->buf);
	}
}

static int handle_quit(struct ldmsctl_ctrl *ctrl, char *kw)
{
	printf("bye ... :)\n");
	ldms_xprt_close(ctrl->x);
	return 0;
}

static void help_script()
{
	printf("\nExecute the command and send the output to ldmsd.\n\n"
	       "  script <CMD>   CMD is the command to be executed.\n");
}

static void help_source()
{
	printf("\nSend the commands in the configuration file to the ldmsd\n\n"
	       "  source <PATH>  PATH is the path to the configuration file.\n");
}

static void help_daemon_exit()
{
	printf(" \nExit the connected LDMS daemon\n\n");
}

static json_entity_t daemon_exit_req(char *args)
{
	json_entity_t spec;
	spec = json_dict_build(NULL, JSON_STRING_VALUE, "startup", "", -1);
	if (!spec) {
		printf("Out of memory\n");
		return NULL;
	}
	return ldmsd_cfgobj_update_req_obj_new(-1, "daemon", 0, NULL, NULL, spec);
}

static void help_quit()
{
	printf( "\nquit\n"
		"   - Exit.\n");
}

static void resp_generic(json_entity_t resp_obj)
{
	int errcode = json_value_int(json_value_find(resp_obj, "status"));
	json_entity_t msg;
	char *msg_str;
	if (errcode) {
		msg = json_value_find(resp_obj, "msg");
		if (!msg) {
			printf("LDMSD failed to handle the request "
					"with the error code %d\n", errcode);
		} else {
			msg_str = json_value_str(msg)->str;
			printf("%s\n", msg_str);
		}
	}
}

static void resp_daemon_exit(json_entity_t resp_obj)
{
	json_entity_t msg;
	int status = json_value_int(json_value_find(resp_obj, "status"));
	if (status) {
		printf("Error %d.", status);
		msg = json_value_find(resp_obj, "msg");
		if (msg)
			printf(" %s", json_value_str(msg)->str);
		printf("\n");
	} else {
		printf("Please 'quit' the ldmsd_controller interface\n");
	}
}

static json_entity_t version_req(char *args)
{
	return ldmsd_req_obj_new("version");
}

static void help_version()
{
	printf( "\nGet the LDMS version.\n");
}

static void resp_version(json_entity_t resp)
{
	json_entity_t result, msg;

	if (json_value_int(json_value_find(resp, "status"))) {
		msg = json_value_find(resp, "msg");
		if (msg) {
			printf("%s\n", json_value_str(msg)->str);
			return;
		}
	}

	result = json_value_find(resp, "result");
	printf("LDMS Version: %s\n",
		json_value_str(json_value_find(result, "LDMS Version"))->str);
	printf("LDMSD Version: %s\n",
		json_value_str(json_value_find(result, "LDMSD Version"))->str);
}

static json_entity_t set_route_req(char *args)
{
	json_entity_t req;
	req = ldmsd_req_obj_new("set_route");
	if (!req)
		return NULL;
	if (args) {
		req = json_dict_build(req,
				JSON_DICT_VALUE, "spec",
					JSON_STRING_VALUE, "instance", args,
					-2,
				-1);
	}
	return req;
}

static void help_set_route()
{
	printf( "\nset_route SET_INSTANCE_NAME\n"
		"Display the route of the set from aggregators to the sampler daemon.\n");
}

static void resp_set_route(json_entity_t rsp)
{
	json_entity_t msg, result, a, hop, name, pi, prdcr, type;
	int status = json_value_int(json_value_find(rsp, "status"));

	if (status) {
		msg = json_value_find(rsp, "msg");
		if (msg)
			printf("%s\n", json_value_str(msg)->str);
	}
	result = json_value_find(rsp, "result");
	if (!result)
		return;

	printf("-------------------\n");
	printf("instance: %s\n", json_value_str(json_value_find(result, "instance"))->str);
	if (json_value_find(result, "schema")) {
		printf("schema: %s\n", json_value_str(json_value_find(result, "schema"))->str);
	}
	printf("===================\n");
	printf("%20s %15s %25s %20s\n",
			"LDMSD name", "Type", "Plugin Instance Name", "Producer Name");

	json_attr_rem(result,  "instance");
	(void)json_attr_rem(result, "schema");

	for (a = json_attr_first(result); a; a = json_attr_next(a)) {
		hop = json_attr_value(a);
		msg = json_value_find(hop, "msg");
		name = json_value_find(hop, "name");
		type = json_value_find(hop, "type");
		pi = json_value_find(hop, "plugin");
		prdcr = json_value_find(hop, "producer");
		if (msg) {
			printf("%20s -- %s --\n",
					json_value_str(name)->str,
					json_value_str(msg)->str);
			continue;
		}

		printf("%20s %15s %25s %20s\n",
			json_value_str(name)->str,
			json_value_str(type)->str,
			((pi)?(json_value_str(pi)->str):""),
			((prdcr)?(json_value_str(prdcr)->str):""));
	}
}

static int handle_help(struct ldmsctl_ctrl *ctrl, char *args);
static int handle_source(struct ldmsctl_ctrl *ctrl, char *path);
static int handle_script(struct ldmsctl_ctrl *ctrl, char *cmd);
static int handle_notify(struct ldmsctl_ctrl *ctrl, char *data);
static void help_notify();

static struct command command_tbl[] = {
	{ "?",			handle_help,		NULL,
				NULL,			NULL },
	{ "daemon_exit",	NULL,			daemon_exit_req,
				help_daemon_exit,	resp_daemon_exit },
	{ "help",		handle_help,		NULL,
				NULL,			NULL },
	{ "json",		NULL,			json_req,
				help_json,		resp_json },
	{ "notify",		handle_notify,		NULL,
				help_notify,		NULL },
	{ "quit",		handle_quit,		NULL,
				help_quit,		resp_generic },
	{ "script",		handle_script,		NULL,
				help_script,		resp_generic },
	{ "set_route",		NULL,			set_route_req,
				help_set_route,		resp_set_route },
	{ "source",		handle_source,		NULL,
				help_source,		resp_generic },
	{ "version",		NULL,			version_req,
				help_version,		resp_version },
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

static int handle_help(struct ldmsctl_ctrl *ctrl, char *args)
{
	if (!args) {
		__print_all_command();
	} else {
		struct command *help_cmd;
		help_cmd = bsearch(args, command_tbl, ARRAY_SIZE(command_tbl),
			     sizeof(*help_cmd), command_comparator);
		if (!help_cmd) {
			printf("Unrecognized command '%s'.\n", args);
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

static uint32_t ldmsctl_msg_no_get()
{
	static uint32_t msg_no = 1;
	return msg_no++;
}

static int handle_notify(struct ldmsctl_ctrl *ctrl, char *args)
{
	int rc = 0;
	ctrl->msg_no = ldmsctl_msg_no_get();
	rc = ldmsctl_send_request(ctrl, ctrl->msg_no, LDMSD_MSG_TYPE_NOTIFY,
							args, strlen(args) + 1);
	if (rc)
		printf("Failed to send data to ldmsd. %s\n", strerror(rc));
	/* LDMSD won't send any response back. */
	return rc;
}

static void help_notify()
{
	printf( "\nnotify JSON_NOTIFICATION_OBJECT\n"
		"Send the given notification message to the LDMSD.\n");
}

static int __handle_cmd(struct ldmsctl_ctrl *ctrl, char *args)
{
	int rc = 0;
	struct command key, *cmd;
	char *dummy, *data, *ptr;
	json_entity_t req_obj  = NULL;
	jbuf_t jb = NULL;

	dummy = strdup(args);
	if (!dummy)
		return ENOMEM;
	key.token = strtok_r(dummy, " \t\n", &ptr);
	data = strtok_r(NULL, "\n", &ptr);
	cmd = bsearch(&key, command_tbl, ARRAY_SIZE(command_tbl),
			sizeof(struct command), command_comparator);
	if (!cmd) {
		printf("Unrecognized command '%s'\n", key.token);
		goto out;
	}

	if (cmd->action) {
		(void)cmd->action(ctrl, data);
		goto out;
	}

	assert(cmd->req_get);
	ctrl->msg_no = ldmsctl_msg_no_get();
	req_obj = cmd->req_get(data);
	if (!req_obj) {
		if (ENOMEM == errno) {
			printf("Out of memory\n");
			exit(-1);
		} else {
			printf("Error %d: failed to process the command.\n" ,errno);
			goto out;
		}
	}
	json_attr_mod(req_obj, "id", ctrl->msg_no);
	jb = json_entity_dump(NULL, req_obj);
	if (!jb) {
		printf("Out of memory\n");
		goto out;
	}

	rc = ldmsctl_send_request(ctrl, ctrl->msg_no, LDMSD_MSG_TYPE_REQ,
							jb->buf, jb->cursor);
	/*
	 * Reuse the send_buf for the next command.
	 */
	ldmsd_req_buf_reset(ctrl->send_buf);
	if (rc) {
		printf("Failed to send data to ldmsd. %s\n", strerror(rc));
		goto out;
	}
	jbuf_free(jb);
	json_entity_free(req_obj);

	/* Get the response json object*/
	sem_wait(&ctrl->recv_sem);
	cmd->resp(ctrl->resp_obj);
	ldmsd_req_buf_reset(ctrl->recv_buf);
	json_entity_free(ctrl->resp_obj);
out:
	free(dummy);
	return rc;
}

static int handle_source(struct ldmsctl_ctrl *ctrl, char *path)
{
	FILE *f;
	int rc = 0;
	ssize_t cnt = 0;

	f = fopen(path, "r");
	if (!f) {
		rc = errno;
		printf("Error %d: Failed to open the configuration file '%s'\n",
								rc, path);
		return rc;
	}
	fseek(f, 0, SEEK_SET);
	cnt = getline(&linebuf, &linebuf_len, f);
	while (cnt != -1) {
		rc = __handle_cmd(ctrl, linebuf);
		if (rc)
			break;
		cnt = getline(&linebuf, &linebuf_len, f);
	}
	fclose(f);
	return rc;
}

static int handle_script(struct ldmsctl_ctrl *ctrl, char *cmd)
{
	int rc = 0;
	FILE *f;
	ssize_t cnt;

	f = popen(cmd, "r");
	if (!f) {
		rc = errno;
		printf("Error %d: Failed to open pipe of the command '%s'\n",
								rc, cmd);

		return rc;
	}

	cnt = getline(&linebuf, &linebuf_len, f);
	while (cnt != -1) {
		rc = __handle_cmd(ctrl, linebuf);
		if (rc)
			break;
		cnt = getline(&linebuf, &linebuf_len, f);
	}
	pclose(f);
	return rc;
}

int main(int argc, char *argv[])
{
	int op;
	char *host, *port, *auth, *sockname, *xprt;
	char *lval, *rval;
	host = port = sockname = xprt = NULL;
	char *source, *script;
	source = script = NULL;
	int rc;
	struct attr_value_list *auth_opt = NULL;
	const int AUTH_OPT_MAX = 128;
	ssize_t cnt;

	auth = "none";

	auth_opt = av_new(AUTH_OPT_MAX);
	if (!auth_opt) {
		printf("ERROR: Not enough memory.\n");
		exit(1);
	}

	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'h':
			host = strdup(optarg);
			break;
		case 'p':
			port = strdup(optarg);
			break;
		case 'x':
			xprt = strdup(optarg);
			break;
		case 'a':
			auth = strdup(optarg);
			break;
		case 'A':
			/* (multiple) auth options */
			lval = strtok(optarg, "=");
			if (!lval) {
				printf("ERROR: Expecting -A name=value\n");
				exit(1);
			}
			rval = strtok(NULL, "");
			if (!rval) {
				printf("ERROR: Expecting -A name=value\n");
				exit(1);
			}
			if (auth_opt->count == auth_opt->size) {
				printf("ERROR: Too many auth options\n");
				exit(1);
			}
			auth_opt->list[auth_opt->count].name = lval;
			auth_opt->list[auth_opt->count].value = rval;
			auth_opt->count++;
			break;
		case 's':
			source = strdup(optarg);
			break;
		case 'X':
			script = strdup(optarg);
			break;
		default:
			usage(argv);
			exit(0);
		}
	}

	buffer_len = LDMSD_CFG_FILE_XPRT_MAX_REC;
	buffer = malloc(buffer_len);
	linebuf = NULL;
	linebuf_len = 0;
	if (!buffer) {
		printf("Out of memory\n");
		exit(ENOMEM);
	}

	if (!host || !port || !xprt)
		goto arg_err;

	struct ldmsctl_ctrl *ctrl;
	ctrl = ldmsctl_ctrl_new(host, port, xprt, auth, auth_opt);
	if (!ctrl) {
		printf("Failed to connect to ldmsd.\n");
		exit(-1);
	}
	/* At this point ldmsctl is connected to the ldmsd */
	if (source) {
		(void) handle_source(ctrl, source);
		return 0;
	}

	if (script) {
		(void) handle_script(ctrl, script);
		return 0;
	}

	do {
#ifdef HAVE_LIBREADLINE
#ifndef HAVE_READLINE_HISTORY
		if (linebuf != NULL) {
			free(linebuf)); /* previous readline output must be freed if not in history */
			linebuf = NULL;
			linebuf_len = 0;
		}
#endif /* HAVE_READLINE_HISTORY */
		if (isatty(0)) {
			linebuf = readline("ldmsctl> ");
			cnt = linebuf?strlen(linebuf):-1;
		} else {
			cnt = getline(&linebuf, &linebuf_len, stdin);
		}
#else /* HAVE_LIBREADLINE */
		if (isatty(0)) {
			fputs("ldmsctl> ", stdout);
		}
		cnt = getline(&linebuf, &linebuf_len, stdin);
#endif /* HAVE_LIBREADLINE */
		if (cnt == -1)
			break;
		if (linebuf && linebuf[0] == '\0')
			continue;
#ifdef HAVE_READLINE_HISTORY
		add_history(linebuf);
#endif /* HAVE_READLINE_HISTORY */

		rc = __handle_cmd(ctrl, linebuf);
		if (rc) {
			if (isatty(0)) {
				/* Not exit in an interactive session */
				continue;
			} else {
				break;
			}
		}
	} while (linebuf);

	return 0;
arg_err:
	printf("Please specify the host, port and transport type.\n");
	usage(argv);
	return 0;
}
