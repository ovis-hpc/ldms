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

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/thread.h>

#define PLUGIN_DEFAULT_PORT 54321u

/**
 * This table contains unix time stamp for each hour of the day in each month.
 * Pre-determined these values are quicker than repeatedly calling mktime().
 * This table is initialized in ::init_once().
 */
time_t ts_mdh[12][32][24];

/**
 * This table determine if a characcter is a delimiter or not.
 * This is also initialized in ::init_once().
 */
char is_delim[256];

char *delim = " \t,.:;`'\"<>\\/|[]{}()+-*=~!@#$%^&?";

typedef enum {
	PSTATUS_STOPPED=0,
	PSTATUS_RUNNING
} plugin_status_t;

/**
 * This structure stores context of this input plugin.
 */
struct plugin_ctxt {
	pthread_t thread; /**< Thread. */
	uint16_t port; /**< Port number to listen to. */
	int status; /**< Status of the plugin. */
};

/**
 * Context for a bufferevent socket connection.
 * Now only contain plugin, but might have more stuffs
 * later ...
 */
struct conn_ctxt {
	struct bplugin *plugin; /**< Plugin instance. */
};

/**
 * Function for freeing connection context.
 * \param ctxt The context to be freed.
 */
void conn_ctxt_free(struct conn_ctxt *ctxt)
{
	free(ctxt);
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
inline
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

/**
 * Extract the first token from \a *s. This function extract the first token and
 * create ::bstr_list_entry structure for the token and return it.
 * \param s (*s) is the input string, which will be changed to point to the
 * 	character next to the extracted token.
 * \return A pointer to ::bstr_list_entry on success.
 * \return NULL on error.
 */
struct bstr_list_entry* get_token(char **s)
{
	struct bstr_list_entry *ent;
	char *_s = *s;
	int len;
	if (!*_s)
		return NULL; /* Empty string */
	if (is_delim[*_s]) {
		len = 1;
		_s++;
		goto out;
	}
	/* else */
	while (*_s && !is_delim[*_s])
		_s++;
	len = _s - *s;
out:
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
struct bwq_entry* prepare_bwq_entry(struct bstr *s)
{
	struct bwq_entry *qent = calloc(1, sizeof(*qent));
	if (!qent)
		goto err0;
	struct binq_data *d = &qent->data.in;
	/* First, handle the header part */
	int midx = parse_msg_hdr(s, d);
	if (midx == -1)
		goto err1;

	/* Then, handle the rest of the tokens */
	struct bstr_list_head *tok_head = malloc(sizeof(*tok_head));
	if (!tok_head)
		goto err1;
	LIST_INIT(tok_head);
	struct bstr_list_entry *tok_tail = NULL;
	/* Start tokenizing */
	char *_s = s->cstr + midx + 1; /* skip the field delimiting ' ' */
	struct bstr_list_entry *lent = NULL;
	int count = 0;
	while (*_s && (lent = get_token(&_s))) {
		if (!tok_tail) {
			LIST_INSERT_HEAD(tok_head, lent, link);
		} else {
			LIST_INSERT_AFTER(tok_tail, lent, link);
		}
		tok_tail = lent;
		count++;
	}
	if (!lent)
		/* Break out of the loop because lent == NULL ==> error */
		goto err1;

	d->tokens = tok_head;
	d->tok_count = count;
	return qent;
err1:
	binq_entry_free(qent);
err0:
	return NULL;
}

/**
 * Read callback for bufferevent.
 * \note 1 \a bev per connection.
 * \param bev The bufferevent object.
 * \param arg Pointer to ::conn_ctxt.
 */
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
	struct bwq_entry *ent = prepare_bwq_entry(str);
	if (ent)
		binq_post(ent);
	goto loop;
}

/**
 * Event handler on bufferevent (in libevent).
 * \param bev The buffer event instance.
 * \param events The events.
 * \param arg The pointer to connection context ::conn_ctxt.
 */
void event_cb(struct bufferevent *bev, short events, void *arg)
{
	if (events & BEV_EVENT_ERROR) {
		berror("BEV_EVENT_ERROR");
	}
	if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF)) {
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
void conn_cb(struct evconnlistener *listener, evutil_socket_t sock,
		struct sockaddr *addr, int len, void *arg)
{
	struct event_base *evbase = evconnlistener_get_base(listener);
	struct bufferevent *bev = bufferevent_socket_new(evbase, sock,
			BEV_OPT_CLOSE_ON_FREE);
	struct conn_ctxt *cctxt = malloc(sizeof(*cctxt));
	cctxt->plugin = arg;
	bufferevent_setcb(bev, read_cb, NULL, event_cb, cctxt);
	bufferevent_enable(bev, EV_READ);
}

/**
 * This is a pthread routine function for listening for connections from
 * rsyslog over TCP. pthread_create() function in ::plugin_start() will call
 * this function.
 * \param arg A pointer to the ::bplugin of this thread.
 * \return (abuse) 0 if there is no error.
 * \return (abuse) errno if there are some error.
 * \note This is a thread routine.
 */
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
	struct event_base *evbase = event_base_new();
	if (!evbase) {
		rc = ENOMEM;
		goto err0;
	}
	int evconn_flag = LEV_OPT_REUSEABLE | LEV_OPT_THREADSAFE;
	struct evconnlistener *evconn = evconnlistener_new_bind( evbase,
			conn_cb,
			p,
			evconn_flag,
			SOMAXCONN,
			(void*) &addr,
			sizeof(addr));
	if (!evconn) {
		rc = ENOMEM;
		goto err1;
	}

	event_base_dispatch(evbase); /* This will loop + listen for new
					connection. */
	/* Clear resources on exit */
	evconnlistener_free(evconn);
err1:
	event_base_free(evbase);
err0:
	return (void*)rc;
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
	return rc;
}

/**
 * This will be called from the main baler daemon to start the plugin
 * (through ::bplugin::start).
 * \param this The plugin instance.
 * \return 0 on success.
 * \return errno on error.
 */
int plugin_start(struct bplugin *this)
{
	struct plugin_ctxt *ctxt = this->context;
	int rc = pthread_create(&ctxt->thread, NULL, rsyslog_tcp_listen, this);
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
int plugin_stop(struct bplugin *this)
{
	struct plugin_ctxt *ctxt = this->context;
	return pthread_cancel(ctxt->thread);
}

/**
 * Free the plugin instance.
 * \param this The plugin to be freed.
 * \return 0 on success.
 * \note Now only returns 0, but the errors will be logged.
 */
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
int __once = 0;

pthread_mutex_t __once_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * This function will initialize global variable (but local to the plugin),
 * and should be called only once. This function is also thread-safe.
 * \return -1 on error.
 * \return 0 on success.
 */
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

	bzero(is_delim, 256);
	int i;
	is_delim[0] = 1;
	for (i=0; i<strlen(delim); i++) {
		is_delim[delim[i]] = 1;
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
	struct plugin_ctxt *ctxt = calloc(1, sizeof(*p->context));
	ctxt->status = PSTATUS_STOPPED;
	ctxt->port = PLUGIN_DEFAULT_PORT;
	p->context = ctxt;
	return p;
}

char* ver()
{
	binfo("%s", "1.1.1.1");
	return "1.1.1.1";
}

/**\}*/
