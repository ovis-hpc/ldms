/*
 * rctrl.c
 *
 *  Created on: May 8, 2015
 *      Author: nichamon
 */

#include <unistd.h>
#include <pthread.h>
#include <malloc.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/queue.h>
#include <errno.h>
#include <assert.h>

#include "ocm/ocm.h"
#include "rctrl.h"

#ifdef ENABLE_AUTH
#include "ovis_auth/auth.h"
#endif /* ENABLE_AUTH */

pthread_mutex_t ctrl_list_lock;
struct rctrl_list ctrl_list;

static void rctrl_active_zap_cb(zap_ep_t zep, zap_event_t ev);
static void rctrl_passive_zap_cb(zap_ep_t zep, zap_event_t ev);
static rctrl_t __rctrl_new(const char *xprt,
			enum rctrl_mode mode,
			rctrl_cb_fn cb,
			zap_cb_fn_t zap_cb,
			const char *word,
			zap_log_fn_t log_fn)
{
	struct rctrl *ctrl = calloc(1, sizeof(*ctrl));
	if (!ctrl)
		return NULL;

	if (word) {
		ctrl->secretword = strdup(word);
		if (!ctrl->secretword) {
			goto err0;
		}
		ctrl->auth_state = RCTRL_AUTH_INIT;
	}

	ctrl->log = log_fn;
	ctrl->zap = zap_get(xprt, log_fn, NULL);
	if (!ctrl->zap) {
		log_fn("rctrl: Failed to get zap plugin: %s\n", xprt);
		goto err0;
	}

	ctrl->zep = zap_new(ctrl->zap, zap_cb);
	if (!ctrl->zep) {
		log_fn("rctrl: Failed to create zap endpoint\n");
		goto err1;
	}
	ctrl->cb = cb;
	ctrl->mode = mode;
	zap_set_ucontext(ctrl->zep, ctrl);
	ctrl->ref_count = 1;
	pthread_mutex_lock(&ctrl_list_lock);
	LIST_INSERT_HEAD(&ctrl_list, ctrl, entry);
	pthread_mutex_unlock(&ctrl_list_lock);

	return ctrl;
err1:
	free(ctrl->zap);
err0:
	if (ctrl->secretword)
		free((void *)ctrl->secretword);
	errno = ENOMEM;
	free(ctrl);
	return NULL;
}

static void __rctrl_ref_put(rctrl_t ctrl)
{
	assert(ctrl->ref_count);
	ctrl->ref_count--;
	if (0 == ctrl->ref_count) {
		pthread_mutex_lock(&ctrl_list_lock);
		LIST_REMOVE(ctrl, entry);
		pthread_mutex_unlock(&ctrl_list_lock);
		assert(0 == ctrl->zep);
		free(ctrl);
	}
}

static void __rctrl_ref_get(rctrl_t ctrl)
{
	ctrl->ref_count++;
}

static void __rctrl_delete(rctrl_t ctrl)
{
	zap_free(ctrl->zep);
	ctrl->zep = NULL;
	__rctrl_ref_put(ctrl);
}

static void handle_zap_conn_req(zap_ep_t zep)
{
	struct sockaddr lcl, rmt;
	socklen_t xlen;
	char rmt_name[16];
	zap_err_t zerr;
	zap_get_name(zep, &lcl, &rmt, &xlen);
	getnameinfo(&rmt, sizeof(rmt), rmt_name, 128, NULL, 0, NI_NUMERICHOST);

	rctrl_t ctrl = zap_get_ucontext(zep);
	rctrl_t new_ctrl = calloc(1, sizeof(*new_ctrl));
	if (!new_ctrl) {
		ctrl->log("rctrl: Failed to create new ctrl for connection "
				"from %s\n", rmt_name);
		goto err;
	}
	new_ctrl->ref_count = 1;
	new_ctrl->zap = ctrl->zap;
	new_ctrl->zep = zep;
	new_ctrl->log = ctrl->log;
	new_ctrl->cb = ctrl->cb;

	char *data = NULL;
	size_t datalen = 0;
#ifdef ENABLE_AUTH
	if (ctrl->secretword) {
		uint64_t ch = ovis_auth_gen_challenge();
		new_ctrl->secretword = ovis_auth_encrypt_password(ch,
						ctrl->secretword);
		if (!new_ctrl->secretword) {
			ctrl->log("rctrl: Failed to encrypt the password.\n");
			goto reject;
		} else {
			struct ovis_auth_challenge och;
			data = (void *)ovis_auth_pack_challenge(ch, &och);
			datalen = sizeof(och);
		}
	}
#endif /* ENABLE_AUTH */
	/* Take a connect reference. */
	__rctrl_ref_get(new_ctrl);
	zerr = zap_accept(zep, rctrl_passive_zap_cb, data, datalen);
	if (zerr) {
		__rctrl_ref_put(new_ctrl);
		ctrl->log("rctrl: conn_req: Failed to accept the connect "
				"request from %s.\n", rmt_name);
		goto err;
	}
	zap_set_ucontext(zep, new_ctrl);
	pthread_mutex_lock(&ctrl_list_lock);
	LIST_INSERT_HEAD(&ctrl_list, new_ctrl, entry);
	pthread_mutex_unlock(&ctrl_list_lock);
	return;

#ifdef ENABLE_AUTH
reject:
	zerr = zap_reject(zep);
	if (zerr) {
		ctrl->log("rctrl: Failed to reject the connection request"
				"from %s. %s\n", rmt_name, zap_err_str(zerr));
		goto err;
	}
	return;
#endif /* ENABLE_AUTH */
err:
	zap_close(zep);
}

#ifdef ENABLE_AUTH
int rctrl_send(rctrl_t ctrl, struct ocm_cfg_buff *data);
int __send_auth_password(zap_ep_t zep, rctrl_t ctrl, const char *password)
{
	int rc = 0;
	size_t len = strlen(password);
	char *buff = malloc(len);
	if (!buff)
		return ENOMEM;

	len += strlen(AUTH_PASSWORD_KEY) + strlen(AUTH_CMD);
	struct ocm_cfg_buff *cfg = ocm_cfg_buff_new(len, AUTH_PASSWORD_KEY);
	if (!cfg) {
		rc = ENOMEM;
		goto out;
	}

	struct ocm_value *v = (void *)buff;
	ocm_value_set_s(v, password);
	ocm_cfg_buff_add_verb(cfg, "");
	ocm_cfg_buff_add_av(cfg, AUTH_CMD, v);

	__rctrl_ref_get(ctrl);
	rc = rctrl_send(ctrl, cfg);
	if (rc)
		__rctrl_ref_put(ctrl);
	ocm_cfg_buff_free(cfg);
out:
	free(buff);
	return rc;
}
#endif /* ENABLE_AUTH */

static void handle_zap_connected(zap_ep_t zep, rctrl_t ctrl, zap_event_t zev)
{
	if (ctrl->mode == RCTRL_LISTENER)
		return;
#ifdef ENABLE_AUTH
	if (zev->data_len) {
		/* The server wants to authenticate. */
		if (!ctrl->secretword) {
			ctrl->auth_state = RCTRL_AUTH_FAILED;
			ctrl->log("rctrl: The server requires authentication.\n");
			ctrl->log("rctrl: No shared secret word was given\n");
			zap_close(zep);
			return;
		}

		struct ovis_auth_challenge *chl;
		chl = (struct ovis_auth_challenge *)zev->data;
		uint64_t ch = ovis_auth_unpack_challenge(chl);
		char *passwd = ovis_auth_encrypt_password(ch, ctrl->secretword);
		if (!passwd) {
			ctrl->auth_state = RCTRL_AUTH_FAILED;
			ctrl->log("rctrl: Auth error: Failed to construct"
					" the password\n");
			zap_close(zep);
			return;
		}
		int rc = __send_auth_password(zep, ctrl, passwd);
		if (rc) {
			ctrl->log("rctrl: Auth error: Failed to "
					"send password.\n");
			zap_close(zep);
			return;
		}
		ctrl->auth_state = RCTRL_AUTH_SENT_PASSWORD;
		free(passwd);
		return;
	}
#endif /* ENABLE_AUTH */
	ctrl->cb(RCTRL_EV_CONNECTED, ctrl);
	return;
}

static void rctrl_active_zap_cb(zap_ep_t zep, zap_event_t ev)
{
	zap_err_t zerr;
	enum rctrl_event rev;
	const char *key;
	rctrl_t ctrl = zap_get_ucontext(zep);
	ctrl->cfg = NULL;
	switch(ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		assert(0 == "Illegal zap event CONNECT_REQUEST");
		break;
	case ZAP_EVENT_CONNECT_ERROR:
		__rctrl_ref_put(ctrl);
		ctrl->cb(RCTRL_EV_CONN_ERROR, ctrl);
		break;
	case ZAP_EVENT_REJECTED:
		__rctrl_ref_put(ctrl);
		ctrl->cb(RCTRL_EV_REJECTED, ctrl);
		break;
	case ZAP_EVENT_CONNECTED:
		handle_zap_connected(zep, ctrl, ev);
		break;
	case ZAP_EVENT_DISCONNECTED:
		rev = RCTRL_EV_DISCONNECTED;
#ifdef ENABLE_AUTH
		if (ctrl->auth_state == RCTRL_AUTH_FAILED ||
				ctrl->auth_state == RCTRL_AUTH_SENT_PASSWORD) {
			rev = RCTRL_EV_REJECTED;
			if (ctrl->auth_state == RCTRL_AUTH_SENT_PASSWORD) {
				/* taken when sent the password */
				__rctrl_ref_put(ctrl);
			}
		}
#endif /* ENABLE_AUTH */
		/* taken when connect */
		__rctrl_ref_put(ctrl);
		ctrl->cb(rev, ctrl);
		break;
	case ZAP_EVENT_RECV_COMPLETE:
		ctrl->cfg = (ocm_cfg_t)ev->data;
		rev = RCTRL_EV_RECV_COMPLETE;
#ifdef ENABLE_AUTH
		key = ocm_cfg_key(ctrl->cfg);
		if (key && 0 == strcmp(key, AUTH_APPROVAL_KEY))
			rev = RCTRL_EV_CONNECTED;
#endif /* ENABLE_AUTH */
		ctrl->cb(rev, ctrl);
		break;
	case ZAP_EVENT_READ_COMPLETE:
		/* ctrl doesn't do read */
		assert(0 == "Illegal zap read");
		break;
	case ZAP_EVENT_WRITE_COMPLETE:
		/* ctrl don't do write. */
		assert(0 == "Illegal zap write");
		break;
	case ZAP_EVENT_RENDEZVOUS:
		/* ctrl doesn't do share */
		assert(0 == "Illegal zap share");
		break;
	}
}

#ifdef ENABLE_AUTH
static int __send_auth_approval(zap_ep_t zep, rctrl_t ctrl)
{
	int rc = 0;
	struct ocm_cfg_buff *cfg = ocm_cfg_buff_new(strlen(AUTH_APPROVAL_KEY),
							AUTH_APPROVAL_KEY);
	if (!cfg) {
		return ENOMEM;
	}
	rc = rctrl_send(ctrl, cfg);
	ocm_cfg_buff_free(cfg);
	return rc;
}

static void handle_auth_challenge_reply(zap_ep_t zep, rctrl_t ctrl)
{
	ocm_cfg_t cfg = ctrl->cfg;
	struct ocm_cfg_cmd_iter cmd_iter;
	ocm_cfg_cmd_iter_init(&cmd_iter, cfg);
	ocm_cfg_cmd_t cmd;
	ocm_cfg_cmd_iter_next(&cmd_iter, &cmd);
	const struct ocm_value *v;
	v = ocm_av_get_value(cmd, AUTH_CMD);
	if (0 != strcmp(v->s.str, ctrl->secretword))
		goto reject_or_error;
	if (__send_auth_approval(zep, ctrl))
		goto reject_or_error;
	ctrl->auth_state = RCTRL_AUTH_APPROVED;
	return;
reject_or_error:
	ctrl->auth_state = RCTRL_AUTH_FAILED;
	zap_close(zep);
}
#endif /* ENABLE_AUTH */

static void rctrl_passive_zap_cb(zap_ep_t zep, zap_event_t ev)
{
	zap_err_t zerr;
	rctrl_t ctrl = zap_get_ucontext(zep);
	const char *key;
	ctrl->cfg = NULL;
	switch(ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		handle_zap_conn_req(zep);
		break;
	case ZAP_EVENT_CONNECT_ERROR:
	case ZAP_EVENT_REJECTED:
		assert(0 == "Illegal zap event CONN_ERROR/REJECTED");
		break;
	case ZAP_EVENT_CONNECTED:
		/* do nothing */
		break;
	case ZAP_EVENT_DISCONNECTED:
		zap_free(ctrl->zep);
		ctrl->zep = NULL;
		/* Taken in handle_zap_conn_req */
		__rctrl_ref_put(ctrl);
		rctrl_free(ctrl);
		break;
	case ZAP_EVENT_RECV_COMPLETE:
		ctrl->cfg = (ocm_cfg_t)ev->data;
#ifdef ENABLE_AUTH
		key = ocm_cfg_key(ctrl->cfg);
		if (key && 0 == strcmp(key, AUTH_PASSWORD_KEY)) {
			handle_auth_challenge_reply(zep, ctrl);
			break;
		}
#endif /* ENABLE_AUTH */
		ctrl->cb(RCTRL_EV_RECV_COMPLETE, ctrl);
		break;
	case ZAP_EVENT_READ_COMPLETE:
		/* ctrl doesn't do read */
		assert(0 == "Illegal zap read");
		break;
	case ZAP_EVENT_WRITE_COMPLETE:
		/* ctrl don't do write. */
		assert(0 == "Illegal zap write");
		break;
	case ZAP_EVENT_RENDEZVOUS:
		/* ctrl doesn't do share */
		assert(0 == "Illegal zap share");
		break;
	}
}

rctrl_t rctrl_listener_setup(const char *xprt, const char *port,
		rctrl_cb_fn recv_cb,
		const char *secretword,
		zap_log_fn_t log_fn)
{
	int rc;
	struct rctrl *ctrl;
	assert(recv_cb);

	ctrl = __rctrl_new(xprt, RCTRL_LISTENER, recv_cb, rctrl_passive_zap_cb,
						secretword, log_fn);
	if (!ctrl)
		return NULL;

	short port_no = atoi(port);
	memset(&ctrl->lcl_sin, 0, sizeof(ctrl->lcl_sin));
	ctrl->lcl_sin.sin_family = AF_INET;
	ctrl->lcl_sin.sin_addr.s_addr = 0;
	ctrl->lcl_sin.sin_port = htons(port_no);
	rc = zap_listen(ctrl->zep, (struct sockaddr *)&ctrl->lcl_sin,
			sizeof(ctrl->lcl_sin));
	if (rc) {
		log_fn("rctrl: Failed to listen on port '%s'\n", port);
		errno = rc;
		goto err;
	}

	return ctrl;
err:
	__rctrl_delete(ctrl);
	return NULL;
}

rctrl_t rctrl_setup_controller(const char *xprt, rctrl_cb_fn cb,
				const char *secretword,
				zap_log_fn_t log_fn)
{
	assert(cb);
	rctrl_t ctrl = __rctrl_new(xprt, RCTRL_CONTROLLER, cb, rctrl_active_zap_cb,
					secretword, log_fn);
	if (!ctrl)
		return NULL;

	return ctrl;
}

void rctrl_free(rctrl_t ctrl)
{
	__rctrl_ref_put(ctrl);
}

void rctrl_disconnect(rctrl_t ctrl)
{
	assert(ctrl->ref_count > 1);
	zap_close(ctrl->zep);
}

int rctrl_connect(const char *host, const char *port, rctrl_t ctrl)
{
	int rc = 0;

	struct addrinfo hints, *ldmsdinfo;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	rc = getaddrinfo(host, port, &hints, &ldmsdinfo);
	if (rc) {
		ctrl->log("rctrl: %s: failed to get the address of %s at %s.\n",
				gai_strerror(rc), host, port);
		return rc;
	}

	/* put back when receive DISCONNECTED, REJECTED, CONN_ERROR. */
	__rctrl_ref_get(ctrl);
	zap_err_t zerr = zap_connect(ctrl->zep, ldmsdinfo->ai_addr,
					ldmsdinfo->ai_addrlen, NULL, 0);
	if (zerr) {
		__rctrl_ref_put(ctrl);
		ctrl->log("rctrl: %s: failed to connect to %s at %s\n",
				zap_err_str(zerr), host, port);
	}
	return rc;
}

int rctrl_send(rctrl_t ctrl, struct ocm_cfg_buff *data)
{
	/* Put when disconnect or receive a disconnected/error event */
	zap_err_t zerr = zap_send(ctrl->zep, data->buff, data->buff_len);
	return zerr;
}

static void __attribute__ ((constructor)) rctrl_init(void)
{
	pthread_mutex_init(&ctrl_list_lock, 0);
}

static void __attribute__ ((destructor)) rctrl_term(void)
{
}
