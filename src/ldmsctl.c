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
#include "un.h"

/*
 * load - load the specified plugin
 * unload - unload the specified plugin
 * list - list the loaded plugins
 * start - begin sampling for the specified plugin
 * stop - stop sampling for the specified plugin
 * status - report the current status of a plugin
 * config - set a parameter for a plugin
 */
#define FMT "S:"

#define CONTROL_SOCKET "/var/run/ldmsd/control"
static char *sockname = CONTROL_SOCKET;

void usage(char *argv[])
{
	printf("%s: [%s]\n"
               "    -S <socket>     The UNIX socket that the ldms daemon is listening on.\n"
               "                    [" CONTROL_SOCKET "].\n",
               argv[0], FMT);
	exit(1);
}

void help()
{
	printf("help\n"
	       "   - Print this menu.\n"
	       "load <plugin_name>\n"
	       "   - Loads the specified plugin. The library that implements\n"
	       "     the plugin should be in the directory specified by the\n"
	       "     LDMS_PLUGIN_LIBPATH environment variable.\n"
	       "ls\n"
	       "   - Show plugin information.\n"
	       "init <plugin_name> <set_name>\n"
	       "   - Specifies the name of the metric set for the plugin\n"
	       "term <plugin_name>\n"
	       "   - Destroys the set associated with the plugin\n"
	       "start <plugin_name> <sample_interval>\n"
	       "   - Begins calling the plugin's 'sample' method at the\n"
	       "     sample interval.\n"
	       "stop <plugin_name>\n"
	       "   - Cancels sampling on the specified plugin.\n"
	       "config <plugin_name> <variable> <value>\n"
	       "   - Provides a mechanism to specify configuration options\n"
	       "     for a plugin.\n"
	       "quit\n"
	       "   - Exit.\n");
}

static char err_str[1024];
static char kw[1024];
static char arg[1024];
static char arg1[1024];
static char arg2[1024];
static char linebuf[1024];
static char ls_buf[2048];
int main(int argc, char *argv[])
{
	int op;

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
	un_set_quiet(1);
	if (un_connect(basename(argv[0]), sockname) < 0) {
		printf("Error setting up connection with ldmsd.\n");
		exit(1);
	}
	char *s;
	do {
		if (isatty(0))
			s = readline("ldmsctl> ");
		else
			s = fgets(linebuf, sizeof linebuf, stdin);
		if (!s)
			break;
		int rc = sscanf(s, "%60s %[^\n]", kw, arg);
		if (rc < 0)
			continue;
		if (rc < 1)
			goto syntax_error;
		if (kw[0] == '?' || 0 == strcmp(kw, "help")) {
			help();
			continue;
		}
		err_str[0] = '\0';
		if (0 == strcmp(kw, "quit"))
			goto quit;

		if (0 == strcmp(kw, "load")) {
			un_load_plugin(arg, err_str);
			goto finit;
		}
		if (0 == strcmp(kw, "ls")) {
			err_str[0] = '\0';
			un_ls_plugins(ls_buf, sizeof ls_buf);
			printf(ls_buf);
			goto finit;
		}
		if (0 == strcmp(kw, "init")) {
			rc = sscanf(arg, "%s %s", arg1, arg2);
			if (rc != 2) {
				printf("init <plugin_name> <set_name>\n");
				continue;
			}
			un_init_plugin(arg1, arg2, err_str);
			goto finit;
		}
		if (0 == strcmp(kw, "term")) {
			un_term_plugin(arg, err_str);
			goto finit;
		}
		if (0 == strcmp(kw, "start")) {
			unsigned long period;
			rc = sscanf(arg, "%s %ld", arg1, &period);
			if (rc != 2) {
				printf("start <plugin_name> <sample_period>\n");
				continue;
			}
			un_start_plugin(arg1, period, err_str);
			goto finit;
		}
		if (0 == strcmp(kw, "stop")) {
			un_stop_plugin(arg, err_str);
			goto finit;
		}
		if (0 == strcmp(kw, "config")) {
			rc = sscanf(arg, "%s %[^\n]", arg1, arg2);
			if (rc != 2) {
				printf("config <plugin_name> <cfg_var> <cfg_val>\n");
				continue;
			}
			un_config_plugin(arg1, arg2, err_str);
			goto finit;
		}
	syntax_error:
		sprintf(err_str, "Unrecognized command\n");
	finit:
		add_history(s);
		if (err_str[0] != '\0')
			printf("%s\n", err_str);
		continue;
	} while (s);
 quit:
	un_close();
 	return(0);
}
