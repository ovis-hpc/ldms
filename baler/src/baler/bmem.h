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

#endif // _BMEM_H
/**\}*/ // bmem
