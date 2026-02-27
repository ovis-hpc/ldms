#define _GNU_SOURCE
#include <pthread.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "ldms.h"
#include "ldms_msg.h"
#include "ldms_msg_chan.h"
#include "ovis_event/ovis_event.h"
#include "ovis_log/ovis_log.h"
#include "ovis_json/ovis_json.h"

static pthread_mutex_t chan_lock;
static LIST_HEAD(ldms_msg_chan_list, ldms_msg_chan_s)
	chan_list = LIST_HEAD_INITIALIZER(chan_list);
static int chan_init;
static pthread_t sched_thread;
static ovis_log_t chan_log;
ovis_scheduler_t chan_scheduler;

static int LERROR  = OVIS_LERROR;
static int LINFO   = OVIS_LINFO;
static int LDEBUG  = OVIS_LDEBUG;

typedef struct chan_client_s {
	ldms_msg_chan_t chan;
	ldms_msg_client_t client;
	char *regex_s;
	ldms_msg_event_cb_t msg_cb_fn;
	void *msg_cb_arg;
	LIST_ENTRY(chan_client_s) entry;
} *chan_client_t;

typedef struct msg_entry_s {
	char *tag;
	ldms_msg_type_t type;
	char *msg;
	size_t msg_len;
	struct ldms_cred cred;
	uint32_t perm;
	TAILQ_ENTRY(msg_entry_s) entry;
} *msg_entry_t;

#define DEFAULT_MAX_Q_DEPTH	(1024 * 1024)
struct ldms_msg_chan_s {
	char *app_name_s;	/* The message channel instance name (informational) */
	char *xprt_s;		/* The transport type */

	char *rem_host_s;	/* The peer host name */
	int rem_port;		/* The peer port number */
	ldms_t rem_ldms;	/* The connecting ldms transport */

	char *lcl_host_s;	/* The host name to listen on, default is 'localhost' */
	int lcl_port;		/* The port to listen on, default is 'port_s' */
	ldms_t lcl_ldms;	/* The listening ldms transport */

	char *auth_s;		/* Authentication plugin name */
	av_list_t auth_avl;	/* Options to authentication plugin */

	int reconnect;		/* The reconnect interval */

	struct sockaddr_storage rem_ss;
	size_t rem_ss_len;
	struct sockaddr_storage lcl_ss;
	size_t lcl_ss_len;

	struct ldms_msg_chan_stats_s stats;

	pthread_t chan_thread;

	pthread_mutex_t lock;

	pthread_cond_t io_cond;	   /* chan->lock */
	pthread_cond_t close_cond; /* chan->lock */

	struct ovis_event_s _connect_ev;
	struct ovis_event_s *connect_ev;


	LIST_HEAD(chan_client_list, chan_client_s) client_list;
	TAILQ_HEAD(msg_entry_list, msg_entry_s) msg_q;

	LIST_ENTRY(ldms_msg_chan_s) close_entry;
	LIST_ENTRY(ldms_msg_chan_s) entry;
};

static void *sched_routine(void *arg)
{
	ovis_scheduler_loop(chan_scheduler, 0);
	return NULL;
}

/* Attempt to resend queued messages */
static void flush_msg_queue(ldms_msg_chan_t chan);
static void __flush_msg_queue(ldms_msg_chan_t chan);

/* Routine to add reconnect event to scheduler */
static void sched_connect(ldms_msg_chan_t chan, int connect_timeout);

/* Transport connect event callback */
static void xprt_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg);

/* Event reconnect callback */
static void reconnect_cb(ovis_event_t ev)
{
	ldms_msg_chan_t chan = ev->param.ctxt;
	int rc;

	pthread_mutex_lock(&chan->lock);
	switch(chan->stats.state) {
	case LDMS_MSG_CHAN_DISCONNECTED:
		/* Destroy and re-create the transport */
		if (chan->rem_ldms) {
			ldms_xprt_close(chan->rem_ldms);
			chan->rem_ldms = NULL;
		}
		if (chan->stats.cancel) {
			if (chan->connect_ev) {
				ovis_scheduler_event_del(chan_scheduler, chan->connect_ev);
				chan->connect_ev = NULL;
			}
			ovis_log(chan_log, LDEBUG,
				 "%s:%d Signalling close_cond channel '%s' state '%s' "
				 "transport '%s:%s'.\n",
				 __func__, __LINE__,
				 chan->app_name_s,
				 ldms_msg_chan_state_str(chan->stats.state),
				 chan->xprt_s, chan->auth_s);
			pthread_cond_signal(&chan->close_cond);
			break;
		}
		chan->rem_ldms = ldms_xprt_new_with_auth(chan->xprt_s,
						     chan->auth_s,
						     chan->auth_avl);
		if (!chan->rem_ldms) {
			ovis_log(chan_log, LERROR,
				 "%s:%d Error %d, channel '%s' transport '%s:%s' "
				 "creation failed'.\n",
				 __func__, __LINE__,
				 errno, chan->app_name_s, chan->xprt_s, chan->auth_s);
		} else {
			ovis_log(chan_log, LINFO,
				 "%s:%d Channel '%s' attempting connection on '%s'\n",
				 __func__, __LINE__,
				 chan->app_name_s, chan->xprt_s);
			chan->stats.pub_conn_attempt ++;
			rc  = ldms_xprt_connect(chan->rem_ldms,
						(struct sockaddr *)&chan->rem_ss,
						chan->rem_ss_len,
						xprt_cb, chan);
			if (rc) {
				ovis_log(chan_log, LINFO,
					 "%s:%d Error %d attempting to "
					 "connect channel '%s'\n",
					 __func__, __LINE__,
					 rc, chan->app_name_s);
			} else {
				chan->stats.state = LDMS_MSG_CHAN_CONNECTING;
				if (chan->connect_ev) {
					ovis_scheduler_event_del(chan_scheduler,
								 chan->connect_ev);
					chan->connect_ev = NULL;
				}
			}
		}
		break;
	case LDMS_MSG_CHAN_CONNECTING:
	case LDMS_MSG_CHAN_CONNECTED:
		assert(0 == "unexpected call to reconnect while connecting or connecte");
		if (chan->connect_ev) {
			ovis_scheduler_event_del(chan_scheduler, chan->connect_ev);
			chan->connect_ev = NULL;
		}
		break;
	default:
		assert(0 == "Unexpected state in reconnecdt_cb");
		if (chan->connect_ev) {
			ovis_scheduler_event_del(chan_scheduler, chan->connect_ev);
			chan->connect_ev = NULL;
		}
		ovis_log(chan_log, LERROR,
			 "%s:%d Receiveed OVIS connect event on channel '%s' "
			 "in invalid state %s.\n",
			 __func__, __LINE__,
			 chan->app_name_s,
			 ldms_msg_chan_state_str(chan->stats.state));
		break;
	}
	pthread_mutex_unlock(&chan->lock);
}

/* Must be called with the chan->lock held */
static void sched_connect(ldms_msg_chan_t chan, int connect_timeout)
{
	assert(NULL == chan->connect_ev);
	chan->connect_ev = &chan->_connect_ev;
	OVIS_EVENT_INIT(chan->connect_ev);
	chan->connect_ev->param.type = OVIS_EVENT_TIMEOUT;
	chan->connect_ev->param.cb_fn = reconnect_cb;
	chan->connect_ev->param.ctxt = chan;
	chan->connect_ev->param.timeout.tv_sec = connect_timeout;
	chan->connect_ev->param.timeout.tv_usec = 0;
	int rc = ovis_scheduler_event_add(chan_scheduler, chan->connect_ev);
	if (rc)
		ovis_log(chan_log, LERROR,
			 "Error %d adding scheduler event for channel '%s'\n",
			 rc, chan->app_name_s);
}

/*
 * This is the LDMS event callback registered with the pub transport
 */
static void xprt_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	ldms_msg_chan_t chan = cb_arg;

	pthread_mutex_lock(&chan->lock);
	assert(NULL == chan->connect_ev);
	ovis_log(chan_log, LDEBUG,
		 "%s:%d Channel '%s' (%s:%s:%d:%s)"
		 " state: %s event type: %s\n",
		 __func__, __LINE__,
		 chan->app_name_s,
		 chan->xprt_s,
		 chan->rem_host_s, chan->rem_port, chan->auth_s,
		 ldms_msg_chan_state_str(chan->stats.state),
		 ldms_xprt_event_type_to_str(e->type));
	switch(e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		clock_gettime(CLOCK_REALTIME, &chan->stats.pub_last_conn);
		if (chan->stats.state == LDMS_MSG_CHAN_CONNECTING) {
			ovis_log(chan_log, LDEBUG,
				 "%s:%d Channel '%s' (%s:%s:%d:%s) %s --> CONNECTED\n",
				 __func__, __LINE__,
				 chan->app_name_s,
				 chan->xprt_s, chan->rem_host_s, chan->rem_port, chan->auth_s,
				 ldms_msg_chan_state_str(chan->stats.state));
			chan->stats.state= LDMS_MSG_CHAN_CONNECTED;
		}
		chan->stats.pub_conn_success ++;
		pthread_cond_broadcast(&chan->io_cond);
		break;
	case LDMS_XPRT_EVENT_ERROR:
	case LDMS_XPRT_EVENT_REJECTED:
	case LDMS_XPRT_EVENT_DISCONNECTED:
		if (chan->stats.closing) {
			if (chan->stats.cancel || TAILQ_EMPTY(&chan->msg_q)) {
				ovis_log(chan_log, LDEBUG,
					 "%s:%d Channel '%s' closing is True %s --> CLOSED\n",
					 __func__, __LINE__,
					 chan->app_name_s,
					 ldms_msg_chan_state_str(chan->stats.state));
				chan->stats.state = LDMS_MSG_CHAN_CLOSED;
			} else if (!TAILQ_EMPTY(&chan->msg_q)) {
				ovis_log(chan_log, LDEBUG,
					 "%s:%d Channel '%s' closing is True %s --> DISCONNECTED\n",
					 __func__, __LINE__,
					 chan->app_name_s,
					 ldms_msg_chan_state_str(chan->stats.state));
				chan->stats.state = LDMS_MSG_CHAN_DISCONNECTED;
				sched_connect(chan, chan->reconnect);
			}
			pthread_cond_signal(&chan->close_cond);
		} else {
			ovis_log(chan_log, LINFO,
				 "%s:%d Channel '%s' %s --> DISCONNECTED\n",
				 __func__, __LINE__,
				 chan->app_name_s,
				 ldms_msg_chan_state_str(chan->stats.state));
			chan->stats.state = LDMS_MSG_CHAN_DISCONNECTED;
			sched_connect(chan, chan->reconnect);
		}
		break;
	case LDMS_XPRT_EVENT_SEND_QUOTA_DEPOSITED:
		ovis_log(chan_log, LDEBUG,
			 "%s:%d Channel %s blocked %zd closing %d state %s event %s\n",
			 __func__, __LINE__,
			 chan->app_name_s, chan->stats.pub_blocked, chan->stats.closing,
			 ldms_msg_chan_state_str(chan->stats.state),
			 ldms_xprt_event_type_to_str(e->type));
		/* Check if we are blocked on ENOBUFS */
		if (chan->stats.pub_blocked) {
			chan->stats.pub_blocked = 0;
			pthread_cond_broadcast(&chan->io_cond);
		}

		chan->stats.pub_msg_acked += 1;
		/* Channel is closing, check that all published
		 * messages have been acknowledged */
		if (chan->stats.closing
		    && (chan->stats.pub_msg_sent == chan->stats.pub_msg_acked)) {
			ovis_log(chan_log, LINFO,
				 "%s:%d Channel '%s' closing %d %s --> CLOSED\n",
				 __func__, __LINE__,
				 chan->app_name_s, chan->stats.closing,
				 ldms_msg_chan_state_str(chan->stats.state));
			if (chan->rem_ldms) {
				ldms_xprt_close(chan->rem_ldms);
				chan->rem_ldms = NULL;
			}
			pthread_cond_signal(&chan->close_cond);
		}
		break;
	case LDMS_XPRT_EVENT_RECV:
		/* No need to process the response message. */
		ovis_log(chan_log, LDEBUG,
			 "%s:%d Channel %s closing %d state %s event %s\n",
			 __func__, __LINE__,
			 chan->app_name_s, chan->stats.closing,
			 ldms_msg_chan_state_str(chan->stats.state),
			 ldms_xprt_event_type_to_str(e->type));
		break;
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
		/* TODO: Ignore ?? */
		ovis_log(chan_log, LERROR,
			 "%s:%d Channel %s closing %d state %s event %s\n",
			 __func__, __LINE__,
			 chan->app_name_s, chan->stats.closing,
			 ldms_msg_chan_state_str(chan->stats.state),
			 ldms_xprt_event_type_to_str(e->type));
		break;
	default:
		break;
	}
	pthread_mutex_unlock(&chan->lock);
	return;
}

/*
 * When a peer channel connects to us.
 */
static void lcl_event_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	ldms_msg_chan_t chan = cb_arg;
	pthread_mutex_lock(&chan->lock);
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		/* You might handle this to filter who you are
		 * allowing connections from */
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
	case LDMS_XPRT_EVENT_REJECTED:
	case LDMS_XPRT_EVENT_ERROR:
		ldms_xprt_close(x);
		chan->lcl_ldms = NULL;
		break;
	case LDMS_XPRT_EVENT_RECV:
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
		/* Ignore .. shouldn't get these events anyway */
		break;
	default:
		break;
	}
	pthread_mutex_unlock(&chan->lock);
}

static int init_listener(ldms_msg_chan_t chan)
{
	int rc;
	struct addrinfo *ai = NULL, hint;
	memset(&hint, 0, sizeof(hint));
	hint.ai_flags = AI_PASSIVE;
	hint.ai_family = AF_INET;
	char port_s[16];
	snprintf(port_s, sizeof(port_s), "%d",chan->lcl_port);
	rc = getaddrinfo(chan->lcl_host_s, port_s, &hint, &ai);
	if (rc)
		return errno;

	chan->lcl_ldms =
		ldms_xprt_new_with_auth(chan->xprt_s, chan->auth_s, chan->auth_avl);
	if (!chan->lcl_ldms) {
		ovis_log(chan_log, LERROR,
			 "Error %d creating the listening '%s' transport\n",
			 errno, chan->xprt_s);
		rc = errno;
		goto err_0;
	}

	rc = ldms_xprt_listen_by_name(chan->lcl_ldms,
				      chan->lcl_host_s, port_s,
				      lcl_event_cb, chan);
	if (rc) {
		ovis_log(chan_log, LERROR,
			 "Error %d attempting to listen at %s:%s:%s:%s\n",
			 rc, chan->xprt_s, chan->lcl_host_s, port_s,
			 chan->auth_s);
		ldms_xprt_close(chan->lcl_ldms);
		chan->lcl_ldms = NULL;
	}

 err_0:
	freeaddrinfo(ai);
	return rc;
}

void *chan_io_proc(void *arg)
{
	ldms_msg_chan_t chan = arg;
	do {
		__flush_msg_queue(chan);
	} while (1);
	return NULL;
}

ldms_msg_chan_t ldms_msg_chan_find(const char *app_name_s)
{
	ldms_msg_chan_t chan;
	pthread_mutex_lock(&chan_lock);
	LIST_FOREACH(chan, &chan_list, entry) {
		if (0 == strcmp(app_name_s, chan->app_name_s))
			break;
	}
	pthread_mutex_unlock(&chan_lock);
	return chan;
}

ldms_msg_chan_t ldms_msg_chan_new(const char *app_name_s,
				  ldms_msg_chan_mode_t mode,
				  const char *xprt_s,
				  const char *rem_host_s,
				  int rem_port,
				  const char *lcl_host_s,
				  int lcl_port,
				  const char *auth_s,
				  av_list_t auth_avl,
				  int reconnect)
{
	ldms_msg_chan_t chan = NULL;
	int rc;

	pthread_mutex_lock(&chan_lock);
	if (!chan_init) {
		(void)ldms_msg_chan_log(NULL);

		chan_scheduler = ovis_scheduler_new();
		if (!chan_scheduler) {
			ovis_log(chan_log, LERROR,
				 "Error %d creating the channel scheduler.\n",
				 errno);
			goto err_0;
		}
		rc = pthread_create(&sched_thread, NULL,
				    sched_routine,
				    NULL);
		if (rc) {
			ovis_log(chan_log, LERROR,
				 "Error %d creating the scheduler thread.\n",
				 rc);
			ovis_scheduler_free(chan_scheduler);
			goto err_0;
		}
		pthread_setname_np(sched_thread, "msg_chan_sched");
		chan_init = 1;
	}

	chan = calloc(1, sizeof(*chan));
	if (!chan)
		goto err_0;

	errno = EINVAL;
	if (!app_name_s || !xprt_s)
		goto err_1;
	chan->app_name_s = strdup(app_name_s);
	if (!chan->app_name_s)
		goto err_1;
	chan->xprt_s = strdup(xprt_s);
	if (!chan->xprt_s)
		goto err_1;

	if (mode & LDMS_MSG_CHAN_MODE_PUBLISH) {
		errno = EINVAL;
		if (!rem_host_s)
			goto err_1;
		chan->rem_host_s = strdup(rem_host_s);
		if (!chan->rem_host_s || rem_port < 1 || rem_port > 65535)
			goto err_1;
		chan->rem_port = rem_port;

		/* Verify and parse the peer address */
		union ldms_sockaddr lss;
		socklen_t sa_len = sizeof(lss);
		char rem_port_s[16];
		snprintf(rem_port_s, sizeof(rem_port_s), "%d", rem_port);
		rc = ldms_getsockaddr(rem_host_s, rem_port_s, &lss.sa, &sa_len);
		switch (rc) {
		case 0:
			break;/* OK */
		case -ENOENT:
			errno = -rc;
			goto err_1;
		default:
			errno = rc;
			goto err_1;
		}
		chan->rem_ss = lss.storage;
		chan->rem_ss_len = sa_len;
	}

	if (mode & LDMS_MSG_CHAN_MODE_SUBSCRIBE) {
		errno = EINVAL;
		if (!lcl_host_s || lcl_port < 1 || lcl_port > 65535)
			goto err_1;
		chan->lcl_host_s = strdup(lcl_host_s);
		if (!chan->lcl_host_s)
			goto err_1;
		chan->lcl_port = lcl_port;

		/* Verify and parse the peer address */
		union ldms_sockaddr lss;
		socklen_t sa_len = sizeof(lss);
		char lcl_port_s[16];
		snprintf(lcl_port_s, sizeof(lcl_port_s), "%d", lcl_port);
		rc = ldms_getsockaddr(lcl_host_s, lcl_port_s, &lss.sa, &sa_len);
		switch (rc) {
		case 0:
			break;/* OK */
		case -ENOENT:
			errno = -rc;
			goto err_1;
		default:
			errno = rc;
			goto err_1;
		}
		chan->lcl_ss = lss.storage;
		chan->lcl_ss_len = sa_len;
	}

	if (!auth_s)
		auth_s = "none";

	LIST_INIT(&chan->client_list);
	chan->stats.mode = mode;
	chan->stats.max_q_depth = DEFAULT_MAX_Q_DEPTH;
	chan->stats.cur_q_depth = 0;
	TAILQ_INIT(&chan->msg_q);

	chan->auth_s = strdup(auth_s);
	if (!chan->auth_s)
		goto err_1;
	chan->auth_avl = av_copy(auth_avl);

	if (mode & LDMS_MSG_CHAN_MODE_SUBSCRIBE) {
		/* Create the listening endpoint */
		rc = init_listener(chan);
		if (rc)
			goto err_1;
	}

	pthread_cond_init(&chan->io_cond, NULL);

	pthread_mutex_init(&chan->lock, NULL);
	pthread_cond_init(&chan->close_cond, NULL);
	chan->stats.state = LDMS_MSG_CHAN_DISCONNECTED;
	chan->connect_ev = NULL;
	OVIS_EVENT_INIT(&chan->_connect_ev);
	chan->reconnect = reconnect;
	chan->auth_avl = av_copy(auth_avl);

	rc = pthread_create(&chan->chan_thread, NULL, chan_io_proc, chan);
	if (rc) {
		ovis_log(chan_log, LERROR,
			 "Error %d creating the channel thread.\n", rc);
		goto err_1;
	}
	pthread_setname_np(chan->chan_thread, "msg_chan_io");
	LIST_INSERT_HEAD(&chan_list, chan, entry);
	pthread_mutex_unlock(&chan_lock);

	pthread_mutex_lock(&chan->lock);
	if (mode & LDMS_MSG_CHAN_MODE_PUBLISH)
		sched_connect(chan, 0);
	pthread_mutex_unlock(&chan->lock);
	return chan;
 err_1:
	free(chan->app_name_s);
	free(chan->xprt_s);
	free(chan->rem_host_s);
	free(chan->lcl_host_s);
	free(chan->auth_s);
	av_free(chan->auth_avl);
	free(chan);
 err_0:
	pthread_mutex_unlock(&chan_lock);
	return NULL;
}

/* Must be called with the chan->lock held */
static void clear_msg_queue(ldms_msg_chan_t chan)
{
	msg_entry_t msg_entry;
	while (!TAILQ_EMPTY(&chan->msg_q)) {
		msg_entry = TAILQ_FIRST(&chan->msg_q);
		TAILQ_REMOVE(&chan->msg_q, msg_entry, entry);
		free(msg_entry->msg);
		free(msg_entry->tag);
		free(msg_entry);
	}
}

static void flush_msg_queue(ldms_msg_chan_t chan)
{
	pthread_cond_broadcast(&chan->io_cond);
}

/* Must be called with the chan->lock held */
static void __flush_msg_queue(ldms_msg_chan_t chan)
{
	int rc;
	msg_entry_t msg_entry;
	struct timespec to;

	pthread_mutex_lock(&chan->lock);
	while ((chan->stats.state != LDMS_MSG_CHAN_CONNECTED)
	       || (TAILQ_EMPTY(&chan->msg_q) && !chan->stats.closing)) {

		ovis_log(chan_log, LDEBUG,
			 "%s:%d Channel '%s' waiting for work or closed.\n",
			 __func__, __LINE__,
			 chan->app_name_s);
		clock_gettime(CLOCK_REALTIME, &to);
		to.tv_sec += 60;
		int xrc = pthread_cond_timedwait(&chan->io_cond, &chan->lock, &to);
		ovis_log(chan_log, LDEBUG,
			 "%s:%d Channel '%s' pthread_cond_timedwait returns %d\n",
			 __func__, __LINE__,
			 chan->app_name_s, xrc);
	}

	while (!TAILQ_EMPTY(&chan->msg_q)) {
		msg_entry = TAILQ_FIRST(&chan->msg_q);
		switch (chan->stats.state) {
		case LDMS_MSG_CHAN_CONNECTING:
		case LDMS_MSG_CHAN_DISCONNECTED:
			/* New publications are being accepted, but
			 * existing ones cannot be retired because
			 * there is no connection yet */
			goto out;
		case LDMS_MSG_CHAN_CONNECTED:
			ovis_log(chan_log, LDEBUG,
				 "%s:%d Channel '%s' flushing %zd bytes in state %s.\n",
				 __func__, __LINE__,
				 chan->app_name_s,
				 msg_entry->msg_len,
				 ldms_msg_chan_state_str(chan->stats.state));
			do {
				rc = ldms_msg_publish(chan->rem_ldms,
						      msg_entry->tag,
						      msg_entry->type,
						      &msg_entry->cred,
						      msg_entry->perm,
						      msg_entry->msg,
						      msg_entry->msg_len);
				switch (rc) {
				case 0:
					chan->stats.pub_blocked = 0;
					break;
				case ENOBUFS:
					chan->stats.pub_blocked = 1;
					while (chan->stats.pub_blocked) {
						int xrc;
						clock_gettime(CLOCK_REALTIME, &to);
						to.tv_sec += 6;
						xrc = pthread_cond_timedwait
							(
							 &chan->io_cond,
							 &chan->lock,
							 &to
							 );
						ovis_log(chan_log, LDEBUG,
							 "%s:%d Channel '%s' blocked %zd "
							 "pthread_cond_timedwait "
							 "returns %d\n",
							 __func__, __LINE__,
							 chan->app_name_s,
							 chan->stats.pub_blocked, xrc);
					}
					break;
				case EPERM:
					ovis_log(chan_log, LDEBUG,
						 "%s:%d Channel '%s' Insufficient "
						 "privilege to publish message as %d:%d\n",
						 __func__, __LINE__,
						 chan->app_name_s,
						 msg_entry->cred.uid, msg_entry->cred.gid);
					break;
				default:
					ovis_log(chan_log, LDEBUG,
						 "%s:%d Channel '%s' error %d attempting "
						 "to publish message\n",
						 __func__, __LINE__,
						 chan->app_name_s, rc);
					break;
				}
			} while (rc == ENOBUFS);
			break;
		case LDMS_MSG_CHAN_CLOSED:
			assert(0 == "msg_q: must be empty");
			break;
		}
		ovis_log(chan_log, LDEBUG,
			 "%s:%d Channel '%s' dequeuing %zd message bytes.\n",
			 __func__, __LINE__,
			 chan->app_name_s, msg_entry->msg_len);
		chan->stats.pub_msg_sent ++;
		chan->stats.pub_byte_cnt += msg_entry->msg_len;
		chan->stats.cur_q_depth -= msg_entry->msg_len;
		TAILQ_REMOVE(&chan->msg_q, msg_entry, entry);
		free(msg_entry->msg);
		free(msg_entry->tag);
		free(msg_entry);
	}
 out:
	/* Wake-up _publish() if blocked on msg_q full */
	pthread_cond_broadcast(&chan->io_cond);

	pthread_mutex_unlock(&chan->lock);
}

int ldms_msg_chan_publish(ldms_msg_chan_t chan, const char *tag,
			  uid_t uid, gid_t gid, uint32_t perm,
			  ldms_msg_type_t type, char *msg, size_t msg_len)
{
	msg_entry_t msg_entry;
	struct ldms_cred cred = {
		.uid = uid,
		.gid = gid
	};

	pthread_mutex_lock(&chan->lock);
	if (0 == (chan->stats.mode & LDMS_MSG_CHAN_MODE_PUBLISH)) {
		/* Channel is not a publisher */
		errno = ENOTSUP;
		goto err_0;
	}
	if (chan->stats.closing) {
		/* Channel is closing, don't accept any new publish requests */
		errno = ENOTCONN;
		goto err_0;
	}

	while (msg_len + chan->stats.cur_q_depth > chan->stats.max_q_depth) {
		/* Channel has reached the maximmum buffer threshold */
		ovis_log(chan_log, LINFO,
			 "%s:%d Channel '%s' blocked queuing %zd bytes.\n",
			 __func__, __LINE__,
			 chan->app_name_s, msg_len);
		struct timespec to;
		clock_gettime(CLOCK_REALTIME, &to);
		to.tv_sec += 100;
		int rc = pthread_cond_timedwait(&chan->io_cond, &chan->lock, &to);
		ovis_log(chan_log, LINFO,
			 "%s:%d Channel '%s' blocked wakeup rc is %d.\n",
			 __func__, __LINE__,
			 chan->app_name_s, rc);
	}

	msg_entry = calloc(1, sizeof(*msg_entry));
	if (!msg_entry)
		goto err_0;
	msg_entry->msg = malloc(msg_len);
	if (!msg_entry->msg)
		goto err_1;
	msg_entry->msg_len = msg_len;

	ovis_log(chan_log, LDEBUG,
		 "%s:%d Channel '%s' queuing %zd bytes.\n",
		 __func__, __LINE__,
		 chan->app_name_s,
		 msg_entry->msg_len);
	memcpy(msg_entry->msg, msg, msg_len);
	msg_entry->cred = cred;
	msg_entry->perm = perm;
	msg_entry->type = type;
	msg_entry->tag = strdup(tag);
	chan->stats.cur_q_depth += msg_len;
	TAILQ_INSERT_TAIL(&chan->msg_q, msg_entry, entry);
	flush_msg_queue(chan);
	pthread_mutex_unlock(&chan->lock);
	return 0;
 err_1:
	free(msg_entry);
 err_0:
	pthread_mutex_unlock(&chan->lock);
	return errno;
}

/*
 * This is a wrapper for messages from subscribed clients. It is here
 * to accumulate statistcs and to handle the CLIENT_CLOSE event that
 * tells us that our peer has nothing left to send.
 *
 * This event is only triggered by calling ldms_msg_client_close().
 */
static int __msg_cb_fn(ldms_msg_event_t ev, void *cb_arg)
{
	chan_client_t subs = cb_arg;
	switch (ev->type) {
	case LDMS_MSG_EVENT_RECV:
		pthread_mutex_lock(&subs->chan->lock);
		subs->chan->stats.sub_msg_cnt ++;
		subs->chan->stats.sub_byte_cnt += ev->recv.data_len;
		pthread_mutex_unlock(&subs->chan->lock);
		return subs->msg_cb_fn(ev, subs->msg_cb_arg);
	case LDMS_MSG_EVENT_CLIENT_CLOSE:
		/* Remove ourselves from the client list */
		pthread_mutex_lock(&subs->chan->lock);
		LIST_REMOVE(subs, entry);
		pthread_mutex_unlock(&subs->chan->lock);
		/* Wake up the waiter on the channel list being empty */
		pthread_cond_signal(&subs->chan->close_cond);
		free(subs->regex_s);
		free(subs);
		break;
	case LDMS_MSG_EVENT_SUBSCRIBE_STATUS:
	case LDMS_MSG_EVENT_UNSUBSCRIBE_STATUS:
		break;
	}
	return 0;
}

int ldms_msg_chan_subscribe(ldms_msg_chan_t chan, const char *regex,
			    ldms_msg_event_cb_t msg_cb_fn, void *cb_arg)
{
	char client_desc[512];
	chan_client_t subs;
	ldms_msg_client_t client;

	pthread_mutex_lock(&chan->lock);
	if (0 == (chan->stats.mode & LDMS_MSG_CHAN_MODE_SUBSCRIBE)) {
		/* Channel is not a publisher */
		errno = ENOTSUP;
		goto err_0;
	}
	subs = calloc(1, sizeof(*subs));
	if (!subs)
		goto err_0;
	subs->regex_s = strdup(regex);
	if (!subs->regex_s)
		goto err_1;

	subs->chan = chan;
	subs->msg_cb_fn = msg_cb_fn;
	subs->msg_cb_arg = cb_arg;

	snprintf(client_desc, sizeof(client_desc),
		 "Message channel '%s:%s' subscription",
		 chan->app_name_s, subs->regex_s);
	client = ldms_msg_subscribe(regex, 1,
				    __msg_cb_fn, subs,
				    client_desc);
	if (!client) {
		ovis_log(chan_log, LERROR,
			 "Channel '%s' Could not subscribe to name '%s'\n",
			 chan->app_name_s, regex);
		goto err_1;
	}
	subs->client = client;
	LIST_INSERT_HEAD(&chan->client_list, subs, entry);
	pthread_mutex_unlock(&chan->lock);
	return 0;
 err_1:
	free(subs->regex_s);
	free(subs);
 err_0:
	pthread_mutex_unlock(&chan->lock);
	return errno;
}

int ldms_msg_chan_unsubscribe(ldms_msg_chan_t chan, const char *regex_s)
{
	chan_client_t subs;
	int rc = ENOENT;
	pthread_mutex_lock(&chan->lock);
	do {
		LIST_FOREACH(subs, &chan->client_list, entry) {
			if (0 == strcmp(subs->regex_s, regex_s)) {
				LIST_REMOVE(subs, entry);
				/* closing the client will result in a
				 * CLOSE event being delivered to the
				 * registered callback. We can't
				 * destroy the subscriber until after
				 * this event has been delivered.
				 */
				ldms_msg_client_close(subs->client);
				break;
			}
		}
		if (subs)
			rc = 0;		/* Found at-least one */
	} while (subs);
	if (chan->stats.closing && chan->stats.state != LDMS_MSG_CHAN_CONNECTING) {
		ovis_log(chan_log, LDEBUG,
			 "%s:%d Signaling close_cond channel '%s' state '%s' "
			 "transport '%s:%s'.\n",
			 __func__, __LINE__,
			 chan->app_name_s,
			 ldms_msg_chan_state_str(chan->stats.state),
			 chan->xprt_s, chan->auth_s);
		pthread_cond_signal(&chan->close_cond);
	}
	pthread_mutex_unlock(&chan->lock);
	return rc;
}

ovis_log_t ldms_msg_chan_log(ldms_msg_chan_t chan)
{
	if (!chan_log)
		chan_log = ovis_log_register("ldms_msg_chan",
					     "Messages for the LDMS Message Channel");
	return chan_log;
}

void ldms_msg_chan_set_q_limit(ldms_msg_chan_t chan, size_t q_limit)
{
	chan->stats.max_q_depth = q_limit;
}

void ldms_msg_chan_stats(ldms_msg_chan_t chan, ldms_msg_chan_stats_t stats)
{
	pthread_mutex_lock(&chan->lock);
	*stats = chan->stats;
#ifdef __not_yet__
	chan_client_t cli;
	char *str;
	LIST_FOREACH(cli, &chan->client_list, entry) {
		str = ldms_msg_stats_str(cli->regex_s, 1, 0);
	}
#endif
	pthread_mutex_unlock(&chan->lock);
}

jbuf_t ldms_msg_chan_subscr_json(ldms_msg_chan_t chan)
{
	int first = 1;
	chan_client_t subs;
	jbuf_t jb = jbuf_new();
	jb = jbuf_append_str(jb, "[");
	pthread_mutex_lock(&chan->lock);
	LIST_FOREACH(subs, &chan->client_list, entry) {
		if (first) {
			first = 0;
		} else {
			jb = jbuf_append_str(jb, ",");
		}
		jb = jbuf_append_str(jb, "\"%s\"", subs->regex_s);
	}
	jb = jbuf_append_str(jb, "]");
	pthread_mutex_unlock(&chan->lock);
	return jb;
}

const char *ldms_msg_chan_state_str(ldms_msg_chan_state_t state)
{
	static const char *state_s[] = {
		"DISCONNECTED",
		"CONNECTING",
		"CONNECTED",
		"CLOSED",
	};
	if (state >= 0 && state < (sizeof(state_s) / sizeof(state_s[0])))
		return state_s[state];
	return "INVALID";
}

const char *ldms_msg_chan_mode_str(ldms_msg_chan_mode_t mode)
{
	static const char *mode_s[] = {
		"",
		"SUBSCRIBE",
		"PUBLISH",
		"BIDIR"
	};
	if (mode > 0 && mode < (sizeof(mode_s) / sizeof(mode_s[0])))
		return mode_s[mode];
	return "INVALID";
}

void ldms_msg_chan_mode(ldms_msg_chan_t chan, ldms_msg_chan_stats_t stats)
{
	pthread_mutex_lock(&chan->lock);
	*stats = chan->stats;
	pthread_mutex_unlock(&chan->lock);
}

/*
 * Must be called while holding the chan->lock
 */
static void chan_close(ldms_msg_chan_t chan, int cancel)
{
	chan->stats.closing = 1;
	chan->stats.cancel = cancel;
}

/*
 * Must be called while _not_ holding the chan->lock
 */
static void chan_close_wait(ldms_msg_chan_t chan, int cancel)
{
	pthread_mutex_lock(&chan->lock);
	if (!cancel) {
		ovis_log(chan_log, LDEBUG,
			 "%s:%d Channel %s %s waiting...\n",
			 __func__, __LINE__,
			 chan->app_name_s,
			 ldms_msg_chan_state_str(chan->stats.state));

		/* Wait until all of our remote I/O is flushed. */
		while (!TAILQ_EMPTY(&chan->msg_q)
		       || (chan->stats.pub_msg_sent != chan->stats.pub_msg_acked))
		{
			pthread_cond_wait(&chan->close_cond, &chan->lock);
		}
		/* Close all the clients */
		chan_client_t client;
		LIST_FOREACH(client, &chan->client_list, entry) {
			ldms_msg_client_close(client->client);
		}
		/* Wait until all of the clients receive their CLOSE events */
		while (!LIST_EMPTY(&chan->client_list)) {
			pthread_cond_wait(&chan->close_cond, &chan->lock);
		}
		ovis_log(chan_log, LDEBUG,
			 "%s:%d Channel %s %s wait complete...\n",
			 __func__, __LINE__,
			 chan->app_name_s,
			 ldms_msg_chan_state_str(chan->stats.state));
	}
	clear_msg_queue(chan);
	free(chan->app_name_s);
	free(chan->xprt_s);
	free(chan->rem_host_s);
	free(chan->lcl_host_s);
	free(chan->auth_s);
	av_free(chan->auth_avl);
	free(chan);
}

void ldms_msg_chan_close(ldms_msg_chan_t chan, int cancel)
{
	struct ldms_msg_chan_list close_list = LIST_HEAD_INITIALIZER(close_list);

	pthread_mutex_lock(&chan_lock);
	/* NB: The close_list is present, so that the global channel
	 * list lock is not held while closing-channels are in
	 * process. This allows ldms_msg_chan_new() to succeed while
	 * other channels are closing.
	 */
	if (chan) {
		LIST_INSERT_HEAD(&close_list, chan, close_entry);
		LIST_REMOVE(chan, entry);
	} else {
		while (!LIST_EMPTY(&chan_list)) {
			chan = LIST_FIRST(&chan_list);
			LIST_INSERT_HEAD(&close_list, chan, close_entry);
			LIST_REMOVE(chan, entry);
		}
	}
	pthread_mutex_unlock(&chan_lock);

	/* NB: The close and the wait are separated so that new I/O
	 * will not be accepted on all closing channels regardless of
	 * how long it takes for other channels to complete.
	 */
	LIST_FOREACH(chan, &close_list, close_entry) {
		pthread_mutex_lock(&chan->lock);
		chan_close(chan, cancel);
		pthread_mutex_unlock(&chan->lock);
	}
	while (!LIST_EMPTY(&close_list)) {
		chan = LIST_FIRST(&close_list);
		LIST_REMOVE(chan, close_entry);
		chan_close_wait(chan, cancel);
	}
}
