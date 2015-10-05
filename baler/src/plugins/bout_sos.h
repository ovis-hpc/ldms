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
 * \file bout_sos.h
 * \author Narate Taerat
 *
 * \defgroup bout_sos Baler Output to SOS
 * \{
 * Baler Output to SOS implements Baler Output Plugin Interface (::boutplugin).
 *
 * This is similar to a virtual class in C++, having many basic functions
 * implemented, but leaving 'process_output' to be implemented by the inherited
 * classes. This is because SOS can support many kind of data. According to
 * current Baler requirement, we will have two kind of storage: messages and
 * images. Please see ::bout_sos_msg and ::bout_sos_img for more information.
 *
 * Please note that baler can have multiple SOS plugin instance loaded. Hence,
 * messages and several images (different resolutions) can be stored
 * simultaneously in different SOSs (taken care of by different plugin
 * instances.)
 *
 * \note bout_sos does not provide \a create_plugin_instance function because it
 * is virtual and should not have an instance.
 */
#ifndef __BOUT_SOS_H
#define __BOUT_SOS_H

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include "baler/bplugin.h"
#include "baler/boutput.h"

#include "sos/sos.h"

/**
 * Output plugin instance for sos, extending ::boutplugin.
 */
struct bout_sos_plugin {
	struct boutplugin base; /**< Base structure. */
	/* Additional data to the base. */
	char *sos_path; /**< SOS path, for reference. */
	pthread_mutex_t sos_mutex; /**< Mutex for sos. */
	int last_rotate; /**< Last rotation timestamp */
	void *bsos_handle; /**< Handle to bsos */
	void *(*bsos_handle_open)(const char *path, sos_perm_t o_perm);
	void (*bsos_handle_close)(void *bsos_handle, sos_commit_t commit);
};

/**
 * Baler Output SOS plugin configuration.
 *
 * In the configuration phase, baler core should give the path to the storage to
 * bout_sos_plugin. The plugin will then memorize the path, but does nothing
 * further.  Currently, bout_sos_plugin knows only ``path'' argument.
 *
 * \param this The plugin instance.
 * \param arg_head The head of argument list (an argument is a pair of strings,
 * 	::bpair_str).
 * \return 0 on success.
 * \return Error code on error.
 */
int bout_sos_config(struct bplugin *this, struct bpair_str_head *arg_head);

/**
 * Start function for ::bout_sos_plugin.
 * This will open sos, of path \a this->sos_path, and return.
 * \return 0 on sucess.
 * \return Error code on error.
 */
int bout_sos_start(struct bplugin *this);

typedef void (*bout_sos_rotate_cb_fn)(struct bout_sos_plugin *_this);

/**
 * This is a destructor of the ::bout_sos_plugin instance.
 * \param this The plugin instance.
 * \return 0 on success.
 * \return Error code on error.
 */
int bout_sos_free(struct bplugin *this);

/**
 * Calling this function will stop \a this plugin instance.
 * Currently, this function only does sos_close.
 * \param this The plugin instance.
 * \return 0 on success.
 * \return Error code on error.
 */
int bout_sos_stop(struct bplugin *this);

/**
 * Plugin initialization.
 *
 * \param this The plugin instance pointer.
 * \param name The name of the plugin.
 * \returns 0 on success.
 * \returns Error code on error.
 */
int bout_sos_init(struct bout_sos_plugin *this, const char *name);

#endif
/** \} */
