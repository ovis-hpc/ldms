#include <stdio.h>
#include <getopt.h>
#include <stdint.h>
#include <stdlib.h>
#include <netinet/ip.h>
#include <stdarg.h>
#include <netdb.h>
#include <pthread.h>

#include "zap/zap.h"

char *short_opt = "h:x:p:?";

struct option long_opt[] = {
	{"host", required_argument, 0, 'h'},
	{"xprt", required_argument, 0, 'x'},
	{"port", required_argument, 0, 'p'},
	{"help", no_argument, 0, '?'},
	{0, 0, 0, 0}
};

void usage()
{
	printf(
"Usage: fake_me [OPTIONS] < INPUT_FILE\n"
"\n"
"INPUT_FILE contains text messages that will be converted into Komondor\n"
"		messages and send to komondor. The format of each text\n"
"		message is:\n"
"			MODEL_ID COMP_ID SEVERITY SEC USEC\n"
"\n"
"OPTIONS:\n"
"	-h,--host <HOST>	Komondor host (default: localhost).\n"
"	-x,--xprt <XPRT>	Zap transport (default: sock).\n"
"	-p,--port <PORT>	Komondor port (default: 55555).\n"
"	-?,--help		Print help message.\n"
	      );
}

#pragma pack(4)
struct kmd_msg {
	uint16_t model_id;
	uint64_t comp_id;
	uint8_t level;
	uint32_t sec;
	uint32_t usec;
};
#pragma pack()

char *host = "localhost";
char *xprt = "sock";
uint16_t port = 55555;

zap_t zap;
zap_ep_t zep;

void zap_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

zap_mem_info_t zap_mem_info(void)
{
	return NULL;
}

void handle_args(int argc, char **argv)
{
	char c;
loop:
	c = getopt_long(argc, argv, short_opt, long_opt, NULL);
	switch (c) {
	case 'h':
		host = optarg;
		break;
	case 'x':
		xprt = optarg;
		break;
	case 'p':
		port = atoi(optarg);
		break;
	case '?':
		usage();
		exit(-1);
		break;
	case -1:
		goto out;
		break;
	default:
		printf("Unknown option %c\n", c);
	}
	goto loop;
out:
	printf("host: %s\n", host);
	printf("xprt: %s\n", xprt);
	printf("port: %hu\n", port);
}

void __assert_zerr(zap_err_t zerr, char *name)
{
	if (zerr != ZAP_ERR_OK) {
		printf("%s error: %s\n", name, zap_err_str(zerr));
		exit(-1);
	}
}

void send_messages(zap_ep_t ep, zap_event_t ev)
{
	int n;
	struct kmd_msg msg;
loop:
	n = scanf(" %hu %"PRIu64" %hhu %"PRIu32" %"PRIu32,
			&msg.model_id, &msg.comp_id, &msg.level, &msg.sec,
			&msg.usec);
	if (n < 5)
		return;
	printf("sending: %hu %"PRIu64" %hhu %"PRIu32" %"PRIu32"\n",
			msg.model_id, msg.comp_id, msg.level, msg.sec,
			msg.usec);
	msg.model_id = htons(msg.model_id);
	msg.comp_id = htobe64(msg.comp_id);
	msg.sec = htonl(msg.sec);
	msg.usec = htonl(msg.usec);
	zap_send(ep, &msg, sizeof(msg));
	goto loop;
}

void zap_cb(zap_ep_t ep, zap_event_t ev)
{
	switch (ev->type) {
	case ZAP_EVENT_CONNECTED:
		printf("Connected\n");
		send_messages(ep, ev);
		break;
	case ZAP_EVENT_REJECTED:
		printf("Connection rejected\n");
		exit(-1);
		break;
	case ZAP_EVENT_DISCONNECTED:
		printf("Connection closed\n");
		exit(0);
		break;
	case ZAP_EVENT_CONNECT_ERROR:
		printf("Connection error\n");
		exit(-1);
		break;
	default:
		printf("Unhandled event: %d\n", ev->type);
	}
}

int main(int argc, char **argv)
{
	zap_err_t zerr;
	handle_args(argc, argv);
	zerr = zap_get(xprt, &zap, zap_log, zap_mem_info);
	__assert_zerr(zerr, "zap_get");
	zerr = zap_new(zap, &zep, zap_cb);
	__assert_zerr(zerr, "zap_new");

	struct hostent *he;
	he = gethostbyname(host);
	if (!he) {
		printf("Cannot resolve host %s\n", host);
		exit(-1);
	}
	if (he->h_addrtype != AF_INET) {
		printf("Unsupported network\n");
		exit(-1);
	}
	struct sockaddr_in sin = {0};
	sin.sin_family = AF_INET;
	sin.sin_addr = *(struct in_addr*)he->h_addr_list[0];
	sin.sin_port = htons(port);
	zerr = zap_connect(zep, (void*)&sin, sizeof(sin));
	__assert_zerr(zerr, "zap_connect");
	pthread_exit(NULL);
	return 0;
}
