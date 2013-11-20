/**
 * \file rsyslog_tcp_test.c
 * \author Narate Taerat (narate@ogc.us)
 * rsyslog_tcp_test is implemented to hack the rsyslog message forwarding
 * over TCP. Surprisingly, the message sender will just do usual TCP connect
 * and then start sending messages right away. The only problem is that
 * at boot time, rsyslog will be able to start forwarding messages AFTER
 * the network has been brought up. rsyslog does not start forwarding messages
 * right away after the network is up though. From what I've seen, rsyslog will
 * start forwarding messages after NTP update.
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/queue.h>
#include <sys/ioctl.h>

#define PERR_EXIT(str) perror(str), exit(-1)

LIST_HEAD(pthread_head, pthread_list);

struct pthread_list {
	pthread_t t; /**< Thread variable */
	int sd; /**< Socket descriptor */
	LIST_ENTRY(pthread_list) link;
};

struct pthread_list* pthread_list_alloc(int sd)
{
	struct pthread_list *t = (typeof(t))calloc(1, sizeof(*t));
	if (!t)
		PERR_EXIT("pthread_list_alloc, calloc");
	t->sd = sd;
	return t;
}

void* routine(void* arg)
{
	struct pthread_list *l = (typeof(l))arg;
	int sd = l->sd;
	int bsz = 4096;
	char *buff = (char*)malloc(bsz);
	int rdsz;
	while (1) {
		rdsz = recv(sd, buff, bsz, 0);
		if (rdsz < 0)
			PERR_EXIT("recv");
		if (!rdsz)
			break; /* The connection is closed. */
		printf("%d: len: %d, data: \n", sd, rdsz);
		int i;
#if 0
		int fd = fileno(stdout);
		write(fd, buff, rdsz);
#else
		for (i=0; i<rdsz; i++) {
			printf("%c", buff[i]);
		}
#endif
		printf("------------------------\n");
	}
	free(buff);
}

int main(int argg, char **argv)
{
	struct pthread_head *head = (typeof(head))calloc(1, sizeof(*head));
	int sd = socket(AF_INET, SOCK_STREAM, 0);
	if (sd==-1)
		PERR_EXIT("socket");
	int reuse = 1;
	setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(9999);
	if (bind(sd, (void*)&addr, sizeof(addr)) == -1)
		PERR_EXIT("bind");
	if (listen(sd, 64) == -1)
		PERR_EXIT("listen");
	printf("listened on %d\n", ntohs(addr.sin_port));
	while (1) {
		int new_sd = accept(sd, NULL, NULL);
		printf("accepted: %d\n", new_sd);
		struct pthread_list *l = pthread_list_alloc(new_sd);
		int nonblock = 0;
		if (ioctl(new_sd, FIONBIO, &nonblock) == -1)
			PERR_EXIT("ioctl");
		pthread_create(&l->t, NULL, routine, l);
		LIST_INSERT_HEAD(head, l, link);
	}
	close(sd);
	printf("Good bye :)\n");
	return 0;
}
