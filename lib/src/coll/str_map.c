/**
 * \file str_map.c
 * \author Narate Taerat <narate@ogc.us>
 * \brief String-Object mapping utility.
 *
 * This shall be moved to lib later, to share with other projects.
 *
 */

#include "str_map.h"
#include "fnv_hash.h"

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
	struct obj_list_head lh_table[0]; /**< hash table */
};

struct str_map* str_map_create(size_t sz)
{
	struct str_map *m = mmap(NULL, sz*sizeof(uint64_t)+sizeof(*m),
				 PROT_READ|PROT_WRITE,
				 MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (m == MAP_FAILED)
		return NULL;
	m->hash_size = sz;
	return m;
}

void str_map_free(str_map_t m)
{
	int i;
	for (i=0; i<m->hash_size; i++) {
		struct obj_list_head *lh = &m->lh_table[i];
		if (!LIST_FIRST(lh))
			continue;
		struct obj_list *ol;
		while (ol = LIST_FIRST(lh)) {
			LIST_REMOVE(ol, link);
			free(ol->key);
			free(ol);
		}
	}
	munmap(m, m->hash_size*sizeof(uint64_t) + sizeof(*m));
}

uint64_t str_map_get(str_map_t map, const char *key)
{
	uint32_t h = fnv_hash_a1_32(key, strlen(key), 0);
	struct obj_list *ol;
	struct obj_list_head *lh = &map->lh_table[h%map->hash_size];
	LIST_FOREACH(ol, lh, link) {
		if (strcmp(ol->key, key)==0)
			return ol->obj;
	}
	return 0;
}

int str_map_insert(str_map_t map, const char *key, uint64_t obj)
{
	uint32_t h = fnv_hash_a1_32(key, strlen(key), 0);
	struct obj_list *ol;
	struct obj_list_head *lh = &map->lh_table[h%map->hash_size];
	LIST_FOREACH(ol, lh, link) {
		if (strcmp(ol->key, key)==0)
			return EEXIST;
	}
	ol = malloc(sizeof(*ol));
	if (!ol)
		return ENOMEM;
	ol->key = strdup(key);
	if (!ol->key) {
		free(ol);
		return ENOMEM;
	}
	ol->obj = obj;
	LIST_INSERT_HEAD(lh, ol, link);
	return 0;
}

int str_map_remove(str_map_t map, const char *key)
{
	uint32_t h = fnv_hash_a1_32(key, strlen(key), 0);
	struct obj_list *ol;
	struct obj_list_head *lh = &map->lh_table[h%map->hash_size];
	LIST_FOREACH(ol, lh, link) {
		if (strcmp(ol->key, key)==0) {
			LIST_REMOVE(ol, link);
			free(ol->key);
			free(ol);
			return 0;
		}
	}
	return ENOENT;
}

int str_map_id_init(str_map_t map, char **keys, int nkeys,
		    uint64_t start_id)
{
	int i, rc;
	uint64_t id = start_id;
	for (i=0; i<nkeys; i++) {
		rc = str_map_insert(map, keys[i], id);
		if (rc==0)
			id++;
		else
			return rc;
	}
	return 0;
}
