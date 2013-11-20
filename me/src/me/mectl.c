/*
 * mectl.c
 *
 *  Created on: Jul 23, 2013
 *      Author: nichamon
 */
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
#include <ovis_ctrl/ctrl.h>
#include <ovis_util/util.h>
#include "me.h"

#define FMT "S:"
#define ARRAY_SIZE(a)  (sizeof(a) / sizeof(a[0]))

struct attr_value_list *av_list, *kw_list;

void usage(char *argv[])
{
	printf("%s: [%s]\n"
               "    -S <socket>     The UNIX socket that the ldms daemon is listening on.\n"
               "                    [" ME_CONTROL_SOCKNAME "].\n",
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
	       "     ME_PLUGIN_LIBPATH environment variable.\n"
	       "     <name>       The plugin name, this is used to locate a loadable\n"
	       "                  library named \"lib<name>.so\"\n"
	       "\n"
	       "config name=<name> [ <attr>=<value> ... ]\n"
	       "   - Provides a mechanism to specify configuration options\n"
	       "     <name>       The plugin name.\n"
	       "     <attr>       An attribute name.\n"
	       "     <value>      An attribute value.\n"
	       "\n"
	       "store name=<store> container=<container>\n"
	       "      [hosts=<hosts>] [metric=<metric>]\n"
	       "   - Saves a set from one or more hosts to a persistent object store.\n"
	       "     <store>      The name of the storage plugin.\n"
	       "     <container>  The store policy ID, e.g., meminfo-essential\n"
	       "\n"
	       "create name=<name> model_id=<model id> thresholds=<thresholds>\n"
	       "       [params=<params>]\n"
	       "   - Create a model policy.\n"
	       "     <name>                 The name of the model\n"
	       "     <model id>             The number id of the model policy\n"
	       "     <thresholds>           The thresholds for the severity levels:\n"
	       "                            INFO, WARNING, and CRITICAL\n"
	       "     <params>               Use 'usage' to see the instruction for each model\n"
	       "\n"
	       "model model_id=<model_id> metric_ids=<metric_ids>\n"
	       "   - Start a model of model_id to handle input_ids\n"
	       "     <model_id>       The model ID that will handles the given input ID(s).\n"
	       "                      A model policy with the given 'model_id' must exist\n"
	       "                      before calling the command.\n"
	       "                      Use 'create' to create a model policy\n"
	       "     <metric_ids>     A metric ID if the model ID is of a univarate model.\n"
	       "                      A comma-separated list of metric IDs if the model ID is\n"
	       "                      of a multivariate model.\n"
	       "\n"
	       "quit\n"
	       "   - Exit.\n");
	return 0;
}

char err_str[8192];
char linebuf[8192];
char *sockname = ME_CONTROL_SOCKNAME;
struct ctrlsock *ctrl_sock;

void cleanup()
{
	if (ctrl_sock)
		ctrl_close(ctrl_sock);
}

int handle_usage(char *kw, char *err_str)
{
	return ctrl_request(ctrl_sock, MECTL_LIST_PLUGINS, av_list, err_str);
}

int handle_list_models(char *kw, char *err_str)
{
	return ctrl_request(ctrl_sock, MECTL_LIST_MODELS, av_list, err_str);
}

int handle_plugin_load(char *kw, char *err_str)
{
	return ctrl_request(ctrl_sock, MECTL_LOAD_PLUGIN, av_list, err_str);
}

int handle_plugin_term(char *kw, char *err_str)
{
	return ctrl_request(ctrl_sock, MECTL_TERM_PLUGIN, av_list, err_str);
}

int handle_plugin_config(char *kw, char *err_str)
{
	return ctrl_request(ctrl_sock, MECTL_CFG_PLUGIN, av_list, err_str);
}

int handle_store(char *kw, char *err_str)
{
	return ctrl_request(ctrl_sock, MECTL_STORE, av_list, err_str);
}

int handle_model(char *kw, char *err_str)
{
	return ctrl_request(ctrl_sock, MECTL_MODEL, av_list, err_str);
}

int handle_create_model(char *kw, char *err_str)
{
	return ctrl_request(ctrl_sock, MECTL_CREATE, av_list, err_str);
}

int handle_start_consumer(char *kw, char *err_str)
{
	return ctrl_request(ctrl_sock, MECTL_START_CONSUMER, av_list, err_str);
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
	{ "config", handle_plugin_config },
	{ "consumer", handle_start_consumer },
	{ "create", handle_create_model },
	{ "help", handle_help },
	{ "list_model", handle_list_models },
	{ "load", handle_plugin_load },
	{ "model", handle_model },
	{ "quit", handle_quit },
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
	printf("Unrecognized keyword '%s'.", key.token);
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
		printf("Error setting up connection with ME.\n");
		exit(1);
	}
	atexit(cleanup);
	do {
		if (isatty(0))
			s = readline("me_ctl> ");
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
			printf("Unrecognized keyword '%s'.\n", key.token);
		if (err_str[0] != '\0')
			printf("%s\n", err_str);
	} while (s);
 	return 0;
}
