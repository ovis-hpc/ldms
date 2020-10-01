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

ldms_auth_plugin_t ldms_auth_plugin_get(const char *plugin_name)
{
	char library_name[LDMS_PLUGIN_LIBPATH_MAX];
	char library_path[LDMS_PLUGIN_LIBPATH_MAX];
	ldms_auth_plugin_t lpi;
	ldms_auth_plugin_get_fn pget;
	char *pathdir = library_path;
	char *libpath;
	char *saveptr = NULL;
	char *path = getenv("LDMSD_PLUGIN_LIBPATH");
	void *d = NULL;

	if (!path)
		path = LDMS_XPRT_LIBPATH_DEFAULT;

	strncpy(library_path, path, sizeof(library_path) - 1);
	int len;
	while ((libpath = strtok_r(pathdir, ":", &saveptr)) != NULL) {
		pathdir = NULL;
		len = snprintf(library_name, sizeof(library_name), "%s/libldms_auth_%s.so",
			libpath, plugin_name);
		if (len >= sizeof(library_name)) {
			errno = ENAMETOOLONG;
			goto err;
		}
		d = dlopen(library_name, RTLD_NOW);
		if (d != NULL) {
			break;
		}
		struct stat buf;
		if (stat(library_name, &buf) == 0) {
			errno = ENOENT;
			char *dlerr = dlerror();
			ldms_log(LDMSD_LERROR,"Bad plugin '%s': dlerror %s\n", plugin_name, dlerr);
			goto err;
		}
	}

	if (!d) {
		char *dlerr = dlerror();
		errno = ENOENT;
		ldms_log(LDMSD_LERROR,"Failed to load the authentication plugin '%s': "
				"dlerror %s\n", plugin_name, dlerr);
		goto err;
	}

	pget = dlsym(d, "__ldms_auth_plugin_get");
	if (!pget) {
		ldms_log(LDMSD_LERROR,"The library, '%s',  is missing the __ldms_auth_plugin_get() "
			 "function.", plugin_name);
		goto err;
	}
	lpi = pget();
	if (!lpi) {
		ldms_log(LDMSD_LERROR, "The library '%s' __ldms_auth_plugin_get() "
			 "function returned NULL.", plugin_name);
		goto err;
	}
	return lpi;
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
	auth->plugin->auth_free(auth);
}

int ldms_auth_cred_get(ldms_auth_t auth, ldms_cred_t cred)
{
	return auth->plugin->auth_cred_get(auth, cred);
}
