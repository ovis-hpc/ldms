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

#include "ocm.h"
#include "rctrl.h"

pthread_mutex_t ctrl_list_lock;
struct rctrl_list ctrl_list;

static void rctrl_zap_cb(zap_ep_t zep, zap_event_t ev);
static rctrl_t __rctrl_new(const char *xprt,
			enum rctrl_mode mode,
			rctrl_cb_fn cb,
			zap_log_fn_t log_fn)
{
	struct rctrl *ctrl = calloc(1, sizeof(*ctrl));
	if (!ctrl)
		return NULL;

	ctrl->log = log_fn;
	ctrl->zap = zap_get(xprt, log_fn, NULL);
	if (!ctrl->zap) {
		log_fn("ctrl: Failed to get zap plugin: %s\n", xprt);
		goto err0;
	}

	ctrl->zep = zap_new(ctrl->zap, rctrl_zap_cb);
	if (!ctrl->zep) {
		log_fn("ctrl: Failed to create zap endpoint\n");
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
	errno = ENOMEM;
	free(ctrl);
	return NULL;
}

static void __rctrl_put(rctrl_t ctrl)
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

static void __rctrl_get(rctrl_t ctrl)
{
	ctrl->ref_count++;
}

static void __rctrl_delete(rctrl_t ctrl)
{
	zap_free(ctrl->zep);
	ctrl->zep = NULL;
	__rctrl_put(ctrl);
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
		ctrl->log("ctrl: Failed to create new ctrl for connection "
				"from %s\n", rmt_name);
		goto err;
	}
	new_ctrl->ref_count = 1;
	new_ctrl->zap = ctrl->zap;
	new_ctrl->zep = zep;
	new_ctrl->log = ctrl->log;
	new_ctrl->cb = ctrl->cb;

	zerr = zap_accept(zep, rctrl_zap_cb, NULL, 0);
	if (zerr) {
		ctrl->log("ctrl: conn_req: Failed to accept the connect "
				"request from %s.\n", rmt_name);
		goto err;
	}

	/* Take a connect reference. */
	__rctrl_get(new_ctrl);
	zap_set_ucontext(zep, new_ctrl);
	pthread_mutex_lock(&ctrl_list_lock);
	LIST_INSERT_HEAD(&ctrl_list, new_ctrl, entry);
	pthread_mutex_unlock(&ctrl_list_lock);
	return;
err:
	zap_close(zep);
}

static void handle_zap_disconnected(zap_ep_t zep)
{
	rctrl_t ctrl = zap_get_ucontext(zep);
	zap_free(ctrl->zep);
	ctrl->zep = NULL;
	/* Pair with get in handle_zap_conn_req */
	__rctrl_put(ctrl);
}

static void rctrl_zap_cb(zap_ep_t zep, zap_event_t ev)
{
	zap_err_t zerr;
	rctrl_t ctrl = zap_get_ucontext(zep);
	ctrl->cfg = NULL;
	switch(ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		handle_zap_conn_req(zep);
		break;
	case ZAP_EVENT_CONNECT_ERROR:
	case ZAP_EVENT_REJECTED:
		if (RCTRL_CONTROLLER == ctrl->mode)
			ctrl->cb(RCTRL_EV_ERROR, ctrl);
		break;
	case ZAP_EVENT_CONNECTED:
		if (RCTRL_CONTROLLER == ctrl->mode)
			ctrl->cb(RCTRL_EV_CONNECTED, ctrl);
		break;
	case ZAP_EVENT_DISCONNECTED:
		switch (ctrl->mode) {
		case RCTRL_CONTROLLER:
			ctrl->cb(RCTRL_EV_DISCONNECTED, ctrl);
			break;
		case RCTRL_LISTENER:
			handle_zap_disconnected(zep);
			break;
		default:
			assert(0);
			break;
		}
		break;
	case ZAP_EVENT_RECV_COMPLETE:
		ctrl->cfg = (ocm_cfg_t)ev->data;
		__rctrl_get(ctrl);
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
		zap_log_fn_t log_fn)
{
	int rc;
	struct rctrl *ctrl;

	ctrl = __rctrl_new(xprt, RCTRL_LISTENER, recv_cb, log_fn);
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
		log_fn("ctrl: Failed to listen on port '%s'\n", port);
		errno = rc;
		goto err;
	}

	return ctrl;
err:
	__rctrl_delete(ctrl);
	return NULL;
}

rctrl_t rctrl_setup_controller(const char *xprt, rctrl_cb_fn cb,
				zap_log_fn_t log_fn)
{
	rctrl_t ctrl = __rctrl_new(xprt, RCTRL_CONTROLLER, cb, log_fn);
	if (!ctrl)
		return NULL;

	return ctrl;
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

	zap_err_t zerr = zap_connect(ctrl->zep, ldmsdinfo->ai_addr,
					ldmsdinfo->ai_addrlen, NULL, 0);
	if (zerr) {
		ctrl->log("rctrl: %s: failed to connect to %s at %s\n",
				zap_err_str(zerr), host, port);
	}
	return rc;
}

int rctrl_send_request(rctrl_t ctrl, struct ocm_cfg_buff *cfg)
{
	/* Put when disconnect or receive a disconnected/error event */
	__rctrl_get(ctrl);
	zap_err_t zerr = zap_send(ctrl->zep, cfg->buff, cfg->buff_len);
	if (zerr)
		__rctrl_put(ctrl);
	return zerr;
}

static void __attribute__ ((constructor)) rctrl_init(void)
{
	pthread_mutex_init(&ctrl_list_lock, 0);
}

static void __attribute__ ((destructor)) rctrl_term(void)
{
}
