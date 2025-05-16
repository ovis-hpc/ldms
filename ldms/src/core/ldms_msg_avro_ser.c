#include "config.h"
#include "ldms.h"

#include "ldms_msg.h"
#include "ldms_msg_avro_ser.h"

#include "avro.h"
#include <libserdes/serdes.h>
#include <libserdes/serdes-avro.h>

struct ldms_msg_client_avro_ser_s {
	ldms_msg_client_t sc;
	serdes_t *serdes;
	ldms_msg_avro_ser_event_cb_t app_cb_fn;
	void *app_cb_arg;
};

serdes_schema_t * serdes_schema_from_avro(serdes_t *sd, avro_schema_t asch)
{
	char buf[4096] = ""; /* should be sufficient? */
	char ebuf[4096];
	serdes_schema_t *ssch = NULL;
	const char *name;
	int rc;
	avro_writer_t aw = avro_writer_memory(buf, sizeof(buf));
	rc = avro_schema_to_json(asch, aw);
	if (rc) {
		errno = rc;
		goto out;
	}
	name = avro_schema_name(asch);
	ssch = serdes_schema_add(sd, name, -1, buf, strlen(buf),
						     ebuf, sizeof(ebuf));
	if (!ssch) {
		errno = EIO;
	}
	/* serdes schema is cached */
	avro_writer_free(aw);
 out:
	return ssch;
}

int ldms_msg_publish_avro_ser(ldms_t x, const char *stream_name,
				 ldms_cred_t cred, uint32_t perm,
				 avro_value_t *value, serdes_t *sd,
				 struct serdes_schema_s **sch)
{
	serdes_schema_t *ssch = NULL;
	avro_schema_t asch;
	char ebuf[4096];
	serdes_err_t serr;
	int rc;
	size_t sz;
	void *payload;

	if (0 == serdes_serializer_framing_size(sd)) {
		/* Need serdes "serializer.framing" enabled */
		return ENOPROTOOPT;
	}

	if (sch)
		ssch = *sch;
	if (!ssch) {
		/* need to build serdes schema */
		asch = avro_value_get_schema(value);
		ssch = serdes_schema_from_avro(sd, asch);
		if (!ssch)
			return errno;
	}
	if (sch)
		*sch = ssch;
	payload = NULL;
	serr = serdes_schema_serialize_avro(ssch, value, &payload, &sz,
					    ebuf, sizeof(ebuf));
	if (serr != SERDES_ERR_OK) {
		return EIO;
	}

	/* We can use existing stream_publish to publish the serialized data */
	rc = ldms_msg_publish(x, stream_name, LDMS_MSG_AVRO_SER,
				 cred, perm, payload, sz);
	return rc;
}

static int avro_value_from_stream_data(const char *data, size_t data_len,
				serdes_t *sd, avro_value_t **aout,
				serdes_schema_t **sout)
{
	int rc = 0;
	avro_value_t *av;
	char ebuf[4096];
	serdes_err_t serr;

	if (!sd) {
		rc = EINVAL;
		goto out;
	}

	av = malloc(sizeof(*av));
	if (!av) {
		rc = errno;
		goto out;
	}

	serr = serdes_deserialize_avro(sd, av, sout,
					data, data_len,
					ebuf, sizeof(ebuf));
	if (serr) {
		free(av);
		av = NULL;
		rc = EIO;
		goto out;
	}
	/* caller will free av later */
	*aout = av;
	rc = 0;
 out:
	return rc;
}


int ldms_msg_avro_ser_interpose(ldms_msg_event_t ev, void *cb_arg)
{
	int rc = 0;
	struct ldms_msg_client_avro_ser_s *ac = cb_arg;
	struct ldms_msg_avro_ser_event_s aev = {.ev = *ev};
	switch (ev->type) {
	case LDMS_MSG_EVENT_RECV:
		rc = avro_value_from_stream_data(ev->recv.data,
				ev->recv.data_len,
				ac->serdes,
				&aev.avro_value,
				&aev.serdes_schema);
		if (rc)
			return rc;
		rc = ac->app_cb_fn(&aev, ac->app_cb_arg);
		avro_value_decref(aev.avro_value);
		break;
	case LDMS_MSG_EVENT_CLIENT_CLOSE:
		rc = ac->app_cb_fn(&aev, ac->app_cb_arg);
		free(ac);
		break;
	default:
		return ac->app_cb_fn(&aev, ac->app_cb_arg);
	}
	return rc;
}

ldms_msg_client_t
ldms_msg_subscribe_avro_ser(const char *stream, int is_regex,
		      ldms_msg_avro_ser_event_cb_t cb_fn, void *cb_arg,
		      const char *desc, serdes_t *serdes)
{
	struct ldms_msg_client_avro_ser_s *ac = NULL;

	if (!cb_fn) {
		errno = EINVAL;
		goto err0;
	}

	if (!serdes) {
		errno = EINVAL;
		goto err0;
	}

	ac = calloc(1, sizeof(*ac));
	if (!ac)
		goto err0;

	ac->serdes = serdes;
	ac->app_cb_fn = cb_fn;
	ac->app_cb_arg = cb_arg;

	ac->sc = ldms_msg_subscribe(stream, is_regex,
			ldms_msg_avro_ser_interpose, ac, desc);

	if (!ac->sc)
		goto err1;

	return ac->sc;

 err1:
	free(ac);
 err0:
	return NULL;
}
