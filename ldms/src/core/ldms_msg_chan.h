#ifndef __LDMS_MSG_CHAN_H__
#define __LDMS_MSG_CHAN_H__

#include <sys/types.h>
#include <regex.h>
#include <sys/queue.h>
#include <pthread.h>
#include "ovis_log/ovis_log.h"
#include "ovis_json/ovis_json.h"
#include "ldms.h"

/**
 * A Message Channel is a reliable connected interface between one or
 * more peers. A Message Channel differs from a Message Client in the
 * following way:
 *
 * - A Message Channel will attempt to connect and reconnect
 *   automatically with a peer.
 *
 * - Messages published ot a channel are queued until the connection
 *   is established up until a configurable buffer limit is
 *   reached. The default is 1MB.
 *
 * - A message Channel can be instantiated in one of three modes:
 *
 *   - PUBLISH    A connection is established with a remote peer and
 *                messages can be published with ldms_msg_chan_publish()
 *
 *   - SUBSCRIBE The message channel will listen for incoming
 *               connections. Channels can be subscribed to with
 *               ldms_msg_chan_subscribe().
 *
 *   - BIDIR     The channel is bi-directional and can both publish and
 *               subscribe.
 */
typedef struct ldms_msg_chan_s *ldms_msg_chan_t;

typedef enum ldms_msg_chan_mode_e {
	LDMS_MSG_CHAN_MODE_SUBSCRIBE = 1,
	LDMS_MSG_CHAN_MODE_PUBLISH = 2,
	LDMS_MSG_CHAN_MODE_BIDIR = 3
} ldms_msg_chan_mode_t;

/**
 * \brief Create a Message channel
 *
 * Create a new message channel. The \c mode specifies whether this
 * channel is a publisher, subscriber, or both. When \c mode is
 * LDMS_MSG_CHAN_PUBLISH, the \c lcl_host, and \c lcl_port parameters
 * are ignored. When \c When in \c mode  is LDMS_MSG_CHAN_SUBSCRIBE,
 * the \c rem_host and \c rem_port parameters are ignored. WHen \c
 * mode is LDMS_MSG_CHAN_BIDIR all parameters are required as defined
 * below.
 *
 * \param app_name  The application name hosting the Message Channel
 *                  object. This is _not_ the name of the message
 *                  channel oon which messages are published and subscribed.
 *
 * \param xprt      The transport type name, e.g. "sock", "rdma",
 *                  "fabric". If NULL, the default is "sock"
 *
 * \param mode      One of LDMS_MSG_CHAN_MODE_PUBLISH,
 *                  LDMS_MSG_CHAN_MODE_SUBSCRIBE, or
 *                  LDMS_MSG_CHAN_MODE_BIDIR
 *
 * \param rem_host  The peer host name. If NULL, the default is "localhost"
 *
 * \param rem_port  The peer host port number
 *
 * \param lcl_host  The local host name, e.g. to identify an
 *                  interface, If NULL, the default "localhost".
 *
 * \param lcl_port  The local port to listen for peer connection requests.
 *
 * \param auth      The authentication plugin name. If NULL, the
 *                  default is "none"
 *
 * \param auth_avl  An av_list containing the authentication plugin
 *                  options. May be NULL.
 *
 * \param reconnect The interval (in seconds) between attempts to
 *                  reconnect with a peer.
 *
 * \returns The message channel handle or NULL to indicate an error. See errno.
 */
ldms_msg_chan_t ldms_msg_chan_new(const char *app_name,
				  ldms_msg_chan_mode_t mode,
				  const char *xprt,
				  const char *rem_host,
				  int rem_port,
				  const char *lcl_host,
				  int lcl_port,
				  const char *auth,
				  struct attr_value_list *auth_avl,
				  int reconnect);

/**
 * \brief Publish a message to the channel
 *
 * Submit a message to be published to the channel. The input buffer
 * is copied and can be reused by the caller immediately. Messages are
 * sent immediately if the channel is connected, otherwise the message
 * is queued until a connection to the peer is established. The number
 * of bytes queued is capped at a limit configured for the channel,
 * see ldms_msg_chan_set_q_limit(). Afer this limit is reached, the
 * function will return ENOBUFS for subsequent messages.
 *
 * \param chan The Message Channel handle
 * \param tag The message tag
 * \param uid The user-id with which messages will be tagged
 * \param gid The group-id with which messages will be tagged
 * \param perm The read-write authorization for consumers
 * \param type The message type: LDMS_MSG_STRING, LDMS_MSG_JSON, or LDMS_MSG_AVRO
 * \param msg Pointer to the buffer containing the message
 * \param msg_len The length of \c msg in bytes
 *
 * \return 0 on success
 * \return ENOMEM memory allocaion error
 * \return ENOBUFS the channel has reached the queued message limit.
 */
int ldms_msg_chan_publish(ldms_msg_chan_t chan, const char *tag,
			  uid_t uid, gid_t gid, uint32_t perm,
			  ldms_msg_type_t type, char *msg, size_t msg_len);

/**
 * \brief Subscribe to receive messages on a channel
 *
 * Messages received on the channel \c regex will be delivered to the
 * callback function \c msg_cb_fn. In order for the client to receive
 * a message, the \c uid, \c gid, and \c perm must allow for the
 * client to read the message. The uid and gid in the message header
 * are compared aginst the geteuid() and getegid() reported for the
 * process and if the \c perm bits allow, the message is delivered to
 * the callback function. If the client is not permitted to read the
 * message, the callback function is not called and the message is not
 * delivered.
 *
 * The \c regex parameter specifies which channel names are to be
 * considered for delivery. If messages are received on a channel name
 * that does not match \c regex, the message is not delivered.
 *
 * \param chan The Message Channel handle
 * \param regex The regular expression used to match channel names
 * \param msg_cb_fn The function that will receive messages
 * \param cb_arg A value delivered to \c msg_cb_fn in addition to the
 * message event.
 *
 * \return 0 on success
 * \return ENOMEM memory allocaion error
 */
int ldms_msg_chan_subscribe(ldms_msg_chan_t chan, const char *regex,
			    ldms_msg_event_cb_t msg_cb_fn, void *cb_arg);


/**
 * \brief Un-subscribe to receive messages on a channel
 *
 * The \c regex parameter specifies which channel names are to be
 * considered for delivery. This is used to find a previous subscription
 * that subscribed with this \c regex.
 *
 * \param chan The Message Channel handle
 * \param regex The regular expression used to match channel names
 *
 * \return 0 on success
 * \return ENOENT A channel with a matching \c regex was not found
 */
int ldms_msg_chan_unsubscribe(ldms_msg_chan_t chan, const char *regex_s);

/**
 * \brief Return the message log handle
 *
 * The message log handle is useful for clients that wish to change
 * the logging level for messages emitted by the library. For example:
 *
 * ``` C
 *    ovis_log_t log = ldms_msg_chan_log(chan);
 *    ovis_log_set_level(log, ovis_log_str_to_level("error,debug");
 * ```
 *
 * \returns The message log handle for the Message Channel library
 */
ovis_log_t ldms_msg_chan_log(ldms_msg_chan_t chan);

/**
 * \brief Set the message channel max queue depth
 *
 * Set the channel queue depth that limits the number of bytes queued
 * for delivery when the peer is disconnected.
 *
 * \brief chan The channel handle
 * \brief q_limit The maximum queue depth in bytes
 */
void ldms_msg_chan_set_q_limit(ldms_msg_chan_t chan, size_t q_limit);

typedef enum ldms_msg_chan_state_e {
	LDMS_MSG_CHAN_DISCONNECTED,
	LDMS_MSG_CHAN_CONNECTING,
	LDMS_MSG_CHAN_CONNECTED,
	LDMS_MSG_CHAN_CLOSED
} ldms_msg_chan_state_t;

typedef struct ldms_msg_chan_stats_s {
	ldms_msg_chan_mode_t mode;

	size_t max_q_depth;
	size_t cur_q_depth;

	size_t pub_msg_sent;
	size_t pub_msg_acked;
	size_t pub_byte_cnt;
	size_t pub_blocked;
	size_t pub_conn_attempt;
	size_t pub_conn_success;
	struct timespec pub_last_conn;

	size_t sub_byte_cnt;
	size_t sub_msg_cnt;
	size_t sub_conn_cnt;
	struct timespec sub_last_conn;

	int closing;
	int cancel;

	ldms_msg_chan_state_t state;
} *ldms_msg_chan_stats_t;

/**
 * \brief Return a message channel status data
 *
 * \param chan The channel handle
 * \param stats Pointer to a status buffer
 */
void ldms_msg_chan_stats(ldms_msg_chan_t chan, ldms_msg_chan_stats_t stats);

/**
 * \brief Return message channel state string
 *
 * Return a string representation of the message channel state.
 *
 * \param state The channel state
 */
const char *ldms_msg_chan_state_str(ldms_msg_chan_state_t state);

/**
 * \brief Return message channel mode string
 *
 * Return a string representation of the message channel mode.
 *
 * \param mode The channel mode
 */
const char *ldms_msg_chan_mode_str(ldms_msg_chan_mode_t mode);

/**
 * \brief Return client subscription list as JSON
 *
 * \param chan The channel handle
 */
jbuf_t ldms_msg_chan_subscr_json(ldms_msg_chan_t chan);

/**
 * \brief Close one or all LDMS Message Channels
 *
 * If the \c chan parameter is NULL, all Message Channels will be
 * closed. Otherwise, only the specifiied message channel is closed.
 *
 * If \c cancel is !0, all outstanding I/O is cancelled and the
 * channel is closed. Otherwise, the message channel will continue to
 * attempt to connect to the peer and complete all queued outstanding
 * communication. Specifically, if \c cancel is 0, This is a blocking
 * operation to ensure that all I/O is flushed to the peer prior to
 * returning. This allows an application to ensure that the peer has
 * received the data before exiting the process. After calling
 * ldms_msg_chan_close(), new I/O submited with
 * ldms_msg_chan_publish() will return ENOTCONN;
 *
 * While ldms_msg_chan_close() is in progress, new channels can be
 * created with ldms_msg_chan_new() and will not be affected by close
 * in progress on channels present at the time ldms_msg_chan_close()
 * was called.
 *
 * If \c cancel is !0 ldms_msg_chan_new() will block waiting for all
 * existing channels to close.
 *
 * Accessing the message channel handle after calling
 * ldms_msg_chan_close() will likely crash.
 *
 * \param chan The message channel to close, NULL implies that all
 *             message channels are closed.
 * \param cancel If !0, cancel outstanding I/O and close immedieately.
 */
void ldms_msg_chan_close(ldms_msg_chan_t chan, int cancel);
#define LDMS_MSG_CHAN_CLOSE_WAIT_IO	0
#define LDMS_MSG_CHAN_CLOSE_CANCEL_IO	1

/**
 * \brief Return the message handle with the matching application name
 *
 * \param app_name The application name to find.
 * \returns The message channel handle, or NULL if not found.
 */
ldms_msg_chan_t ldms_msg_chan_find(const char *app_name);

/**
 * \brief Return a list of all message channels
 *
 * \param app_name The application name to find.
 * \returns The message channel handle, or NULL if not found.
 */
ldms_msg_chan_t ldms_msg_chan_list(const char *app_name);

#endif /* __LDMS_MSG_CHAN_H__ */
