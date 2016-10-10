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
 * \file bout_sos_img.h
 * \defgroup bout_sos_img SOS Image Output
 * \ingroup bout_sos
 *
 * \brief An extension of ::bout_sos for Image data storage.
 *
 * bout_sos_img requires only information stored in ::bout_sos, so this plugin
 * will not extend the SOS plugin instance structure. It does only override
 * \a start and \a process_output to correctly create SOS and process data to
 * store in the created SOS.
 */

#ifndef __BOUT_SOS_IMG_H
#define __BOUT_SOS_IMG_H

#include "bout_sos.h"

struct bout_sos_img_plugin {
	struct bout_sos_plugin base; /** base structure. */
	uint32_t delta_ts; /** ts granularity */
	uint32_t delta_node; /** node granularity */
};

/**
 * This function overrides ::bout_sos_start().
 */
int bout_sos_img_start(struct bplugin *this);

/**
 * This function overrides ::bout_sos_config().
 */
int bout_sos_img_config(struct bplugin *this, struct bpair_str_head *cfg_head);

/**
 * This function overrides ::bout_sos_stop().
 */
int bout_sos_img_stop(struct bplugin *this);

/**
 * Process \a odata from SOS plugin instance, and put it into the storage.
 */
int bout_sos_img_process_output(struct boutplugin *this,
		struct boutq_data *odata);

#endif
/** \} */
