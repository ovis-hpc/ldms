/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013,2015-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013,2015-2016 Sandia Corporation. All rights reserved.
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
 * \file boutput.h
 * \author Narate Taerat (narate@ogc.us)
 *
 * \defgroup boutput Baler Output Plugin
 * \{
 * This module contains an interface and definitions for implementing Baler
 * Output Plugin. Similar to implementing Baler Input Plugin, the Output Plugin
 * needs to implement ::create_plugin_instance() function that returns an
 * instance of ::boutplugin (which is an extension of ::bplugin). The life cycle
 * of the output plugin is the same as input plugin (create -> config -> start
 * -> EVENT_LOOP -> stop -> free).
 */
#ifndef __BOUTPUT_H
#define __BOUTPUT_H

#include "bplugin.h"
#include "bwqueue.h"

/*
 * Real definition is defined afterwards.
 */
struct boutplugin;

/**
 * Baler Output Plugin interface structure.
 */
struct boutplugin {
	struct bplugin base;

	/**
	 * \brief Process output function.
	 *
	 * This function will be called when an output from Baler Daemon Core is
	 * ready.  Please note that the calls can be overlapped and
	 * asynchronous. The output plugin implementing this function should
	 * guard this function if necessary.
	 *
	 * \return 0 if success
	 * \return Error code if fail
	 * \param this The plugin instance.
	 * \param odata The output data from Baler core.
	 */
	int (*process_output)(struct boutplugin *this, struct boutq_data *odata);

	/**
	 * \brief Internal output queue corresponding to the plugin.
	 *
	 * This field is internally-used by baler daemon. Plugin should not
	 * access nor modify this field.
	 */
	struct bwq *_outq;
};

/**
 * Output plugins can get balerd's store path from this function.
 * \return store path.
 */
const char *bget_store_path();

/**
 * Set global store path.
 *
 * This function will copy the input \c path to the global \c store_path.
 *
 * \param path The path.
 * \return 0 on success.
 * \return Error code on error.
 */
int bset_store_path(const char *path);

#endif /* __BOUTPUT_H */

/**\}*/
