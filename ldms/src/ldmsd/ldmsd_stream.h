#ifndef _LDMS_STREAM_
#define _LDMS_STREAM_
#ifdef __cplusplus
extern "C" {
#endif
#include <json/json_util.h>

struct ldmsd_stream_client_s;
typedef struct ldmsd_stream_client_s *ldmsd_stream_client_t;
struct ldmsd_stream_s;
typedef struct ldmsd_stream_s *ldmsd_stream_t;

typedef struct ldmsd_stream_name {
	char *name;
	size_t len;
} *ldmsd_stream_name_t;

typedef enum ldmsd_stream_type_e {
	LDMSD_STREAM_STRING,
	LDMSD_STREAM_JSON
} ldmsd_stream_type_t;

enum ldmsd_stream_type_e ldmsd_stream_type_str2enum(const char *type);
const char *ldmsd_stream_type_enum2str(enum ldmsd_stream_type_e type);
/*
 * \brief Extract the stream header from \c s.
 *
 * \param s buffer containing the stream header
 * \param _stream_name		Buffer to contain the stream name
 * \param _stream_type 		Type of stream
 * \param _data			Stream payload
 * \param _offset 		Offset of the stream payload in the buffer \c s
 */
void __ldmsd_stream_extract_hdr(const char *s, char **_stream_name,
			enum ldmsd_stream_type_e *_stream_type,
			char **_data, size_t *_offset);
extern int ldmsd_stream_publish(ldms_t xprt, const char *stream_name,
				ldmsd_stream_type_t stream_type,
				const char *data, size_t data_len);
typedef int (*ldmsd_stream_recv_cb_t)(ldmsd_stream_client_t c, void *ctxt,
				      ldmsd_stream_type_t stream_type,
				      const char *data, size_t data_len,
				      json_entity_t entity);
extern ldmsd_stream_client_t
ldmsd_stream_subscribe(const char *stream_name,
		       ldmsd_stream_recv_cb_t cb_fn, void *ctxt);
extern void ldmsd_stream_close(ldmsd_stream_client_t c);
extern const char* ldmsd_stream_name(ldmsd_stream_t s);
extern const char* ldmsd_stream_client_name(ldmsd_stream_client_t c);
extern int ldmsd_stream_publish_file(const char *stream, const char *type,
				     const char *xprt, const char *host, const char *port,
				     const char *auth, struct attr_value_list *auth_opt,
				     FILE *file);
void ldmsd_stream_deliver(const char *stream_name, ldmsd_stream_type_t stream_type,
			  const char *data, size_t data_len,
			  json_entity_t entity);

#ifdef __cplusplus
}
#endif
#endif
