/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2020 Open Grid Computing, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of Sandia nor the names of any contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <json/json_util.h>

#include "ldmsd.h"
#include "ldmsd_request.h"


void ldmsd_listen___del(ldmsd_cfgobj_t obj)
{
	ldmsd_listen_t listen = (ldmsd_listen_t)obj;
	if (listen->x)
		ldms_xprt_put(listen->x);
	if (listen->xprt)
		free(listen->xprt);
	if (listen->host)
		free(listen->host);
	if (listen->auth_name)
		free(listen->auth_name);
	ldmsd_cfgobj___del(obj);
}

static void __listen_connect_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
	case LDMS_XPRT_EVENT_REJECTED:
	case LDMS_XPRT_EVENT_ERROR:
		/* TODO: cleanup all resources referenced to this endpoint */
		ldms_xprt_put(x);
		break;
	case LDMS_XPRT_EVENT_RECV:
		ldmsd_recv_msg(x, e->data, e->data_len);
		break;
	default:
		assert(0);
		break;
	}
}

static int ldmsd_listen_enable(ldmsd_cfgobj_t obj)
{
	int rc;
	ldmsd_listen_t listen = (ldmsd_listen_t)obj;
	ldmsd_auth_t auth_dom;
	struct addrinfo ai_hints = { .ai_family = AF_INET, .ai_flags = AI_PASSIVE };
	struct addrinfo *ai;
	char *ip;  /* for info print */
	char port[8];

	auth_dom = ldmsd_auth_find(listen->auth_name);
	if (!auth_dom) {
		ldmsd_log(LDMSD_LERROR, "Failed creating listening endpoint %s:%d "
				"because the auth '%s' not found.", listen->xprt,
						listen->port_no, listen->auth_name);
		return EINTR;
	}

	listen->x = ldms_xprt_new_with_auth(listen->xprt, ldmsd_linfo,
					auth_dom->plugin, auth_dom->attrs);
	if (!listen->x) {
		rc = errno;
		ldmsd_log(LDMSD_LERROR,
			  "'%s' transport creation with auth '%s' "
			  "failed, error: %s(%d). Please check transpot "
			  "configuration, authentication configuration, "
			  "ZAP_LIBPATH (env var), and LD_LIBRARY_PATH.\n",
			  listen->xprt,
			  listen->auth_name,
			  ovis_errno_abbvr(errno),
			  errno);
		return rc;
	}
	ldmsd_cfgobj_put(&auth_dom->obj); /* Put back the 'find' reference */

	/* Open the listening port */
	snprintf(port, sizeof(port), "%u", listen->port_no);
	rc = getaddrinfo(listen->host, port, &ai_hints, &ai);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "Failed to get the network address '%s:%d'. "
				"%s\n", listen->host, listen->port_no, strerror(rc));
		return rc;
	}
	ip = (char *)&((struct sockaddr_in *)ai->ai_addr)->sin_addr;

	rc = ldms_xprt_listen(listen->x, ai->ai_addr, ai->ai_addrlen,
			       	       __listen_connect_cb, NULL);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "Error %d listening on the '%s' "
				"transport addr: %hhu.%hhu.%hhu.%hhu:%hu.\n",
				rc, listen->xprt, ip[0], ip[1], ip[2], ip[3],
				listen->port_no);
		freeaddrinfo(ai);
		return rc;
	}
	ldmsd_log(LDMSD_LINFO,  "Listening on transport %s, "
				"addr: %hhu.%hhu.%hhu.%hhu:%hu\n",
				listen->xprt, ip[0], ip[1], ip[2], ip[3],
				listen->port_no);
	freeaddrinfo(ai);
	return 0;
}

static json_entity_t __listen_get_attr(const char *name, json_entity_t dft,
					json_entity_t spc, char **_xprt, short *_port,
							char **_host, char **_auth)
{
	ldmsd_auth_t auth_domain;
	ldmsd_req_buf_t buf;
	json_entity_t xprt, port, host, auth;
	json_entity_t value = NULL;
	xprt = port = host = auth = NULL;
	*_xprt = *_host = *_auth = NULL;
	*_port = 0;
	int rc = 0;

	buf = ldmsd_req_buf_alloc(512);
	if (!buf)
		goto oom;

	if (spc) {
		xprt = json_value_find(spc, "xprt");
		port = json_value_find(spc, "port");
		host = json_value_find(spc, "host");
		auth = json_value_find(spc, "auth");
	}

	if (dft) {
		if (!xprt)
			xprt = json_value_find(dft, "xprt");
		if (!port)
			port = json_value_find(dft, "port");
		if (!host)
			host = json_value_find(dft, "host");
		if (!auth)
			auth = json_value_find(dft, "auth");
	}
	if (xprt) {
		if (JSON_STRING_VALUE != json_entity_type(xprt)) {
			value = json_dict_build(value, JSON_STRING_VALUE, "xprt",
						"'xprt' is not a JSON string.", -1);
			if (!value)
				goto oom;
		} else {
			*_xprt = json_value_str(xprt)->str;
		}
	}
	if (host) {
		if (JSON_STRING_VALUE != json_entity_type(host)) {
			value = json_dict_build(value, JSON_STRING_VALUE, "host",
						"'host' is not a JSON string.", -1);
			if (!value)
				goto oom;
		} else {
			*_host = json_value_str(host)->str;
		}
	}
	if (auth) {
		if (JSON_STRING_VALUE != json_entity_type(auth)) {
			value = json_dict_build(value, JSON_STRING_VALUE, "auth",
						"'auth' is not a JSON string.", -1);
			if (!value)
				goto oom;
		} else {
			*_auth = json_value_str(auth)->str;
			auth_domain = ldmsd_auth_find(*_auth);
			if (!auth_domain) {
				rc = ldmsd_req_buf_append(buf,
						"Authentication '%s' not found.", *_auth);
				if (rc < 0)
					goto oom;
				value = json_dict_build(value, JSON_STRING_VALUE,
								"auth", buf->buf, -1);
				ldmsd_req_buf_reset(buf);
				if (!value)
					goto oom;
			} else {
				ldmsd_cfgobj_put(&auth_domain->obj);
			}
		}
	}
	if (port) {
		if ((JSON_STRING_VALUE != json_entity_type(port)) &&
				(JSON_INT_VALUE != json_entity_type(port))) {
			value = json_dict_build(value, JSON_STRING_VALUE, "port",
					"'port' is not a JSON integer or a JSON string.", -1);
			if (!value)
				goto oom;
		} else {
			if (JSON_STRING_VALUE == json_entity_type(port)) {
				*_port = atoi(json_value_str(port)->str);
			} else {
				*_port = json_value_int(port);
			}
			if (*_port < 1 || *_port > USHRT_MAX) {
				rc = ldmsd_req_buf_append(buf,
						"port '%us' is invalid", *_port);
				if (rc < 0)
					goto oom;
				value = json_dict_build(value, JSON_STRING_VALUE, buf->buf, -1);
				if (!value)
					goto oom;
				*_port = 0;
			}
		}
	}
	ldmsd_req_buf_free(buf);
	return value;
oom:
	if (value)
		json_entity_free(value);
	ldmsd_req_buf_free(buf);
	errno = ENOMEM;
	return NULL;
}

json_entity_t ldmsd_listen_query(ldmsd_cfgobj_t obj)
{
	json_entity_t query;
	ldmsd_listen_t listen = (ldmsd_listen_t)obj;
	query = ldmsd_cfgobj_query_result_new(obj);
	if (!query)
		goto oom;

	query = json_dict_build(query,
				JSON_STRING_VALUE, "xprt", listen->xprt,
				JSON_INT_VALUE, "port", listen->port_no,
				JSON_STRING_VALUE, "auth", listen->auth_name,
				-1);
	if (!query)
		goto oom;

	if (listen->host) {
		query = json_dict_build(query, JSON_STRING_VALUE, "host", listen->host, -1);
		if (!query)
			goto oom;
	}

	return ldmsd_result_new(0, NULL, query);
oom:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	return NULL;
}

json_entity_t ldmsd_listen_update(ldmsd_cfgobj_t obj, short enabled,
					json_entity_t dft, json_entity_t spc)
{
	json_entity_t err;
	char *xprt, *host, *auth;
	short port;
	ldmsd_listen_t listen = (ldmsd_listen_t)obj;

	if (obj->enabled)
		return ldmsd_result_new(EBUSY, NULL, NULL);

	err = __listen_get_attr(obj->name, dft, spc, &xprt, &port, &host, &auth);
	if (!err) {
		if (ENOMEM == errno)
			goto oom;
	} else {
		return ldmsd_result_new(EINVAL, NULL, err);
	}

	if (xprt) {
		free(listen->xprt);
		listen->xprt = strdup(xprt);
		if (!listen->xprt)
			goto oom;
	}
	if (port)
		listen->port_no = port;
	if (host) {
		if (listen->host)
			free(listen->host);
		listen->host = strdup(host);
		if (!listen->host)
			goto oom;
	}
	if (auth) {
		if (listen->auth_name)
			free(listen->auth_name);
		listen->auth_name = strdup(auth);
		if (!listen->auth_name)
			goto oom;
	}
	obj->enabled = (enabled < 0)?obj->enabled:enabled;
	return ldmsd_result_new(0, NULL, NULL);
oom:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	return NULL;
}

ldmsd_listen_t __listen_new(char *xprt, unsigned short port, char *host, char *auth, short enabled)
{
	char *name;
	size_t len;
	struct ldmsd_listen *listen = NULL;

	len = strlen(xprt) + 8; /* +6 for max port number and +2 for ':' and '\0' */
	name = malloc(len);
	if (!name)
		return NULL;
	(void) snprintf(name, len, "%s:%d", xprt, port);
	/*
	 * We don't close any opened listening ports.
	 */
	listen = (struct ldmsd_listen *)
		ldmsd_cfgobj_new(name, LDMSD_CFGOBJ_LISTEN, sizeof *listen,
				ldmsd_listen___del,
				ldmsd_listen_update,
				ldmsd_cfgobj_delete,
				ldmsd_listen_query,
				ldmsd_listen_query,
				ldmsd_listen_enable,
				NULL,
				getuid(), getgid(), 0550, enabled);
	if (!listen)
		return NULL;

	listen->xprt = strdup(xprt);
	if (!listen->xprt)
		goto err;
	listen->port_no = port;
	if (host) {
		listen->host = strdup(host);
		if (!listen->host)
			goto err;
	}
	if (auth) {
		listen->auth_name = strdup(auth);
	} else {
		listen->auth_name = strdup(ldmsd_global_auth_name_get());
	}
	if (!listen->auth_name)
		goto err;

	ldmsd_cfgobj_unlock(&listen->obj);
	return listen;
err:
	ldmsd_cfgobj_unlock(&listen->obj);
	ldmsd_cfgobj_put(&listen->obj);
	return NULL;
}

json_entity_t ldmsd_listen_create(const char *name, short enabled, json_entity_t dft,
					json_entity_t spc, uid_t uid, gid_t gid)
{
	ldmsd_listen_t listen;
	json_entity_t err;
	char *xprt, *host, *auth;
	short port;

	err = __listen_get_attr(name, dft, spc, &xprt, &port, &host, &auth);
	if (!err) {
		if (ENOMEM == errno)
			goto oom;
	} else {
		return ldmsd_result_new(EINVAL, NULL, err);
	}

	if (!xprt) {
		err = json_dict_build(err, JSON_STRING_VALUE, "xprt", "'xprt' is missing.", -1);
		if (!err)
			goto oom;
	}
	if (0 == port) {
		err = json_dict_build(err, JSON_STRING_VALUE, "port", "'port' is missing.", -1);
		if (!err)
			goto oom;
	}
	if (err)
		return ldmsd_result_new(EINVAL, NULL, err);

	listen = __listen_new(xprt, port, host, auth, enabled);
	if (!listen)
		goto oom;

	return ldmsd_result_new(0, NULL, NULL);
oom:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory.\n");
	return NULL;
}
