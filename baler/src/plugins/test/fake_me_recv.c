#include <stdio.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <getopt.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>

#include "zap/zap.h"

enum me_input_type {
	ME_INPUT_DATA,
	ME_NO_DATA
};

#pragma pack(4)
struct me_msg {
	enum me_input_type tag;
	uint32_t comp_id;
	uint32_t metric_type_id;
	struct timeval timestamp;
	double value;
};
#pragma pack()

const char *short_opt = "x:p:?";
struct option long_opt[] = {
	{"xprt", 1, 0, 'x'},
	{"port", 1, 0, 'p'},
	{"help", 0, 0, '?'},
	{0, 0, 0, 0}
};

pthread_mutex_t done_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t done_cond = PTHREAD_COND_INITIALIZER;

void usage()
{
	printf(
"Usage: fake_me_recv [-x XPRT] [-p PORT]\n"
"\n"
"	XPRT	can be sock or rdma (default: sock)\n"
"	PORT	listening port number (default: 55555)\n"
	);
}

char *xprt = "sock";
uint16_t port = 55555;

zap_t zap;
zap_ep_t ep;

void handle_args(int argc, char **argv)
{
	char c;
loop:
	c = getopt_long(argc, argv, short_opt, long_opt, NULL);
	switch (c) {
	case 'x':
		xprt = optarg;
		break;
	case 'p':
		port = atoi(optarg);
		break;
	case -1:
		/* No more args */
		return;
	default:
		usage();
		exit(-1);
	}
	goto loop;
}

void zerr_assert(zap_err_t zerr, const char *prefix)
{
	if (zerr) {
		printf("%s: %s\n", prefix, zap_err_str(zerr));
		exit(-1);
	}
}

void zap_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

zap_mem_info_t zap_meminfo(void)
{
	return NULL;
}

void print_me_msg(struct me_msg *msg)
{
	printf("----------\n");
	printf("metric_type_id: %u\n", msg->metric_type_id);
	printf("comp_id: %u\n", msg->comp_id);
	printf("tag: %u\n", msg->tag);
	printf("ts: %ld:%ld\n", msg->timestamp.tv_sec, msg->timestamp.tv_usec);
	printf("value: %lf\n", msg->value);
	printf("----------\n");
}

void handle_recv(zap_ep_t zep, zap_event_t ev)
{
	struct me_msg *msg;
	if (ev->data_len != sizeof(*msg)) {
		printf("Expecting len: %zu, but got %zu\n", sizeof(*msg),
				ev->data_len);
		return;
	}
	msg = ev->data;
	msg->metric_type_id = ntohl(msg->metric_type_id);
	msg->comp_id = ntohl(msg->comp_id);
	msg->tag = ntohl(msg->tag);
	msg->timestamp.tv_sec = ntohl(msg->timestamp.tv_sec);
	msg->timestamp.tv_usec = ntohl(msg->timestamp.tv_usec);
	print_me_msg(msg);
}

void callback(zap_ep_t zep, zap_event_t ev)
{
	switch (ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		printf("Connection request.\n");
		zap_accept(zep, callback);
		break;
	case ZAP_EVENT_DISCONNECTED:
		printf("Disconnected.\n");
		break;
	case ZAP_EVENT_CONNECT_ERROR:
		printf("Connect error ...\n");
		break;
	case ZAP_EVENT_CONNECTED:
		printf("Connected.\n");
		break;
	case ZAP_EVENT_RECV_COMPLETE:
		handle_recv(zep, ev);
		break;
	}
}

int main(int argc, char **argv)
{
	zap_err_t zerr;
	handle_args(argc, argv);
	zerr = zap_get(xprt, &zap, zap_log, zap_meminfo);
	zerr_assert(zerr, "zap_get");
	zerr = zap_new(zap, &ep, callback);
	zerr_assert(zerr, "zap_new");
	struct sockaddr_in sin = {0};
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	zerr = zap_listen(ep, (void*)&sin, sizeof(sin));
	zerr_assert(zerr, "zap_listen");
	pthread_mutex_lock(&done_mutex);
	pthread_cond_wait(&done_cond, &done_mutex);
	return 0;
}
