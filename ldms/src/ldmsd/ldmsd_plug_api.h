/*
 * Copyright (c) 2025 Lawrence Livermore National Security, LLC
 * Copyright (c) 2025 Open Grid Computing, Inc. All rights reserved.
 *
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
#ifndef _LDMSD_PLUGIN_H_
#define _LDMSD_PLUGIN_H_
typedef void *ldmsd_plug_handle_t;
/*
 * Record an opaque context pointer in the plugin configuration handle.
 *
 * In order to be multi-configuration capable, a plugin will need to
 * maintain separate state (i.e. context) for each configuration. A
 * plugin will allocate a context structure in the plugin constructor()
 * and use this function to assign it. Subsequent calls to plugin functions
 * will use the ldmsd_plug_ctxt_get() function to return this pointer.
 */
void ldmsd_plug_ctxt_set(ldmsd_plug_handle_t handle, void *context);

/*
 * Retrieve the context pointer from the plugin configuration handle
 * that was set by the ldmsd_plug_ctxt_set() function.
 *
 * This is typically a pointer to the structure created in the
 * plugin's constructor() function.
 */
void *ldmsd_plug_ctxt_get(ldmsd_plug_handle_t handle);

/*
 * Retreive a pointer to the plugin's configuration name. Plugin
 * configuration names will be unique to a give configuration (in
 * contrast to the plugin name).
 *
 * This function is usefull when informing the administrator of the
 * configuration to which this message applies
 */
const char *ldmsd_plug_cfg_name_get(ldmsd_plug_handle_t handle);

/*
 * Retrive a pointer to the plugin's name. Multiple configurations
 * may be created from the same plugin, and so multiple configurations
 * may have the same plugin name.
 */
const char *ldmsd_plug_name_get(ldmsd_plug_handle_t handle);

/*
 * Retrieve the log handle used to write log messages
 */
ovis_log_t ldmsd_plug_log_get(ldmsd_plug_handle_t handle);

#endif
