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
/**
 * \file ldms_shm_obj.c
 * \brief Routines to manage shared memory
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h> /* For mode constants */
#include <fcntl.h> /* For O_* constants */
#include <string.h>
#include <errno.h>
#include "ldms_shm_obj.h"
#include "ovis_util/util.h" /* for strerror macro only */

/**
 * opens the shared memory and creates the mapping
 */
ldms_shm_obj_t ldms_shm_init(const char *name, int size)
{
	if(NULL == name)
		return NULL;
	int new = 1;
	ldms_shm_obj_t shm_obj = calloc(1, sizeof(*shm_obj));
	if(NULL == shm_obj) {
		printf("Failed to allocated memory in shm_init\n\r");
		return NULL;
	}

	shm_obj->perms = S_IRUSR | S_IWUSR;
	shm_obj->size = size;
	shm_obj->name = strdup(name);

	/* Create shared memory object and set its size */

	shm_obj->flags = O_RDWR | O_CREAT | O_EXCL;

	shm_obj->fd = shm_open(shm_obj->name, shm_obj->flags, shm_obj->perms);
	if(shm_obj->fd == -1) {
		if(EEXIST == errno) {
			new = 0;
			shm_obj->flags = O_RDWR;
			shm_obj->fd = shm_open(shm_obj->name, shm_obj->flags,
					shm_obj->perms);
			if(shm_obj->fd == -1) {
				free(shm_obj);
				printf("ERROR: shm_open error: %d: %s\n\r",
						errno, STRERROR(errno));
				return NULL;
			} else {
				struct stat sb;
				fstat(shm_obj->fd, &sb);
				shm_obj->size = sb.st_size;
			}
		} else {
			free(shm_obj);
			printf("ERROR: shm_open error: %d: %s\n\r", errno,
					STRERROR(errno));
			return NULL;
		}
	}

	if(new) {
		if(ftruncate(shm_obj->fd, shm_obj->size) == -1) {
			free(shm_obj);
			printf("ERROR: ftruncate error: %d: %s\n\r", errno,
					STRERROR(errno));
			return NULL;
		}
	}
	shm_obj->addr = mmap(NULL, shm_obj->size, PROT_READ | PROT_WRITE,
	MAP_SHARED, shm_obj->fd, 0);
	if(MAP_FAILED == shm_obj->addr) {
		free(shm_obj);
		printf(
				"ERROR: mmap error! (%d): %s, shm_obj->size=%ld, name=%s, requested size=%d\n\r",
				errno, STRERROR(errno), shm_obj->size, name,
				size);
		return NULL;
	}
	if(new)
		memset(shm_obj->addr, 0, shm_obj->size);
	return shm_obj;
}

int ldms_shm_clean(const char *name)
{
	int rc;
	ldms_shm_obj_t shm_obj = ldms_shm_init(name, 1);
	if(NULL == shm_obj) {
		rc = ENOMEM;
		printf(
				"ERROR: failed to free the shared memory(%s)  %d: %s\n\r",
				name, errno, STRERROR(errno));
		return rc;
	}
	rc = munmap(shm_obj->addr, shm_obj->size);
	if(rc) {
		printf(
				"ERROR: failed to free the shared memory(%s)  %d: %s\n\r",
				name, errno, STRERROR(errno));
		return rc;
	}
	rc = shm_unlink(name);
	if(rc) {
		printf(
				"ERROR: failed to free the shared memory(%s)  %d: %s\n\r",
				name, errno, STRERROR(errno));
		return rc;
	}
	rc = close(shm_obj->fd);
	if(rc) {
		printf(
				"ERROR: failed to free the shared memory(%s)  %d: %s\n\r",
				name, errno, STRERROR(errno));
		return rc;
	}
	free(shm_obj->name);
	free(shm_obj);
	return rc;
}
