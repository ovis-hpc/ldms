#include <stdarg.h>
#include <stdio.h>
#include <event2/event.h>
#include <event2/thread.h>

#include "ocm.h"
#include "ocm_priv.h"

struct event_base *__ocm_evbase = NULL;
void __ocm_zap_cb(zap_ep_t zep, zap_event_t ev);
pthread_t __ocm_evthread;

void __ocm_init();

/**
 * ::ocm_value size calculation.
 */
static size_t ocm_value_size(struct ocm_value *v)
{
	size_t size;
	switch (v->type) {
	case OCM_VALUE_STR:
		size = ((void*)&v->s - (void*)v) + ocm_str_size(&v->s);
		if (size < sizeof(*v))
			size = sizeof(*v);
		break;
	case OCM_VALUE_CMD:
		size = sizeof(*v);
		size += v->cmd->len;
		break;
	default:
		size = sizeof(*v);
	}
	return size;
}

const char *ocm_cfg_cmd_verb(struct ocm_cfg_cmd *cmd)
{
	struct ocm_str *verb = (void*)cmd->data;
	return verb->str;
}

int ocm_av_iter_init(struct ocm_av_iter* iter, struct ocm_cfg_cmd *cmd)
{
	iter->cmd = cmd;
	ocm_av_iter_reset(iter);
	return 0;
}

void ocm_av_iter_reset(struct ocm_av_iter *iter)
{
	struct ocm_str *verb = (void*)iter->cmd->data;
	iter->next_av = iter->cmd->data + ocm_str_size(verb);
}

const struct ocm_value *ocm_av_get_value(ocm_cfg_cmd_t cmd, const char *attr)
{
	struct ocm_av_iter iter;
	const char *_attr;
	const struct ocm_value *_value;
	ocm_av_iter_init(&iter, cmd);
	while (ocm_av_iter_next(&iter, &_attr, &_value) == 0) {
		if (strcmp(attr, _attr) == 0)
			return _value;
	}
	return NULL;
}

int ocm_av_iter_next(struct ocm_av_iter *iter, const char **attr,
		const struct ocm_value **value)
{
	if ((void*)iter->cmd + iter->cmd->len <= iter->next_av)
		return -1;
	struct ocm_str *a = iter->next_av;
	struct ocm_value *v = iter->next_av + ocm_str_size(a);
	if (v->type == OCM_VALUE_CMD) {
		v->cmd = (void*)v + sizeof(*v);
	}
	*attr = a->str;
	*value = v;
	iter->next_av = (void*)v + ocm_value_size(v);
	return 0;
}

void ocm_cfg_cmd_iter_init(struct ocm_cfg_cmd_iter *iter, struct ocm_cfg *cfg)
{
	iter->cfg = cfg;
	ocm_cfg_cmd_iter_reset(iter);
}

void ocm_cfg_cmd_iter_reset(struct ocm_cfg_cmd_iter *iter)
{
	struct ocm_str *verb = (void*)iter->cfg->data;
	iter->next_cmd = iter->cfg->data + ocm_str_size(verb);
}

int ocm_cfg_cmd_iter_next(struct ocm_cfg_cmd_iter *iter,
					struct ocm_cfg_cmd **cmd)
{
	if ((void*)iter->cfg + iter->cfg->len <= iter->next_cmd)
		return -1;
	struct ocm_cfg_cmd *c = iter->next_cmd;
	iter->next_cmd += c->len;
	*cmd = c;
	return 0;
}

struct ocm_cfg_buff *ocm_cfg_buff_new(size_t init_buff_size, const char *key)
{
	struct ocm_cfg_buff *buff = calloc(1, sizeof(*buff));
	if (!buff)
		goto err0;

	buff->buff = malloc(init_buff_size);
	if (!buff->buff)
		goto err1;

	buff->buff_len = init_buff_size;
	if (ocm_cfg_buff_reset(buff, key))
		goto err2;

	return buff;

err2:
	free(buff->buff);
err1:
	free(buff);
err0:
	return NULL;
}

void ocm_cfg_buff_free(struct ocm_cfg_buff *buff)
{
	free(buff->buff);
	free(buff);
}

#define OCM_SZ(X) ((X | 4095) + 1)

size_t __ocm_cfg_buff_space(const struct ocm_cfg_buff *buff)
{
	return  buff->buff_len - (buff->current_ptr - (void*)buff->buff);
}

/**
 * Calculate the size for \c s if it was a ::ocm_str.
 */
size_t __ocm_str_size(const char *s)
{
	size_t len = strlen(s) + 1;
	return sizeof(struct ocm_str) + len;
}

/**
 * Set C string \c s to OCM string \c os.
 * This assumes that the caller has enough memory. Please use with care.
 */
void __ocm_str_set(struct ocm_str *os, const char *s)
{
	strcpy(os->str, s);
	os->len = strlen(os->str) + 1;
}

/**
 * Set C string \c s to OCM string \c os, with limited \c n characters.
 * This assumes that the caller has enough memory. Please use with care.
 */
void __ocm_strn_set(struct ocm_str *os, const char *s, int n)
{
	strncpy(os->str, s, n);
	os->len = strlen(os->str) + 1;
}

int __ocm_cfg_buff_add_str(struct ocm_cfg_buff *buff, const char *str)
{
	size_t sz = __ocm_str_size(str);
	if (__ocm_cfg_buff_space(buff) <= sz) {
		if (ocm_cfg_buff_resize(buff, OCM_SZ(buff->buff_len + sz)))
			return ENOMEM;
	}
	struct ocm_str *s = buff->current_ptr;
	__ocm_str_set(s, str);
	buff->current_ptr += sz;
	buff->buff->len += sz;
	return 0;
}

int __ocm_cfg_buff_add_data(struct ocm_cfg_buff *buff, void *data, size_t sz)
{
	if (__ocm_cfg_buff_space(buff) <= sz) {
		if (ocm_cfg_buff_resize(buff, OCM_SZ(buff->buff_len + sz)))
			return ENOMEM;
	}
	memcpy(buff->current_ptr, data, sz);
	buff->current_ptr += sz;
	buff->buff->len += sz;
	return 0;
}

int ocm_cfg_buff_resize(struct ocm_cfg_buff *buff, size_t new_size)
{
	size_t cmd_offset = (buff->current_cmd == 0)?(0):
				((void*)buff->current_cmd - (void*)buff->buff);
	size_t ptr_offset = buff->current_ptr - (void*)buff->buff;
	void *new_buff = realloc(buff->buff, new_size);
	if (!new_buff)
		return ENOMEM;
	buff->buff = new_buff;
	buff->buff_len = new_size;
	buff->current_cmd = (cmd_offset)?((void*)buff->buff + cmd_offset):(0);
	buff->current_ptr = (void*)buff->buff + ptr_offset;
	return 0;
}

int ocm_cfg_buff_reset(struct ocm_cfg_buff *buff, const char *new_key)
{
	buff->buff->len = sizeof(struct ocm_cfg);
	buff->current_ptr = buff->buff->data;
	buff->current_cmd = NULL;
	return __ocm_cfg_buff_add_str(buff, new_key);
}

int ocm_cfg_buff_add_verb(struct ocm_cfg_buff *buff, const char *verb)
{
	if (__ocm_cfg_buff_space(buff) <= sizeof(struct ocm_cfg_cmd)) {
		if (ocm_cfg_buff_resize(buff, OCM_SZ(buff->buff_len + 4096)))
			return ENOMEM;
	}
	void *prev_cmd = buff->current_cmd;
	void *prev_ptr = buff->current_ptr;
	buff->current_cmd = buff->current_ptr;
	buff->current_ptr = buff->current_cmd->data;
	int rc = __ocm_cfg_buff_add_str(buff, verb);
	if (rc)
		goto err;
	buff->current_cmd->len = buff->current_ptr - prev_ptr;
	/* also, update buff->len */
	buff->buff->len = buff->current_ptr - (void*)buff->buff;
	return 0;
err:
	buff->current_cmd = prev_cmd;
	buff->current_ptr = prev_ptr;
	return rc;
}

int ocm_cfg_buff_add_av(struct ocm_cfg_buff *buff, const char *attr,
						struct ocm_value *value)
{
	void *prev_ptr = buff->current_ptr;
	int rc = __ocm_cfg_buff_add_str(buff, attr);
	if (rc)
		goto err;
	size_t value_sz = ocm_value_size(value);
	rc = __ocm_cfg_buff_add_data(buff, value, value_sz);
	if (rc)
		goto err;
	buff->current_cmd->len += buff->current_ptr - prev_ptr;
	return 0;
err:
	buff->current_ptr = prev_ptr;
	return rc;
}

int ocm_cfg_buff_add_cmd_as_av(struct ocm_cfg_buff *buff, const char *attr,
		ocm_cfg_cmd_t cmd)
{
	void *prev_ptr = buff->current_ptr;
	int rc = __ocm_cfg_buff_add_str(buff, attr);
	if (rc)
		goto err;
	struct ocm_value v;
	v.type = OCM_VALUE_CMD;
	v.cmd = cmd;
	rc = __ocm_cfg_buff_add_data(buff, &v, sizeof(v));
	if (rc)
		goto err;
	rc = __ocm_cfg_buff_add_data(buff, cmd, cmd->len);
	buff->current_cmd->len += buff->current_ptr - prev_ptr;
	return 0;
err:
	buff->current_ptr = prev_ptr;
	return rc;
}

void __ocm_default_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

zap_mem_info_t __ocm_zap_meminfo(void)
{
	return NULL;
}

size_t ocm_cfg_req_size(struct ocm_cfg_req *req)
{
	return ocm_str_size(&req->key) + sizeof(req->hdr);
}

/**
 * Send requests for all registered cb functions.
 */
void __ocm_send_all_requests(ocm_t ocm, zap_ep_t ep)
{
	pthread_mutex_lock(&ocm->mutex);
	char buff[4096];
	struct ocm_cfg_req *req = (void*)buff;
	req->hdr.type = OCM_MSG_REQ;
	struct ocm_registered_cb *rcb;
	LIST_FOREACH(rcb, &ocm->cb_list, entry){
		if (rcb->is_called)
			continue;
		__ocm_str_set(&req->key, rcb->key);
		zap_send(ep, buff, ocm_cfg_req_size(req));
	}
	pthread_mutex_unlock(&ocm->mutex);
}

zap_mem_info_fn_t map_info_fn = __ocm_zap_meminfo;

void __ocm_reconnect(struct ocm_ep_ctxt *ctxt)
{
	struct timeval tv = {20, 0}; /* 20 sec */
	evtimer_add(ctxt->reconn_event, &tv);
	/* NOTE: The callback of ctxt->reconn_event is __ocm_reconnect_cb */
}

void __ocm_reconnect_cb(evutil_socket_t fd, short what, void *arg)
{
	struct ocm_ep_ctxt *ctxt = arg;
	zap_ep_t ep;
	zap_err_t zerr = zap_new(ctxt->ocm->zap, &ep, __ocm_zap_cb);
	if (zerr)
		goto err0;
	zap_set_ucontext(ep, ctxt);
	zerr = zap_connect(ep, &ctxt->sa, ctxt->sa_len);
	if (zerr)
		goto err1;

	return ;

err1:
	zap_close(ep);
err0:
	__ocm_reconnect(ctxt);
}

void __ocm_recv_complete(zap_ep_t ep, struct ocm_msg_hdr *hdr, struct ocm_ep_ctxt *ctxt)
{
	struct ocm *ocm = ctxt->ocm;
	struct ocm_event *oev;
	struct ocm_str *key;
	struct ocm_registered_cb *rcb;
	ocm_cb_fn_t cb = 0;
	uint64_t obj;
	/* NOTE: oev will be freed in ocm_event_resp_err() or
	 *       ocm_event_resp_cfg(). */
	oev = calloc(1, sizeof(*oev));
	if (!oev) {
		fprintf(stderr, "Cannot allocate ocm_event\n");
		return;
	}
	oev->ep = ep;
	oev->ocm = ocm;
	switch (hdr->type) {
	case OCM_MSG_REQ:
		oev->type = OCM_EVENT_CFG_REQUESTED;
		oev->req = (void*)hdr;
		cb = ocm->cb;
		break;
	case OCM_MSG_CFG:
		oev->type = OCM_EVENT_CFG_RECEIVED;
		oev->cfg = (void*)hdr;
		key = (void*)oev->cfg->data;
		obj = str_map_get(ocm->cb_map, key->str);
		if (obj) {
			rcb = (void*)obj;
			rcb->is_called = 1;
			cb = rcb->cb;
		}
		break;
	case OCM_MSG_ERR:
		oev->type = OCM_EVENT_ERROR;
		oev->err = (void*)hdr;
		key = (void*)oev->err->data;
		obj = str_map_get(ocm->cb_map, key->str);
		if (obj)
			cb = ((struct ocm_registered_cb*)obj)->cb;
		break;
	default:
		fprintf(stderr, "OCM: WARN: Unknown OCM message type: %d\n",
								hdr->type);
		return;
	}

	if (!cb) {
		fprintf(stderr, "OCM: WARN: No callback for key:%s\n",
								key->str);
		return;
	}

	cb(oev);
}

void __ocm_zap_cb(zap_ep_t zep, zap_event_t ev)
{
	struct ocm_ep_ctxt *ctxt = zap_get_ucontext(zep);
	switch (ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		zap_accept(zep, __ocm_zap_cb);
		break;
	case ZAP_EVENT_CONNECTED:
		__ocm_send_all_requests(ctxt->ocm, zep);
		break;
	case ZAP_EVENT_CONNECT_ERROR:
	case ZAP_EVENT_REJECTED:
	case ZAP_EVENT_DISCONNECTED:
		if (ctxt->is_active)
			__ocm_reconnect(ctxt);
		zap_close(zep);
		break;
	case ZAP_EVENT_RECV_COMPLETE:
		__ocm_recv_complete(zep, ev->data, ctxt);
		break;
	}
}

ocm_t ocm_create(const char *xprt, uint16_t port, ocm_cb_fn_t request_cb,
		 void (*log_fn)(const char *fmt, ...))
{
	if (!port)
		port = OCM_DEFAULT_PORT;
	if (!xprt)
		xprt = getenv("OCM_XPRT");
	if (!xprt)
		xprt = "sock";
	ocm_t ocm = calloc(1, sizeof(*ocm));
	if (!ocm)
		goto err0;
	ocm->cb_map = str_map_create(1021);
	if (!ocm->cb_map)
		goto err1;
	ocm->active_idx = idx_create();
	if (!ocm->active_idx)
		goto err2;
	ocm->cb = request_cb;
	ocm->port = port;
	if (!log_fn)
		log_fn = __ocm_default_log;
	ocm->log_fn = log_fn;
	zap_err_t zerr = zap_get(xprt, &ocm->zap, log_fn, map_info_fn);
	if (zerr) {
		log_fn("OCM ERROR: cannot get xprt: %s\n", xprt);
		goto err3;
	}

	return ocm;

err3:
	idx_destroy(ocm->active_idx);
err2:
	str_map_free(ocm->cb_map);
err1:
	free(ocm);
err0:
	return NULL;
}

int ocm_add_receiver(ocm_t ocm, struct sockaddr *sa, socklen_t sa_len)
{
	pthread_mutex_lock(&ocm->mutex);
	int rc = -1;
	zap_err_t zerr;
	zap_ep_t ep;
	zerr = zap_new(ocm->zap, &ep, __ocm_zap_cb);
	if (zerr)
		goto err0;
	struct ocm_ep_ctxt *ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto err1;
	ctxt->is_active = 1;
	ctxt->sa = *sa;
	ctxt->sa_len = sa_len;
	ctxt->ocm = ocm;
	ctxt->reconn_event = evtimer_new(__ocm_evbase, __ocm_reconnect_cb, ctxt);
	if (!ctxt->reconn_event)
		goto err2;
	rc = idx_add(ocm->active_idx, sa, sa_len, ctxt);
	if (rc)
		goto err3;
	zap_set_ucontext(ep, ctxt);
	zerr = zap_connect(ep, sa, sa_len);
	if (zerr)
		goto err3;

	rc = 0;
	goto out;

err3:
	event_free(ctxt->reconn_event);
err2:
	free(ctxt);
err1:
	zap_close(ep);
err0:
out:
	pthread_mutex_unlock(&ocm->mutex);
	return rc;
}

int ocm_remove_receiver(ocm_t ocm, struct sockaddr *sa, socklen_t sa_len)
{
	pthread_mutex_lock(&ocm->mutex);
	int rc = 0;
	struct ocm_ep_ctxt *ctxt = idx_delete(ocm->active_idx, sa, sa_len);
	if (!ctxt) {
		rc = ENOENT;
		goto out;
	}
	ctxt->is_active = 0;
out:
	pthread_mutex_unlock(&ocm->mutex);
	return rc;
}

int ocm_register(ocm_t ocm, const char *key, ocm_cb_fn_t cb)
{
	int rc;
	pthread_mutex_lock(&ocm->mutex);
	struct ocm_registered_cb *rcb = calloc(1, sizeof(*rcb));
	if (!rcb) {
		rc = ENOMEM;
		goto err0;
	}
	rcb->cb = cb;
	rcb->key = strdup(key);
	if (!rcb->key) {
		rc = ENOMEM;
		goto err1;
	}
	rc = str_map_insert(ocm->cb_map, key, (uint64_t)rcb);
	if (rc)
		goto err2;

	LIST_INSERT_HEAD(&ocm->cb_list, rcb, entry);

	goto out;

err2:
	free(rcb->key);
err1:
	free(rcb);
err0:
out:
	pthread_mutex_unlock(&ocm->mutex);
	return rc;
}

int ocm_enable(ocm_t ocm)
{
	__ocm_init();

	zap_err_t zerr = zap_new(ocm->zap, &ocm->ep, __ocm_zap_cb);
	int rc = 0;
	struct ocm_ep_ctxt *ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto err0;
	ctxt->ocm = ocm;
	ctxt->is_active = 0;
	if (zerr) {
		ocm->log_fn("OCM ERROR: cannot create endpoint, zap_err: %d\n",
				zerr);
		rc = zerr;
		goto err1;
	}
	zap_set_ucontext(ocm->ep, ctxt);
	struct sockaddr_in sin = {0};
	sin.sin_family = AF_INET;
	sin.sin_port = htons(ocm->port);
	zerr = zap_listen(ocm->ep, (void*)&sin, sizeof(sin));
	if (zerr) {
		ocm->log_fn("OCM ERROR: zap_listen failed, zap_err: %d\n",
				zerr);
		rc = zerr;
		goto err2;
	}

	goto out;
err2:
	zap_free(ocm->ep);
err1:
	free(ctxt);
err0:
out:
	return rc;
}

int ocm_deregister(ocm_t ocm, const char *key)
{
	int rc;
	pthread_mutex_lock(&ocm->mutex);
	rc = str_map_remove(ocm->cb_map, key);
	pthread_mutex_unlock(&ocm->mutex);
	return rc;
}

int ocm_event_resp_err(struct ocm_event *e, int code, const char *key,
				const char *emsg)
{
	zap_err_t zerr;
	char buff[1024];
	struct ocm_err *err = (void*)buff;
	err->hdr.type = OCM_MSG_ERR;
	err->code = code;
	struct ocm_str *okey = (void*) err->data;
	__ocm_str_set(okey, key);
	int key_sz = ocm_str_size(okey);
	struct ocm_str *omsg = (void*)err->data + key_sz;
	__ocm_strn_set(omsg, emsg, 1024 - ((void*)omsg - (void*)buff));
	err->len = sizeof(*err) + key_sz + ocm_str_size(omsg);
	zerr = zap_send(e->ep, err, err->len);
	free(e);
	if (zerr)
		return zerr;
	return 0;
}

int ocm_event_resp_cfg(struct ocm_event *e, struct ocm_cfg *cfg)
{
	int rc;
	cfg->hdr.type = OCM_MSG_CFG;
	rc = zap_send(e->ep, cfg, cfg->len);
	free(e);
	return rc;
}

const char *ocm_err_key(const ocm_err_t e)
{
	return ((struct ocm_str*)e->data)->str;
}

const char *ocm_err_msg(const ocm_err_t e)
{
	struct ocm_str *s = (void*)e->data;
	int sz = ocm_str_size(s);
	return ((struct ocm_str*)(e->data + sz))->str;
}

int ocm_err_code(const ocm_err_t e)
{
	return e->code;
}

const char *ocm_cfg_req_key(ocm_cfg_req_t req)
{
	return req->key.str;
}

const char *ocm_cfg_key(ocm_cfg_t cfg)
{
	struct ocm_str *key = (void*)cfg->data;
	return key->str;
}

void ocm_value_set_s(struct ocm_value *v, const char *s)
{
	v->type = OCM_VALUE_STR;
	__ocm_str_set(&v->s, s);
}

void ocm_value_set(struct ocm_value *v, ocm_value_type_t type, ...)
{
	va_list ap;
	va_start(ap, type);
	v->type = type;
	switch (type) {
	case OCM_VALUE_INT8:
		v->i8 = va_arg(ap, int);
		break;
	case OCM_VALUE_INT16:
		v->i16 = va_arg(ap, int);
		break;
	case OCM_VALUE_INT32:
		v->i32 = va_arg(ap, int32_t);
		break;
	case OCM_VALUE_INT64:
		v->i64 = va_arg(ap, int64_t);
		break;
	case OCM_VALUE_UINT8:
		v->u8 = va_arg(ap, int);
		break;
	case OCM_VALUE_UINT16:
		v->u16 = va_arg(ap, int);
		break;
	case OCM_VALUE_UINT32:
		v->u32 = va_arg(ap, uint32_t);
		break;
	case OCM_VALUE_UINT64:
		v->u64 = va_arg(ap, uint64_t);
		break;
	case OCM_VALUE_FLOAT:
		v->f = va_arg(ap, double);
		break;
	case OCM_VALUE_DOUBLE:
		v->d = va_arg(ap, double);
		break;
	case OCM_VALUE_STR:
		ocm_value_set_s(v, va_arg(ap, const char *));
		break;
	}
	va_end(ap);
}

int ocm_close(ocm_t ocm)
{
	zap_close(ocm->ep);
	str_map_free(ocm->cb_map);
}

void *__ocm_evthread_proc(void *arg)
{
	event_base_dispatch(__ocm_evbase);
	fprintf(stderr, "OCM: evthread exit.\n");
}

void __ocm_init()
{
	static visited = 0;
	if (visited)
		return;
	visited = 1;
	if (evthread_use_pthreads()) {
		fprintf(stderr, "OCM: evthread_use_pthreads() failed.\n");
		goto err;
	}
	__ocm_evbase = event_base_new();
	if (!__ocm_evbase) {
		fprintf(stderr, "OCM: event_base_new() failed.\n");
		goto err;
	}
	/* This event will keep event thread alive */
	struct event *ev = event_new(__ocm_evbase, -1, EV_READ|EV_PERSIST, NULL,
					NULL);
	if (!ev) {
		fprintf(stderr, "OCM: event_new() failed.\n");
		goto err;
	}
	event_add(ev, NULL);
	int rc = pthread_create(&__ocm_evthread, NULL, __ocm_evthread_proc,
									NULL);
	if (rc) {
		fprintf(stderr, "OCM: pthread_create() failed.\n");
		goto err;
	}
	return ;
err:
	exit(-1);
}

void __ocm_destroy()
{
	if (__ocm_evbase)
		event_base_free(__ocm_evbase);
	pthread_cancel(__ocm_evthread);
}


