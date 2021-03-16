/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
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
#ifndef __LDMS_AUTH_H
#define __LDMS_AUTH_H

#include <stdint.h>
#include <sys/types.h>
#include <ovis_util/util.h>

/**
 * \page ldms_auth_dev LDMS Authentication Plugin Development
 *
 * \section ldms_auth_dev_desc DESCRIPTION
 *
 * LDMS Authentication Plugin must implement \c __ldms_auth_plugin_get()
 * function that returns a pointer to the \c ldms_auth_plugin structure that
 * describes the plugin. The interfaces inside the structure are expected to
 * behave as follows:
 *
 * \par
 * \b name contains the plugin name. The maximum length (including
 * the null-terminated character) is 64 bytes.
 *
 * \par
 * \b auth_new() returns a new authentication object (to be attached to an
 * LDMS transport in the future.
 *
 * \par
 * \b auth_clone() clones an authentication object. This is for the newly
 * accepted passive endpoint.
 *
 * \par
 * \b auth_free() frees the authentication object and its resources.
 *
 * \par
 * \b auth_xprt_bind() will be called from LDMS when the LDMS xprt binds the
 * authentication object to it.
 *
 * \par
 * \b auth_xprt_begin() will be called from LDMS when the LDMS xprt begins the
 * authentication process. At this point, the authentication plugin takes the
 * control of the transport. To end the authentication process and hand the
 * control of the transport back to LDMS, the plugin must call \c
 * ::ldms_xprt_auth_end() -- with the authentication result. During the
 * authentication process, the authentication plugin can use \c
 * ::ldms_xprt_auth_send() to send messages to the other side of the transport.
 * If the LDMS transport receives any data during authentication process, the
 * data will be passed to the plugin via \c auth_xprt_recv_cb() callback
 * function.
 *
 * \par
 * \b auth_xprt_recv_cb() will be called from LDMS when the LDMS xprt receive
 * any data during authentication process (after \b auth_xprt_begin() is called
 * from LDMS, and before \c ldms_xprt_auth_end() is called from the plugin).
 *
 * \par
 * \b auth_cred_get() tells the caller about the credential of the local side.
 */

#define LDMS_AUTH_NAME_MAX 15

typedef struct ldms_auth_plugin *ldms_auth_plugin_t;
typedef struct ldms_auth *ldms_auth_t;

/* include ldms_xprt.h here so that LDMS_AUTH_NAME_MAX and auth types are
 * defined. */
#include "ldms_xprt.h"

typedef ldms_auth_plugin_t (*ldms_auth_plugin_get_fn)();

struct ldms_auth {
	struct ldms_auth_plugin *plugin;
};

struct ldms_auth_plugin {
	char name[LDMS_AUTH_NAME_MAX + 1];
	ldms_auth_t (*auth_new)(ldms_auth_plugin_t plugin,
				struct attr_value_list *av_list);
	ldms_auth_t (*auth_clone)(ldms_auth_t auth);
	void (*auth_free)(ldms_auth_t auth);
	int (*auth_xprt_bind)(ldms_auth_t auth, ldms_t xprt);
	int (*auth_xprt_begin)(ldms_auth_t auth, ldms_t xprt);
	int (*auth_xprt_recv_cb)(ldms_auth_t auth, ldms_t xprt,
				 const char *data, uint32_t data_len);
	int (*auth_cred_get)(ldms_auth_t auth, ldms_cred_t cred);
};

/**
 * \defgroup ldms_auth_grp LDMS Auth semi-Public interfaces
 * \{
 * These are LDMS Auth functions provided for LDMS xprt service.
 */

/**
 * \brief Get LDMS authentication plugin by name
 *
 * If the plugin has been loaded, the loaded plugin is returned. The newly
 * loaded plugin will replace the previously loaded plugin.
 *
 * \retval plugin the plugin handle, if there is no error.
 * \retval errno if there is an error. In this case, \c errno is also set.
 */
ldms_auth_plugin_t ldms_auth_plugin_get(const char *name);

/**
 * \brief Create a new authentication object
 *
 * \param plugin The LDMS Auth Plugin handle.
 * \param av_list The attribute-value list of options for the plugin.
 *
 * \retval NULL if failed. In this case, \c errno is alos set.
 * \retval aobj authentication object, if success.
 */
ldms_auth_t ldms_auth_new(ldms_auth_plugin_t plugin,
			  struct attr_value_list *av_list);

/**
 * \brief Clone the authentication object
 *
 * \param auth The original authentication object.
 *
 * \retval NULL if failed. \c errno is also set in this case.
 * \retval aobj authentication object, if success.
 */
ldms_auth_t ldms_auth_clone(ldms_auth_t auth);

/**
 * \brief Free the authentication object
 *
 * \param auth The authentication object handle. Ignores NULL input.
 */
void ldms_auth_free(ldms_auth_t auth);

/**
 * \brief Obtain the credential information from the authentication object
 *
 * \param auth The authentication object handle.
 * \param [out] cred The credential output buffer.
 *
 * \retval 0 if no error.
 * \retval errno if there is an error.
 */
int ldms_auth_cred_get(ldms_auth_t auth, ldms_cred_t cred);

/**
 * \}
 */

#endif
