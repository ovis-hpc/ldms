#ifndef _LDMS_STREAM_
#define _LDMS_STREAM_
#ifdef __cplusplus
extern "C" {
#endif
#include "ldms.h"
#include <ovis_json/ovis_json.h>

struct ldmsd_stream_client_s;
typedef struct ldmsd_stream_client_s *ldmsd_stream_client_t;

typedef enum ldmsd_stream_type_e {
	LDMSD_STREAM_STRING,
	LDMSD_STREAM_JSON
} ldmsd_stream_type_t;

/**
 * \brief Create a new stream
 *
 * Create a stream of the given name \c name.
 *
 * \param name   Stream name
 *
 * \return 0 on success.
 *         EEXIST if the stream already exists.
 *         ENOMEM if it fails to allocate memory
 *
 * \seealso ldmsd_streame_new_publish
 */
extern int ldmsd_stream_new(const char *name);

/**
 * \brief Notify a client of a new stream
 *
 * Send a LDMSD_STREAM_NEW_REQ message of a stream \c name to a client connected by \c xprt.
 * If stream \c name does not exist, it is created and then a notification is sent
 * to the client.
 *
 * Applications may call the function multiple times to notify multiple clients.
 *
 * \param name   Stream name
 * \param xprt   LDMS endpoint to a client
 *
 * \return 0 on success.
 *         EINVAL if \c xprt is NULL.
 *         ENOMEM if it fails to allocate memory.
 *         Other errno, if it fails to send the STREAM_NEW message.
 */
extern int ldmsd_stream_new_publish(const char *name, ldms_t xprt);

/**
 * \brief Publish data to a stream
 * Publish JSON or STRING data to a stream. JSON data is formatted as
 * a JSON object. JSON formatted data is validated at the subscriber;
 * no error is returned for an incorrectly formatted JSON object, however,
 * the data will be discarded at the subscriber.
 *
 * STRING data can be in any format, and is delivered as-is to
 * subscribers.
 *
 * \param xprt The LDMS transport handle
 * \param stream_name The stream name
 * \param stream_type The format of the data to be published
 * \param data Pointer to a buffer containting the data to pubish
 * \param data_len The size of the buffer to publish
 * \return 0 The data was succesfully sent
 * \return !0 An error indicating why the data could not be published
 */
extern int ldmsd_stream_publish(ldms_t xprt, const char *stream_name,
				ldmsd_stream_type_t stream_type,
				const char *data, size_t data_len);
/**
 * \brief Callback function invoked when stream data arrives
 *
 * \param c The stream client handle
 * \param cb_arg The \c cb_arg parameter provided to ldmsd_stream_subscribe()
 * \param stream_type One of LDMSD_STREAM_STRING or LDMSD_STREAM_JSON
 * \param data Pointer to the published data
 * \param data_len The number of bytes of data pointed to by \c data
 * \param entity If stream_type is LDMSD_STREAM_JSON, a pointer to a
 * parsed JSON object.
 * \returns An integer indication the success or failure of handling the data
 */
typedef int (*ldmsd_stream_recv_cb_t)(ldmsd_stream_client_t c, void *cb_arg,
				      ldmsd_stream_type_t stream_type,
				      const char *data, size_t data_len,
				      json_entity_t entity);
/**
 * \brief Subscribe to a named LDMSD stream
 *
 * \param stream_name The name of the stream
 * \param cb_fn The function to call when data arrives on the stream
 * \param cb_arg Provided as an argument to the callback function
 * \returns The client handle
 */
extern ldmsd_stream_client_t
ldmsd_stream_subscribe(const char *stream_name,
		       ldmsd_stream_recv_cb_t cb_fn, void *cb_arg);
/**
 * \brief Close a subscribed stream
 * \param c The client handle
 */
extern void ldmsd_stream_close(ldmsd_stream_client_t c);
/**
 * \brief Return the name of the stream to which the client is subscribed
 *
 * \param c The client handle
 * \returns The stream name
 */
extern const char* ldmsd_stream_client_name(ldmsd_stream_client_t c);
/**
 * \brief Publish the contents of a file to a stream
 *
 * \param stream The stream name
 * \param type The format of the published data: "json", or "string"
 * \param xprt The transport name
 * \param host The host name
 * \param port The port number
 * \param auth The authentication plugin name
 * \param auth_opt The authentication plugin options
 * \param file The file name
 * \returns 0 on success
 * \returns errno on failure
 */
extern int ldmsd_stream_publish_file(const char *stream, const char *type,
				     const char *xprt, const char *host, const char *port,
				     const char *auth, struct attr_value_list *auth_opt,
				     FILE *file);
/**
 * \brief Deliver stream data to a local subscriber
 *
 * \param stream_name The stream name
 * \param stream_type The stream type: "json" or "string"
 * \param data Pointer to the buffer containing the data
 * \param data_len Bytes of data in \c data
 * \param entity An optional parsed JSON object. If entity is NULL, and
 * stream_type == JSON, the data will be parsed perior to delivery to the subscribers.
 * \param p_name   Publisher name or NULL. If a string is given, the statistic of
 *                 stream data sent by the publisher will be collected.
 */
void ldmsd_stream_deliver(const char *stream_name, ldmsd_stream_type_t stream_type,
			  const char *data, size_t data_len,
			  json_entity_t entitym, const char *p_name);

int ldmsd_stream_response(ldms_xprt_event_t e);

#define LDMSD_STREAM_F_RAW	1	/*< Don't parse incoming stream data */
/**
 * \brief Set stream delivery flags
 *
 * \param c The stream client handle
 * \param flags The flags as a bitmap
 */
void ldmsd_stream_flags_set(ldmsd_stream_client_t c, uint32_t flags);
/**
 * \brief Get the stream delivery flags
 *
 * \param c The stream client handle
 */
uint32_t ldmsd_stream_flags_get(ldmsd_stream_client_t c);

/**
 * \brief Report the number of subscribers
 *
 * Returns the number of clients subscribed to \c stream_name
 *
 * \param stream_name The stream name
 * \returns The number of stream subscribers
 */
int ldmsd_stream_subscriber_count(const char *stream_name);

/**
 * \brief Update the stream's publish statistics
 *
 * Applications call the function in the subscribe callback function to republish the stream.
 * The function updates the stream's publish statistics.
 */
int ldmsd_client_stream_pubstats_update(ldmsd_stream_client_t c, size_t data_len);

/**
 * Dump stream clients in JSON.
 *
 * The caller is responsible for freeing the returned string.
 *
 * \retval s    The stream clients in JSON format.
 * \retval NULL If there is an error. \c errno is also set in this case.
 */
char * ldmsd_stream_client_dump();

/**
 * \brief Dump stream information in JSON
 *
 * The caller is responsible for freeing the returned string.
 *
 * \return s    The stream information in JSON format.
 *         NULL if there is an error. \c errno is also set in this case.
 */
char *ldmsd_stream_dir_dump();

/**
 * \brief Reset the statistics of all streams
 */
void ldmsd_stream_stats_reset_all();

/**
 * \brief Remove a publisher from all streams
 *
 * \param p_name   Publisher name
 */
void ldmsd_stream_publisher_remove(const char *p_name);
#ifdef __cplusplus
}
#endif
#endif
