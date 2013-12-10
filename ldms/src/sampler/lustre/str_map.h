/**
 * \file str_map.h
 * \author Narate Taerat <narate@ogc.us>
 * \brief String-Object mapping utility.
 *
 * This shall be moved to lib later, to share with other projects.
 *
 */
#ifndef __STR_MAP_H
#define __STR_MAP_H
#include <sys/mman.h>
#include <sys/queue.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include "fnv_hash.h"
#include "config.h"

LIST_HEAD(obj_list_head, obj_list);
struct obj_list {
	char *key;
	uint64_t obj;
	LIST_ENTRY(obj_list) link;
};

/**
 * \brief str_map definition.
 *
 * ::str_map is a map that maps strings (keys) to objects (void* or uint64_t).
 *
 * To create a str_map, call ::str_map_create(size_t sz).
 * To insert an object into the map, call ::str_map_insert(map, key, obj);
 * To remove an object, call ::str_map_remove(map, key);
 * To get an object, call ::str_map_get(map, key);
 */
struct str_map {
	size_t hash_size; /**< hash size */
	struct obj_list_head lh_table[FLEXIBLE_ARRAY_MEMBER]; /**< hash table */
};

/**
 * \brief Create a String-Object map.
 * \param sz Hash table size (the unit is number of elements not bytes).
 * \returns NULL on error.
 * \returns Pointer to ::str_map on success.
 */
struct str_map* str_map_create(size_t sz);

/**
 * Free the map.
 */
void str_map_free(struct str_map *m);

/**
 * \param map The map.
 * \param key The key.
 * \returns NULL if there is no such object.
 * \returns Pointer to the object.
 */
uint64_t str_map_get(struct str_map *map, const char *key);

/**
 * \returns 0 on success.
 * \returns Error code on error.
 */
int str_map_insert(struct str_map *map, const char *key, uint64_t obj);

/**
 * \brief Remove an object (specified by key) from the map.
 * \returns 0 on success.
 * \returns Error code on error.
 */
int str_map_remove(struct str_map *map, const char *key);

/**
 * \param map The empty map to be initialized.
 * \param keys The keys
 * \param nkeys The number of keys
 * \param start_id The starting number of the IDs.
 */
int str_map_id_init(struct str_map *map, char **keys, int nkeys,
		    uint64_t start_id);

#endif
