/* -*- c-basic-offset: 8 -*- */
/* Copyright 2026 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#ifndef __DECOMP_PARSER_PRIV_H
#define __DECOMP_PARSER_PRIV_H

/*
 * Private interface between the decomposition config parser core
 * (decomp_config_parser.c) and the parser backends (libfyaml, libyaml,
 * jansson).
 *
 * A backend's only job is to load a file and convert it into a generic
 * node tree using the decomp_cfg_*() helpers below.  The core builds the
 * ldmsd_decomp_*_config_t structures from that tree and implements the
 * location API on top of it, so the behavior is identical no matter which
 * library parsed the file.
 */

#include <stddef.h>
#include "ldmsd_decomp_config_parser.h"

typedef enum {
        DECOMP_CFG_NODE_SCALAR,
        DECOMP_CFG_NODE_SEQUENCE,
        DECOMP_CFG_NODE_MAPPING,
} decomp_cfg_node_type_t;

/*
 * A node of the generic config tree.  The public opaque
 * ldmsd_decomp_cfg_node_t handle points to one of these.
 *
 * A node may be referenced from more than one parent (e.g. a YAML alias),
 * so nodes are owned by the loc_tab (via \c next_alloc), not by their
 * parents.
 */
struct decomp_cfg_node_s {
        struct decomp_cfg_node_s *next_alloc;   /* loc_tab ownership list */
        struct decomp_cfg_loc_tab_s *tab;
        decomp_cfg_node_type_t type;
        int line;               /* 1-based, 0 if unknown */
        int column;             /* 1-based, 0 if unknown */
        char *scalar;           /* SCALAR: NUL-terminated value */
        size_t count;           /* SEQUENCE: items, MAPPING: pairs */
        size_t alloc;           /* allocated entries in kids */
        /* SEQUENCE: kids[i] is item i.
         * MAPPING:  kids[2*i] is key i (always a SCALAR), kids[2*i+1]
         *           is value i. */
        struct decomp_cfg_node_s **kids;
};

/* Owns every node of a parsed config tree */
struct decomp_cfg_loc_tab_s {
        char *file;
        struct decomp_cfg_node_s *nodes;        /* allocation list */
};

/**
 * A parser backend
 */
struct decomp_parser_s {
        const char *name;       /* value used in LDMSD_DECOMP_PARSER */
        int yaml;               /* non-zero if it can parse YAML, not just JSON */
        /**
         * Parse \c tab->file and build the node tree with the helpers
         * below.
         *
         * \return The root node, or NULL with a message in \c err_buf.
         *         Nodes created before a failure are freed by the core.
         */
        ldmsd_decomp_cfg_node_t (*load)(struct decomp_cfg_loc_tab_s *tab,
                                        char *err_buf, size_t err_sz);
};

#ifdef DECOMP_PARSER_LIBFYAML
extern const struct decomp_parser_s decomp_parser_libfyaml;
#endif
#ifdef DECOMP_PARSER_LIBYAML
extern const struct decomp_parser_s decomp_parser_libyaml;
#endif
#ifdef DECOMP_PARSER_JANSSON
extern const struct decomp_parser_s decomp_parser_jansson;
#endif

/*
 * Tree building helpers for backends.  All return NULL / an errno and fill
 * \c err_buf on failure.
 */

/** Create a node.  \c line and \c column are 1-based, or 0 if unknown. */
ldmsd_decomp_cfg_node_t decomp_cfg_node_new(struct decomp_cfg_loc_tab_s *tab,
                                            decomp_cfg_node_type_t type,
                                            int line, int column,
                                            char *err_buf, size_t err_sz);

/** Create a SCALAR node with a copy of \c len bytes of \c str */
ldmsd_decomp_cfg_node_t decomp_cfg_scalar_new(struct decomp_cfg_loc_tab_s *tab,
                                              const char *str, size_t len,
                                              int line, int column,
                                              char *err_buf, size_t err_sz);

/** Append \c item to SEQUENCE node \c seq */
int decomp_cfg_seq_append(ldmsd_decomp_cfg_node_t seq,
                          ldmsd_decomp_cfg_node_t item,
                          char *err_buf, size_t err_sz);

/**
 * Append a pair to MAPPING node \c map.  \c key must be a SCALAR.
 *
 * \retval EEXIST  \c map already contains the key
 */
int decomp_cfg_map_append(ldmsd_decomp_cfg_node_t map,
                          ldmsd_decomp_cfg_node_t key,
                          ldmsd_decomp_cfg_node_t value,
                          char *err_buf, size_t err_sz);

#endif /* __DECOMP_PARSER_PRIV_H */
