/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018,2023 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018,2023 Open Grid Computing, Inc. All rights reserved.
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

/**
 * \file ldms_auth_ovis.c
 * \brief An LDMS authentication plugin using `ovis_auth`.
 */

#include <pwd.h>
#include "ovis_log/ovis_log.h"
#include "ovis_util/util.h"
#include "ovis_auth/auth.h"

#include "../core/ldms_auth.h"

#define ENVCONFNAME "LDMS_AUTH_FILE"
#define SYSCONFNAME "ldmsauth.conf"
#define USRCONFNAME ".ldmsauth.conf"

static ovis_log_t aolog = NULL;

#define LOG(_level_, _fmt_, ...) do { \
	ovis_log(aolog, _level_, _fmt_, ##__VA_ARGS__); \
} while (0);

#define LOG_OOM() do { \
	ovis_log(aolog, OVIS_LCRITICAL, "Memory allocation failure.\n"); \
} while (0);

#define LOG_ERROR(_fmt_, ...) do { \
	ovis_log(aolog, OVIS_LERROR, _fmt_, ##__VA_ARGS__); \
} while (0);

static
ldms_auth_t __auth_ovis_new(ldms_auth_plugin_t plugin,
		       struct attr_value_list *av_list);
static
ldms_auth_t __auth_ovis_clone(ldms_auth_t auth);
static
void __auth_ovis_free(ldms_auth_t auth);
static
int __auth_ovis_xprt_bind(ldms_auth_t auth, ldms_t xprt);
static
int __auth_ovis_xprt_begin(ldms_auth_t auth, ldms_t xprt);
static
int __auth_ovis_xprt_recv_cb(ldms_auth_t auth, ldms_t xprt,
		const char *data, uint32_t data_len);
static
int __auth_ovis_cred_get(ldms_auth_t auth, ldms_cred_t cred);

struct ldms_auth_plugin plugin = {
	.name = "ovis",
	.auth_new = __auth_ovis_new,
	.auth_clone = __auth_ovis_clone,
	.auth_free = __auth_ovis_free,
	.auth_xprt_bind = __auth_ovis_xprt_bind,
	.auth_xprt_begin = __auth_ovis_xprt_begin,
	.auth_xprt_recv_cb = __auth_ovis_xprt_recv_cb,
	.auth_cred_get = __auth_ovis_cred_get,
};

struct ldms_auth_ovis {
	struct ldms_auth base;
	uid_t luid;
	gid_t lgid;
	char *secret;
	uint64_t challenge;
	char *hash;
	char conf[4096];
	char buff[4096];
};

ldms_auth_plugin_t __ldms_auth_plugin_get()
{
	if (!aolog) {
		aolog = ovis_log_register("auth.ovis", "Messages for ldms_auth_ovis");
		if (!aolog) {
			LOG_ERROR("Failed to register %s's log. Error %d\n",
					__FILE__, errno);
		}
	}
	return &plugin;
}

static
ldms_auth_t __auth_ovis_new(ldms_auth_plugin_t plugin,
		       struct attr_value_list *av_list)
{
	struct ldms_auth_ovis *a;
	const char *val;
	struct passwd *pwd, _pwd;
	int len;

	a = calloc(1, sizeof(*a));
	if (!a) {
		LOG_OOM();
		goto err0;
	}

	val = av_value(av_list, "conf");
	if (!val)
		val = getenv(ENVCONFNAME);
	if (val) {
		len = snprintf(a->conf, sizeof(a->conf), "%s", val);
		if (len >= sizeof(a->conf)) {
			/* name too long */
			LOG_ERROR("The file path is too long. "
				  "It must be at most %zd.\n", sizeof(a->conf));
			errno = ENAMETOOLONG;
			goto err1;
		}
		goto load_conf;
	}

	/* `conf` not given, and env LDMS_AUTH_FILE is not set, try
	 * various default locations. */

	/* try "~/.ldmsauth.conf" */
	(void)getpwuid_r(getuid(), &_pwd, a->buff, sizeof(a->buff), &pwd);
	if (pwd) {
		len = snprintf(a->conf, sizeof(a->conf), "%s/" USRCONFNAME,
							 pwd->pw_dir);
		if (len >= sizeof(a->conf)) {
			LOG_ERROR("The secret word is too long. "
				  "It must be at most %zd.\n", sizeof(a->conf));
			errno = ENAMETOOLONG;
			goto err1;
		}
		if (f_file_exists(a->conf))
			goto load_conf;
		/* else, try another location */
	}

	/* try SYSCONFDIR/ldmsauth.conf */
	len = snprintf(a->conf, sizeof(a->conf), SYSCONFDIR "/" SYSCONFNAME);
	if (len >= sizeof(a->conf)) {
		LOG_ERROR("The secret word is too long. "
			  "It must be at most %zd.\n", sizeof(a->conf));
		errno = ENAMETOOLONG;
		goto err1;
	}
	if (f_file_exists(a->conf))
		goto load_conf;
	/* else error */
	LOG_ERROR("Cannot find any files that contains a secret word.\n");
	errno = ENOENT;
	goto err1;

load_conf:
	/* a->conf should contain the path */
	a->secret = ovis_auth_get_secretword(a->conf, aolog);
	if (!a->secret) {
		LOG_ERROR("Failed to read the secret word.\n")
		goto err1;
	}
	return &a->base;

err1:
	free(a);
err0:
	return NULL;
}

static
ldms_auth_t __auth_ovis_clone(ldms_auth_t auth)
{
	struct ldms_auth_ovis *_a = (void*)auth;
	struct ldms_auth_ovis *a = calloc(1, sizeof(*a));
	if (!a)
		goto err0;
	memcpy(a, auth, sizeof(*a));
	a->hash = NULL;
	a->secret = NULL;
	if (_a->hash) {
		a->hash = strdup(_a->hash);
		if (!a->hash) {
			LOG_OOM();
			goto err1;
		}
	}
	if (_a->secret) {
		a->secret = strdup(_a->secret);
		if (!a->secret) {
			LOG_OOM();
			goto err1;
		}
	}

	return &a->base;

err1:
	__auth_ovis_free(&a->base);
err0:
	return NULL;
}

static
void __auth_ovis_free(ldms_auth_t auth)
{
	struct ldms_auth_ovis *a = (void*)auth;
	if (a->secret)
		free(a->secret);
	if (a->hash)
		free(a->hash);
	free(a);
}

static
int __auth_ovis_xprt_bind(ldms_auth_t auth, ldms_t xprt)
{
	xprt->luid = 0;
	xprt->lgid = 0;
	return 0;
}

typedef enum {
	AUTH_OVIS_CHALLENGE = 1,
	AUTH_OVIS_REPLY,
} auth_ovis_msg_type_t;

#pragma pack(push, 1)
struct auth_ovis_msg_hdr {
	auth_ovis_msg_type_t type;
};

struct auth_ovis_msg_challenge {
	struct auth_ovis_msg_hdr hdr;
	uint64_t challenge;
};

struct auth_ovis_msg_reply {
	struct auth_ovis_msg_hdr hdr;
	char hash[]; /* null-terminated */
};

typedef union auth_ovis_msg {
	struct auth_ovis_msg_hdr hdr;
	struct auth_ovis_msg_challenge chl;
	struct auth_ovis_msg_reply rpl;
} *auth_ovis_msg_t;
#pragma pack(pop)

static
int __auth_ovis_xprt_begin(ldms_auth_t auth, ldms_t xprt)
{
	struct ldms_auth_ovis *a = (void*)auth;
	struct auth_ovis_msg_challenge msg;
	int rc;
	/* prepare challenge and hash */
	a->challenge = ovis_auth_gen_challenge();
	a->hash = ovis_auth_encrypt_password(a->challenge, a->secret);
	if (!a->hash) {
		rc = errno;
		LOG_ERROR("Failed to encrypt the password. Error %d\n", rc);
		return rc;
	}
	msg.hdr.type = AUTH_OVIS_CHALLENGE;
	msg.challenge = htobe64(a->challenge);
	return ldms_xprt_auth_send(xprt, (void*)&msg, sizeof(msg));
}

static
int __auth_ovis_xprt_recv_cb(ldms_auth_t auth, ldms_t xprt,
		const char *data, uint32_t data_len)
{
	char *hash = NULL;
	struct ldms_auth_ovis *a = (void*)auth;
	uint64_t challenge;
	auth_ovis_msg_t msg = (void*)data;
	struct auth_ovis_msg_reply *rpl;
	int rc = 0;
	int len;
	switch (msg->hdr.type) {
	case AUTH_OVIS_CHALLENGE:
		/* receive a challenge from the other side */
		challenge = be64toh(msg->chl.challenge);
		hash = ovis_auth_encrypt_password(challenge, a->secret);
		if (!hash) {
			rc = errno;
			LOG_ERROR("Failed to encrypt the password. Error %d\n", rc);
			return rc;
		}
		len = strlen(hash);
		rpl = malloc(sizeof(*rpl) + len + 1);
		if (!rpl) {
			LOG_OOM();
			free(hash);
			return errno;
		}
		rpl->hdr.type = AUTH_OVIS_REPLY;
		memcpy(rpl->hash, hash, len + 1);
		free(hash);
		rc = ldms_xprt_auth_send(xprt, (void*)rpl, sizeof(*rpl) + len + 1);
		if (rc) {
			LOG_ERROR("Failed to send the auth info. Error %d\n", rc);
		}
		free( rpl );
		break;
	case AUTH_OVIS_REPLY:
		/* the other side send back a hash from our challenge */
		if (data[data_len-1] != 0) {
			/* data must be null-terminated */
			rc = EINVAL;
			break;
		}
		rc = (strcmp(msg->rpl.hash, a->hash))?(EBADE):(0);
		if (rc == 0) {
			xprt->rgid = 0;
			xprt->ruid = 0;
		}
		ldms_xprt_auth_end(xprt, rc);
		/* reset rc as this is not a real transport error */
		rc = 0;
		break;
	default:
		/* invalid */
		rc = EINVAL;
	}
	return rc;
}

static
int __auth_ovis_cred_get(ldms_auth_t auth, ldms_cred_t cred)
{
	struct ldms_auth_ovis *a = (void*)auth;
	cred->uid = a->luid;
	cred->gid = a->lgid;
	return 0;
}
