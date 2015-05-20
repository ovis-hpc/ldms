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
#ifndef __OCMD_PLUGIN_H
#define __OCMD_PLUGIN_H

#include <sys/queue.h>

#include "ovis_util/util.h"
#include "ocm.h"

struct ocm_receiver {
	char hostname[128];
	uint16_t port;
	LIST_ENTRY(ocm_receiver) entry;
};

typedef LIST_HEAD(_ocm_recv_head_, ocm_receiver) * ocm_recv_head_t;

typedef struct ocmd_plugin* ocmd_plugin_t;

/**
 * OCMD Plugin structure.
 *
 * OCMD Plugin must implement the function:
 * <pre>
 * struct ocmd_plugin* ocmd_plugin_create(void (*log_fn)(const char *fmt, ...),
 *					  struct attr_value_list *avl);
 * </pre>
 *
 * The \c create function should create and initialize \c ocmd_plugin.
 * Attribute-value list (\c avl) is provided for plugin initialization.
 * Upon error, the \c create function should return NULL and set errno
 * appropriately.
 *
 * The \c log_fn is a log function for the plugin to log its messages.
 *
 * The create function should return \c ocmd_plugin structure described below.
 */
struct ocmd_plugin {
	/**
	 * Get configuration for \c key.
	 *
	 * The plugin should put configuration for \c key into \c buff using
	 * ::ocm_cfg_buff_add_verb() ::ocm_cfg_buff_add_av() and
	 * ::ocm_cfg_buff_add_cmd_as_av().
	 *
	 * \returns 0 on success.
	 * \returns ENOENT if there is no configuration for the key.
	 * \returns Error code on error.
	 */
	int (*get_config)(ocmd_plugin_t p, const char *key, struct ocm_cfg_buff *buff);

	/**
	 * Log function.
	 *
	 * This function will be set to the same \c log_fn in \c
	 * ocmd_plugin_create() so that the plugin can use this function for
	 * logging.
	 *
	 */
	void (*log_fn)(const char *fmt, ...);

	void (*destroy)(ocmd_plugin_t p);
};

#endif
