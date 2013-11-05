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
	b->hdr = mmap(NULL, BPGSZ, PROT_READ|PROT_WRITE, MAP_SHARED, b->fd, 0);
	if (b->hdr == MAP_FAILED) {
		berror("mmap");
		goto err1;
	}
	/* Then, do the rest. */
	b->hdr = mmap(NULL, b->hdr->flen, PROT_READ|PROT_WRITE, MAP_SHARED,
			b->fd, 0);
	if (b->hdr == MAP_FAILED) {
		berror("mmap");
		goto err1;
	}
	b->ptr = b->hdr + 1;
out:
	return b;

err1:
	close(b->fd);
err0:
	free(b);
	return NULL;

}

void bmem_close(struct bmem *b)
{
	if (munmap(b->hdr, b->hdr->flen) == -1) {
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
		return -1;
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
			return -1;
		}
		if (write(b->fd, "", 1) == -1) {
			berror("write");
			return -1;
		}
		if ((b->hdr=mremap(b->hdr, b->hdr->flen, nflen, MREMAP_MAYMOVE))
				== MAP_FAILED) {
			berror("mremap");
			return -1;
		}
		b->hdr->flen = nflen;
		b->ptr = b->hdr + 1;
	}
	int64_t off = b->hdr->ulen;

	b->hdr->ulen = nulen;

	return off;
}
