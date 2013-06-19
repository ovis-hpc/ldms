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
#ifndef __MMALLOC_H__
#define __MMALLOC_H__
struct mm_info {
	size_t grain;		/*! The minimum allocation size as 2^x  */
	size_t grain_bits;	/*! x in 2^x*/
	size_t size;		/*! The size of the heap in bytes */
	void *start;		/*! The address of the start of the heap */
};

/**
 * \brief Get information about the heap configuration
 *
 * \param mmi	Pointer to the mm_info structure to be filled in.
 */
void mm_get_info(struct mm_info *mmi);

/** 
 * \brief Initialize the heap.
 *
 * Allocates memory for the heap and configures the minimum block size.
 *
 * \param size	The requested size of the heap in bytes.
 * \param grain	The minimum allocation size.
 * \returns 	Zero on success, or an errno indicating the reason for
 *		failure.
 */
int mm_init(size_t size, size_t grain);

/**
 * \brief Allocate memory from the heap.
 *
 * Allocates memory of the requested size from the heap. The memory
 * allocated will be aligned on the \c grain boundary specified in \c mm_init
 *
 * \param size	The requested buffer size in bytes.
 * \returns	A pointer to the allocated memory or NULL if there is
 *		insufficient memory.
 */
void *mm_alloc(size_t size);

/**
 * \brief Return memory to the heap.
 *
 * \param ptr	Pointer to the buffer to free.
 */
void mm_free(void *ptr);
#endif

