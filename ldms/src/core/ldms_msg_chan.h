#ifndef __LDMS_MSG_CHAN_H__
#define __LDMS_MSG_CHAN_H__

#include <sys/types.h>
#include <regex.h>
#include <sys/queue.h>
#include <pthread.h>
#include "ovis_log/ovis_log.h"
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
 * - A message Channel is bi-directional and can publish and subscribe
 *   to receive messages on the same channel.
 */
typedef struct ldms_msg_chan_s *ldms_msg_chan_t;

/**
 * \brief Create a Message channel
 *
 * \param app_name  The application name hosting the Message Channel
 *                  object. This is _not_ the name of the message
 *                  channel oon which messages are published and subscribed.
 * \param xprt      The transport type name, e.g. "sock", "rdma", "fabric"
 * \param rem_host  The peer host name
 * \param rem_port  The peer host port number
 * \param lcl_host  The local host name, e.g. to identify an interface,
 *                  by default, this is "localhost".
 * \param lcl_port  The local port to listen for peer connection requests
 * \param auth      The authentication plugin name
 * \param auth_avl  An av_list containing the authentication plugin options
 * \param reconnect The interval (in seconds) between attempts to reconnect with a peer.
 *
 * \returns The message channel handle or NULL to indicate an error. See errno.
 */
ldms_msg_chan_t ldms_msg_chan_new(const char *app_name,
				  const char *xprt,
				  const char *rem_host,
				  const char *rem_port,
				  const char *lcl_host,
				  const char *lcl_port,
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
 * of bytes queued is capped at a limit configured for the
 * channel. Afer this limit is reached, the function will return
 * ENOBUFS for subsequent messages.
 *
 * \param chan The Message Channel handle
 * \param type The message type: LDMS_MSG_STRING, LDMS_MSG_JSON, or LDMS_MSG_AVRO
 * \param uid The user-id with which messages will be tagged
 * \param gid The group-id with which messages will be tagged
 * \param perm The read-write authorization for consumers
 * \param msg Pointer to the buffer containing the message
 * \param msg_len The length of \c msg in bytes
 *
 * \return 0 on success
 * \return ENOMEM memory allocaion error
 * \return ENOBUFS the channel has reached the queued message limit.
 */
int ldms_msg_chan_publish(ldms_msg_chan_t chan, const char *chan_name,
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
 * \brief q_depth The maximum queue depth in bytes
 */
void ldms_msg_chan_set_q_depth(ldms_msg_chan_t chan, size_t q_depth);

typedef enum ldms_msg_chan_state_e {
	LDMS_MSG_CHAN_DISCONNECTED,
	LDMS_MSG_CHAN_CONNECTING,
	LDMS_MSG_CHAN_CONNECTED,
} ldms_msg_chan_state_t;

typedef struct ldms_msg_chan_stats_s {
	size_t max_q_depth;
	size_t cur_q_depth;

	size_t pub_msg_cnt;
	size_t pub_byte_cnt;
	size_t pub_conn_attempt;
	size_t pub_conn_success;
	struct timespec pub_last_conn;

	size_t sub_byte_cnt;
	size_t sub_msg_cnt;
	size_t sub_conn_cnt;
	struct timespec sub_last_conn;

	ldms_msg_chan_state_t state;
} *ldms_msg_chan_stats_t;

/**
 * \brief Return a message channel status data
 *
 * \param chan The channel handle
 * \param stats Pointer to a status buffer
 */
void ldms_msg_chan_stats(ldms_msg_chan_t chan, ldms_msg_chan_stats_t stats);

const char *ldms_msg_chan_state_str(ldms_msg_chan_state_t state);

#endif /* __LDMS_MSG_CHAN_H__ */
