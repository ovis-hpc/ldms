#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>
#include <dlfcn.h>
#include <zap/zap.h>
#include "../../src/core/me.h"
#include <ovis_util/util.h>
#include <netdb.h>

#define PROD_EX_BUFFER 80

FILE *log_fp;
void pdc_ex_log(const char *fmt, ...)
{
	va_list ap;
	time_t t;
	struct tm *tm;
	char dtsz[200];

	t = time(NULL);
	tm = localtime(&t);
	if (strftime(dtsz, sizeof(dtsz), "%a %b %d %H:%M:%S %Y", tm))
		fprintf(log_fp, "%s: ", dtsz);
	va_start(ap, fmt);
	vfprintf(log_fp, fmt, ap);
	fflush(log_fp);
}

void read_metric_file(zap_ep_t zep, zap_event_t ev)
{
	struct me_msg msg;
	int msg_len;
	char *filename = "data";

	FILE *f;
	f = fopen(filename, "rt");
	if (!f) {
		printf("failed to open file %s\n", filename);
		exit(1);
	}

	char buffer[PROD_EX_BUFFER];
	int total_count, count;
	total_count = count = 1;
	char *tmp;
	zap_err_t zerr;
	while (1) {
		if (fgets(buffer, PROD_EX_BUFFER, f) == NULL || count == 1000) {
			fclose(f);
			count = 1;
			f = fopen(filename, "rt");
			fgets(buffer, PROD_EX_BUFFER, f);
			printf("Sleeping.\n");
			sleep(2);
		}
		msg.tag = htonl(ME_INPUT_DATA);

		tmp = strtok(buffer, ",");
		msg.metric_type_id = htonl(atoi(tmp));
		msg.comp_id = htonl(atoi(strtok(NULL, ",")));
		msg.value = htonl(atoi(strtok(NULL, ",")));
		gettimeofday(&msg.timestamp, NULL);

		zerr = zap_send(zep, (void *)&msg, sizeof(msg) + 1);
		if (zerr) {
			printf("Error sending data '%d'. ERROR '%d'\n",
					count + 1, errno);
			assert(0);
		}
		if (total_count % 1000 == 0) {
			printf("%d\n", total_count);
			msg.tag = htonl(ME_NO_DATA);
			gettimeofday(&msg.timestamp, NULL);
			zerr = zap_send(zep, (void *)&msg, sizeof(msg) + 1);
			if (zerr) {
				printf("Error sending data '%d'. ERROR '%d'\n",
						count + 1, errno);
				assert(0);
			}
			count++;
			total_count++;
		}
		count++;
		total_count++;
	}
}

void connect_cb(zap_ep_t zep, zap_event_t ev)
{
	zap_err_t z_err;

	switch (ev->type) {
	case ZAP_EVENT_DISCONNECTED:
		zap_close(zep);
		exit(1);
		break;
	case ZAP_EVENT_REJECTED:
		pdc_ex_log("The connect request was rejected.\n");
		exit(1);
		break;
	case ZAP_EVENT_CONNECTED:
		read_metric_file(zep, ev);
		break;
	case ZAP_EVENT_CONNECT_ERROR:
		pdc_ex_log("The connect request failed due to a network error.\n");
		exit(7);
		break;
	case ZAP_EVENT_CONNECT_REQUEST:
	case ZAP_EVENT_RECV_COMPLETE:
	case ZAP_EVENT_RENDEZVOUS:
	case ZAP_EVENT_READ_COMPLETE:
	case ZAP_EVENT_WRITE_COMPLETE:
	default:
		assert(0);
		break;
	}
}

int resolve(const char *hostname, struct sockaddr_in *sin)
{
	struct hostent *h;

	h = gethostbyname(hostname);
	if (!h) {
		printf("Error resolving hostname '%s'\n", hostname);
		return -1;
	}

	if (h->h_addrtype != AF_INET) {
		printf("Hostname '%s' resolved to an unsupported address family\n",
		       hostname);
		return -1;
	}

	memset(sin, 0, sizeof *sin);
	sin->sin_addr.s_addr = *(unsigned int *)(h->h_addr_list[0]);
	sin->sin_family = h->h_addrtype;
	return 0;
}

zap_mem_info_t test_meminfo(void)
{
	return NULL;
}

int main(int argc, char *argv[])
{
	if (argc < 5) {
		printf("[1:0] hostname xprt:port_no logfile\n");
		exit(1);
	}

	char *hostname = strdup(argv[2]);
	char *xprt = argv[3];
	char *xprt_name = strtok(xprt, ":");
	char *port_no_s = strtok(NULL, ":");

	struct sockaddr_in sin;
	memset(&sin, 0, sizeof sin);
	if (resolve(hostname, &sin)) {
		printf("failed to resolve.\n");
	}
	sin.sin_port = htons(atoi(port_no_s));

	char *logfile = strdup(argv[4]);
	log_fp = fopen(logfile, "a");

	zap_t zap;
	zap_err_t zerr;

	zerr = zap_get(xprt_name, &zap, pdc_ex_log, test_meminfo);
	if (zerr) {
		printf("Error %d: Failed to create zap.\n", zerr);
		exit(1);
	}

	zap_ep_t zep;
	zerr = zap_new(zap, &zep, connect_cb);
	if (zerr) {
		printf("Error %d: Error in new zap endpoint.\n", zerr);
		exit(1);
	}

	zerr = zap_connect(zep, (struct sockaddr *)&sin, sizeof(sin));
	if (zerr) {
		printf("Error %d: failed in sending a connect request.\n", zerr);
		exit(1);
	}

	char *pi_name;
	if (atoi(argv[1]) == 1)
		pi_name = "producer_intf_ex";
	else
		pi_name = "producer_intf_ex_a\0";

	printf("%s\n", pi_name);

	while (1) {
		/* Keep thread alive. */
	}

	printf("done\n");
	return 0;
}
