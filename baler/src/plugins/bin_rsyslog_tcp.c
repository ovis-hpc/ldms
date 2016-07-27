/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-16 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-16 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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
 *      Neither the name of Sandia nor the names of any contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
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
 */
/**
 * \file bin_rsyslog_tcp.c
 * \author Narate Taerat (narate@ogc.us)
 *
 * \defgroup bin_rsyslog_tcp rsyslog TCP input plugin
 * \{
 */
#include "baler/binput.h"
#include "baler/butils.h"
#include <pthread.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/queue.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/thread.h>

#include <unistd.h>
#include <assert.h>

#define PLUGIN_DEFAULT_PORT 10514

static struct event_base *io_evbase;

/**
 * This table contains unix time stamp for each hour of the day in each month.
 * Pre-determined these values are quicker than repeatedly calling mktime().
 * This table is initialized in ::init_once().
 */
static
time_t ts_mdh[12][32][24];

/**
 * ts_ym[YEAR - 1900][MONTH - 1] contain timestamp of the beginning of the first
 * day of MONTH, YEAR. This is good from 1970 through 2199.
 */
static
time_t ts_ym[300][12] = {0};

static
int is_leap[300] = {0};

static
int max_mday[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};


/**
 * This table determine if a characcter is a delimiter or not.
 * This is also initialized in ::init_once().
 */
static char is_delim[256];
static char is_quote[256];

static char *delim = " \t,.:;`'\"<>\\/|[]{}()+-*=~!@#$%^&?_";
static char *quote = "\'\"";

typedef enum {
	PSTATUS_STOPPED=0,
	PSTATUS_RUNNING
} plugin_status_t;

/**
 * This structure stores context of this input plugin.
 */
struct plugin_ctxt {
	pthread_t io_thread; /**< Thread handling socket IO. */
	pthread_t conn_req_thread; /**< Thread handling conn req. */
	uint16_t port; /**< Port number to listen to. */
	int status; /**< Status of the plugin. */
	int collapse_spaces; /**< `collapse_spaces` flag */
};

/**
 * Context for a bufferevent socket connection.
 * Now only contain plugin, but might have more stuffs
 * later ...
 */
struct conn_ctxt {
	struct bplugin *plugin; /**< Plugin instance. */
	struct sockaddr_in sin;
	LIST_ENTRY(conn_ctxt) link;
};

LIST_HEAD(, conn_ctxt) conn_ctxt_list = LIST_HEAD_INITIALIZER();
pthread_mutex_t conn_ctxt_list_mutex = PTHREAD_MUTEX_INITIALIZER;

static
void bin_rsyslog_tcp_print_conn_ctxt_list()
{
	struct conn_ctxt *ctxt;
	union {uint8_t a[4]; uint32_t v;} addr;
	uint16_t port;
	pthread_mutex_lock(&conn_ctxt_list_mutex);
	LIST_FOREACH(ctxt, &conn_ctxt_list, link) {
		addr.v = ctxt->sin.sin_addr.s_addr;
		port = be16toh(ctxt->sin.sin_port);
		binfo("conn_ctxt, peer: %d.%d.%d.%d:%d",
			(int)addr.a[0],
			(int)addr.a[1],
			(int)addr.a[2],
			(int)addr.a[3],
			(int)port);
	}
	pthread_mutex_unlock(&conn_ctxt_list_mutex);
}

static
struct conn_ctxt *conn_ctxt_alloc(struct bplugin *bplugin,
				  struct sockaddr_in *sin)
{
	struct conn_ctxt *ctxt = calloc(1, sizeof(*ctxt));
	if (ctxt) {
		pthread_mutex_lock(&conn_ctxt_list_mutex);
		LIST_INSERT_HEAD(&conn_ctxt_list, ctxt, link);
		pthread_mutex_unlock(&conn_ctxt_list_mutex);
		ctxt->plugin = bplugin;
		ctxt->sin = *sin;
	}
	return ctxt;
}

/**
 * Function for freeing connection context.
 * \param ctxt The context to be freed.
 */
static
void conn_ctxt_free(struct conn_ctxt *ctxt)
{
	pthread_mutex_lock(&conn_ctxt_list_mutex);
	LIST_REMOVE(ctxt, link);
	pthread_mutex_unlock(&conn_ctxt_list_mutex);
	free(ctxt);
}

static inline
time_t __get_utc_ts(int yyyy, int mm, int dd, int HH, int MM, int SS)
{
	if (yyyy < 1970 ||  2199 < yyyy)
		return -1;
	if (mm < 1 || 12 < mm)
		return -1;
	if (dd < 1)
		return -1;
	if (max_mday[mm-1] + (mm==2 && is_leap[yyyy - 1900]) < dd)
		return -1;
	if (HH < 0 || 23 < HH)
		return -1;
	if (MM < 0 || 59 < MM)
		return -1;
	if (SS < 0 || 59 < SS)
		return -1;
	return ts_ym[yyyy-1900][mm-1] + (3600*24)*(dd-1) + 3600*HH + 60*MM + SS;
}

/**
 * A convenient function to map 3 character month string to tm_mon number
 * (0 - 11).
 * \note This is a quick convenient function, hence it has no error checking.
 * \param s The string.
 * \return tm_mon number (0 - 11).
 * \return -1 on error.
 */
static
int __month3(char *s)
{
	uint32_t x = 0;
	memcpy(&x, s, 3);
	x = htole32(x);
	switch (x) {
	/* These are unsigned integer interpretation of 3-character strings
	 * in LITTLE ENDIAN. */
	case 0x6e614a: /* Jan */
	case 0x4e414a: /* JAN */
		return 0;
	case 0x626546: /* Feb */
	case 0x424546: /* FEB */
		return 1;
	case 0x72614d: /* Mar */
	case 0x52414d: /* MAR */
		return 2;
	case 0x727041: /* Apr */
	case 0x525041: /* APR */
		return 3;
	case 0x79614d: /* May */
	case 0x59414d: /* MAY */
		return 4;
	case 0x6e754a: /* Jun */
	case 0x4e554a: /* JUN */
		return 5;
	case 0x6c754a: /* Jul */
	case 0x4c554a: /* JUL */
		return 6;
	case 0x677541: /* Aug */
	case 0x475541: /* AUG */
		return 7;
	case 0x706553: /* Sep */
	case 0x504553: /* SEP */
		return 8;
	case 0x74634f: /* Oct */
	case 0x54434f: /* OCT */
		return 9;
	case 0x766f4e: /* Nov */
	case 0x564f4e: /* NOV */
		return 10;
	case 0x636544: /* Dec */
	case 0x434544: /* DEC */
		return 11;
	}
	/* On unmatched */
	return -1;
}

/**
 * This function extract an integer string from the input string and put the
 * parsed integer in \a *iptr.
 * \param[in,out] sptr (*sptr) is the input string, it will point to the
 * 	position after the consumed numbers when the function returned.
 * 	It will not change on error though.
 * \param[out] iptr (*ipt) is the result of the function.
 * \return 0 on success.
 * \return Error number on error.
 */
static inline
int get_int(char **sptr, int *iptr)
{
	char *s = *sptr;
	char buff[16];
	int i=0;
	while ('0'<=*s && *s<='9' && i<15) {
		buff[i] = *s;
		i++;
		s++;
	}
	if (i==0)
		return -1;
	buff[i] = 0; /* Terminate the buff string */
	*iptr = atoi(buff);
	*sptr = s;
	return 0;
}

/**
 * Get host token from the input string. The host is expected to be the first
 * token pointed by \a *sptr, and delimited by ' '.
 * \param[in,out] sptr (*sptr) is the input string. It will also be changed to
 * 	point at the end of the token.
 * \return A pointer to ::bstr containing the token, on success.
 * \return NULL on error.
 */
static
struct bstr* get_host(char **sptr)
{
	char *s = *sptr;
	while (*s && *s != ' ') {
		s++;
	}
	int len = s - *sptr;
	struct bstr *bstr = bstr_alloc(len);
	if (!bstr)
		return NULL;
	bstr->blen = len;
	memcpy(bstr->cstr, *sptr, len);
	*sptr = s;
	return bstr;
}

/**
 * Similar to ::get_host(), but this version does not allocate ::bstr.
 * \param sptr \a *sptr is an input string to extract the leading host from.
 * \param bstr The pre-allocated bstr structure.
 * \return The input parameter \a bstr (to keep it consistent to ::get_host()).
 * \return NULL if \a bstr->blen cannot hold the entire host token.
 */
static
struct bstr* get_host_r(char **sptr, struct bstr *bstr)
{
	char *s = *sptr;
	while (*s && *s != ' ') {
		s++;
	}
	int len = s - *sptr;
	if (bstr->blen < len)
		return NULL;
	bstr->blen = len;
	memcpy(bstr->cstr, *sptr, len);
	*sptr = s;
	return bstr;
}

static
int parse_msg_hdr_0(struct bstr *str, char *s, struct binq_data *d)
{
	int M, D, hh, mm, ss;
	/* Month */
	M = __month3(s);
	if (M == -1)
		return -1;
	s += 3;

	/* skip spaces */
	while (*s == ' ')
		s++;

	/* Day hh:mm:ss */
	if (get_int(&s, &D) == -1)
		return -1;

	if (*(s++) != ' ')
		return -1;

	if (get_int(&s, &hh) == -1)
		return -1;

	if (*(s++) != ':')
		return -1;

	if (get_int(&s, &mm) == -1)
		return -1;

	if (*(s++) != ':')
		return -1;

	if (get_int(&s, &ss) == -1)
		return -1;

	if (*(s++) != ' ')
		return -1;

	/* TODO XXX Handle daylight saving later
	 * NOTE Problem: currently the daylight saving is handled by mktime in
	 * ts_mdh initialization. However, the "fall back" hour won't be handled
	 * properly as the wall clock will just set back 1 hour, leaving the
	 * entire 2 hour of undetermined daylight saving time.
	 */
	d->tv.tv_sec = ts_mdh[M][D][hh] + 60*mm + ss;
	d->tv.tv_usec = 0; /* Default syslog does not support usec. */

	/* hostname */
	struct bstr *host;
	host = get_host(&s);
	if (!host)
		return -1;
	d->hostname = host;

	/* The rest is ' ' with the real message */
	return s - str->cstr;
}

static
int parse_msg_hdr_1(struct bstr *str, char *s, struct binq_data *d)
{
	/*
	 * s is expected to point at TIMESTAMP part of the rsyslog format
	 * ver 1 (see RFC5424 https://tools.ietf.org/html/rfc5424#section-6).
	 *
	 * Eventhough the header part in RFC5424 has more than TIMESTAMP and
	 * HOSTNAME, baler will care only TIMESTAMP and HOSTNAME. The rest of
	 * the header is treated as a part of the message.
	 */

	/* FULL-DATE T TIME */
	int dd,mm,yyyy;
	int HH,MM,SS,US = 0, TZH, TZM;
	int n, len;
	int tznegative = 0;
	n = sscanf(s, "%d-%d-%dT%d:%d:%d%n.%d%n", &yyyy, &mm, &dd, &HH, &MM,
							&SS, &len, &US, &len);
	if (n < 6) {
		bwarn("Date-time parse error for message: %.*s", str->blen, str->cstr);
		errno = EINVAL;
		return -1;
	}
	/* Treat local wallclock as UTC wallclock, and adjust it later. */
	d->tv.tv_sec = __get_utc_ts(yyyy, mm, dd, HH, MM, SS);
	if (d->tv.tv_sec == -1) {
		bwarn("Date-time range error for message: %.*s", str->blen, str->cstr);
		errno = EINVAL;
		return -1;
	}
	d->tv.tv_usec = US;
	s += len;
	switch (*s) {
	case 'Z':
		TZH = TZM = 0;
		s++;
		break;
	case '-':
		tznegative = 1;
	case '+':
		n = sscanf(s, "%d:%d%n", &TZH, &TZM, &len);
		if (n != 2) {
			bwarn("timezone parse error, msg: %.*s", str->blen, str->cstr);
		errno = EINVAL;
			return -1;
		}
		if (tznegative)
			TZM = -TZM;
		s += len;
		break;
	default:
		bwarn("timezone parse error, msg: %.*s", str->blen, str->cstr);
		errno = EINVAL;
		return -1;
	}
	/* adjust to UTC time */
	d->tv.tv_sec -= 3600 * TZH;
	d->tv.tv_sec -= 60 * TZM;

	/* expecting space */
	if (*s++ != ' ')
		return -1;

	/* hostname */
	struct bstr *host;
	host = get_host(&s);
	if (!host)
		return -1;
	d->hostname = host;

	return s - str->cstr;
}

/**
 * Parse message header.
 * \param str The ::bstr that contain original message.
 * \param[out] d The ::binq_data structure to contain parsed header information.
 * \return -1 on error.
 * \return Index \a i of \a str->cstr such that \a str->cstr[0..i-1] contains
 * 	message header information (e.g. Date, Time and hostname). The rest
 * 	(\a str->cstr[i..n]) is the message part, which also includes a leading
 * 	white space. In other words, \a i is the index next to the last index
 * 	that this function processed.
 */
static
int parse_msg_hdr(struct bstr *str, struct binq_data *d)
{
	char *s = str->cstr;
	/* Expecting '<###>' first. */
	if (*(s++) != '<')
		return -1;
	while ('0'<=*s && *s<='9') {
		s++;
	}
	if (*(s++) != '>')
		return -1;
	if (*s == '1')
		return parse_msg_hdr_1(str, s+2, d);
	/* If not version 1, assumes old format */
	return parse_msg_hdr_0(str, s, d);
}

/**
 * Extract the first token from \a *s. This function extract the first token and
 * create ::bstr_list_entry structure for the token and return it.
 * \param s (*s) is the input string, which will be changed to point to the
 * 	character next to the extracted token.
 * \return A pointer to ::bstr_list_entry on success.
 * \return NULL on error.
 */
static
struct bstr_list_entry* get_token(struct bplugin *p, char **s)
{
	struct bstr_list_entry *ent;
	char *_s = *s;
	int len;
	if (!*_s)
		return NULL; /* Empty string */
	if (is_quote[*_s]) {
		char endquote = *_s;
		_s++;		/* consume the quote */
		while (*_s && *_s != endquote)
			_s++;
		if (*_s) {	/* check that string is terminated */
			_s++;	/* consume the closing quote */
			goto out_0;
		}
		/* String is not terminated, back up and treat quote as a
		 * delimiter */
		_s = *s;
	}
	if (*_s == ' ' && ((struct plugin_ctxt *)p->context)->collapse_spaces) {
		while (*_s == ' ') {
			_s++;
		}
		len = 1;
		goto out_1;
	}
	if (is_delim[*_s]) {
		len = 1;
		_s++;
		goto out_1;
	}
	/* else */
	while (*_s && !is_delim[*_s])
		_s++;
out_0:
	len = _s - *s;
out_1:
	ent = bstr_list_entry_alloci(len, *s);
	if (!ent)
		return NULL;
	*s = _s;
	return ent;
}

/**
 * This function prepare ::bwq_entry from the given ::bstr \a s.
 * \param s The entire raw log message in ::bstr structure.
 * \return NULL on error.
 * \return A pointer to ::bwq_entry on success.
 */
static
struct bwq_entry* prepare_bwq_entry(struct bplugin *p, struct bstr *s)
{
	struct bwq_entry *qent = calloc(1, sizeof(*qent));
	if (!qent)
		goto err0;
	bdebug("rsyslog: %.*s", s->blen, s->cstr);
	struct binq_data *d = &qent->data.in;
	/* First, handle the header part */
	int midx = parse_msg_hdr(s, d);
	if (midx == -1)
		goto err1;

	/* Then, handle the rest of the tokens */
	struct bstr_list_head *tok_head = &d->tokens;
	LIST_INIT(tok_head);
	struct bstr_list_entry *tok_tail = NULL;
	/* Start tokenizing */
	char *_s = s->cstr + midx + 1; /* skip the field delimiting ' ' */
	struct bstr_list_entry *lent = NULL;
	int count = 0;
	while (*_s && (lent = get_token(p, &_s))) {
		if (!tok_tail)
			LIST_INSERT_HEAD(tok_head, lent, link);
		else
			LIST_INSERT_AFTER(tok_tail, lent, link);

		tok_tail = lent;
		count++;
	}
	if (!lent)
		/* Break out of the loop because lent == NULL ==> error */
		goto err1;

	d->tok_count = count;
	return qent;
err1:
	binq_entry_free(qent);
err0:
	berr("INPUT ERROR: %.*s", s->blen, s->cstr);
	return NULL;
}

/**
 * Read callback for bufferevent.
 * \note 1 \a bev per connection.
 * \param bev The bufferevent object.
 * \param arg Pointer to ::conn_ctxt.
 */
static
void read_cb(struct bufferevent *bev, void *arg)
{
	struct conn_ctxt *cctxt = arg;
	struct bplugin *p = cctxt->plugin;
	struct plugin_ctxt *pctxt = p->context;
	struct evbuffer_ptr evbptr;
	struct evbuffer *input = bufferevent_get_input(bev);
loop:
	/* Look for '\n' in the buffer as it is the end of each message. */
	evbptr = evbuffer_search(input, "\n", 1, NULL);
	if (evbptr.pos == -1)
		return; /* Do nothing & return as there's no complete message
			   in the buffer */
	struct bstr *str = bstr_alloc(evbptr.pos+1);
	if (!str) {
		berror("bstr_alloc");
		return;
	}
	str->blen = evbptr.pos+1;
	int len = evbuffer_remove(input, str->cstr, str->blen);
	if (len != str->blen) {
		berr("Expecting %d bytes, but only %d bytes are copied out",
				str->blen, len);
	}
	str->cstr[str->blen-1] = 0; /* Eliminate the '\n' */
	struct bwq_entry *ent = prepare_bwq_entry(p, str);
	if (ent)
		binq_post(ent);
	bstr_free(str);
	goto loop;
}

/**
 * Event handler on bufferevent (in libevent).
 * \param bev The buffer event instance.
 * \param events The events.
 * \param arg The pointer to connection context ::conn_ctxt.
 */
static
void event_cb(struct bufferevent *bev, short events, void *arg)
{
	if (events & BEV_EVENT_ERROR) {
		berror("BEV_EVENT_ERROR");
	}
	if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF)) {
		#if DEBUG_BALER_RSYSLOG_CONN
		struct conn_ctxt *ctxt = arg;
		union {uint8_t a[4]; uint32_t u32;} ipaddr;
		uint16_t port;
		ipaddr.u32 = ctxt->sin.sin_addr.s_addr;
		port = be16toh(ctxt->sin.sin_port);
		binfo("%d.%d.%d.%d:%d disconnected",
			(int)ipaddr.a[0],
			(int)ipaddr.a[1],
			(int)ipaddr.a[2],
			(int)ipaddr.a[3],
			port
			);
		#endif
		bufferevent_free(bev);
		conn_ctxt_free(arg);
	}
}

/**
 * Connect callback. This function will be called when there is a connection
 * request coming in. This is a call back function for
 * evconnlistener_new_bind().
 * \param listener Listener
 * \param sock Socket file descriptor
 * \param addr Socket address ( sockaddr_in )
 * \param len Length of \a addr
 * \param arg Pointer to plugin instance.
 */
static
void conn_cb(struct event_base *evbase, int sock,
		struct sockaddr *addr, int len, void *arg)
{
	struct bufferevent *bev = NULL;
	struct conn_ctxt *cctxt = NULL;

	evutil_make_socket_nonblocking(sock);
	bev = bufferevent_socket_new(evbase, sock, BEV_OPT_CLOSE_ON_FREE);
	if (!bev) {
		berr("conn_cb(): bufferevent_socket_new() error, errno: %d",
			errno);
		goto cleanup;
	}
	cctxt = conn_ctxt_alloc(arg, (void*)addr);
	if (!cctxt) {
		berr("conn_cb(): malloc() error, errno: %d", errno);
	}
	cctxt->plugin = arg;
	bufferevent_setcb(bev, read_cb, NULL, event_cb, cctxt);
	bufferevent_enable(bev, EV_READ);
	#if DEBUG_BALER_RSYSLOG_CONN
	union {uint8_t a[4]; uint32_t u32;} ipaddr;
	uint16_t port;
	struct sockaddr_in *sin = (void*)addr;
	ipaddr.u32 = sin->sin_addr.s_addr;
	port = be16toh(sin->sin_port);
	binfo("connected from %d.%d.%d.%d:%d",
		(int)ipaddr.a[0],
		(int)ipaddr.a[1],
		(int)ipaddr.a[2],
		(int)ipaddr.a[3],
		port
		);
	#endif
	return;

cleanup:
	if (bev)
		bufferevent_free(bev); /* this will also close sock */
	if (cctxt)
		conn_ctxt_free(cctxt);
	if (!bev)
		close(sock);
	return;
}

/**
 * This is a pthread routine function for listening for connections from
 * rsyslog over TCP. pthread_create() function in ::plugin_start() will call
 * this function.
 * \param arg A pointer to the ::bplugin of this thread.
 * \retval (abuse)0 if there is no error.
 * \retval (abuse)errno if there are some error.
 * \note This is a thread routine.
 */
static
void* rsyslog_tcp_listen(void *arg)
{
	int64_t rc = 0;
	struct bplugin *p = arg;
	struct plugin_ctxt *ctxt = p->context;
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_addr = { .s_addr = INADDR_ANY },
		.sin_port = htons(ctxt->port)
	};
	struct sockaddr_in peer_addr;
	socklen_t peer_addr_len = sizeof(peer_addr);
	int peer_sd;
	int sd = socket(AF_INET, SOCK_STREAM, 0);
	if (sd < 0) {
		rc = errno;
		goto out;
	}
	rc = bind(sd, (void*)&addr, sizeof(addr));
	if (rc) {
		rc = errno;
		goto out;
	}

	rc = listen(sd, 8192);
	if (rc) {
		rc = errno;
		goto out;
	}

	while (0 <= (peer_sd = accept(sd, (void*)&peer_addr, &peer_addr_len))) {
		conn_cb(io_evbase, peer_sd, (void*)&peer_addr, peer_addr_len, p);
		peer_addr_len = sizeof(peer_addr); /* set for next accept() */
	}

	berr("rsyslog_tcp_listen(): accept() error, errno: %d", errno);
	rc = errno;

out:
	return (void*)rc;
}

void __dummy_cb(int fd, short what, void *arg)
{
	/* This should not be called */
	assert(0);
}

static
void* rsyslog_tcp_io_ev_proc(void *arg)
{
	int rc = 0;
	struct event *dummy;
	io_evbase = event_base_new();
	struct bplugin *this = arg;
	struct plugin_ctxt *ctxt = this->context;
	if (!io_evbase) {
		rc = ENOMEM;
		goto err0;
	}

	/* dummy event to prevent the termination of event_base_dispatch() */
	dummy = event_new(io_evbase, -1, EV_READ, __dummy_cb, io_evbase);
	if (!dummy) {
		rc = ENOMEM;
		goto err1;
	}
	rc = event_add(dummy, NULL);
	if (rc) {
		goto err1;
	}

	/* Dedicated thread for socket accept() */
	rc = pthread_create(&ctxt->conn_req_thread, NULL, rsyslog_tcp_listen, this);
	if (rc)
		goto err2;

	/* evbase will handle accepted socket IO */
	event_base_dispatch(io_evbase);

	/* clean up */
err2:
	event_free(dummy);
err1:
	event_base_free(io_evbase);
err0:
	berr("rsyslog_tcp_ev_proc() thread exit, rc: %d", rc);
	return NULL; /* return code abuse */
}

/**
 * This is called from the main daemon (through ::bplugin::config) to configure
 * the plugin before the main daemon calls ::plugin_start()
 * (through ::bplugin::start).
 * \param this The plugin.
 * \param arg_head The head of the list of arguments.
 * \return 0 on success.
 * \return errno on error.
 * \note Now only accept 'port' for rsyslog_tcp plugin.
 */
static
int plugin_config(struct bplugin *this, struct bpair_str_head *arg_head)
{
	struct bpair_str *bpstr;
	struct plugin_ctxt *ctxt = this->context;
	int rc = 0;
	bpstr = bpair_str_search(arg_head, "port", NULL);
	if (bpstr) {
		uint16_t port = atoi(bpstr->s1);
		ctxt->port = port;
	}
	bpstr = bpair_str_search(arg_head, "collapse_spaces", NULL);
	if (bpstr) {
		ctxt->collapse_spaces = atoi(bpstr->s1);
	}
	return rc;
}

/**
 * This will be called from the main baler daemon to start the plugin
 * (through ::bplugin::start).
 * \param this The plugin instance.
 * \return 0 on success.
 * \return errno on error.
 */
static
int plugin_start(struct bplugin *this)
{
	struct plugin_ctxt *ctxt = this->context;
	int rc = pthread_create(&ctxt->io_thread, NULL, rsyslog_tcp_io_ev_proc, this);
	if (rc)
		return rc;
	return 0;
}

/**
 * Calling this will stop the execution of \a this plugin.
 * \param this The plugin to be stopped.
 * \return 0 on success.
 * \return errno on error.
 */
static
int plugin_stop(struct bplugin *this)
{
	struct plugin_ctxt *ctxt = this->context;
	pthread_cancel(ctxt->conn_req_thread);
	pthread_cancel(ctxt->io_thread);
	return 0;
}

/**
 * Free the plugin instance.
 * \param this The plugin to be freed.
 * \return 0 on success.
 * \note Now only returns 0, but the errors will be logged.
 */
static
int plugin_free(struct bplugin *this)
{
	/* If context is not null, meaning that the plugin is running,
	 * as it is set to null in ::plugin_stop(). */
	struct plugin_ctxt *ctxt = (typeof(ctxt)) this->context;
	int rc = 0;
	if (ctxt && ctxt->status == PSTATUS_RUNNING) {
		rc = plugin_stop(this);
		if (rc) {
			errno  = rc;
			berror("plugin_stop");
		}
	}
	bplugin_free(this);
	return 0;
}

/**
 * Global variable flag for ::init_once() function.
 */
static
int __once = 0;

static
pthread_mutex_t __once_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * This function populate ::ts_ym global variable.
 */
static
void init_ts_ym()
{
	/* days of previous month */
	static int dpm[12] = {31, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30};
	int yyyy, mm, i;
	int Y;
	for (i = 70*12+1; i < (300*12); i++) {
		mm = i % 12;
		yyyy = i / 12;
		Y = 1900 + yyyy;
		ts_ym[yyyy][mm] = ts_ym[yyyy][mm-1] + dpm[mm]*24*60*60;
		if (mm == 2 && (Y % 4 == 0) &&
				((Y % 100 != 0) || (Y % 400 == 0))) {
			/* leap year: add Feb 29 before Mar 1. */
			ts_ym[yyyy][mm] += 24*60*60;
			/* also mark leapyear */
			is_leap[yyyy] = 1;
		}
	}
}

/**
 * This function will initialize global variable (but local to the plugin),
 * and should be called only once. This function is also thread-safe.
 * \return -1 on error.
 * \return 0 on success.
 */
static
int init_once()
{
	int rc = 0;
	pthread_mutex_lock(&__once_mutex);
	__once = 1;
	rc = evthread_use_pthreads();
	if (rc)
		goto err0;
	time_t now = time(NULL);
	if (now == -1) {
		rc = -1;
		goto err0;
	}
	struct tm tm;
	if (localtime_r(&now, &tm) == NULL) {
		rc = -1;
		goto err0;
	}

	/* Now, tm has the current year. Let's reset it and keep only the year
	 * and starts filling in the ::ts_mdh table. */
	tm.tm_min = 0;
	tm.tm_sec = 0;
	for (tm.tm_mon = 0; tm.tm_mon < 12; tm.tm_mon++) {
		for (tm.tm_mday = 1; tm.tm_mday < 32; tm.tm_mday++) {
			for (tm.tm_hour = 0; tm.tm_hour < 24; tm.tm_hour++) {
				tm.tm_isdst = -1;
				ts_mdh[tm.tm_mon][tm.tm_mday][tm.tm_hour]
					= mktime(&tm);
			}
		}
	}

	init_ts_ym();

	int i;
	is_delim[0] = 1;
	for (i=0; i<strlen(delim); i++) {
		is_delim[delim[i]] = 1;
	}
	for (i=0; i<strlen(quote); i++) {
		is_quote[quote[i]] = 1;
	}
	goto out;
err0:
	__once = 0;
out:
	pthread_mutex_unlock(&__once_mutex);
	return rc;
}

struct bplugin* create_plugin_instance()
{
	if (!__once && init_once())
		return NULL;
	struct bplugin *p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;
	p->name = strdup("rsyslog_tcp");
	p->version = strdup("0.1a");
	p->config = plugin_config;
	p->start = plugin_start;
	p->stop = plugin_stop;
	p->free = plugin_free;
	struct plugin_ctxt *ctxt = calloc(1, sizeof(*ctxt));
	ctxt->status = PSTATUS_STOPPED;
	ctxt->port = PLUGIN_DEFAULT_PORT;
	ctxt->collapse_spaces = 0;
	p->context = ctxt;
	return p;
}

static
char* ver()
{
	binfo("%s", "1.1.1.1");
	return "1.1.1.1";
}

/**\}*/
