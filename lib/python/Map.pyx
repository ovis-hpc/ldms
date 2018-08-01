from __future__ import print_function
import cython
from cpython cimport PyObject, Py_INCREF
import struct
import datetime as dt
cimport Map

cdef class lMap:
    cdef map_t map_s
    cdef char *path
    cdef char *name

    def __init__(self, path, name):
        self.map_s = NULL
        self.path = path
        self.name = name
        #self.open(path, name)
        try:
            self.map_s = map_open(self.path, self.name)
        except:
            raise ValueError("Map does not exist at path {0}".format(self.path))

    # Return target value given source value
    def transform(self, match_val):
        cdef uint64_t result
        cdef int rc = map_transform(self.map_s, match_val, &result)
        if rc:
            raise IndexError("{0} not found".format(match_val))
        return int(result)

    def transform_min(self):
        cdef uint64_t ret
        cdef int rc = map_transform_min(self.map_s, &ret)
        if rc:
            raise RuntimeError("map_transform_min() error, rc: {0}".format(rc))
        return ret

    def transform_max(self):
        cdef uint64_t ret
        cdef int rc = map_transform_max(self.map_s, &ret)
        if rc:
            raise RuntimeError("map_transform_max() error, rc: {0}".format(rc))
        return ret

    def transform_ge(self, val):
        cdef uint64_t ret
        cdef int rc = map_transform_ge(self.map_s, val, &ret)
        if rc:
            raise RuntimeError("map_transform_ge() error, rc: {0}".format(rc))
        return ret

    def transform_le(self, val):
        cdef uint64_t ret
        cdef int rc = map_transform_le(self.map_s, val, &ret)
        if rc:
            raise RuntimeError("map_transform_le() error, rc: {0}".format(rc))
        return ret

    # Return source value given target value
    def inverse(self, match_val):
        cdef uint64_t result
        cdef int rc = map_inverse(self.map_s, match_val, &result)
        if rc:
            raise IndexError("{0} not found".format(match_val))
        return int(result)

    def inverse_min(self):
        cdef uint64_t ret
        cdef int rc = map_inverse_min(self.map_s, &ret)
        if rc:
            raise RuntimeError("map_inverse_min() error, rc: {0}".format(rc))
        return ret

    def inverse_max(self):
        cdef uint64_t ret
        cdef int rc = map_inverse_max(self.map_s, &ret)
        if rc:
            raise RuntimeError("map_inverse_max() error, rc: {0}".format(rc))
        return ret

    def inverse_ge(self, val):
        cdef uint64_t ret
        cdef int rc = map_inverse_ge(self.map_s, val, &ret)
        if rc:
            raise RuntimeError("map_inverse_ge() error, rc: {0}".format(rc))
        return ret

    def inverse_le(self, val):
        cdef uint64_t ret
        cdef int rc = map_inverse_le(self.map_s, val, &ret)
        if rc:
            raise RuntimeError("map_inverse_le() error, rc: {0}".format(rc))
        return ret

    def new_entry(self, obj_cols):
        rc = map_entry_new(self.map_s, obj_cols)
        if rc != 0:
            raise ValueError("New object failed to be added to map with error {0}".format(rc))
        return rc

    def close(self):
        rc = map_close(self.map_s)
        return rc

def get_map_list(path):
    pmap_list = []
    map_list = map_list_get(path)
    #if map_list is NULL:
    #    raise ValueError("No Maps found at {0}".format(path))
    i = 0
    while map_list[i] != NULL:
        pmap_list.append(map_list[i])
        i += 1
    map_list_free(map_list)
    return pmap_list

def new_map_container(path):
    rc = map_container_new(path)
    if rc:
        raise ValueError("Creation of new map container failed with error {0}".format(rc))
    return rc

def new_map(path, name):
    rc = map_new(path, name)
    if rc:
        raise ValueError("New Map cration failed with error {0}".format(rc))
    m = lMap(path, name)
    return m
