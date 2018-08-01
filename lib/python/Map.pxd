import cython
from libc.stdint cimport *
from libc.string cimport *

cdef extern from "coll/map.h":
    cdef struct map_s:
        pass
    ctypedef map_s *map_t

    char **map_list_get(const char *path)
    void map_list_free(char *map_list[])
    int map_container_new(const char *path)
    int map_new(const char *path, const char *name)
    int map_close(map_t map_s)
    map_t map_open(char *path, char *map_name)
    int map_entry_new(map_t map_s, char *obj_cols)
    uint64_t map_transform(map_t map_s, uint64_t match_val, uint64_t *result)
    uint64_t map_inverse(map_t map_s, uint64_t match_val, uint64_t *result)
    int map_transform_min(map_t _map, uint64_t *ret);
    int map_transform_max(map_t _map, uint64_t *ret);
    int map_inverse_min(map_t _map, uint64_t *ret);
    int map_inverse_max(map_t _map, uint64_t *ret);
    int map_transform_ge(map_t map, uint64_t src, uint64_t *dst);
    int map_transform_le(map_t map, uint64_t src, uint64_t *dst);
    int map_inverse_ge(map_t map, uint64_t dst, uint64_t *src);
    int map_inverse_le(map_t map, uint64_t dst, uint64_t *src);
