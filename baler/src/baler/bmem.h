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
 * \file bmem.h
 * \author Narate Taerat (narate@ogc.us)
 * \date Mar 20, 2013
 *
 * \defgroup bmem Baler Memory Management Module.
 * \{
 * \brief Baler Memory Management Utility Module.
 * This module helps Baler manage mmapped memory. Currently bmem can only be
 * allocated but not deallocated because its usage needs only allocation
 * but not the other.
 */

#ifndef _BMEM_H
#define _BMEM_H
#include <stdint.h>
#include <linux/types.h>
#include <linux/limits.h>
#include "bcommon.h"

/**
 * Offset value that represent NULL for Baler Memory Mapping handler.
 */
#define BMEM_NULL 0

/**
 * Header for ::bmem. This structure is always mapped to the beginning
 * of the mapped file.
 */
struct bmem_hdr {
	uint64_t flen; /**< Length of the file. */
	uint64_t ulen; /**< Used length. */
};

/**
 * \brief Baler memory structure, storing mapped memory.
 */
struct __attribute__((packed)) bmem {
	char path[PATH_MAX]; /**< Mapped file path. */
	int fd; /**< File descriptor for \a path. */
	struct bmem_hdr *hdr; /**< Header part of the mapped memory. */
	void *ptr; /**< Mapped memory after the header part. */
	uint64_t flen; /**< flen, for remap checking. */
};

/**
 * Calculating a pointer from an offset \a off to the structure ::bmem \a bmem.
 * \param bmem A pointer to the ::bmem structure.
 * \param off Offset
 * \return A pointer to \a off bytes in bmem.
 */
#define BMPTR(bmem, off) ((off)?(BPTR((bmem)->hdr, off)):(NULL))

/**
 * Calculating an offset given the pointer \a ptr and the ::bmem structure
 * \a bmem that contain the pointer.
 * \param bmem The pointer to the ::bmem structure.
 * \param ptr The pointer to some object inside \a bmem.
 * \return An offset to the beginning of the mapped memory region handled by
 * 		\a bmem.
 */
#define BMOFF(bmem, ptr) BOFF((bmem)->hdr, ptr)

/**
 * \brief Open a file, initialize and map it to bmem structure.
 * If the file existed with size > 0, it won't be initialized.
 * \return A pointer to \<struct bmem*\> if success, or NULL if error.
 */
struct bmem* bmem_open(const char *path);

/**
 * \brief Close the bmem structure \a b, but not freeing it.
 * \param b The bmem structure.
 */
void bmem_close(struct bmem *b);

/**
 * \brief Close and free \a b.
 * \param b The bmem structure.
 */
void bmem_close_free(struct bmem *b);

/**
 * \brief Allocate memory from bmem structure (map file).
 * \param b The bmem structure.
 * \param size The size of the requested memory.
 * \return Offset to the allocated memory, relative to b->ptr.
 */
int64_t bmem_alloc(struct bmem *b, uint64_t size);

/**
 * \brief Utility function to unlink bmem of the given \c path.
 * \retval 0 if success.
 * \retval errno if error.
 */
int bmem_unlink(const char *path);

/**
 * \brief Refresh bmem, mremap() if necessary.
 */
int bmem_refresh(struct bmem *b);

/**
 * \brief reset \c b to initial state.
 */
void bmem_reset(struct bmem *b);

#endif // _BMEM_H
/**\}*/ // bmem
