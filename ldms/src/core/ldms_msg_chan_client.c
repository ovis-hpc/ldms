#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "ldms_msg_chan.h"
#include "ovis_util/util.h"

#define OPT_FMT "h:p:a:A:c:C:t:r:H:P:m:L:I:v:"

static char *verbose_s;

void usage(int argc, char *argv[])
{
	printf("usage: ldms_msg_chan_publish -h REM_HOST -p REM_PORT "
	       "-P PERM -a AUTH -A AUTH_OPT -r RECONNECT_SECS "
	       "-c PUB_CHAN_NAME -C SUB_CHAN_REGEX"
	       "-L LCL_PORT "
	       "-t MSG_TYPE -v SRING\n");
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
			printf("%s type: %s\n", verbose_s, ldms_msg_type_sym(ev->recv.type));
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
	printf("%s max_q_depth      : %zd\n", verbose_s, stats.max_q_depth);
	printf("%s cur_q_depth      : %zd\n", verbose_s, stats.cur_q_depth);
	printf("%s state            : %s\n", verbose_s,
	       ldms_msg_chan_state_str(stats.state));
	printf("%s pub_msg_cnt      : %zd\n", verbose_s, stats.pub_msg_cnt);
	printf("%s pub_byte_cnt     : %zd\n", verbose_s, stats.pub_byte_cnt);
	printf("%s pub_conn_attempt : %zd\n", verbose_s, stats.pub_conn_attempt);
	printf("%s pub_conn_success : %zd\n", verbose_s, stats.pub_conn_success);
	printf("%s pub_last_conn    : %10jd.%03ld (ms)\n", verbose_s,
	       (intmax_t) stats.pub_last_conn.tv_sec,
	       stats.pub_last_conn.tv_nsec / 1000000);
	struct tm tm;
	localtime_r(&stats.pub_last_conn.tv_sec, &tm);
	strftime(tm_s, sizeof(tm_s), "%a, %d %b %Y %T %z", &tm);
	printf("%s pub_last_conn    : %s\n", verbose_s, tm_s);
}

typedef void (*cmd_fn_t)(ldms_msg_chan_t chan, const char *s);
struct cmd_tbl_entry_s {
	const char *cmd_s;
	cmd_fn_t cmd_fn;
};
static struct cmd_tbl_entry_s cmd_tbl[] = {
	{ "log_level", log_level_fn },
	{ "stats", stats_fn },
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
	char *xprt_s = "sock";
	char *rem_host_s = NULL;
	char *rem_port_s = NULL;
	char *lcl_host_s = NULL;
	char *lcl_port_s = NULL;
	char *auth_s = "none";
	char *auth_opt_s = NULL;
	char *pub_name_s = NULL;
	char *sub_regex_s = NULL;
	ldms_msg_type_t msg_type = LDMS_MSG_STRING;
	char *msg_s = NULL;
	int reconnect = 5;
	char *log_lvl_s = NULL;
	uint32_t perm;

	while ((op = getopt(argc, argv, OPT_FMT)) != -1) {
		switch (op) {
		case 'h':
			rem_host_s = strdup(optarg);
			break;
		case 'p':
			rem_port_s = strdup(optarg);
			break;
		case 'H':
			lcl_host_s = strdup(optarg);
			break;
		case 'L':
			lcl_port_s = strdup(optarg);
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
		case 'm':
			msg_s = strdup(optarg);
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
		default:
			usage(argc, argv);
		}
	}
	if (!rem_host_s || !rem_port_s || !lcl_port_s || !pub_name_s)
		usage(argc, argv);

	if (!lcl_host_s)
		lcl_host_s = strdup("localhost");

	if (!sub_regex_s)
		sub_regex_s = ".*";

	av_list_t auth_avl = av_value_list(auth_opt_s, ",");
	ldms_msg_chan_t chan = ldms_msg_chan_new("ldms_msg_chan_client",
						 xprt_s,
						 rem_host_s,
						 rem_port_s,
						 lcl_host_s,
						 lcl_port_s,
						 auth_s,
						 auth_avl,
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

	size_t max_msg_size = 1024 * 1024;
	if (msg_s) {
		rc = ldms_msg_chan_publish(chan, pub_name_s,
					   geteuid(), getegid(), perm,
					   msg_type, msg_s, strlen(msg_s) + 1);
		if (rc) {
			printf("Error %d publishing message.\n", rc);
			exit(1);
		}
		int sleep_interval = 10;
		while (sleep_interval) {
			sleep(10);
		}
	} else {
		rc = ldms_msg_chan_subscribe(chan, sub_regex_s, subs_msg_cb, chan);
		if (rc) {
			printf("Error %d subscribing to channel \"%s\"\n", rc, sub_regex_s);
			exit(1);
		}
		char *s;
		msg_s = malloc(max_msg_size);
		while (NULL != (s = fgets(msg_s, max_msg_size, stdin))) {
			char *t = strstr(s, "\n");
			if (t)
				*t = '\0';
			/* Look for command escape sequenct */
			if (!verbose_s || (NULL == strstr(s, verbose_s)))
				goto skip_cmds;
			s += strlen(verbose_s);
			while (isspace(*s))
				s++;
			struct cmd_tbl_entry_s *ce;
			ce = bsearch(s, cmd_tbl, sizeof(cmd_tbl) / sizeof(cmd_tbl[0]),
				     sizeof(cmd_tbl[0]),
				     cmd_cmp);
			if (!ce) {
				printf("Unrecognized command '%s'\n", s);
				continue;
			}
			ce->cmd_fn(chan, s);
			continue;

		skip_cmds:
			rc = ldms_msg_chan_publish(chan, pub_name_s,
						   geteuid(), getegid(), perm,
						   msg_type, s, strlen(s) + 1);
			if (rc) {
				printf("Error %d publishing message.\n", rc);
				exit(1);
			}
		}
		free(msg_s);
	}
	exit(0);
}
