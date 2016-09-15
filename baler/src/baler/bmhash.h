/**
 * \file bmhash.h
 * \author Narate Taerat (narate at ogc dot us)
 */
#ifndef __BMHASH_H
#define __BMHASH_H

#include <linux/limits.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "bcommon.h"
#include "bmlist.h"
#include "btypes.h"
#include "bmem.h"

typedef enum {
	BMHASH_FN_FNV,
	BMHASH_FN_MURMUR,
} bmhash_fn_e;

struct bmhash_header {
	union {
		struct {
			char ver[16];
			uint32_t hash_size;
			uint32_t seed;
			bmhash_fn_e fn_enum;
			uint32_t count;
		};
		char __space[4096];
	};
	struct bvec_u64 table[0]; /* Convenience reference to the hash table
				   * that is always next to the header */
};

struct bmhash_entry {
	uint64_t value;
	struct bmlist_link link;
	struct bstr key;
};

/**
 * Baler Utility - Serializable Hash Container.
 *
 * This is an insert-only container. The key of the hash is ::bstr and the value
 * is uint64_t.
 */
struct bmhash {
	/**
	 * \brief A path to bmap file.
	 */
	char path[PATH_MAX];

	/**
	 * Mapped memory region of ::bmhash.
	 */
	struct bmem *mem;

	/**
	 * Hash function.
	 */
	uint32_t (*hash_fn)(const char *key, int len, uint32_t seed);
};

struct bmhash_iter {
	struct bmhash *bmh;
	uint32_t idx;
	struct bmhash_entry *ent;
};

struct bmhash *bmhash_open(const char *path, int flag, ...);

void bmhash_close_free(struct bmhash *bmh);

int bmhash_unlink(const char *path);

struct bmhash_entry *bmhash_entry_set(struct bmhash *bmh,
				      const struct bstr *key, uint64_t value);

struct bmhash_entry *bmhash_entry_get(struct bmhash *bmh,
				      const struct bstr *key);

struct bmhash_iter *bmhash_iter_new(struct bmhash *bmh);

void bmhash_iter_init(struct bmhash_iter *iter, struct bmhash *bmh);

void bmhash_iter_free(struct bmhash_iter *iter);

struct bmhash_entry *bmhash_iter_entry(struct bmhash_iter *iter);

int bmhash_iter_begin(struct bmhash_iter *iter);

int bmhash_iter_next(struct bmhash_iter *iter);
#endif
