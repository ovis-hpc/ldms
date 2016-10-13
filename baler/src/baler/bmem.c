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
 * \file bmem.c
 * \author Narate Taerat (narate@ogc.us)
 * \date Mar 20, 2013
 *
 * \brief Implementation of functions in bmem.h.
 */
#define _GNU_SOURCE
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <malloc.h>
#include <fcntl.h>
#include <unistd.h>

#include "bmem.h"
#include "butils.h"

#define BPGSZ sysconf(_SC_PAGE_SIZE)

struct bmem* bmem_open(const char *path)
{
	int errno_tmp;
	struct bmem *b = calloc(1, sizeof(*b));
	strncpy(b->path, path, PATH_MAX);
	b->fd = open(path, O_RDWR|O_CREAT, 0600);
	if (b->fd < 0) {
		berror("open");
		goto err0;
	}

	// determining file size
	int64_t fsize = lseek(b->fd, 0, SEEK_END);

	if (fsize == -1) {
		goto err1;
	}

	if (!fsize) {
		// This is a new file.
		fsize = BPGSZ; // Required fsize

		// Initialize the header first.
		struct bmem_hdr hdr;
		hdr.ulen = sizeof(hdr); // used len
		hdr.flen = fsize; // file len
		if (write(b->fd, &hdr, sizeof(hdr)) == -1) {
			berror("write");
			goto err1;
		}

		// Resize it before the mapping.
		if (lseek(b->fd, fsize-1, SEEK_SET) == -1) {
			berror("lseek");
			goto err1;
		}
		// lightly touch to expand the file.
		if (write(b->fd, "", 1) == -1) {
			berror("write");
			goto err1;
		}
	}

	/* Do the mapping for the header first. */
	b->flen = BPGSZ;
	b->hdr = mmap(NULL, BPGSZ, PROT_READ|PROT_WRITE, MAP_SHARED, b->fd, 0);
	if (b->hdr == MAP_FAILED) {
		berror("mmap");
		goto err1;
	}
	b->ptr = b->hdr + 1;

	/* Then, do the rest. */
	if (bmem_refresh(b))
		goto err2;
out:
	return b;

err2:
	munmap(b->hdr, BPGSZ);
err1:
	close(b->fd);
err0:
	free(b);
	return NULL;

}

void bmem_close(struct bmem *b)
{
	if (munmap(b->hdr, b->flen) == -1) {
		berror("munmap");
	}
	if (close(b->fd) == -1) {
		berror("close");
	}
}

void bmem_close_free(struct bmem *b)
{
	bmem_close(b);
	free(b);
}

int64_t bmem_alloc(struct bmem *b, uint64_t size)
{
	if (!b->fd) {
		errno = EBADF;
		return 0;
	}
	uint64_t nulen = b->hdr->ulen + size; // new used len
	if (nulen > b->hdr->flen) {
		// Request more than previously mapped,
		// extend the file and remap.

		// New file len, making it a multiplication of BPGSZ that
		// greater than new used len.
		uint64_t nflen = (nulen | (BPGSZ - 1)) + 1;
		if (lseek(b->fd, nflen-1, SEEK_SET) == -1) {
			// errno has already been set with lseek
			berror("lseek");
			return 0;
		}
		if (write(b->fd, "", 1) == -1) {
			berror("write");
			return 0;
		}
		b->hdr->flen = nflen;
		int rc = bmem_refresh(b);
		if (rc != 0) {
			errno = rc;
			return 0;
		}
	}
	int64_t off = b->hdr->ulen;

	b->hdr->ulen = nulen;

	return off;
}

int bmem_unlink(const char *path)
{
	int rc;
	rc = unlink(path);
	if (rc)
		rc = errno;
	return rc;
}

int bmem_refresh(struct bmem *b)
{
	int rc;
	void *new_mem;
	uint64_t flen = b->hdr->flen;
	if (b->flen == flen)
		/* Nothing to do */
		return 0;

	/* Need remap */
	new_mem = mmap(NULL, flen, PROT_READ|PROT_WRITE, MAP_SHARED, b->fd, 0);
	if (new_mem == MAP_FAILED) {
		berror("bmem_refresh(): mmap");
		return errno;
	}

	/* unmap the old map */
	rc = munmap(b->hdr, b->flen);
	if (rc) {
		berror("bmem_refresh(): munmap");
	}

	/* Update */
	b->flen = flen;
	b->hdr = new_mem;
	b->ptr = b->hdr + 1;

	return 0;
}

void bmem_reset(struct bmem *b)
{
	b->hdr->ulen = sizeof(*b->hdr);
}

/* EOF */
