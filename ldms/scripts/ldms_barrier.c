/**
 * Free-standing interprocess barrier implementation.
 * Protocol not secured in any way.
 * Server can wait for N anonymous connections
 * or a list of named connections.
 * The server and clients are invoked nearly identically: this is intentional.
 *
 * Typical use with tags is:
 * barrier -t $timeoutseconds -c $myname -p $serverportnumber -i $preferred_server_device -s $server_name -m $nc [tag...]
 * where nc is the number of tags (or a slight overestimate).
 * For the caller in the group that will be the server for rendezvous, add the -l option..
 * Will wait until clients posting all tags given have connected (or timeout).
 * Without tags expected use is:
 * barrier -t $timeoutseconds  -p $serverportnumber -i $preferred_server_device -s $server_name -m $nc
 * For the caller in the group that will be the server for rendezvous, add the -l option.
 * Will wait until nc connections occur (or timeout).
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define TINKER_SO 0

#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <limits.h>

int verbose; // 1 noise, 2 timing noise, 3 retry noise
#define PRINTF(args...) \
	if (verbose) printf(args)

#define PRINTF2(args...) \
	if (verbose > 1) printf(args)

#define PRINTF3(args...) \
	if (verbose > 2) printf(args)

// client bits
#define DEFAULT_INTERFACE "lo"  // Default network interface
#define DEFAULT_MAX_CLIENTS 3	  // Default number of clients
#define DEFAULT_TIMEOUT 1000000000  // Default timeout in seconds infinity
#define DEFAULT_RETRY_INTERVAL 0.1    // Default retry interval (seconds)

// server bits
#define DEFAULT_HOSTNAME "localhost"  // Default hostname
#define DEFAULT_PORT 8080	  // Default port number
#define DEFAULT_PORT_S "8080"

char server_name[HOST_NAME_MAX] = DEFAULT_HOSTNAME;

/////////////////////// client functions
/// look for server until tmax, at retry_interval second intervals.
int connect_to_server(struct sockaddr *server_addr, socklen_t addr_len,
		      double retry_interval, const struct timespec * tmax)
{
	int sock = 0;

	struct timespec tnow;
	struct timespec tsleep;
	struct timespec trem;
	tsleep.tv_sec = floor(retry_interval);
	tsleep.tv_nsec = fmod(retry_interval, 1)*1000000000;
	clock_gettime(CLOCK_MONOTONIC, &tnow);
	while (tnow.tv_sec < tmax->tv_sec ) {
		sock = socket(server_addr->sa_family, SOCK_STREAM, 0);
		if (sock < 0) {
			perror("Socket creation failed");
			exit(EXIT_FAILURE);
		}

		if (connect(sock, server_addr, addr_len) == 0) {
			return sock;
		}

		PRINTF3("Server not available, retrying in %g seconds\n",
		       retry_interval);
		close(sock);
		nanosleep(&tsleep, &trem);
		clock_gettime(CLOCK_MONOTONIC, &tnow);
	}

	PRINTF("Client failed to connect before timeout\n");
	errno = ETIMEDOUT;
	return -1;
}

void resolve_hostname(const char *hostname,
		      struct sockaddr_storage *server_addr,
		      socklen_t *addr_len)
{
	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;  // Allow IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(hostname, NULL, &hints, &res) != 0) {
		perror("Hostname resolution failed");
		exit(EXIT_FAILURE);
	}

	memcpy(server_addr, res->ai_addr, res->ai_addrlen);
	*addr_len = res->ai_addrlen;

	char ip_str[INET6_ADDRSTRLEN];
	inet_ntop(res->ai_family,
		  res->ai_family == AF_INET
			  ? (void *)&((struct sockaddr_in *)res->ai_addr)
				    ->sin_addr
			  : (void *)&((struct sockaddr_in6 *)res->ai_addr)
				    ->sin6_addr,
		  ip_str, sizeof(ip_str));

	PRINTF("Resolved hostname %s -> IP %s (family: %s)\n", hostname, ip_str,
	       res->ai_family == AF_INET ? "IPv4" : "IPv6");

	freeaddrinfo(res);
}


int client_main(int timeout, const char *client_tag, const char *port, double retry_interval)
{
	struct sockaddr_storage server_addr;
	socklen_t addr_len;
	char *hostname = server_name;
	int iport = atoi(port);
	int rc = 0;

	if (iport <= 0 || iport > 65534) {
		fprintf(stderr, "Invalid port number %s\n", port);
	}

	// Resolve hostname to IP (IPv4 or IPv6)
	resolve_hostname(hostname, &server_addr, &addr_len);

	// Set the port for IPv4 or IPv6
	if (server_addr.ss_family == AF_INET) {
		((struct sockaddr_in *)&server_addr)->sin_port = htons(iport);
	} else {
		((struct sockaddr_in6 *)&server_addr)->sin6_port = htons(iport);
	}

	PRINTF("Connecting to server on port %d...\n", iport);

	struct timespec tmax;
	clock_gettime(CLOCK_MONOTONIC, &tmax);
	tmax.tv_sec += (timeout+1); // pad the timeout a little so we can ignore nsec
	char buffer[1024] = {0};
	// Attempt to connect to the server
	int sock = connect_to_server((struct sockaddr *)&server_addr, addr_len,
				     retry_interval, &tmax);
	if (sock < 0) {
		rc = errno;
	} else {
#if TINKER_SO
		struct linger so_linger;
		so_linger.l_onoff = 1;
		so_linger.l_linger = 1;
		int z = setsockopt(sock, SOL_SOCKET, SO_LINGER, &so_linger, sizeof so_linger);
		if ( z ) {
			perror("cannot set linger");
		}
#endif
		// Send ready signal
		if (!client_tag) {
			client_tag = "READY";
		}
		send(sock, client_tag, strlen(client_tag)+1, 0);
		struct timespec trcv;
		clock_gettime(CLOCK_MONOTONIC, &trcv);
		PRINTF2("CS: %s: %ld.%09ld\n", client_tag, trcv.tv_sec, trcv.tv_nsec);
		PRINTF("Sent %s tag, waiting for reply...\n", client_tag);

		recv(sock, buffer, sizeof(buffer), 0);
		// expect ok, timeout, error strings
		if (strcmp(buffer, "ok") == 0) {
			PRINTF("Received ok\n");
		} else if (strcmp(buffer, "timeout") == 0) {
			PRINTF("Received timeout\n");
			rc = ETIMEDOUT;
		} else if (strcmp(buffer, "error") == 0) {
			PRINTF("Server error\n");
			rc = EPROTO;
		} else {
			PRINTF("Unexpected error: %s\n", buffer);
			rc = EBADE;
		}
		clock_gettime(CLOCK_MONOTONIC, &trcv);
		PRINTF3("CR: %s: %ld.%09ld\n", client_tag, trcv.tv_sec, trcv.tv_nsec);

		close(sock);
	}
	return rc;
}

//////////////// server support functions
void handle_client(int client_sock, int ntags, const char **tags, int *client, int cidx)
{
	char buffer[1024] = {0};
	recv(client_sock, buffer, sizeof(buffer), 0);
	if (!ntags) {
		if (strcmp(buffer, "READY") == 0) {
			PRINTF("Some daemon checked in\n");
		}
		client[cidx] = client_sock;
	} else {
		int t = 0;
		int found = 0;
		for (t = 0; t < ntags; t++) {
			if (tags[t] && strcmp(buffer, tags[t]) == 0) {
				PRINTF3("Daemon %s checked in: \n", buffer);
				found = 1;
				tags[t] = NULL;
				struct timespec trcv;
				clock_gettime(CLOCK_MONOTONIC, &trcv);
				PRINTF2("SR: daemon %s: %ld.%09ld\n", buffer, trcv.tv_sec, trcv.tv_nsec);
				client[t] = client_sock;
				break;
			}
		}
		if (!found) {
			PRINTF("Unexpected or repeated daemon %s ignored: \n", buffer);
		}
	}
}

void resolve_interface_ip(const char *interface, char *ip_buffer)
{
	struct ifaddrs *ifaddr, *ifa;
	if (getifaddrs(&ifaddr) == -1) {
		perror("getifaddrs failed");
		exit(EXIT_FAILURE);
	}

	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL ||
		    strcmp(ifa->ifa_name, interface) != 0)
			continue;

		if (ifa->ifa_addr->sa_family == AF_INET) {
			struct sockaddr_in *addr =
				(struct sockaddr_in *)ifa->ifa_addr;
			inet_ntop(AF_INET, &addr->sin_addr, ip_buffer,
				  INET_ADDRSTRLEN);
			break;
		} else if (ifa->ifa_addr->sa_family == AF_INET6) {
			struct sockaddr_in6 *addr =
				(struct sockaddr_in6 *)ifa->ifa_addr;
			inet_ntop(AF_INET6, &addr->sin6_addr, ip_buffer,
				  INET6_ADDRSTRLEN);
			break;
		}
	}

	freeifaddrs(ifaddr);
}

//////////////// driver
int usage(const char *prog)
{
	fprintf(stderr, "Usage: %s "
		"[-d,--debug-label <label>] "
		"[-c,--client-tag <tag>] "
		"[-m,--max-clients <count>] "
		"[-t,--timeout <seconds>] "
		"[-r,--retry-interval <seconds(float)>] "
		"[-s,--server <server_hostname>] "
		"[-i,--interface <name>] "
		"[-p,--port <number>] "
		"[-l,--leader] "
		"[-v,--verbose] "
		"[-h,--help] "
		"\n",
		prog);
	return EXIT_FAILURE;
}

int main(int argc, char *argv[])
{
	char interface[256] = DEFAULT_INTERFACE;
	char ip_address[INET6_ADDRSTRLEN];
	char port[6] = DEFAULT_PORT_S;
	int max_clients = DEFAULT_MAX_CLIENTS;
	int timeout = DEFAULT_TIMEOUT;
	struct addrinfo hints, *res, *p;
	int server_sock, clients_ready = 0;
	int is_server = 0;
	char client_tag[256] = { '\0' };
	char debug_label[256] = { '\0' }; // defaults to host-$pid
	double client_retry_interval = DEFAULT_RETRY_INTERVAL;

	static struct option long_options[] = {
		{"client-tag", required_argument, 0, 'c'},
		{"server", required_argument, 0, 's'},
		{"interface", required_argument, 0, 'i'},
		{"max-clients", required_argument, 0, 'm'},
		{"port", required_argument, 0, 'p'},
		{"timeout", required_argument, 0, 't'},
		{"retry-interval", required_argument, 0, 'r'},
		{"debug-label", required_argument, 0, 'd'},
		{"verbose", no_argument, 0, 'v'},
		{"leader", no_argument, 0, 'l'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}};

	int opt;
	while ((opt = getopt_long(argc, argv, "d:c:i:p:m:t:s:r:vl", long_options,
				  NULL)) != -1) {
		switch (opt) {
			case 'h':
				exit(usage(argv[0]));
				break;
			case 'r':
				client_retry_interval = atof(optarg);
				if (client_retry_interval <= 0.001) {
					fprintf(stderr, "Invalid retry-interval %s.\n",
						optarg);
					exit(EINVAL);
				}
				break;
			case 'd':
				strncpy(debug_label, optarg, sizeof(debug_label)-1);
				break;
			case 's':
				strncpy(server_name, optarg, sizeof(server_name)-1);
				break;
			case 'l':
				is_server = 1;
				break;
			case 'v':
				verbose++;
				break;
			case 'c':
				strncpy(client_tag, optarg, sizeof(client_tag)-1);
				break;
			case 'i':
				strncpy(interface, optarg, sizeof(interface)-1);
				break;
			case 'p':
				strncpy(port, optarg, sizeof(port)-1);
				break;
			case 'm':
				max_clients = atoi(optarg);
				if (max_clients <= 0) {
					fprintf(stderr, "Invalid max clients.\n");
					exit(EINVAL);
				}
				break;
			case 't':
				timeout = atoi(optarg);
				if (timeout < 0) {
					fprintf(stderr, "Invalid timeout %s.\n", optarg);
					exit(EINVAL);
				}
				if (!timeout)
					timeout = DEFAULT_TIMEOUT;
				break;
			default:
				exit(usage(argv[0]));
		}
	}
	if (debug_label[0] == '\0') {
		char hname[230];
		gethostname(hname, sizeof(hname));
		sprintf(debug_label, "%s:pid-%ld", hname, (long) getpid());
	}
	const char *tags[max_clients + 1];
	const char *print_tags[max_clients + 1];

	int ntags = -max_clients;  // if stays negative, tags are ignored and we
				   // just want max_clients connections.
	if (optind < argc && is_server) {
		ntags = argc - optind;
		int next = 0;
		/* copy all client tags but ours into the servers wait tags list
		 */
		while (optind < argc) {
			if (next > max_clients) {
				fprintf(stderr, "%s%s%sMore tags given than"
				       " allowed by -m %d\n",
				       (debug_label[0] != '\0')? "(" : "",
				       (debug_label[0] != '\0')? debug_label : "",
				       (debug_label[0] != '\0')? ")" : "",
				       max_clients);
				exit(EINVAL);
			}
			if (strcmp(argv[optind], client_tag)) {
				PRINTF("got tag %s\n", argv[optind]);
				tags[next++] = argv[optind++];
			 } else {
				PRINTF("server tag %s\n", argv[optind]);
				optind++;
				ntags--;
			 }
		}
		if (!next) {
			PRINTF("no tags except the server itself. no waiting.\n");
			exit(0);  // self is the only expected client, so we're
				  // done.
		}
		while (next < max_clients) tags[next++] = NULL;
		for (next = 0; next < max_clients; next++) {
			print_tags[next] = tags[next];
		}
	}

	if (is_server) {
		int client[max_clients + 1];
		for (optind = 0; optind <= max_clients; optind++) {
			client[optind] =-1;
		}
		//// do listen for all clients and reply en-mass
		// Resolve interface IP
		resolve_interface_ip(interface, ip_address);
		PRINTF("Binding to interface %s (IP: %s)\n", interface,
			ip_address);

		// Set up hints for getaddrinfo()
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;  // IPv4 or IPv6
		hints.ai_socktype = SOCK_STREAM;
		// Bind to all available interfaces
		hints.ai_flags = AI_PASSIVE;

		int s;
		if ((s = getaddrinfo(ip_address, port, &hints, &res)) != 0) {
			fprintf(stderr, "getaddrinfo failed: %s\n",
				gai_strerror(s));
			exit(EXIT_FAILURE);
		}

		// Try each returned address until one works
		for (p = res; p != NULL; p = p->ai_next) {
			server_sock = socket(p->ai_family, p->ai_socktype,
						p->ai_protocol);
			if (server_sock == -1) continue;

			if (bind(server_sock, p->ai_addr, p->ai_addrlen) == 0)
				break;	// Success

			close(server_sock);
		}

		freeaddrinfo(res);
		if (p == NULL) {
			perror("Failed to bind");
			exit(EXIT_FAILURE);
		}

		// if is_server, listen until max_clients or tag list is
		// satisfied or timed out. else: contact server and wait until
		// reply is complete or timed out. Set timeout using SO_RCVTIMEO
		struct timeval tv;
		tv.tv_sec = timeout;
		tv.tv_usec = 0;
		setsockopt(server_sock, SOL_SOCKET, SO_RCVTIMEO, &tv,
			sizeof(tv));
#if TINKER_SO
		// reuse addr; may need to reuse port also.
		int reuse = 1;
		setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &reuse,
			sizeof(reuse));
		setsockopt(server_sock, SOL_SOCKET, SO_REUSEPORT, &reuse,
			sizeof(reuse));
		struct linger so_linger;
		so_linger.l_onoff = 1;
		so_linger.l_linger = 1;
		setsockopt(server_sock, SOL_SOCKET, SO_LINGER, &so_linger, sizeof so_linger);
#endif

		listen(server_sock, max_clients);
		struct sockaddr_storage client_addr;
		socklen_t client_addr_len = sizeof(client_addr);

		int client_result = 0;
		if (ntags <= 0) {
			PRINTF("Server started on %s:%s, waiting for %d clients "
			       "(Timeout: %d sec) %s%s%s...\n",
				       ip_address, port, max_clients, timeout,
				       (debug_label[0] != '\0')? "(" : "",
				       (debug_label[0] != '\0')? debug_label : "",
				       (debug_label[0] != '\0')? ")" : ""
			       );

			while (clients_ready < max_clients) {
				int client_sock = accept(
					server_sock, (struct sockaddr *)&client_addr,
					&client_addr_len); // blocking
				if (client_sock == -1) {
					if (errno == EAGAIN || errno == EWOULDBLOCK) {
						PRINTF("\nTimeout reached! Proceeding "
						       "with available clients...\n");
						client_result = ETIMEDOUT;
						break;
					}
					continue;
				}

				handle_client(client_sock, 0, NULL, client, clients_ready);
				clients_ready++;
				PRINTF("Client %d/%d checked in\n", clients_ready,
				       max_clients);
			}
		} else {
			PRINTF("Server started on %s:%s, waiting for %d tags "
			       "(Timeout: %d sec) %s%s%s...\n",
				       ip_address, port, ntags, timeout,
				       (debug_label[0] != '\0')? "(" : "",
				       (debug_label[0] != '\0')? debug_label : "",
				       (debug_label[0] != '\0')? ")" : ""
			       );

			while (clients_ready < ntags) {
				int client_sock = accept(
					server_sock, (struct sockaddr *)&client_addr,
					&client_addr_len); // blocking
				if (client_sock == -1) {
					if (errno == EAGAIN || errno == EWOULDBLOCK) {
						PRINTF("\nTimeout reached! Proceeding "
						       "with available clients...\n");
						client_result = ETIMEDOUT;
						break;
					}
					continue;
				}

				handle_client(client_sock, ntags, tags, client,
					      clients_ready);
				clients_ready++;
				PRINTF("Client %d/%d checked in\n",
					clients_ready, ntags);
			}
		}


		// Notify connected clients to proceed
		const char *reply = "ok";
		switch (client_result) {
		case 0:
			break;
		case ETIMEDOUT:
			reply = "timeout";
			break;
		default:
			reply = "error";
		}
		if (ntags < 0) {
			int i;
			PRINTF("Notifying counted clients: %s\n", reply);
			for (i = 0; i < clients_ready; i++) {
				send(client[i], reply, strlen(reply)+1, 0);
				close(client[i]);
				struct timespec trcv;
				clock_gettime(CLOCK_MONOTONIC, &trcv);
				PRINTF2("SS: %d: %ld.%09ld\n", i, trcv.tv_sec,
					trcv.tv_nsec);
			}
		} else {
			int i;
			PRINTF("Notifying tagged clients: %s\n", reply);
			for (i = 0; i < max_clients; i++) {
				if (client[i] != -1) {
					send(client[i], reply, strlen(reply)+1, 0);
					close(client[i]);
					struct timespec trcv;
					clock_gettime(CLOCK_MONOTONIC, &trcv);
					PRINTF2("SS: %s: %ld.%09ld\n", print_tags[i],
						trcv.tv_sec, trcv.tv_nsec);
				}
			}
			int t, na = 0;
			for  (t = 0; t < ntags; t++) {
				if (tags[t]) {
					na++;
				}
			}
			if (na > 0) {
				PRINTF("%s: missing tags:", argv[0]);
				for (t = 0; t < ntags; t++) {
					if (tags[t]) {
						PRINTF("%s ", tags[t]);
					}
				}
				PRINTF("\n%s  did not hear from %d daemons\n",argv[0], na);
			}
		}
		sleep(3);
		close(server_sock);
		return client_result;
	} else {
		return client_main(timeout, client_tag, port, client_retry_interval);
	}
}
