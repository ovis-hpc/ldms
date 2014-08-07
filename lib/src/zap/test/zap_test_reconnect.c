#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <netinet/ip.h>
#include <sys/time.h>
#include <netdb.h>
#include "zap/zap.h"

char *short_opt = "h:x:p:sf?";
struct option long_opt[] = {
	{"host",     1,  0,  'h'},
	{"xprt",     1,  0,  'x'},
	{"port",     1,  0,  'p'},
	{"server",   0,  0,  's'},
	{"forever",  0,  0,  'f'},
	{"help",     0,  0,  '?'},
	{0,0,        0,  0}
};

void usage()
{
	printf(
"Usage: test [-x <XPRT>] [-p <PORT>] [-h <HOST>] [--forever] [-s]\n"
"\n"
"	The default XPRT is sock, and the default PORT is 55555.\n"
"	The default HOST is localhost. -s indicates server mode.\n"
"	With '--forever' (or -f), the client will keep sending messages to\n"
"	the server forever.\n"
	);
}

char *host = "localhost";
char *xprt = "rdma";
uint16_t port = 55555;
int server_mode = 0;
int forever = 0;

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
		port = strtoul(optarg, NULL, 10);
		break;
	case 'h':
		host = optarg;
		break;
	case 's':
		server_mode = 1;
		break;
	case 'f':
		forever = 1;
		break;
	case -1:
		goto out;
	case '?':
	default:
		usage();
		exit(0);
	}
	goto loop;
out:
	return;
}

zap_t zap;
pthread_cond_t reconnect_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t ep_mutex = PTHREAD_MUTEX_INITIALIZER;

int exiting = 0;
pthread_mutex_t exiting_mutex = PTHREAD_MUTEX_INITIALIZER;

int done = 0;

void zap_log(const char *fmt, ...)
{
	va_list l;
	va_start(l, fmt);
	vprintf(fmt, l);
	va_end(l);
}

zap_mem_info_t zap_mem_info(void)
{
	return NULL;
}

enum {
	DISCONNECTED,
	CONNECTING,
	CONNECTED
} flag;
pthread_mutex_t flag_lock = PTHREAD_MUTEX_INITIALIZER;
void server_cb(zap_ep_t zep, zap_event_t ev)
{
	struct sockaddr_in lsin = {0};
	struct sockaddr_in rsin = {0};
	socklen_t slen;
	char *data;

	switch (ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		zap_accept(zep, server_cb);
		break;
	case ZAP_EVENT_CONNECTED:
		pthread_mutex_lock(&flag_lock);
		flag = CONNECTED;
		pthread_mutex_unlock(&flag_lock);
		printf("connected\n");
		break;
	case ZAP_EVENT_DISCONNECTED:
		pthread_mutex_lock(&flag_lock);
		flag = DISCONNECTED;
		pthread_mutex_unlock(&flag_lock);
		zap_get_name(zep, (void*)&lsin, (void*)&rsin, &slen);
		printf("%X disconnected\n", rsin.sin_addr.s_addr);
		zap_close(zep);
		break;
	case ZAP_EVENT_RECV_COMPLETE:
		data = (char *)ev->data;
		printf("recv: %s\n", data);
		break;
	}
}

void client_cb(zap_ep_t zep, zap_event_t ev)
{

	struct sockaddr_in lsin = {0};
	struct sockaddr_in rsin = {0};


	struct timeval tv;
	gettimeofday(&tv, NULL);

	switch(ev->type) {
	case ZAP_EVENT_CONNECTED:
		flag = CONNECTED;
		printf("connected\n");
		break;
	case ZAP_EVENT_REJECTED:
	case ZAP_EVENT_CONNECT_ERROR:
	case ZAP_EVENT_DISCONNECTED:
		pthread_mutex_lock(&exiting_mutex);
		if (!exiting) {
			/* try reconnecting */
			printf("%s\n", zap_event_str(ev->type));
			flag = DISCONNECTED;
			zap_close(zep);
		}
		pthread_mutex_unlock(&exiting_mutex);
		break;
	}
}

void do_server(struct sockaddr_in *sin)
{
	zap_ep_t ep;
	zap_err_t zerr = zap_new(zap, &ep, server_cb);
	if (zerr) {
		printf("zap_new error: %d\n", zerr);
		exit(-1);
	}

	zerr = zap_listen(ep, (void*)sin, sizeof(*sin));
	if (zerr) {
		printf("zap_listen error: %d\n", zerr);
		exit(-1);
	}
	printf("Listening on port %hu\n", port);
	/* look for ^D to terminate the server */
	char c;
	while (read(0, &c, 1) > 0) {
		/* do nothing */
	}
}

void *send_msg(void *arg)
{
	struct sockaddr_in *sin = arg;
	struct sockaddr_in lsin = {0};
	struct sockaddr_in rsin = {0};
	socklen_t slen;
	struct timeval tv;
	zap_err_t zerr;
	int i, count;
	count = 10;
	char data[512];

	while (1) {
		zap_ep_t ep;
		pthread_mutex_lock(&flag_lock);
		if (flag != CONNECTED) {
			if (flag == CONNECTING) {
				pthread_mutex_unlock(&flag_lock);
				sleep(5);
				continue;
			}

			printf("reconnecting...\n");
			zerr = zap_new(zap, &ep, client_cb);
			if (zerr) {
				printf("zap_new error: %d\n", zerr);
				exit(-1);
			}
			printf("Connecting to %s:%hu\n", host, port);
			flag = CONNECTING;
			zerr = zap_connect(ep, (void*)sin, sizeof(*sin));
			if (zerr) {
				printf("zap_connect error: %d\n", zerr);
				exit(-1);
			}
			pthread_mutex_unlock(&flag_lock);
			sleep(2);
			continue;
		}

		pthread_mutex_unlock(&flag_lock);
		gettimeofday(&tv, NULL);
		zap_get_name(ep, (void*)&lsin, (void*)&rsin, &slen);
		for (i = 0; i < count; i++) {
			printf("%d: Sending %u.%u to %X\n", i,
				tv.tv_sec, tv.tv_usec,
				rsin.sin_addr.s_addr);
			sprintf(data, "%d: %u.%u", i, tv.tv_sec, tv.tv_usec);
			zerr = zap_send(ep, data, strlen(data));
			if (zerr)
				printf("Error %d in zap_send.\n", zerr);
		}

		sleep(2);
		if (!forever) {
			pthread_mutex_lock(&exiting_mutex);
			exiting = 1;
			pthread_mutex_unlock(&exiting_mutex);
			zerr = zap_close(ep);
			if (zerr)
				printf("Error %d in zap_close.\n", zerr);
			break;
		}
	}
	return NULL;
}

void do_client(struct sockaddr_in *sin)
{
	struct addrinfo *ai;

	int rc;

	rc = getaddrinfo(host, NULL, NULL, &ai);
	if (rc) {
		printf("getaddrinfo error: %d\n", rc);
		exit(-1);
	}
	*sin = *(typeof(sin))ai[0].ai_addr;
	sin->sin_port = htons(port);
	freeaddrinfo(ai);
	pthread_t t;
	rc = pthread_create(&t, NULL, send_msg, sin);
	if (rc) {
		printf("pthread_create error %d\n", rc);
		exit(-1);
	}
	pthread_join(t, NULL);
}

int main(int argc, char **argv)
{
	zap_err_t zerr;
	handle_args(argc, argv);
	zerr = zap_get(xprt, &zap, zap_log, zap_mem_info);
	if (zerr) {
		printf("zap_get error: %d\n", zerr);
		exit(-1);
	}

	struct sockaddr_in sin = {0};
	sin.sin_port = htons(port);
	sin.sin_family = AF_INET;

	if (server_mode)
		do_server(&sin);
	else
		do_client(&sin);
	sleep(1);
	return 0;
}
