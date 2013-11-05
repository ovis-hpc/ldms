/**
 * \file bptn.h
 * \author Narate Taerat (narate@ogc.us)
 *
 * \defgroup bptn Baler Pattern Management
 * \{
 * \brief Baler Pattern Management Utilities.
 * This module contains pattern storage (see ::bptn_store), which stores
 * patterns and their attributes. The information is managed in both pure
 * in-memory data structure and mapped-memory data structure for fast
 * serialization.
 *
 * \note A pattern is defined as a string of token IDs (the wildcard token is a
 * special token), utilizing the structure ::bstr. However, the pattern
 * storage only see patterns as simple ::bstr. Conversion between the two is
 * safe as they basically are the same structure \<byte_length, data\>, with
 * explicit data type for convenient access.
 */

#ifndef __BPTH_H
#define __BPTH_H

#include <pthread.h>

#include "bcommon.h"
#include "btypes.h"
#include "bset.h"
#include "bmvec.h"
#include "barray.h"
#include "bmapper.h"
#include "btkn.h"

/**
 * Attributes for a Baler Pattern (stored in ::bptn_store::mattr).
 */
struct bptn_attrM {
	uint32_t argc; /**< Number of arguments. */
	uint64_t arg_off[0]; /**< Offset to the argument list in
			   	  ::bptn_store::marg */
};

/**
 * In-memory pattern attribute.
 */
struct bptn_attr {
	uint32_t argc;
	struct bset_u32 arg[0]; /**< arg[i] is a set that hold the ith arg. */
};

/**
 * Convenient allocation function for ::bptn_attr.
 * \param argc The number of arguments for the pattern.
 */
struct bptn_attr* bptn_attr_alloc(uint32_t argc);

/**
 * Free the \a attr, along with all of the data owned by it.
 */
void bptn_attr_free(struct bptn_attr *attr);

/**
 * Definition for bvec_set_u32.
 */
BVEC_DEF(bvec_set_u32, struct bset_u32);

/**
 * Store for Baler Pattern.
 */
struct bptn_store {
	char *path; /**< The path of the store. */
	pthread_mutex_t mutex; /**< Write mutex. */
	struct bmem *marg; /**< ::bmem for arguments. */
	struct bmem *mattr; /**< ::bmem for attributes. */
	struct bmvec_u64 *attr_idx; /**< Index to attribute.
					attr_idx[ID] is the attribute of
					pattern ID. */
	struct barray *aattr; /**< aattr[ID] is the pointer to the
					in-memory attribute
				     	of patern ID (::bptn_attr). */
	struct bmap *map; /**< map STR\<--\>ID */
};

/**
 * Open ::bptn_store at path \a path, or initialize a new store
 * if it does not exist.
 * \return NULL on error.
 * \return The pointer to the opened ::bptn_store on success.
 */
struct bptn_store* bptn_store_open(const char *path);

/**
 * Close the \a store and free the structure (together with all data owned
 * by \a store).
 * \note The caller has to make sure that no one is using this store, otherwise
 * 	program may crash from segmentation fault.
 * \param store The pointer to ::bptn_store.
 */
void bptn_store_close_free(struct bptn_store *store);

/**
 * Add \a ptn into \a store. This is only a convenient wrapper function
 * for \a store->map.
 * \param ptn The pattern to be added.
 * \param store The pattern store.
 * \return An ID of the \a ptn if success.
 * \return BMAP_ID_ERR if error.
 */
static
uint32_t bptn_store_addptn(struct bptn_store *store, struct bstr *ptn)
{
	return bmap_insert(store->map, ptn, NULL);
}

/**
 * Add \a msg (which is pattern + args) into \a store.
 * \param store The store.
 * \param msg The message.
 * \return 0 on success.
 * \return Error code on failure.
 */
int bptn_store_addmsg(struct bptn_store *store, struct bmsg *msg);

/**
 * Convert \c ptn_id to C string.
 *
 * \param store The pattern store.
 * \param ptn_id The pattern ID.
 * \param dest The buffer for the output string.
 * \param len The maximum length for \a dest.
 *
 * \returns Error code on error.
 * \returns 0 on success.
 */
int bptn_store_id2str(struct bptn_store *ptns, struct btkn_store *tkns,
		      uint32_t ptn_id, char *dest, int len);

/**
 * Last ID.
 * \param ptns The pattern store.
 * \returns Current last ID of the given \c ptns (the store).
 */
uint32_t bptn_store_last_id(struct bptn_store *ptns);

/**
 * First ID.
 * \param ptns The pattern store.
 * \returns Current first ID of the given \c ptns (the store).
 */
uint32_t bptn_store_first_id(struct bptn_store *ptns);

#endif

/**\}*/
