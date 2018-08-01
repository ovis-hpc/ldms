#ifndef __MAP_H_
#define __MAP_H_

#include <inttypes.h>

struct map_s;
typedef struct map_s *map_t;

/* Return a list of available maps */
char **map_list_get(const char *path);
void map_list_free(char *map_list[]);
/* create new map container */
int map_container_new(const char *path);
/* create new map in existing container */
int map_new(const char *path, const char *name);
int map_close(map_t map_s);
/* create a new map */
map_t map_open(char *path, char *map_name);
/* Add new object to map */
int map_entry_new(map_t map_s, char *obj_cols);
/* Return inverse value for transform val */
int map_transform(map_t map_s, uint64_t match_val, uint64_t *transformed_val);
/* Return transform val for inverse val */
int map_inverse(map_t map_s, uint64_t match_val, uint64_t *inversed_val);

/* Min transformed value from map entries */
int map_transform_min(map_t map, uint64_t *ret);
/* Max transformed value from map entries */
int map_transform_max(map_t map, uint64_t *ret);
/* Min inverse value from map entries */
int map_inverse_min(map_t map, uint64_t *ret);
/* Max inverse value from map entries */
int map_inverse_max(map_t map, uint64_t *ret);

/**
 * Transform with nearest entry greater than or equal to [src,].
 *
 * \retval 0 If an entry greater than or equal to [src] is found. In this case
 *           \c *dst is set from the associated entry.
 * \retval ENOENT If no more entry can be found.
 */
int map_transform_ge(map_t map, uint64_t src, uint64_t *dst);

/**
 * Transform with nearest entry less than or equal to [src,].
 *
 * \retval 0 If an entry less than or equal to [src] is found. In this case
 *           \c *dst is set from the associated entry.
 * \retval ENOENT If no more entry can be found.
 */
int map_transform_le(map_t map, uint64_t src, uint64_t *dst);

/**
 * Inverse transform with nearest entry greater than or equal to [,dst].
 *
 * \retval 0 If an entry greater than or equal to [dst] is found. In this case
 *           \c *src is set from the associated entry.
 * \retval ENOENT If no more entry can be found.
 */
int map_inverse_ge(map_t map, uint64_t dst, uint64_t *src);

/**
 * Inverse transform with nearest entry less than or equal to [,dst].
 *
 * \retval 0 If an entry less than or equal to [dst] is found. In this case
 *           \c *src is set from the associated entry.
 * \retval ENOENT If no more entry can be found.
 */
int map_inverse_le(map_t map, uint64_t dst, uint64_t *src);
#endif
