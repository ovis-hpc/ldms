#include "bout_sos_msg.h"
#include "sos_msg_class_def.h"

/**
 * \brief process_output for SOS.
 *
 * This function will create a new sos_object according to \a odata, and
 * put it into the opened sos storage.
 * \param this The pointer to the plugin instance.
 * \param odata The output data.
 * \return 0 on success.
 * \return Error code on error.
 */
int bout_sos_msg_process_output(struct boutplugin *this,
				struct boutq_data *odata)
{
	struct bout_sos_plugin *_this = (typeof(_this))this;
	pthread_mutex_lock(&_this->sos_mutex);
	sos_t sos = _this->sos;
	if (!sos)
		return EBADFD;
	sos_obj_t obj = sos_obj_new(sos);
	sos_obj_attr_set(sos, 0, obj, &odata->tv.tv_sec);
	sos_obj_attr_set(sos, 1, obj, &odata->tv.tv_usec);
	sos_obj_attr_set(sos, 2, obj, &odata->comp_id);
	sos_obj_attr_set(sos, 3, obj, &(struct sos_blob_arg_s) {
				sizeof(struct bmsg) +
				odata->msg->argc * sizeof(odata->msg->argv[0]),
				odata->msg
			});
	if (sos_obj_add(sos, obj))
		goto err1;
	pthread_mutex_unlock(&_this->sos_mutex);
	return 0;
err1:
	sos_obj_delete(sos, obj);
	pthread_mutex_unlock(&_this->sos_mutex);
err0:
	return EINVAL;
}

struct bplugin *create_plugin_instance()
{
	struct bout_sos_msg_plugin *_p = calloc(1, sizeof(*_p));
	_p->base.sos_class = &sos_msg_class;
	struct boutplugin *p = (typeof(p)) _p;
	bout_sos_init((void*)_p, "bout_sos_msg");
	p->process_output = bout_sos_msg_process_output;
	return (void*)p;
}
