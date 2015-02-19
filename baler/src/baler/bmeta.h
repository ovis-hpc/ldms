/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015 Sandia Corporation. All rights reserved.
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
 * \file bmeta.h
 * \author Narate Taerat (narate at ogc dot us)
 */
#ifndef __BMETA_H
#define __BMETA_H

#include <stdint.h>
#include "barray.h"

typedef enum {
	BMPTN_STORE_STATE_NA,
	BMPTN_STORE_STATE_ERROR,
	BMPTN_STORE_STATE_INITIALIZED,
	BMPTN_STORE_STATE_META_1,
	BMPTN_STORE_STATE_META_2,
	BMPTN_STORE_STATE_REFINING,
	BMPTN_STORE_STATE_DONE,
	BMPTN_STORE_STATE_LAST,
} bmptn_store_state_e;

struct bmeta_cluster_param {
	float diff_ratio;
	float looseness;
	float refinement_speed;
};

/* Declaring structure names to suppress compilation warnings */
struct bmptn_store;
struct bptn_store;
struct btkn_store;

/**
 * \param path store path.
 * \param create If this is true, the new store will be created if it does not
 *               exist. If the store existed, it won't be re-initialized.
 *
 * \retval ptr The pointer to the store handle if success.
 * \retval NULL If failed. errno is also set appropriately.
 */
struct bmptn_store *bmptn_store_open(const char *path, const char *bstore_path,
					int create);

/**
 * Purge the data in the store (of given \c path).
 *
 * \param path The path of the store.
 *
 * \retval 0 if success.
 * \retval errno if error.
 */
int bmptn_store_purge(const char *path);

/**
 * Re-initialize the store, also purge the existing data.
 *
 * \retval 0 if success.
 * \retval errno if error.
 */
int bmptn_store_reinit(struct bmptn_store *store);

/**
 * A function to check whether the \c store is initialized.
 *
 * \retval 0 if the store is *NOT* initialized.
 * \retval 1 if the store is initialized.
 */
int bmptn_store_initialized(struct bmptn_store *store);

/**
 * Perform a meta-pattern clustering.
 */
int bmptn_cluster(struct bmptn_store *store, struct bmeta_cluster_param *param);

/**
 * An interface to get the state of the store.
 */
bmptn_store_state_e bmptn_store_get_state(struct bmptn_store *store);

/**
 * The string version of ::bmptn_store_get_state().
 */
const char *bmptn_store_get_state_str(struct bmptn_store *store);

/**
 * An interface to get a working class ID.
 */
uint32_t bmptn_store_get_working_cls_id(struct bmptn_store *store);

/**
 * An interface to get last class ID.
 */
uint32_t bmptn_store_get_last_cls_id(struct bmptn_store *store);

/**
 * Percentage of work done in the current store state.
 */
uint32_t bmptn_store_get_percent(struct bmptn_store *store);

/**
 * Get meta-class ID for the pattern \c ptn_id.
 * \retval -1 Error. In this case, \c errno is also set to describe the error.
 * \retval 0 No class.
 * \retval clsid Positive number for class ID.
 */
int bmptn_store_get_class_id(struct bmptn_store *store, uint32_t ptn_id);

/**
 * \brief Get meta-class IDs for all pattern IDs.
 *
 * The \c array[ptn_id] contains class ID for \c ptn_id.
 *
 * \param store Store handle.
 * \param array ::barray utility handle.
 *
 * \retval 0 Success.
 * \retval errno Error.
 */
int bmptn_store_get_class_id_array(struct bmptn_store *store, struct barray *array);

#endif /* __BMETA_H */
