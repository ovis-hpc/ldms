%module bset_wrap
%{
#include "baler/bset.h"
%}

typedef unsigned int uint32_t;

/**
 * Allocation function for ::bset_u32.
 */
struct bset_u32* bset_u32_alloc(int hsize);

/**
 * Initialize \a set. This is used in the case that ::bset_u32 is allocated
 * without calling ::bset_u32_alloc().
 * \return -1 on error.
 * \return 0 on success.
 */
int bset_u32_init(struct bset_u32 *set, int hsize);

/**
 * Clear and free ONLY the contents inside the \a set, but NOT
 * freeing the \a set itself. This function complements ::bset_u32_init().
 * \param set The set to be cleared.
 */
void bset_u32_clear(struct bset_u32 *set);

/**
 * Free for ::bset_u32.
 */
void bset_u32_free(struct bset_u32 *set);

/**
 * Check for existence of \a val in \a set.
 * \param set The pointer to ::bset_u32.
 * \param val The value to check for.
 */
int bset_u32_exist(struct bset_u32 *set, uint32_t val);

/**
 * bset insert return code.
 */
typedef enum {
	BSET_INSERT_ERR = -1,
	BSET_INSERT_OK = 0,
	BSET_INSERT_EXIST
} bset_insert_rc;

/**
 * Insert \a val into \a set.
 * \param set The pointer to ::bset_u32.
 * \param val The value to be inserted.
 * \return 0 on success.
 * \return Error code on error.
 */
int bset_u32_insert(struct bset_u32 *set, uint32_t val);

