/*
 * rctrl.h
 *
 *  Created on: May 8, 2015
 *      Author: nichamon
 */

#ifndef RCTRL_H_
#define RCTRL_H_

#include <netinet/in.h>
#include <sys/un.h>
#include <sys/queue.h>
#include <ovis_util/util.h>
#include <zap/zap.h>

#include "ocm/ocm.h"

enum rctrl_event {
	RCTRL_EV_CONNECTED = 0,
	RCTRL_EV_DISCONNECTED,
	RCTRL_EV_RECV_COMPLETE,
	RCTRL_EV_ERROR,
};
typedef struct rctrl *rctrl_t;

typedef void (*rctrl_cb_fn)(enum rctrl_event ev, rctrl_t ctrl);

enum rctrl_mode {
	RCTRL_LISTENER = 0,
	RCTRL_CONTROLLER
};
struct rctrl {
	zap_t zap;
	zap_ep_t zep;
	int ref_count;
	enum rctrl_mode mode;
	struct sockaddr_in lcl_sin;
	struct sockaddr_in rem_sin;
	rctrl_cb_fn cb;
	ocm_cfg_t cfg;
	zap_log_fn_t log;
	LIST_ENTRY(rctrl) entry;
};
LIST_HEAD(rctrl_list, rctrl);

/**
 * \brief  Setup listener socket
 */
rctrl_t rctrl_listener_setup(const char *xprt, const char *port,
		rctrl_cb_fn recv_cb,
		zap_log_fn_t log_fn);

/**
 * \brief Setup control socket
 */
rctrl_t rctrl_setup_controller(const char *xprt, rctrl_cb_fn cb,
				zap_log_fn_t log_fn);

int rctrl_connect(const char *host, const char *port, rctrl_t ctrl);

int rctrl_send_request();

#endif /* RCTRL_H_ */
