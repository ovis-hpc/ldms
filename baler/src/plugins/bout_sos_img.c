#include "bout_sos_img.h"
#include "sos_img_class_def.h"

int bout_sos_img_config(struct bplugin *this, struct bpair_str_head *cfg_head)
{
	int rc = bout_sos_config(this, cfg_head);
	if (rc)
		return rc;
	struct bout_sos_img_plugin *_this = (void*)this;
	_this->delta_node = 1;
	_this->delta_ts = 3600;
	struct bpair_str *bps;
	if ((bps = bpair_str_search(cfg_head, "delta_ts", NULL))) {
		_this->delta_ts = strtoul(bps->s1, NULL, 0);
	}
	if ((bps = bpair_str_search(cfg_head, "delta_node", NULL))) {
		_this->delta_node = strtoul(bps->s1, NULL, 0);
	}
	return 0;
}

int bout_sos_img_start(struct bplugin *this)
{
	struct bout_sos_img_plugin *_this = (void*)this;
	int rc;
	if ((rc = bout_sos_start(this)))
		return rc;
	_this->sos_iter = sos_iter_new(_this->base.sos, 0);
	if (!_this->sos_iter) {
		rc = errno;
		bout_sos_stop(this);
		return rc;
	}
	return 0;
}

int bout_sos_img_process_output(struct boutplugin *this,
		struct boutq_data *odata)
{
	int rc = 0;
	struct bout_sos_plugin *_base = (void*)this;
	struct bout_sos_img_plugin *_this = (typeof(_this))this;
	uint32_t *tmp;
	pthread_mutex_lock(&_base->sos_mutex);
	sos_iter_t iter = _this->sos_iter;
	sos_t sos = _base->sos;
	if (!sos || !iter) {
		rc = EBADF;
		goto err0;
	}

	sos_attr_t attr = sos_obj_attr_by_id(sos, 0);
	if (!attr) {
		rc = EBADF;
		goto err0;
	}

	struct sos_key_s sk;
	struct bout_sos_img_key bk = {
		.ts = (odata->tv.tv_sec / _this->delta_ts) * _this->delta_ts,
		.comp_id = odata->comp_id
	};
	sos_attr_key_set(attr, &bk, &sk);
	sos_obj_t obj;
	uint32_t count = 1;
	if (!sos_iter_seek(iter, &sk))
		goto not_found;
	/* found key, look for correct pattern_id */

	/* TODO Expand SOS Key size and change img key to <ts,comp,ptnid> */

	/* Current code is inefficient, but we have to live with it for now
	 * until maximum sos key length is changed. */
	while ((obj = sos_iter_next(iter))) {
		if (sos_obj_attr_key_cmp(sos, 0, obj, &sk))
			break;
		uint32_t *ptn = sos_obj_attr_get(sos, 1, obj);
		if (odata->msg->ptn_id == *ptn)
			goto found;
	}

	/* reaching here means not found */

not_found:
	/* not found, add new data */
	obj = sos_obj_new(sos);
	sos_obj_attr_set(sos, 0, obj, &bk);
	sos_obj_attr_set(sos, 1, obj, &odata->msg->ptn_id);
	sos_obj_attr_set(sos, 2, obj, &count);
	rc = sos_obj_add(sos, obj);
	if (rc)
		goto err1;
	goto out;

found:
	/* found, increment the couter */
	tmp = sos_obj_attr_get(sos, 2, obj);
	(*tmp)++;
	goto out;

err1:
	sos_obj_delete(sos, obj);
err0:
out:
	pthread_mutex_unlock(&_base->sos_mutex);
	return rc;
}

int bout_sos_img_stop(struct bplugin *this)
{
	struct bout_sos_img_plugin *_this = (typeof(_this))this;
	pthread_mutex_lock(&_this->base.sos_mutex);
	sos_iter_free(_this->sos_iter);
	_this->sos_iter = 0;
	pthread_mutex_unlock(&_this->base.sos_mutex);
	return bout_sos_stop(this);
}

struct bplugin *create_plugin_instance()
{
	struct bout_sos_img_plugin *_p = calloc(1, sizeof(*_p));
	_p->base.sos_class = &sos_img_class;
	struct boutplugin *p = (typeof(p)) _p;
	bout_sos_init((void*)_p, "bout_sos_img");
	p->process_output = bout_sos_img_process_output;
	/* override some functions */
	p->base.config = bout_sos_img_config;
	p->base.start = bout_sos_img_start;
	p->base.stop = bout_sos_img_stop;
	return (void*)p;
}
