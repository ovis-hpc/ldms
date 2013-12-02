/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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
 * \file bout_sos.c
 * \author Narate Taerat (narate@ogc.us)
 */

#include "bout_sos.h"
#include "baler/butils.h"
#include <limits.h>

int bout_sos_config(struct bplugin *this, struct bpair_str_head *arg_head)
{
	struct bout_sos_plugin *_this = (typeof(_this))this;
	/* handle only 'path' argument */
	struct bpair_str *bpstr = bpair_str_search(arg_head, "path", NULL);
	char *sos_path;
	if (bpstr) {
		/* if path is given, use that path */
		sos_path = strdup(bpstr->s1);
		if (!sos_path)
			return ENOMEM;
	} else {
		/* if path is not given, construct from store_path
		 * and plugin name. */
		const char *store_path = bget_store_path();
		sos_path = malloc(PATH_MAX);
		if (!sos_path)
			return ENOMEM;
		char *type = this->name;
		if (strncmp(type, "bout_sos_", 9) == 0)
			type += 9;
		/* first set the sos store directory to the path */
		sprintf(sos_path, "%s/%s_store", store_path, type);

		/* also try to make directory if it does not exist */
		if (!bis_dir(sos_path)) {
			int rc = bmkdir_p(sos_path, 0755);
			if (rc) {
				free(sos_path);
				return rc;
			}
		}

		/* then append the type to create the real path if the
		 * store_name is not given. */
		bpstr = bpair_str_search(arg_head, "store_name", NULL);
		if (bpstr)
			sprintf(sos_path + strlen(sos_path), "/%s", bpstr->s1);
		else
			sprintf(sos_path + strlen(sos_path), "/%s", type);
	}
	_this->sos_path = sos_path;
	return 0;
}

int bout_sos_free(struct bplugin *this)
{
	struct bout_sos_plugin *_this = (typeof(_this))this;
	if (_this->sos)
		bout_sos_stop(this);
	free(_this->sos_path);
	bplugin_free(this);
	return 0;
}

int bout_sos_stop(struct bplugin *this)
{
	struct bout_sos_plugin *_this = (typeof(_this))this;

	if (!_this->sos)
		return EINVAL;
	pthread_mutex_lock(&_this->sos_mutex);
	sos_close(_this->sos);
	_this->sos = 0;
	pthread_mutex_unlock(&_this->sos_mutex);
	return 0;
}

int bout_sos_start(struct bplugin *this)
{
	struct bout_sos_plugin *_this = (typeof(_this))this;
	_this->sos = sos_open(_this->sos_path, O_RDWR|O_CREAT, 0660,
			_this->sos_class);
	if (!_this->sos)
		return errno;
	return 0;
}

int bout_sos_init(struct bout_sos_plugin *this, const char *name)
{
	struct boutplugin *p = (typeof(p))this;
	p->base.name = strdup(name);
	if (!p->base.name)
		return ENOMEM;
	p->base.config = bout_sos_config;
	p->base.start = bout_sos_start;
	p->base.free = bout_sos_free;
	/* start, process_output, name and version should be initialized in the
	 * child class's implementation */
	pthread_mutex_init(&this->sos_mutex, NULL);
	return 0;
}

/**\}*/
