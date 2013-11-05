/**
 * \file btkn.h
 * \author Narate Taerat (narate@ogc.us)
 *
 * \defgroup btkn Baler Token Management
 * \{
 * Additional to ::bmap module, this module contains extra functions necessary
 * for token management as ::bmap manages only ID\<--\>STR mapping.
 */
#ifndef __BTKN_H
#define __BTKN_H

#include "bcommon.h"
#include "btypes.h"
#include "bmapper.h"
#include "bmvec.h"
#include <sys/mman.h>
#include <linux/limits.h>

#define BTKN_TYPE__LIST(P, CMD) \
	CMD(P, STAR), /**< Wild card (*) token */ \
	CMD(P, ENG),  /**< English word */ \
	CMD(P, SYM),  /**< Symbol */ \
	CMD(P, SPC),  /**< White spaces */ \
	CMD(P, NAME), /**< Names, such as service name */ \
	CMD(P, HOST), /**< Host name */ \
	CMD(P, OTHER) /**< Other type */

/**
 * Types of a token.
 */
typedef enum {
	BTKN_TYPE__LIST(BTKN_TYPE_, BENUM),
	BTKN_TYPE_LAST
} btkn_type_t;

extern
const char *btkn_type_str[];

btkn_type_t btkn_type(const char *str);

/**
 * Baler's Token Attributes, containing attributes of a token.
 */
struct btkn_attr {
	btkn_type_t type;
};

/**
 * bvec definition for ::btkn_attr.
 */
BVEC_DEF(bvec_tkn_attr, struct btkn_attr);

/**
 * bmvec definition for bvec_tkn_attr.
 */
BMVEC_DEF(bmvec_tkn_attr, struct bvec_tkn_attr);

/**
 * Baler token store.
 * This structure contains various structures for token management.
 */
struct btkn_store {
	char *path; /**< Path of the token store. */
	struct bmvec_tkn_attr *attr; /**< Token attribute. */
	struct bmap *map; /**< Token\<--\>ID map */
};

/**
 * Open btkn_store.
 * \param path Path to the btkn_store.
 * \returns NULL on error.
 * \returns A pointer to the store on success.
 */
struct btkn_store* btkn_store_open(char *path);

/**
 * Close and free the given ::btkn_store \c s.
 * \param s The store to be closed and freed.
 */
void btkn_store_close_free(struct btkn_store *s);

/**
 * A function to obtain a C string from \c id.
 * \param store Tht store handle.
 * \param id The token id.
 * \param dest The destination string buffer to copy data to.
 * \param len The maximum length of the \c dest.
 * \returns Error code on error.
 * \returns 0 on success.
 */
int btkn_store_id2str(struct btkn_store *store, uint32_t id, char *dest,
		      int len);

/**
 * Getting token attribute of token ID \c tkn_id.
 * \param store The token store.
 * \param tkn_id The token ID.
 * \return Pointer to the attribute.
 */
static inline
struct btkn_attr btkn_store_get_attr(struct btkn_store *store, uint32_t tkn_id)
{
	return store->attr->bvec->data[tkn_id];
}

/**
 * Set token type of the token with ID \c tkn_id to \c type.
 *
 * \param store The store handle.
 * \param tkn_id Token ID.
 * \param type The type of the token.
 */
static inline
void btkn_store_set_attr(struct btkn_store *store, uint32_t tkn_id,
			 struct btkn_attr attr)
{
	bmvec_generic_set(store->attr, tkn_id, &attr, sizeof(attr));
}

/**
 * Insert \c str into the store.
 *
 * If \c str existed, this function does nothing and return the ID of \c str.
 *
 * \return If \c str existed or is successfully inserted, the ID of the token
 * 	\c str is returned.
 * \return On error, the function returns ::BMAP_ID_ERR.
 */
static inline
uint32_t btkn_store_insert(struct btkn_store *store, struct bstr *str)
{
	uint32_t id;
	bmap_ins_ret_t ret_flag;
	id = bmap_insert(store->map, str, &ret_flag);
	if (id == BMAP_ID_ERR || ret_flag == BMAP_CODE_EXIST)
		/* errno should be set in bmap_insert() */
		goto out;

	/* set attribute to '*' by default */
	struct btkn_attr attr = {BTKN_TYPE_STAR};
	btkn_store_set_attr(store, id, attr);

out:
	return id;
}

/**
 * Convenient function for inserting C string with token type into the store.
 * \returns Token ID on success.
 * \returns BMAP_ID_ERR on error, and errno will be set accordingly.
 */
uint32_t btkn_store_insert_cstr(struct btkn_store *store, const char *str,
							btkn_type_t type);

/**
 * Treat \c cstr as an array of char and insert each of the character into the
 * \c store as single token. The tokens will be assigned with type \c type.
 */
int btkn_store_char_insert(struct btkn_store *store, const char *cstr,
							btkn_type_t type);

#endif /* __BTKN_H */
/**\}*/
