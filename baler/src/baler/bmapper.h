/**
 * \file bmapper.h
 * \author Narate Taerat (narate@ogc.us)
 * \date Mar 19, 2013
 *
 * \defgroup bmapper Baler Mapper Module
 * \{
 * \brief Baler Mapper module.
 *
 * Baler Mapper module serve mapping functionality of Tokens and Patterns for
 * Baler. In other words, it does seq<-->id.
 */
#ifndef _BMAPPER_H
#define _BMAPPER_H
#include <stdint.h>
#include <sys/mman.h>
#include <wchar.h>
#include <sys/queue.h>
#include <linux/limits.h>
#include <pthread.h>
#include "bmem.h"
#include "btypes.h"
#include "bmlist.h"
#include "bmvec.h"

/**
 * Special ID for Baler Map.
 */
typedef enum {
	BMAP_ID_ERR=-1,		/**< For various error */
	BMAP_ID_NOTFOUND=0,	/**< For search not foundt */
	BMAP_ID_STAR,		/**< The special ID for KLEEN_STAR */
	BMAP_ID_BEGIN=128,	/**< The first non-special ID */
} bmap_special_id;

/**
 * Baler Map return codes.
 */
typedef enum {
	BMAP_CODE_ERR=-1, /**< Generic error */
	BMAP_CODE_OK=0, /**< OK */
	BMAP_CODE_EXIST, /**< Element exists in the ::bmap structure (not an
			      error) */
} bmap_code_t;

/**
 * Information to be saved in additional mmap file as header of ::bmap.
 */
struct bmap_hdr {
	uint32_t next_id;
	uint32_t count;
};

/**
 * \brief Baler Mapper structure.
 *
 * Baler Mapper structure holds information regarding the map, which are the
 * path to the bmap file and memory region that mmapped to that file.
 */
struct bmap {
	/**
	 * \brief A path to bmap file.
	 */
	char path[PATH_MAX];

	/**
	 * \brief A write mutex for the map.
	 */
	pthread_mutex_t mutex;

	/**
	 * The ::bmem that stores header information.
	 */
	struct bmem *mhdr;

	/**
	 * This pointer points to ::bmap::mhdr->ptr for convenient usage.
	 * The rest of the ::bmem structures do not get convenient pointers
	 * because they are dynamically growing and can be re-mapped.
	 * The header, on the other hand, is fixed length.
	 */
	struct bmap_hdr *hdr;

	/**
	 * \brief Hash table.
	 * mhash->ptr is of type ::bvec_u64's
	 */
	struct bmvec_u64 *bmhash;

	/**
	 * \brief Linked list of ID's.
	 * mlist->ptr is of type ::bvec_u32's.
	 * They're stored contiguously in the mapped file. Each of them is
	 * either referred to by Hash table (mhash) or by another one of them
	 * (as a linked list should be)
	 */
	struct bmem *mlist;

	/**
	 * \brief Memory that store ::bstr indices (offset) of strings.
	 * This will be mapped to \a path /mstr_idx.mmap.
	 * ::BPTR(mstr, str_idx->data[ID]) is a
	 * ::bstr pointer, pointing to a Baler string corresponding to
	 * ID.
	 */
	struct bmvec_u64 *bmstr_idx;

	/**
	 * \brief Memory that store ::bstr's.
	 * This part of the memory will be mapped to \a path /str.mmap.
	 */
	struct bmem *mstr;
};

/**
 * \brief Open a bmap file, specified by \a path.
 * \param path The path to bmap file.
 * \return A pointer to bmap structure allocated by this function.
 */
struct bmap* bmap_open(const char *path);

/**
 * \brief Close the ::bmap \a m.
 * \param m The ::bmap to be closed.
 */
void bmap_close_free(struct bmap* m);

/**
 * \brief Initialize the bmap structure.
 * \param map The pointer to bmap structure to be initialized.
 * \param nmemb The number of elements in the hash.
 */
int bmap_init(struct bmap *map, int nmemb);

/**
 * \brief Re-initialize the bmap structure.
 * This function is used in the case of hash resizing + rehashing.
 * \param map The map.
 * \param nmemb The number of elements of new hash.
 */
int bmap_rehash(struct bmap *map, int nmemb);

/**
 * \brief Get ID from a wchar_t sequence.
 * \param map The map to obtain information from.
 * \param s The sequence of wchar_t (describing a token or a pattern).
 * \return ID of \a s.
 */
uint32_t bmap_get_id(struct bmap *map, const struct bstr *s);

/**
 * \brief Get the bstr from \a map, speified by \a id.
 * \param map The map.
 * \param id ID of the wanted ::bstr.
 * \return The wchar_t* to the sequence specified by \a id.
 */
const struct bstr* bmap_get_bstr(struct bmap *map, uint32_t id);

/**
 * Return flags for ::bmap_insert.
 */
typedef enum {
	BMAP_INS_RET_NEW=0, /**< The inserted data is new. */
	BMAP_INS_RET_EXIST, /**< The inserted data existed. */
} bmap_ins_ret_t;

static char* bmap_ins_ret_str[] = {
	"BMAP_INS_RET_NEW",
	"BMAP_INS_RET_EXIST",
};

/**
 * Insert ::bstr \a s into ::bmap \a bm.
 * \param bm The ::bmap structure.
 * \param s The ::bstr structure.
 * \param ret_flags The pointer to ret_flag variable. If it is not null, the
 * 	(*ret_flags) will be set to let the caller know whether the \a s
 * 	existed in \a bm or not. See more about ret_flags at ::bmap_ins_ret_t.
 * \return ID on success.
 */
uint32_t bmap_insert(struct bmap *bm, const struct bstr *s,
		bmap_ins_ret_t *ret_flags);

#endif // _BMAPPER_H
/**\}*/
