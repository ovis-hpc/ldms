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

#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>

#include "zap/zap.h"
#include "ldms_auth.h"

ldms_auth_plugin_t ldms_auth_plugin_get(const char *name)
{
	char libname[LDMS_AUTH_NAME_MAX + 17];
	int len;
	void *d = NULL;
	ldms_auth_plugin_t p;
	ldms_auth_plugin_get_fn f;

	len = snprintf(libname, sizeof(libname), "libldms_auth_%s.so", name);
	if (len >= sizeof(libname)) {
		errno = ENAMETOOLONG;
		goto err;
	}
	d = dlopen(libname, RTLD_NOW);
	if (!d) {
		errno = ENOENT;
		goto err;
	}
	f = dlsym(d, "__ldms_auth_plugin_get");
	if (!f) {
		errno = ENOENT;
		goto err;
	}
	p = f();
	if (!p)
		goto err;
	return p;

 err:
	if (d)
		dlclose(d);
	return NULL;
}

ldms_auth_t ldms_auth_new(ldms_auth_plugin_t plugin,
			  struct attr_value_list *av_list)
{
	ldms_auth_t auth = plugin->auth_new(plugin, av_list);
	if (auth) {
		auth->plugin = plugin;
	}
	return auth;
}

ldms_auth_t ldms_auth_clone(ldms_auth_t auth)
{
	return auth->plugin->auth_clone(auth);
}

void ldms_auth_free(ldms_auth_t auth)
{
	if (!auth)
		return;
	auth->plugin->auth_free(auth);
}

int ldms_auth_cred_get(ldms_auth_t auth, ldms_cred_t cred)
{
	return auth->plugin->auth_cred_get(auth, cred);
}
