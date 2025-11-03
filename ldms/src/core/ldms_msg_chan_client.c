#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "ldms_msg_chan.h"
#include "ovis_util/util.h"

#define OPT_FMT "h:p:a:A:c:C:t:r:H:P:m:L:I:v:f:u"

static char *verbose_s;

void usage(int argc, char *argv[])
{
	printf("usage: ldms_msg_chan_publish "
	       "[-h REM_HOST] -p REM_PORT "
	       "[-f PATH] "
	       "[-H LCL_HOST] -L LCL_PORT "
	       "[-P PERM -a AUTH -A AUTH_OPTS -r RECONNECT_SECS]]"
	       "-c PUB_CHAN_NAME -C SUB_CHAN_REGEX "
	       "[-t MSG_TYPE] [-v LOG_STR] "
	       "-u\n");
	printf("    -m MODE         The channel mode is one of:\n");
	printf("                        \"subscribe\", \"publish\", or \"bidir\".\n");
	printf("                        (default is \"bidir\")\n");
	printf("    -h REM_HOST     The remote host name (publish), default is\n");
	printf("                        \"localhost\".\n");
	printf("    -p REM_PORT     The remote port number (publish).\n");
	printf("    -f PATH         A file from which data will be published.\n");
	printf("                        (default is stdin)\n");
	printf("    -H LCL_HOST     The local host name (subscribe), default is\n");
	printf("                        \"localhost\".\n");
	printf("    -L LCL_PORT     The local port number (subscribe)\n");
	printf("    -a AUTH_NAME    The authentication plugin name (default is \"none\").\n");
	printf("    -A AUTH_OPTS    An optional comma separated list of authencation\n");
	printf("                        plugin otions\n");
	printf("    -A AUTH_OPTS    An optional comma separated list of authencation\n");
	printf("                    plugin otions\n");
	printf("    -P PERM         The permission bits in the message header to\n");
	printf("                        authorize remote peers. (default is 0660)\n");
	printf("    -t MSG_TYPE     The published message type. One of \"json\",\n");
	printf("                        \"string\", or \"avro\". (default is \"string\") \n");
	printf("    -c PUB_TAG      The message tag with which to send published messages.\n");
	printf("    -C SUB_TAG      The message tag (regex) with which to subscribe for\n");
	printf("                        message receipt.\n");
	printf("    -r RECONNECT    The reconnect interval in seconds (publish), default is 6s.\n");
	printf("    -v LOG_STR      A comma separated list of log level names.\n");
	printf("                        (default is \"error\")\n");
	printf("    -u Unsubscribe after any published I/O is complete.\n");
	exit(1);
}

int subs_msg_cb(ldms_msg_event_t ev, void *cb_arg)
{
	/* cb_arg is the pointer supplied to ldms_msg_subscribe() */
	switch (ev->type) {
	case LDMS_MSG_EVENT_RECV:
		if (verbose_s) {
			printf("%s name: %s\n", verbose_s, ev->recv.name);
			printf("%s hop : %d\n", verbose_s, ev->hop_num);
			printf("%s type: %s\n", verbose_s,
			       ldms_msg_type_sym(ev->recv.type));
		}
		printf("%s\n", ev->recv.data);
		fflush(stdout);
		/* See `struct ldms_msg_event_s` for more information. */
		break;
	case LDMS_MSG_EVENT_CLIENT_CLOSE:
		/* This is the last event guaranteed to be delivered
		 * to this client. The resources associated with this
		 * client (e.g. cb_arg) can be safely freed at this
		 * point. */
		break;
	default:
		/* ignore other events */;
	}
	return 0;
}

void log_level_fn(ldms_msg_chan_t chan, const char *s)
{
	ovis_log_t log = ldms_msg_chan_log(chan);
	int level;
	char *log_lvl_s = (char *)s;
	s = strsep(&log_lvl_s, " ");
	if (!log_lvl_s) {
		printf("%s The log_level command is missing the level string\n",
		       verbose_s);
		return;
	}
	level = ovis_log_str_to_level(log_lvl_s);
	if (level < 0)
		printf("%s Invalid log level string '%s' specified\n",
		       verbose_s, log_lvl_s);
	else
		ovis_log_set_level(log, level);
}

static void stats_fn(ldms_msg_chan_t chan, const char *s)
{
	char tm_s[256];
	struct ldms_msg_chan_stats_s stats;
	ldms_msg_chan_stats(chan, &stats);
	printf("%s mode             : %s\n", verbose_s,
	       ldms_msg_chan_mode_str(stats.mode));
	printf("%s max_q_depth      : %zd\n", verbose_s, stats.max_q_depth);
	printf("%s cur_q_depth      : %zd\n", verbose_s, stats.cur_q_depth);
	printf("%s state            : %s\n", verbose_s,
	       ldms_msg_chan_state_str(stats.state));
	printf("%s sub_msg_cnt      : %zd\n", verbose_s, stats.sub_msg_cnt);
	printf("%s sub_byte_cnt     : %zd\n", verbose_s, stats.sub_byte_cnt);
	printf("%s pub_msg_sent     : %zd\n", verbose_s, stats.pub_msg_sent);
	printf("%s pub_msg_acked    : %zd\n", verbose_s, stats.pub_msg_acked);
	printf("%s pub_byte_cnt     : %zd\n", verbose_s, stats.pub_byte_cnt);
	printf("%s pub_conn_attempt : %zd\n", verbose_s, stats.pub_conn_attempt);
	printf("%s pub_conn_success : %zd\n", verbose_s, stats.pub_conn_success);
	printf("%s pub_last_conn    : %10jd.%03ld (s)\n", verbose_s,
	       (intmax_t) stats.pub_last_conn.tv_sec,
	       stats.pub_last_conn.tv_nsec / 1000000);
	struct tm tm;
	localtime_r(&stats.pub_last_conn.tv_sec, &tm);
	strftime(tm_s, sizeof(tm_s), "%a, %d %b %Y %T %z", &tm);
	printf("%s pub_last_conn    : %s\n", verbose_s, tm_s);
	printf("%s pub_blocked      : %zd\n", verbose_s, stats.pub_blocked);
	jbuf_t jb = ldms_msg_chan_subscr_json(chan);
	printf("%s subscribers      : %s\n", verbose_s, jb->buf);
}

static void subscribe_fn(ldms_msg_chan_t chan, const char *s)
{
	int rc;
	char *regex_s = (char *)s;
	s = strsep(&regex_s, " ");
	if (!regex_s) {
		printf("%s The unsubscribe command is missing the "
		       "regular expression string\n",
		       verbose_s);
		return;
	}
	rc = ldms_msg_chan_subscribe(chan, regex_s, subs_msg_cb, chan);
	if (rc)
		printf("%s Error %d subscribing to channel  '%s'.\n",
		       verbose_s, rc, regex_s);
}

static void unsubscribe_fn(ldms_msg_chan_t chan, const char *s)
{
	int rc;
	char *regex_s = (char *)s;

	s = strsep(&regex_s, " ");
	if (!regex_s) {
		printf("%s The unsubscribe command is missing the "
		       "regular expression string\n",
		       verbose_s);
		return;
	}

	rc = ldms_msg_chan_unsubscribe(chan, regex_s);
	if (rc)
		printf("%s Error %d unsubscribing from '%s'.\n", verbose_s, rc, regex_s);
}

typedef void (*cmd_fn_t)(ldms_msg_chan_t chan, const char *s);
struct cmd_tbl_entry_s {
	const char *cmd_s;
	cmd_fn_t cmd_fn;
};

static struct cmd_tbl_entry_s cmd_tbl[] = {
	{ "log_level", log_level_fn },
	{ "stats", stats_fn },
	{ "subscribe", subscribe_fn },
	{ "unsubscribe", unsubscribe_fn },
};

static int cmd_cmp(const void *_a, const void *_b)
{
	const char *a = _a;
	const struct cmd_tbl_entry_s *b = (struct cmd_tbl_entry_s *)_b;

	return strncmp(a, b->cmd_s, strlen(b->cmd_s));
}

int main(int argc, char *argv[])
{
	int rc, op;
	FILE *file = stdin;
	char *xprt_s = "sock";
	ldms_msg_chan_mode_t mode = LDMS_MSG_CHAN_MODE_BIDIR;
	char *mode_s = NULL;
	char *rem_host_s = "localhost";
	int rem_port = -1;
	char *lcl_host_s = "localhost";
	int lcl_port = -1;
	char *auth_s = "none";
	char *auth_opt_s = NULL;
	char *pub_name_s = NULL;
	char *sub_regex_s = NULL;
	ldms_msg_type_t msg_type = LDMS_MSG_STRING;
	int reconnect = 5;
	char *log_lvl_s = NULL;
	uint32_t perm = 0660;
	int unsubscribe = 0;

	while ((op = getopt(argc, argv, OPT_FMT)) != -1) {
		switch (op) {
		case 'f':
			file = fopen(optarg, "r");
			if (!file) {
				printf("Error %d opening the input file '%s'.\n",
				       errno, optarg);
				exit(1);
			}
			break;
		case 'm':
			mode_s = strdup(optarg);
			break;
		case 'h':
			rem_host_s = strdup(optarg);
			break;
		case 'p':
			rem_port = atoi(optarg);
			break;
		case 'H':
			lcl_host_s = strdup(optarg);
			break;
		case 'L':
			lcl_port = atoi(optarg);
			break;
		case 'a':
			auth_s = strdup(optarg);
			break;
		case 'A':
			auth_opt_s = strdup(optarg);
			break;
		case 'c':
			pub_name_s = strdup(optarg);
			break;
		case 'C':
			sub_regex_s = strdup(optarg);
			break;
		case 't':
			if (0 == strncasecmp(optarg, "json", 4)) {
				msg_type = LDMS_MSG_JSON;
			} else if (0 == strncasecmp(optarg, "string", 6)) {
				msg_type = LDMS_MSG_STRING;
			} else {
				printf("Bad message type '%s'\n", optarg);
				usage(argc, argv);
			}
			break;
		case 'r':
			reconnect = atoi(optarg);
			break;
		case 'P':
			perm = strtol(optarg, NULL, 0);
			break;
		case 'I':
			log_lvl_s = strdup(optarg);
			break;
		case 'v':
			verbose_s = strdup(optarg);
			break;
		case 'u':
			unsubscribe = 1;
			break;
		default:
			usage(argc, argv);
		}
	}

	if (mode_s) {
		if (0 == strcmp(mode_s, "publish")) {
			mode = LDMS_MSG_CHAN_MODE_PUBLISH;
		} else if (0 == strcmp(mode_s, "subscribe")) {
			mode = LDMS_MSG_CHAN_MODE_SUBSCRIBE;
		} else if (0 == strcmp(mode_s, "bidir")) {
			mode = LDMS_MSG_CHAN_MODE_BIDIR;
		} else {
			printf("The mode specified '%s' is invalid.\n", mode_s);
			exit(1);
		}
	}

	if (mode & LDMS_MSG_CHAN_MODE_PUBLISH) {
		if (rem_port < 0 || !pub_name_s) {
			printf("The remote port and publish channel name are required.\n");
			usage(argc, argv);
		}
	}

	if (mode & LDMS_MSG_CHAN_MODE_SUBSCRIBE) {
		if (lcl_port < 0 || !sub_regex_s) {
			printf("The local port and subscriber regex are required.\n");
			usage(argc, argv);
		}
	}

	av_list_t auth_avl = av_value_list(auth_opt_s, ",");
	ldms_msg_chan_t chan =
		ldms_msg_chan_new("ldms_msg_chan_client",
				  mode, xprt_s,
				  rem_host_s, rem_port,
				  lcl_host_s, lcl_port,
				  auth_s, auth_avl,
				  reconnect);
	if (!chan)
		exit(1);

	if (log_lvl_s) {
		int level = ovis_log_str_to_level(log_lvl_s);
		ovis_log_t log = ldms_msg_chan_log(chan);
		if (level < 0) {
			printf("Invalid log level string specified\n");
			exit(1);
		}
		ovis_log_set_level(log, level);
	}

	if (mode & LDMS_MSG_CHAN_MODE_SUBSCRIBE) {
		rc = ldms_msg_chan_subscribe(chan, sub_regex_s, subs_msg_cb, chan);
		if (rc) {
			printf("Error %d subscribing to channel \"%s\"\n", rc, sub_regex_s);
			exit(1);
		}
	}

	if (mode & LDMS_MSG_CHAN_MODE_PUBLISH) {
		char *lbuf = NULL;
		size_t lbuf_sz = 0;
		ssize_t cnt;
		int cmd_pfx_sz;
		if (verbose_s)
			cmd_pfx_sz = strlen(verbose_s);
		while ((cnt = getline(&lbuf, &lbuf_sz, file)) > 0) {
			if (verbose_s && (0 == strncmp(lbuf, verbose_s, cmd_pfx_sz))) {
				char *cmd_s, *end_s;
				cmd_s = &lbuf[cmd_pfx_sz];
				while (*cmd_s != '\0' && isspace(*cmd_s))
					cmd_s ++;
				if (*cmd_s == '\0') {
					printf("'%s' command prefix is missing the "
					       "command.\n",
					       verbose_s);
					continue;
				}
				end_s = cmd_s;
				while (*end_s != '\0' && !isspace(*end_s))
					end_s ++;
				*end_s = '\0';
				struct cmd_tbl_entry_s *ce;
				ce = bsearch(cmd_s,
					     cmd_tbl, sizeof(cmd_tbl) / sizeof(cmd_tbl[0]),
					     sizeof(cmd_tbl[0]),
					     cmd_cmp);
				if (!ce) {
					printf("'%s' is an unrecognized command\n",
					       cmd_s);
				} else {
					ce->cmd_fn(chan, lbuf);
				}
				continue;
			}
			rc = ldms_msg_chan_publish(chan, pub_name_s,
						   geteuid(), getegid(), perm,
						   msg_type, lbuf, cnt + 1);
			if (rc) {
				printf("Error %d publishing message.\n", rc);
				exit(1);
			}
		}
		free(lbuf);
	}
	if (unsubscribe)
		ldms_msg_chan_unsubscribe(chan, sub_regex_s);
	if (mode & LDMS_MSG_CHAN_MODE_SUBSCRIBE) {
		do {
			sleep(6);
			if (!verbose_s)
				verbose_s = "%%";
			stats_fn(chan, "");
		} while (1);
	}
	ldms_msg_chan_close(NULL, 0);
	exit(0);
}
