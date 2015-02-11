/**
 * \file fnv_hash.h
 *
 * \defgroup fnv_hash Fowler-Noll-Vo hash functions.
 * \{
 * \brief Implementation of Fowler-Noll-Vo hash functions.
 * The algorithms are developed by Glenn Fowler, London Curt Noll and Phong Vo.
 * For more information about FNV hash functions, please visit
 * http://www.isthe.com/chongo/tech/comp/fnv/index.html
 */

#ifndef __FMV_HASH_H
#define __FMV_HASH_H

#define FNV_32_PRIME 0x01000193
#define FNV_64_PRIME 0x100000001b3ULL

/**
 * \brief An implementation of FNV-A1 hash algorithm, 32-bit hash value.
 * \param str The string (or byte sequence) to be hashed.
 * \param len The length of the string.
 * \param seed The seed of (re-)hash, usually being 0.
 * \return A 32-bit unsigned integer hash value.
 */
static
uint32_t fnv_hash_a1_32(const char *str, int len, uint32_t seed)
{
	uint32_t h = seed;
	const char *end = str + len;
	const char *s;
	for (s = str; s < end; s++) {
		h ^= *s;
		h *= FNV_32_PRIME;
	}

	return h;
}

/**
 * \brief An implementation of FNV-A1 hash algorithm, 64-bit hash value.
 * \param str The string (or byte sequence) to be hashed.
 * \param len The length of the string.
 * \param seed The seed of (re-)hash, usually being 0.
 * \return A 64-bit unsigned integer hash value.
 */
static
uint64_t fnv_hash_a1_64(const char *str, int len, uint64_t seed)
{
	uint64_t h = seed;
	const char *end = str + len;
	const char *s;
	for (s = str; s < end; s++) {
		h ^= *s;
		h *= FNV_64_PRIME;
	}

	return h;
}

#endif // __FMV_HASH_H
/**\}*/
