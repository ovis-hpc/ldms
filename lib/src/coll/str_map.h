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

LIST_HEAD(obj_list_head, obj_list);
struct obj_list {
	char *key;
	uint64_t obj;
	LIST_ENTRY(obj_list) link;
};

typedef struct str_map* str_map_t;

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
void str_map_free(str_map_t m);

/**
 * \param map The map.
 * \param key The key.
 * \returns NULL if there is no such object.
 * \returns Pointer to the object.
 */
uint64_t str_map_get(str_map_t map, const char *key);

/**
 * \returns 0 on success.
 * \returns Error code on error.
 */
int str_map_insert(str_map_t map, const char *key, uint64_t obj);

/**
 * \brief Remove an object (specified by key) from the map.
 * \returns 0 on success.
 * \returns Error code on error.
 */
int str_map_remove(str_map_t map, const char *key);

/**
 * \param map The empty map to be initialized.
 * \param keys The keys
 * \param nkeys The number of keys
 * \param start_id The starting number of the IDs.
 */
int str_map_id_init(str_map_t map, char **keys, int nkeys,
		    uint64_t start_id);

#endif
