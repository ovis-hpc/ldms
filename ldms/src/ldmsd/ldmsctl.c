/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2011-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2011-2018 Open Grid Computing, Inc. All rights reserved.
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
#include "json_parser/json.h"
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

struct ldmsctl_ctrl;
typedef int (*ctrl_send_fn_t)(struct ldmsctl_ctrl *ctrl, ldmsd_req_hdr_t req, size_t len);
typedef char *(*ctrl_recv_fn_t)(struct ldmsctl_ctrl *ctrl);
typedef void (*ctrl_close_fn_t)(struct ldmsctl_ctrl *ctrl);
struct ldmsctl_ctrl {
	union {
		struct ldmsctl_sock {
			int sock;
		} sock;
		struct ldmsctl_ldms_xprt {
			ldms_t x;
			sem_t connected_sem;
			sem_t recv_sem;
		} ldms_xprt;
	};
	ctrl_send_fn_t send_req;
	ctrl_recv_fn_t recv_resp;
	ctrl_close_fn_t close;
};

struct command {
	char *token;
	int cmd_id;
	int (*action)(struct ldmsctl_ctrl *ctrl, char *args);
	void (*help)();
	void (*resp)(ldmsd_req_hdr_t resp, size_t len, uint32_t rsp_err);
};

static int command_comparator(const void *a, const void *b)
{
	struct command *_a = (struct command *)a;
	struct command *_b = (struct command *)b;
	return strcmp(_a->token, _b->token);
}

#define LDMSCTL_HELP LDMSD_NOTSUPPORT_REQ + 1
#define LDMSCTL_QUIT LDMSD_NOTSUPPORT_REQ + 2
#define LDMSCTL_SCRIPT LDMSD_NOTSUPPORT_REQ + 3
#define LDMSCTL_SOURCE LDMSD_NOTSUPPORT_REQ + 4

static void ldmsctl_log(enum ldmsd_loglevel level, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
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

static json_value *ldmsctl_json_value_get(json_value *json_obj, const char *name)
{
	int i;
	json_object_entry entry;
	if (json_obj->type != json_object)
		return NULL;
	for (i = 0; i < json_obj->u.object.length; i++) {
		entry = json_obj->u.object.values[i];
		if (0 == strcmp(entry.name, name))
			return entry.value;
	}
	return NULL;
}

static json_value *ldmsctl_json_array_ele_get(json_value *json_obj, int idx)
{
	if (json_obj->type != json_array)
		return NULL;
	if ((idx < 0) || (idx >= json_obj->u.array.length))
		return NULL;
	return json_obj->u.array.values[idx];
}

static char *ldmsctl_json_str_value_get(json_value *json_obj, const char *name)
{
	json_value *value = ldmsctl_json_value_get(json_obj, name);
	if (!value)
		return NULL;
	if (value->type != json_string)
		return NULL;
	return value->u.string.ptr;
}

static void help_greeting()
{
	printf("\nGreet ldmsd\n\n"
	       "Parameters:"
	       "     [name=]   The string ldmsd will echo back.\n"
	       "               If 'name' is not given, nothing will be returned\n"
	       "     [offset=] The response will contain <offset> characters\n"
	       "     [level=]  The response will consist of <level> + 1 records\n"
	       "     [test]    The response is 'Hi'\n"
	       "     [path]    The response is a string 'XXX:YYY:...:ZZZ',\n"
	       "               where 'XXX', 'YYY' and 'ZZZ' are myhostname of\n"
	       "               the first producer in the list of each daemon");
}

static void resp_greeting(ldmsd_req_hdr_t resp, size_t len, uint32_t rsp_err)
{
	ldmsd_req_attr_t attr = ldmsd_first_attr(resp);
	ldmsd_req_attr_t next_attr;
	int count = 0;
	while (attr->discrim) {
		next_attr = ldmsd_next_attr(attr);
		if ((0 == next_attr->discrim) && (count == 0)) {
			char *str = strdup(attr->attr_value);
			char *tok = strtok(str, " ");
			if (!isdigit(tok[0])) {
				/* The attribute 'level' isn't used. */
				printf("%s\n", attr->attr_value);
			} else {
				printf("%s\n", str);
			}
			free(str);
		} else {
			printf("%s\n", strtok(attr->attr_value, " "));
		}
		attr = next_attr;
		count++;
	}
}

static int handle_quit(struct ldmsctl_ctrl *ctrl, char *kw)
{
	printf("bye ... :)\n");
	ctrl->close(ctrl);
	exit(0);
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

static void resp_usage(ldmsd_req_hdr_t resp, size_t len, uint32_t rsp_err)
{
	ldmsd_req_attr_t attr = ldmsd_first_attr(resp);
	if (attr->discrim && (attr->attr_id == LDMSD_ATTR_STRING))
		printf("%s\n", attr->attr_value);
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

static void resp_generic(ldmsd_req_hdr_t resp, size_t len, uint32_t rsp_err)
{
	ldmsd_req_attr_t attr;
	if (rsp_err) {
		attr = ldmsd_first_attr(resp);
		if (attr->discrim && (attr->attr_id == LDMSD_ATTR_STRING))
			printf("%s\n", attr->attr_value);
	}
}

void __print_prdcr_status(json_value *jvalue)
{
	if (jvalue->type != json_object) {
		printf("Invalid producer status format\n");
		return;
	}

	char *name, *host, *xprt, *state;
	json_int_t port;

	name = ldmsctl_json_str_value_get(jvalue, "name");
	host = ldmsctl_json_str_value_get(jvalue, "host");
	port = ldmsctl_json_value_get(jvalue, "port")->u.integer;
	xprt = ldmsctl_json_str_value_get(jvalue, "transport");
	state = ldmsctl_json_str_value_get(jvalue, "state");

	printf("%-16s %-16s %-12" PRId64 "%-12s %-12s\n", name, host, port, xprt, state);

	json_value *prd_set_array_jvalue;
	prd_set_array_jvalue = ldmsctl_json_value_get(jvalue, "sets");
	if (prd_set_array_jvalue->type != json_array) {
		printf("---Invalid producer status format---\n");
		return;
	}

	char *inst_name, *schema_name, *set_state;
	json_value *prd_set_jvalue;
	int i;
	for (i = 0; i < prd_set_array_jvalue->u.array.length; i++) {
		prd_set_jvalue = ldmsctl_json_array_ele_get(prd_set_array_jvalue, i);
		if (prd_set_jvalue->type != json_object) {
			printf("---Invalid producer status format---\n");
			return;
		}
		inst_name = ldmsctl_json_str_value_get(prd_set_jvalue, "inst_name");
		schema_name = ldmsctl_json_str_value_get(prd_set_jvalue, "schema_name");
		set_state = ldmsctl_json_str_value_get(prd_set_jvalue, "state");

		printf("    %-16s %-16s %s\n", inst_name, schema_name, set_state);
	}
}

static void resp_prdcr_status(ldmsd_req_hdr_t resp, size_t len, uint32_t rsp_err)
{
	ldmsd_req_attr_t attr = ldmsd_first_attr(resp);
	if (!attr->discrim || (attr->attr_id != LDMSD_ATTR_JSON))
		return;

	json_value *json, *prdcr_json;
	json = json_parse((char*)attr->attr_value, len);
	if (!json)
		return;

	if (json->type != json_array) {
		printf("Unrecognized producer status format\n");
		return;
	}
	int i;

	printf("Name             Host             Port         Transport    State\n");
	printf("---------------- ---------------- ------------ ------------ ------------\n");

	for (i = 0; i < json->u.array.length; i++) {
		prdcr_json = ldmsctl_json_array_ele_get(json, i);
		__print_prdcr_status(prdcr_json);
	}
	json_value_free(json);
}

static void help_prdcr_status()
{
	printf( "\nGet status of all producers\n");
}

void __print_prdcr_set_status(json_value *jvalue)
{
	if (jvalue->type != json_object) {
		printf("---Invalid producer set status format---\n");
		return;
	}

	char *name, *schema, *state, *origin, *prdcr;
	char *ts_sec, *ts_usec, *dur_sec_str, *dur_usec_str;
	uint32_t dur_sec, dur_usec;

	name = ldmsctl_json_str_value_get(jvalue, "inst_name");
	schema = ldmsctl_json_str_value_get(jvalue, "schema_name");
	state = ldmsctl_json_str_value_get(jvalue, "state");
	origin = ldmsctl_json_str_value_get(jvalue, "origin");
	prdcr = ldmsctl_json_str_value_get(jvalue, "producer");
	ts_sec = ldmsctl_json_str_value_get(jvalue, "timestamp.sec");
	ts_usec = ldmsctl_json_str_value_get(jvalue, "timestamp.usec");
	dur_sec_str = ldmsctl_json_str_value_get(jvalue, "duration.sec");
	dur_usec_str = ldmsctl_json_str_value_get(jvalue, "duration.usec");
	if (dur_sec_str)
		dur_sec = strtoul(dur_sec_str, NULL, 0);
	else
		dur_sec = 0;
	if (dur_usec_str)
		dur_usec = strtoul(dur_usec_str, NULL, 0);
	else
		dur_usec = 0;

	char ts[64];
	char dur[64];
	snprintf(ts, 63, "%s [%s]", ts_sec, ts_usec);
	snprintf(dur, 63, "%" PRIu32 ".%06" PRIu32, dur_sec, dur_usec);

	printf("%-20s %-16s %-10s %-16s %-16s %-25s %-12s\n",
			name, schema, state, origin, prdcr, ts, dur);
}

static void resp_prdcr_set_status(ldmsd_req_hdr_t resp, size_t len, uint32_t rsp_err)
{
	ldmsd_req_attr_t attr = ldmsd_first_attr(resp);
	if (!attr->discrim || (attr->attr_id != LDMSD_ATTR_JSON))
		return;
	json_value *json, *prdcr_json;
	json = json_parse((char*)attr->attr_value, len);
	if (!json)
		return;

	if (json->type != json_array) {
		printf("Unrecognized producer set status format\n");
		return;
	}
	int i;

	printf("Name                 Schema Name      State      Origin           "
			"Producer         timestamp                 duration (sec)\n");
	printf("-------------------- ---------------- ---------- ---------------- "
			"---------------- ------------------------- ---------------\n");

	for (i = 0; i < json->u.array.length; i++) {
		prdcr_json = ldmsctl_json_array_ele_get(json, i);
		__print_prdcr_set_status(prdcr_json);
	}
	json_value_free(json);
}

static void help_prdcr_set_status()
{
	printf( "\nGet status of all producer sets\n");
}

static void __print_prdcr_hint_tree(json_value *prdcr_json)
{
	json_value *hints, *hint, *sets;
	char *name, *intrvl, *offset;
	int i, j;
	name = ldmsctl_json_str_value_get(prdcr_json, "name");
	printf("prdcr: %s\n", name);

	hints = ldmsctl_json_value_get(prdcr_json, "hints");
	if (hints->type != json_array) {
		printf("Unrecognized reply format\n");
		return;
	}
	for (i = 0; i < hints->u.array.length; i++) {
		hint = ldmsctl_json_array_ele_get(hints, i);
		intrvl = ldmsctl_json_str_value_get(hint, "interval_us");
		offset = ldmsctl_json_str_value_get(hint, "offset_us");
		sets = ldmsctl_json_value_get(hint, "sets");
		printf("   update hint: %s:%s\n", intrvl, offset);
		if (sets->type != json_array) {
			printf("Unrecognized reply format\n");
			return;
		}
		for (j = 0; j < sets->u.array.length; j++) {
			printf("     %s\n", sets->u.array.values[j]->u.string.ptr);
		}
	}
}

static void resp_prdcr_hint_tree(ldmsd_req_hdr_t resp, size_t len, uint32_t rsp_err)
{
	int i;

	if (0 != rsp_err) {
		resp_generic(resp, len, rsp_err);
		return;
	}

	ldmsd_req_attr_t attr = ldmsd_first_attr(resp);
	if (!attr->discrim || (attr->attr_id != LDMSD_ATTR_JSON)) {
		printf("Unrecognized reply format\n");
		return;
	}
	json_value *json, *prdcr_json;
	json = json_parse((char *)attr->attr_value, len);
	if (!json)
		return;
	if (json->type != json_array) {
		printf("Unrecognized reply format\n");
		return;
	}
	for (i = 0; i < json->u.array.length; i++) {
		prdcr_json = json->u.array.values[i];
		__print_prdcr_hint_tree(prdcr_json);
	}
	json_value_free(json);
}

static void help_prdcr_hint_tree()
{
	printf("\nPrint producer sets by the update hints\n"
	       "Parameters:\n"
	       "      [name=]     The producer name\n");
}

static void help_updtr_add()
{
	printf( "\nAdd an updater process that will periodically sample\n"
		"Producer metric sets\n\n"
		"Parameters:\n"
		"     name=       The update policy name\n"
		"     interval=   The update/collect interval\n"
		"     [offset=]   Offset for synchronized aggregation\n"
		"     [push=]     Push mode: 'onchange' and 'onpush'. 'onchange' means the\n"
		"                 Updater will get an update whenever the set source ends a\n"
		"                 transaction or pushes the update. 'onpush' means the Updater\n"
		"                 will receive an update only when the set source pushes the\n"
		"                 update. If `push` is used, `auto_interval` cannot be `true`.\n"
		"    [auto_interval=]   [true|false] If true, the updater will schedule\n"
		"                       set updates according to the update hint. The sets\n"
		"                       with no hints will not be updated. If false, the\n"
		"                       updater will schedule the set updates according to\n"
		"                       the given interval and offset values. If not\n"
		"                       specified, the value is `false`.\n"
		);

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
		"	      the expression will match the set's instance name, if\n"
		"	      match=schema, the expression will match the set's\n"
		"	      schema name.\n");
}

static void help_updtr_match_del()
{
	printf( "\nRemove a match condition that specifies the sets from an Updater policy.\n\n"
		"Parameters:\n"
		"     name=   The update policy name\n"
		"     regex=  The regular expression string\n"
		"     match=  The value with which to compare; if match=inst,\n"
		"	      the expression will match the set's instance name, if\n"
		"	      match=schema, the expression will match the set's\n"
		"	      schema name.\n");
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
		"     [offset=]   Offset for synchronization\n"
		"                 If 'interval' is given but not 'offset',\n"
		"                 the updater will update sets asynchronously.\n"
		"     [auto_interval=]   [true|false] If true, the updater will schedule\n"
		"                        set updates according to the update hint. If false,\n"
		"                        the updater will schedule the set updates according\n"
		"                        to the default schedule, i.e., the given interval and offset values.\n");
}

static void help_updtr_stop()
{
	printf( "\nStop an update policy. The Updater must be stopped in order to\n"
		"change it's configuration.\n\n"
		"Parameters:\n"
		"     name=   The update policy name\n");
}

void __print_updtr_status(json_value *jvalue)
{
	if (jvalue->type != json_object) {
		printf("Invalid updater status format\n");
		return;
	}

	char *name, *interval, *mode, *state;
	json_int_t offset;

	name = ldmsctl_json_str_value_get(jvalue, "name");;
	interval = ldmsctl_json_str_value_get(jvalue, "interval");;
	offset = ldmsctl_json_value_get(jvalue, "offset")->u.integer;
	mode = ldmsctl_json_str_value_get(jvalue, "mode");
	state = ldmsctl_json_str_value_get(jvalue, "state");
	printf("%-16s %-12s %-12" PRId64 "%-15s %s\n",
			name, interval, offset, mode, state);

	json_value *prd_array_jvalue;
	prd_array_jvalue = ldmsctl_json_value_get(jvalue, "producers");
	if (!prd_array_jvalue || (prd_array_jvalue->type != json_array)) {
		printf("---Invalid updater status format---\n");
		return;
	}

	char *prdcr_name, *host, *xprt, *prdcr_state;
	json_int_t port;
	json_value *prd_jvalue;
	int i;
	for (i = 0; i < prd_array_jvalue->u.array.length; i++) {
		prd_jvalue = ldmsctl_json_array_ele_get(prd_array_jvalue, i);
		if (prd_jvalue->type != json_object) {
			printf("---Invalid updater status format---\n");
			return;
		}
		prdcr_name = ldmsctl_json_str_value_get(prd_jvalue, "name");
		host = ldmsctl_json_str_value_get(prd_jvalue, "host");
		port = ldmsctl_json_value_get(prd_jvalue, "port")->u.integer;
		xprt = ldmsctl_json_str_value_get(prd_jvalue, "transport");
		prdcr_state = ldmsctl_json_str_value_get(prd_jvalue, "state");
		printf("    %-16s %-16s %-12" PRId64 "%-12s %s\n",
				prdcr_name, host, port, xprt, prdcr_state);
	}
}

static void resp_updtr_status(ldmsd_req_hdr_t resp, size_t len, uint32_t rsp_err)
{
	ldmsd_req_attr_t attr = ldmsd_first_attr(resp);
	if (!attr->discrim || (attr->attr_id != LDMSD_ATTR_JSON))
		return;
	json_value *json, *prdcr_json;
	json = json_parse((char*)attr->attr_value, len);
	if (!json)
		return;

	if (json->type != json_array) {
		printf("Unrecognized updater status format\n");
		return;
	}
	int i;

	printf("Name             Interval     Offset       Mode            State\n");
	printf("---------------- ------------ ------------ --------------- ------------\n");

	for (i = 0; i < json->u.array.length; i++) {
		prdcr_json = ldmsctl_json_array_ele_get(json, i);
		__print_updtr_status(prdcr_json);
	}
	json_value_free(json);
}

static void help_updtr_status()
{
	printf("\nGet the statuses of all Updaters\n"
	       "Parameters:\n"
	       "      None\n");
}

static void __print_updtr_task(json_value *updtr_json)
{
	json_value *tasks, *task;
	char *name, *intrvl, *offset, *is_default;
	int i;
	name = ldmsctl_json_str_value_get(updtr_json, "name");
	tasks = ldmsctl_json_value_get(updtr_json, "tasks");
	printf("Updater: %s\n", name);
	printf("   tasks: <interval_us>:<offset_us>\n");
	for (i = 0; i < tasks->u.array.length; i++) {
		task = tasks->u.array.values[i];
		intrvl = ldmsctl_json_str_value_get(task, "interval_us");
		offset = ldmsctl_json_str_value_get(task, "offset_us");
		is_default = ldmsctl_json_str_value_get(task, "default_task");
		if (0 == strcmp(is_default, "true")) {
			printf("     %s:%s     default\n", intrvl, offset);
		} else {
			printf("     %s:%s\n", intrvl, offset);
		}
	}
}

static void resp_updtr_task(ldmsd_req_hdr_t resp, size_t len, uint32_t rsp_err)
{
	json_value *json, *updtr;
	int i;

	if (0 != rsp_err) {
		resp_generic(resp, len, rsp_err);
		return;
	}

	ldmsd_req_attr_t attr = ldmsd_first_attr(resp);
	if (!attr->discrim || (attr->attr_id != LDMSD_ATTR_JSON))
		return;
	json = json_parse((char*)attr->attr_value, len);
	if (!json)
		return;
	if (json->type != json_array) {
		printf("Unrecognized updater status format\n");
		return;
	}
	for (i = 0; i < json->u.array.length; i++) {
		updtr = json->u.array.values[i];
		__print_updtr_task(updtr);
	}
	json_value_free(json);
}

static void help_updtr_task()
{
	printf("\bGet the tasks of an update\n"
	       "Parameters:\n"
	       "      [name=]     The updater policy name\n");
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

void __print_strgp_status(json_value *jvalue)
{
	if (jvalue->type != json_object) {
		printf("Invalid producer status format\n");
		return;
	}

	char *name, *container, *schema, *plugin, *state;
	json_int_t offset;

	name = ldmsctl_json_str_value_get(jvalue, "name");
	container = ldmsctl_json_str_value_get(jvalue, "container");
	schema = ldmsctl_json_str_value_get(jvalue, "schema");
	plugin = ldmsctl_json_str_value_get(jvalue, "plugin");
	state = ldmsctl_json_str_value_get(jvalue, "state");
	printf("%-16s %-16s %-16s %-16s %s\n",
			name, container, schema, plugin, state);

	json_value *prd_array_jvalue, *metric_array_jvalue;
	prd_array_jvalue = ldmsctl_json_value_get(jvalue, "producers");
	if (!prd_array_jvalue || (prd_array_jvalue->type != json_array)) {
		printf("---Invalid storage policy status format---\n");
		return;
	}
	printf("    producers:");

	json_value *prd_jvalue, *metric_jvalue;
	int i;
	for (i = 0; i < prd_array_jvalue->u.array.length; i++) {
		prd_jvalue = ldmsctl_json_array_ele_get(prd_array_jvalue, i);
		if (!prd_jvalue || (prd_jvalue->type != json_string)) {
			printf("---Invalid storage policy status format---\n");
			return;
		}
		printf(" %s", prd_jvalue->u.string.ptr);
	}
	printf("\n");

	metric_array_jvalue = ldmsctl_json_value_get(jvalue, "metrics");
	if (!metric_array_jvalue || (metric_array_jvalue->type != json_array)) {
		printf("---Invalid storage policy status format---\n");
		return;
	}
	printf("     metrics:");
	for (i = 0; i < metric_array_jvalue->u.array.length; i++) {
		metric_jvalue = ldmsctl_json_array_ele_get(metric_array_jvalue, i);
		if (!metric_jvalue || (metric_jvalue->type != json_string)) {
			printf("---Invalid storage policy status format---\n");
			return;
		}
		printf(" %s", metric_jvalue->u.string.ptr);
	}
	printf("\n");
}

static void resp_strgp_status(ldmsd_req_hdr_t resp, size_t len, uint32_t rsp_err)
{
	ldmsd_req_attr_t attr = ldmsd_first_attr(resp);
	if (!attr->discrim || (attr->attr_id != LDMSD_ATTR_JSON))
		return;
	json_value *json, *prdcr_json;
	json = json_parse((char*)attr->attr_value, len);
	if (!json)
		return;

	if (json->type != json_array) {
		printf("Unrecognized producer status format\n");
		return;
	}
	int i;

	printf("Name             Container        Schema           Plugin           State\n");
	printf("---------------- ---------------- ---------------- ---------------- ------------\n");

	for (i = 0; i < json->u.array.length; i++) {
		prdcr_json = ldmsctl_json_array_ele_get(json, i);
		__print_strgp_status(prdcr_json);
	}
	json_value_free(json);

}

static void help_strgp_status()
{
	printf("\nGet the statuses of all Storage policies\n"
	       "Parameters:\n"
	       "      None\n");
}

static void __print_plugn_sets(json_value *plugin_sets)
{
	json_value *sets, *set_name;
	char *pi_name, *sname;
	int i;
	pi_name = ldmsctl_json_str_value_get(plugin_sets, "plugin");
	if (!pi_name) {
		printf("Unrecognized json object\n");
		return;
	}
	printf("%s:\n", pi_name);
	sets = ldmsctl_json_value_get(plugin_sets, "sets");
	if (!sets) {
		printf("   None\n");
		return;
	}
	for (i = 0; i < sets->u.array.length; i++) {
		set_name = ldmsctl_json_array_ele_get(sets, i);
		printf("   %s\n", set_name->u.string.ptr);
	}
}

static void resp_plugn_sets(ldmsd_req_hdr_t resp, size_t len, uint32_t rsp_err)
{
	ldmsd_req_attr_t attr = ldmsd_first_attr(resp);
	if (!attr->discrim || (attr->attr_id != LDMSD_ATTR_JSON))
		return;
	json_value *json, *plugn_sets_json;
	json = json_parse((char*)attr->attr_value, len);
	if (!json)
		return;

	if (json->type != json_array) {
		printf("Unrecognized plugn_sets json object\n");
		return;
	}
	int i;

	for (i = 0; i < json->u.array.length; i++) {
		plugn_sets_json = json->u.array.values[i];
		__print_plugn_sets(plugn_sets_json);
	}
	json_value_free(json);

}

static void help_plugn_sets()
{
	printf("\nPrint sets by plugins\n"
	       "Parameters:\n"
	       "      [name]=   Plugin name\n");
}

static void help_version()
{
	printf( "\nGet the LDMS version.\n");
}

static void help_set_route()
{
	printf("\nDisplay the route of the set from aggregators to the sampler daemon.\n"
	       "Parameters:\n"
	       "     instance=   Set instance name\n");
}

static void resp_set_route(ldmsd_req_hdr_t resp, size_t len, uint32_t rsp_err)
{
	ldmsd_req_attr_t attr = ldmsd_first_attr(resp);
	if (!attr->discrim || (attr->attr_id != LDMSD_ATTR_JSON))
		return;
	json_value *json, *route, *hop, *hinfo;
	char *inst_name, *schema_name;

	if (rsp_err) {
		/* There is an error. */
		resp_generic(resp, len, rsp_err);
		return;
	}

	json = json_parse((char*)attr->attr_value, len);
	if (!json)
		return;

	if (json->type != json_object) {
		printf("Unrecognized set info response format\n");
		return;
	}
	inst_name = ldmsctl_json_str_value_get(json, "instance");
	schema_name = ldmsctl_json_str_value_get(json, "schema");

	printf("-----------------------------\n");
	printf("instance: %s\n", inst_name);
	printf("schema_name: %s\n", schema_name);
	printf("=============================\n");
	printf("%20s %15s %15s %15s %10s %10s %5s %25s %25s\n",
			"host", "type", "name", "prdcr_host",
			"interval", "offset", "sync", "start", "end");
	printf("-------------------- --------------- --------------- --------------- "
		"---------- ---------- ----- ------------------------- -------------------------\n");
	route = ldmsctl_json_value_get(json, "route");
	if (!route || (route->type != json_array)) {
		printf("Unrecognized set info response format\n");
		return;
	}
	int i;
	char *host, *type, *name, *prdcr_host, *intrvl, *offset, *is_sync;
	char *start_sec, *start_usec, *end_sec, *end_usec, *start, *end;
	uint32_t sec, usec;
	for (i = 0; i < route->u.array.length; i++) {
		hop = ldmsctl_json_array_ele_get(route, i);
		hinfo = ldmsctl_json_value_get(hop, "detail");
		type = ldmsctl_json_str_value_get(hop, "type");
		host = ldmsctl_json_str_value_get(hop, "host");
		name = ldmsctl_json_str_value_get(hinfo, "name");
		if (0 == strcmp(type, "producer")) {
			prdcr_host = ldmsctl_json_str_value_get(hinfo, "host");
			intrvl = ldmsctl_json_str_value_get(hinfo, "update_int");
			offset = ldmsctl_json_str_value_get(hinfo, "update_off");
			is_sync = ldmsctl_json_str_value_get(hinfo, "update_sync");
			start_sec = ldmsctl_json_str_value_get(hinfo, "last_start_sec");
			start_usec = ldmsctl_json_str_value_get(hinfo, "last_start_usec");
			end_sec = ldmsctl_json_str_value_get(hinfo, "last_end_sec");
			end_usec = ldmsctl_json_str_value_get(hinfo, "last_end_usec");
		} else {
			prdcr_host = "---";
			intrvl = ldmsctl_json_str_value_get(hinfo, "interval_us");
			offset = ldmsctl_json_str_value_get(hinfo, "offset_us");
			is_sync = ldmsctl_json_str_value_get(hinfo, "sync");
			start_sec = ldmsctl_json_str_value_get(hinfo, "trans_start_sec");
			start_usec = ldmsctl_json_str_value_get(hinfo, "trans_start_usec");
			end_sec = ldmsctl_json_str_value_get(hinfo, "trans_end_sec");
			end_usec = ldmsctl_json_str_value_get(hinfo, "trans_end_usec");
		}
		sec = strtoul(start_sec, NULL, 0);
		usec = strtoul(start_usec, NULL, 0);
		start = ldmsctl_ts_str(sec, usec);
		sec = strtoul(end_sec, NULL, 0);
		usec = strtoul(end_usec, NULL, 0);
		end = ldmsctl_ts_str(sec, usec);
		printf("%20s %15s %15s %15s %10s %10s %5s %25s %25s\n",
					host, type, name, prdcr_host, intrvl,
					offset, is_sync, start, end);
		free(start);
		free(end);
	}
	json_value_free(json);
}

/* failover related functions */

static void help_failover_peercfg_stop()
{
	printf("Stop peer configuration.\n\n");
}

static void help_failover_peercfg_start()
{
	printf("Start peer configuration.\n\n");
}

static void help_failover_config()
{
	printf("Configure LDMSD failover.\n\n");
	printf("Parameters:\n");
	printf("    host=             The host name of the failover partner.\n");
	printf("                      This is optional in re-configuration.\n");
	printf("    xprt=             The transport of the failover partner.\n");
	printf("                      This is optional in re-configuration.\n");
	printf("    port=             The LDMS port of the failover partner.\n");
	printf("                      This is optional in re-configuration.\n");
	printf("    [auto_switch=0|1] Auto switching (failover/failback).\n");
	printf("    [interval=]       The interval of the heartbeat.\n");
	printf("    [timeout_factor=] The heartbeat timeout factor.\n");
	printf("    [peer_name=]      The failover partner name. If not given,\n");
	printf("                      the ldmsd will accept any partner.\n");
}

static void help_failover_mod()
{
	printf("Modify LDMSD failover.\n\n");
	printf("Parameters:\n");
	printf("    [auto_switch=0|1] Auto switching (failover/failback).\n");
}

static void help_failover_status()
{
	printf("Get failover status.\n\n");
}

static void help_failover_start()
{
	printf("Start LDMSD failover service.\n\n");
	printf("    NOTE: After the failover service has started,\n");
	printf("    aggregator configuration objects (prdcr, updtr, and \n");
	printf("    strgp) are not allowed to be altered (start, stop, or \n");
	printf("    reconfigure).\n\n");
}

static void help_failover_stop()
{
	printf("Stop LDMSD failover service.\n\n");
}

static void help_setgroup_add()
{
	printf("Create a new setgroup.\n");
	printf("Parameters:\n");
	printf("    name=           The set group name.\n");
	printf("    [producer=]     The producer name of the set group.\n");
	printf("    [interval=]     The update interval hint (in usec).\n");
	printf("    [offset=]       The update offset hint (in usec).\n");
}

static void help_setgroup_mod()
{
	printf("Modify attributes of a set group.\n");
	printf("Parameters:\n");
	printf("    name=           The set group name.\n");
	printf("    [interval=]     The update interval hint (in usec).\n");
	printf("    [offset=]       The update offset hint (in usec).\n");
}

static void help_setgroup_del()
{
	printf("Delete a set group\n");
	printf("Parameters:\n");
	printf("    name=    The set group name to delete.\n");
}

static void help_setgroup_ins()
{
	printf("Insert sets into the set group\n");
	printf("Parameters:\n");
	printf("    name=     The set group name.\n");
	printf("    instance= The comma-separated list of set instances to add.\n");
}

static void help_setgroup_rm()
{
	printf("Remove sets from the set group\n");
	printf("Parameters:\n");
	printf("    name=     The set group name.\n");
	printf("    instance= The comma-separated list of set instances to remove.\n");
}

static void __indent_print(int indent)
{
	int i;
	for (i = 0; i < indent; i++) {
		printf("    ");
	}
}

static void __json_value_print(json_value *v, int indent)
{
	int i;
	switch (v->type) {
	case json_object:
		for (i = 0; i < v->u.object.length; i++) {
			printf("\n");
			__indent_print(indent);
			printf("%s: ", v->u.object.values[i].name);
			__json_value_print(v->u.object.values[i].value,
					   indent + 1);
		}
		break;
	case json_array:
		for (i = 0; i < v->u.array.length; i++) {
			printf("\n");
			__indent_print(indent);
			printf("* ");
			__json_value_print(v->u.array.values[i], indent + 1);
		}
		break;
	case json_none:
		printf("NONE");
		break;
	case json_null:
		printf("NULL");
		break;
	case json_integer:
		printf("%ld", v->u.integer);
		break;
	case json_double:
		printf("%lf", v->u.dbl);
		break;
	case json_string:
		printf("%s", v->u.string.ptr);
		break;
	case json_boolean:
		printf("%s", v->u.boolean?"True":"False");
		break;
	}
}

static void resp_failover_status(ldmsd_req_hdr_t resp, size_t len,
				 uint32_t rsp_err)
{
	ldmsd_req_attr_t attr = ldmsd_first_attr(resp);
	if (!attr->discrim || (attr->attr_id != LDMSD_ATTR_JSON))
		return;

	json_value *json;
	json = json_parse((char*)attr->attr_value, len);
	if (!json)
		return;

	if (json->type != json_object) {
		printf("Unrecognized failover status format\n");
		return;
	}

	printf("--- Failover Status ---");
	__json_value_print(json, 0);
	printf("\n\n");

	json_value_free(json);
}

static int handle_help(struct ldmsctl_ctrl *ctrl, char *args);
static int handle_source(struct ldmsctl_ctrl *ctrl, char *path);
static int handle_script(struct ldmsctl_ctrl *ctrl, char *cmd);

static struct command command_tbl[] = {
	{ "?", LDMSCTL_HELP, handle_help, NULL, NULL },
	{ "config", LDMSD_PLUGN_CONFIG_REQ, NULL, help_config, resp_generic },
	{ "daemon_status", LDMSD_DAEMON_STATUS_REQ, NULL, help_daemon_status, resp_generic },
	{ "failover_config", LDMSD_FAILOVER_CONFIG_REQ, NULL,
			     help_failover_config, resp_generic },
	{ "failover_peercfg_start", LDMSD_FAILOVER_PEERCFG_START_REQ, NULL,
		      help_failover_peercfg_start, resp_generic },
	{ "failover_peercfg_stop", LDMSD_FAILOVER_PEERCFG_STOP_REQ, NULL,
		      help_failover_peercfg_stop, resp_generic },
	{ "failover_start", LDMSD_FAILOVER_START_REQ, NULL,
			     help_failover_start, resp_generic },
	{ "failover_status", LDMSD_FAILOVER_STATUS_REQ, NULL,
			     help_failover_status, resp_failover_status },
	{ "failover_stop", LDMSD_FAILOVER_STOP_REQ, NULL,
			     help_failover_stop, resp_generic },
	{ "greeting", LDMSD_GREETING_REQ, NULL, help_greeting, resp_greeting },
	{ "help", LDMSCTL_HELP, handle_help, NULL, NULL },
	{ "load", LDMSD_PLUGN_LOAD_REQ, NULL, help_load, resp_generic },
	{ "loglevel", LDMSD_VERBOSE_REQ, NULL, help_loglevel, resp_generic },
	{ "oneshot", LDMSD_ONESHOT_REQ, NULL, help_oneshot, resp_generic },
	{ "plugn_sets", LDMSD_PLUGN_SETS_REQ, NULL, help_plugn_sets, resp_plugn_sets },
	{ "prdcr_add", LDMSD_PRDCR_ADD_REQ, NULL, help_prdcr_add, resp_generic },
	{ "prdcr_del", LDMSD_PRDCR_DEL_REQ, NULL, help_prdcr_del, resp_generic },
	{ "prdcr_hint_tree", LDMSD_PRDCR_HINT_TREE_REQ, NULL, help_prdcr_hint_tree, resp_prdcr_hint_tree },
	{ "prdcr_set_status", LDMSD_PRDCR_SET_REQ, NULL, help_prdcr_set_status, resp_prdcr_set_status },
	{ "prdcr_start", LDMSD_PRDCR_START_REQ, NULL, help_prdcr_start, resp_generic },
	{ "prdcr_start_regex", LDMSD_PRDCR_START_REGEX_REQ, NULL, help_prdcr_start_regex, resp_generic },
	{ "prdcr_status", LDMSD_PRDCR_STATUS_REQ, NULL, help_prdcr_status, resp_prdcr_status },
	{ "prdcr_stop", LDMSD_PRDCR_STOP_REQ, NULL, help_prdcr_stop, resp_generic },
	{ "prdcr_stop_regex", LDMSD_PRDCR_STOP_REGEX_REQ, NULL, help_prdcr_stop_regex, resp_generic },
	{ "quit", LDMSCTL_QUIT, handle_quit, help_quit, resp_generic },
	{ "script", LDMSCTL_SCRIPT, handle_script, help_script, resp_generic },
	{ "set_route", LDMSD_SET_ROUTE_REQ, NULL, help_set_route, resp_set_route },
	{ "setgroup_add", LDMSD_SETGROUP_ADD_REQ, NULL, help_setgroup_add, resp_generic },
	{ "setgroup_del", LDMSD_SETGROUP_DEL_REQ, NULL, help_setgroup_del, resp_generic },
	{ "setgroup_ins", LDMSD_SETGROUP_INS_REQ, NULL, help_setgroup_ins, resp_generic },
	{ "setgroup_mod", LDMSD_SETGROUP_MOD_REQ, NULL, help_setgroup_mod, resp_generic },
	{ "setgroup_rm",  LDMSD_SETGROUP_RM_REQ,  NULL, help_setgroup_rm,  resp_generic },
	{ "source", LDMSCTL_SOURCE, handle_source, help_source, resp_generic },
	{ "start", LDMSD_PLUGN_START_REQ, NULL, help_start, resp_generic },
	{ "stop", LDMSD_PLUGN_STOP_REQ, NULL, help_stop, resp_generic },
	{ "strgp_add", LDMSD_STRGP_ADD_REQ, NULL, help_strgp_add, resp_generic },
	{ "strgp_del", LDMSD_STRGP_DEL_REQ, NULL, help_strgp_del, resp_generic },
	{ "strgp_metric_add", LDMSD_STRGP_METRIC_ADD_REQ, NULL, help_strgp_metric_add, resp_generic },
	{ "strgp_metric_del", LDMSD_STRGP_METRIC_DEL_REQ, NULL, help_strgp_metric_del, resp_generic },
	{ "strgp_prdcr_add", LDMSD_STRGP_PRDCR_ADD_REQ, NULL, help_strgp_prdcr_add, resp_generic },
	{ "strgp_prdcr_del", LDMSD_STRGP_PRDCR_DEL_REQ, NULL, help_strgp_prdcr_del, resp_generic },
	{ "strgp_start", LDMSD_STRGP_START_REQ, NULL, help_strgp_start, resp_generic },
	{ "strgp_status", LDMSD_STRGP_STATUS_REQ, NULL, help_strgp_status, resp_strgp_status },
	{ "strgp_stop", LDMSD_STRGP_STOP_REQ, NULL, help_strgp_stop, resp_generic },
	{ "term", LDMSD_PLUGN_TERM_REQ, NULL, help_term, resp_generic },
	{ "udata", LDMSD_SET_UDATA_REQ, NULL, help_udata, resp_generic },
	{ "udata_regex", LDMSD_SET_UDATA_REGEX_REQ, NULL, help_udata_regex, resp_generic },
	{ "updtr_add", LDMSD_UPDTR_ADD_REQ, NULL, help_updtr_add, resp_generic },
	{ "updtr_del", LDMSD_UPDTR_DEL_REQ, NULL, help_updtr_del, resp_generic },
	{ "updtr_match_add", LDMSD_UPDTR_MATCH_ADD_REQ, NULL, help_updtr_match_add, resp_generic },
	{ "updtr_match_del", LDMSD_UPDTR_DEL_REQ, NULL, help_updtr_match_del, resp_generic },
	{ "updtr_prdcr_add", LDMSD_UPDTR_PRDCR_ADD_REQ, NULL, help_updtr_prdcr_add, resp_generic },
	{ "updtr_prdcr_del", LDMSD_UPDTR_PRDCR_DEL_REQ, NULL, help_updtr_prdcr_del, resp_generic },
	{ "updtr_start", LDMSD_UPDTR_START_REQ, NULL, help_updtr_start, resp_generic },
	{ "updtr_status", LDMSD_UPDTR_STATUS_REQ, NULL, help_updtr_status, resp_updtr_status },
	{ "updtr_stop", LDMSD_UPDTR_STOP_REQ, NULL, help_updtr_stop, resp_generic },
	{ "updtr_task", LDMSD_UPDTR_TASK_REQ, NULL, help_updtr_task, resp_updtr_task },
	{ "usage", LDMSD_PLUGN_LIST_REQ, NULL, help_usage, resp_usage },
	{ "version", LDMSD_VERSION_REQ, NULL, help_version , resp_generic },
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
		char *_args, *ptr;
		_args = strtok_r(args, " \t\n", &ptr);
		if (!_args) {
			__print_all_command();
			return 0;
		}

		struct command *help_cmd;
		help_cmd = bsearch(&_args, command_tbl, ARRAY_SIZE(command_tbl),
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

static int __sock_send(struct ldmsctl_ctrl *ctrl, ldmsd_req_hdr_t req, size_t len)
{
	int rc;
	rc = send(ctrl->sock.sock, req, len, 0);
	if (rc == -1) {
		printf("Error %d: Failed to send the request.\n", errno);
		exit(1);
	}
	return 0;
}

static void __sock_close(struct ldmsctl_ctrl *ctrl)
{
	close(ctrl->sock.sock);
}

static int __ldms_xprt_send(struct ldmsctl_ctrl *ctrl, ldmsd_req_hdr_t req, size_t len)
{
	size_t req_sz = sizeof(*req);
	char *req_buf = malloc(len);
	if (!req_buf) {
		printf("Out of memory\n");
		return ENOMEM;
	}

	memcpy(req_buf, req, len);
	int rc = ldms_xprt_send(ctrl->ldms_xprt.x, req_buf, len);
	free(req_buf);
	return rc;
}

static void __ldms_xprt_close(struct ldmsctl_ctrl *ctrl)
{
	sem_destroy(&ctrl->ldms_xprt.connected_sem);
	sem_destroy(&ctrl->ldms_xprt.recv_sem);
	ldms_xprt_close(ctrl->ldms_xprt.x);
}

static char * __sock_recv(struct ldmsctl_ctrl *ctrl)
{
	struct ldmsd_req_hdr_s resp;
	ssize_t msglen;

	msglen = recv(ctrl->sock.sock, &resp, sizeof(resp), MSG_PEEK);
	if (msglen <= 0)
		/* closing */
		return NULL;

	/* Convert the response byte order from network to host */
	ldmsd_ntoh_req_hdr(&resp);

	/* Verify the marker */
	if (resp.marker != LDMSD_RECORD_MARKER
			|| (msglen < sizeof(resp))) {
		printf("Invalid response: missing record marker.\n");
		return NULL;
	}

	if (buffer_len < resp.rec_len) {
		free(buffer);
		buffer = malloc(resp.rec_len);
		if (!buffer) {
			printf("Out of memory\n");
			exit(ENOMEM);
		}
		buffer_len = resp.rec_len;
	}
	memset(buffer, 0, buffer_len);

	msglen = recv(ctrl->sock.sock, buffer, resp.rec_len, MSG_WAITALL);
	if (msglen < resp.rec_len) {
		printf("Error: Received short response record.\n");
		return NULL;
	}

	return buffer;
}

static char *__ldms_xprt_recv(struct ldmsctl_ctrl *ctrl)
{
loop:
	sem_wait(&ctrl->ldms_xprt.recv_sem);
	if (!buffer) {
		return NULL;
	}
	return buffer;
}

static int __handle_cmd(struct ldmsctl_ctrl *ctrl, char *cmd_str)
{
	static int msg_no = 0;
	ldmsd_req_hdr_t request;
	struct ldmsd_req_array *req_array = NULL;
	size_t len;
	int rc, i;

	struct command key, *cmd;
	char *ptr, *args, *dummy;
	int cmd_id;

	/* Strip the new-line character */
	char *newline = strrchr(cmd_str, '\n');
	if (newline)
		*newline = '\0';

	dummy = strdup(cmd_str);
	if (!dummy) {
		printf("Out of memory\n");
		exit(ENOMEM);
	}

	key.token = strtok_r(dummy, " \t\n", &ptr);
	args = strtok_r(NULL, "\n", &ptr);
	cmd = bsearch(&key, command_tbl, ARRAY_SIZE(command_tbl),
			sizeof(struct command), command_comparator);
	if (!cmd) {
		printf("Unrecognized command '%s'\n", key.token);
		return 0;
	}

	if (cmd->action) {
		(void)cmd->action(ctrl, args);
		free(dummy);
		return 0;
	}
	free(dummy);

	size_t buffer_offset = 0;
	memset(buffer, 0, buffer_len);
	req_array = ldmsd_parse_config_str(cmd_str, msg_no,
					   ldms_xprt_msg_max(ctrl->ldms_xprt.x),
					   ldmsctl_log);
	if (!req_array) {
		printf("Failed to process the request. ");
		if (errno == ENOMEM)
			printf("Out of memory\n");
		else
			printf("Please make sure that there is no typo.\n");
		return EINVAL;
	}
	msg_no++;

	for (i = 0; i < req_array->num_reqs; i++) {
		request = req_array->reqs[i];
		len = ntohl(request->rec_len);

		rc = ctrl->send_req(ctrl, request, len);
		if (rc) {
			printf("Failed to send data to ldmsd. %s\n", strerror(errno));
			return rc;
		}
	}
	/*
	 * Send all the records and handle the response now.
	 */
	ldmsd_req_hdr_t resp;
	size_t req_hdr_sz = sizeof(*resp);
	size_t lbufsz = 1024;
	size_t lbufoffset = 0;
	char *lbuf = malloc(lbufsz);
	if (!lbuf) {
		printf("Out of memory\n");
		exit(1);
	}

	char *rec;
	size_t reclen = 0;
	size_t msglen = 0;
	rc = 0;
	while (1) {
		resp = (ldmsd_req_hdr_t)ctrl->recv_resp(ctrl);
		if (!resp) {
			printf("Failed to receive the response\n");
			rc = -1;
			goto out;
		}
		if (ntohl(resp->flags) & LDMSD_REQ_SOM_F) {
			reclen = ntohl(resp->rec_len);
			rec = (char *)resp;
		} else {
			reclen = ntohl(resp->rec_len) - req_hdr_sz;
			rec = (char *)(resp + 1);
		}
		if (lbufsz < msglen + reclen) {
			lbuf = realloc(lbuf, msglen + (reclen * 2));
			if (!lbuf) {
				printf("Out of memory\n");
				exit(1);
			}
			lbufsz = msglen + (reclen * 2);
			memset(&lbuf[msglen], 0, lbufsz - msglen);
		}
		memcpy(&lbuf[msglen], rec, reclen);
		msglen += reclen;
		if ((ntohl(resp->flags) & LDMSD_REQ_EOM_F) != 0) {
			break;
		}
	}
	ldmsd_ntoh_req_msg((ldmsd_req_hdr_t)lbuf);

	cmd->resp((ldmsd_req_hdr_t)lbuf, msglen, resp->rsp_err);
out:
	free(lbuf);
	return rc;
}

void __ldms_event_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	size_t msg_len;
	static size_t resp_hdr_sz = sizeof(struct ldmsd_req_hdr_s);
	struct ldmsctl_ctrl *ctrl = cb_arg;
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		sem_post(&ctrl->ldms_xprt.connected_sem);
		break;
	case LDMS_XPRT_EVENT_REJECTED:
		printf("The connected request is rejected.\n");
		ldms_xprt_put(ctrl->ldms_xprt.x);
		exit(0);
	case LDMS_XPRT_EVENT_DISCONNECTED:
		ldms_xprt_put(ctrl->ldms_xprt.x);
		printf("The connection is disconnected.\n");
		exit(0);
	case LDMS_XPRT_EVENT_ERROR:
		printf("Connection error\n");
		exit(0);
		break;
	case LDMS_XPRT_EVENT_RECV:
		if (buffer_len < e->data_len) {
			free(buffer);
			buffer = malloc(e->data_len);
			if (!buffer) {
				printf("Out of memory\n");
				buffer = NULL;
				buffer_len = 0;
				ldms_xprt_close(ctrl->ldms_xprt.x);
				sem_post(&ctrl->ldms_xprt.recv_sem);
				break;
			}
			buffer_len = e->data_len;
		}
		memset(buffer, 0, buffer_len);
		memcpy(buffer, e->data, e->data_len);
		sem_post(&ctrl->ldms_xprt.recv_sem);
		break;
	default:
		assert(0);
	}
}

struct ldmsctl_ctrl *__ldms_xprt_ctrl(const char *host, const char *port,
			const char *xprt, const char *auth,
			struct attr_value_list *auth_opt)
{
	ldms_t x;
	struct ldmsctl_ctrl *ctrl;
	int rc;

	ctrl = calloc(1, sizeof(*ctrl));
	if (!ctrl)
		return NULL;

	sem_init(&ctrl->ldms_xprt.connected_sem, 0, 0);
	sem_init(&ctrl->ldms_xprt.recv_sem, 0, 0);

	ctrl->send_req = __ldms_xprt_send;
	ctrl->recv_resp = __ldms_xprt_recv;
	ctrl->close = __ldms_xprt_close;

	ctrl->ldms_xprt.x = ldms_xprt_new_with_auth(xprt, NULL, auth, auth_opt);
	if (!ctrl->ldms_xprt.x) {
		printf("Failed to create an ldms transport. %s\n",
						strerror(errno));
		return NULL;
	}

	rc = ldms_xprt_connect_by_name(ctrl->ldms_xprt.x, host, port,
						__ldms_event_cb, ctrl);
	if (rc) {
		ldms_xprt_put(ctrl->ldms_xprt.x);
		sem_destroy(&ctrl->ldms_xprt.connected_sem);
		sem_destroy(&ctrl->ldms_xprt.recv_sem);
		free(ctrl);
		return NULL;
	}

	sem_wait(&ctrl->ldms_xprt.connected_sem);
	return ctrl;
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
	char *host, *port, *auth, *sockname, *env, *xprt;
	char *lval, *rval;
	host = port = sockname = xprt = NULL;
	char *source, *script;
	source = script = NULL;
	int rc, is_inband = 1;
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

	char *secretword = NULL;

	struct ldmsctl_ctrl *ctrl;
	if (is_inband) {
		ctrl = __ldms_xprt_ctrl(host, port, xprt, auth, auth_opt);
		if (!ctrl) {
			printf("Failed to connect to ldmsd.\n");
			exit(-1);
		}
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
		if (linebuf[0] == '\0')
			continue;
#ifdef HAVE_READLINE_HISTORY
		add_history(linebuf);
#endif /* HAVE_READLINE_HISTORY */

		rc = __handle_cmd(ctrl, linebuf);
		if (rc)
			break;
	} while (linebuf);

	ctrl->close(ctrl);
	return 0;
arg_err:
	printf("Please specify the connection type.\n");
	usage(argv);
	return 0;
}
