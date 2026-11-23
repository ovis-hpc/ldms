/* -*- c-basic-offset: 8 -*- */
/* Copyright (c) 2022 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2022 Open Grid Computing, Inc. All rights reserved.
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#ifndef __LDMSD_DECOMP_CONFIG_PARSER_H
#define __LDMSD_DECOMP_CONFIG_PARSER_H

#include <stddef.h>

/**
 * \file ldmsd_decomp_config_parser.h
 * \brief Centralized parser for decomposition configuration files (JSON/YAML)
 *
 * This module provides a unified interface for parsing decomposition
 * configuration files in either JSON or YAML format.
 *
 * Several parser backends may be built in (see ldmsd_decomp_parser_name()).
 * By default the first available of libfyaml, libyaml and jansson is used;
 * the LDMSD_DECOMP_PARSER environment variable selects a specific one.
 * The libfyaml and libyaml parsers accept YAML and JSON.  The jansson
 * parser accepts only JSON and does not record source locations for
 * parsed values.
 *
 * The parser converts configuration files into well-typed C structures
 * that decomposition modules can consume directly, eliminating the need
 * for each module to implement its own parsing logic.
 */

/* Decomposition configuration types */
typedef enum {
        LDMSD_DECOMP_CFG_TYPE_STATIC,
        LDMSD_DECOMP_CFG_TYPE_AS_IS,
        LDMSD_DECOMP_CFG_TYPE_FLEX,
} ldmsd_decomp_cfg_type_t;

/* Forward declarations */
typedef struct decomp_static_config_s ldmsd_decomp_static_config_t;
typedef struct decomp_as_is_config_s ldmsd_decomp_as_is_config_t;
typedef struct decomp_flex_config_s ldmsd_decomp_flex_config_t;

/*
 * Source location tracking
 *
 * Every parsed structure below carries a \c node handle that refers to the
 * YAML/JSON node it was parsed from.  Decomposition code can pass that
 * handle (and optionally a key name) to ldmsd_decomp_cfg_loc() or
 * ldmsd_decomp_cfg_loc_str() to find out where in the configuration file a
 * value came from, so that errors detected while configuring a
 * decomposition can be reported as "file:line:column".
 *
 * Node handles are owned by the ldmsd_decomp_config_t they came from and
 * are invalid after ldmsd_decomp_config_free().  ldmsd keeps the parsed
 * config alive (in strgp->decomp_cfg) until the storage policy is deleted,
 * so a decomposition may save node handles in its own state during
 * config() and use them for error reporting at decompose time.
 */

/** Opaque handle to the location information of a parsed config node */
typedef struct decomp_cfg_node_s *ldmsd_decomp_cfg_node_t;

/** A location in a decomposition configuration file */
typedef struct decomp_cfg_loc_s {
        const char *file;       /**< Config file path (valid while the config is alive) */
        int line;               /**< 1-based line number, 0 if unknown */
        int column;             /**< 1-based column number, 0 if unknown */
} ldmsd_decomp_cfg_loc_t;

/*
 * Static Decomposition Structures
 */

/**
 * Column definition for static decomposition
 */
typedef struct decomp_static_col_s {
        char *src;              /**< Source metric name */
        char *dst;              /**< Destination column name (optional) */
        char *type;             /**< LDMS type string (e.g., "u64", "char_array") */
        int array_len;          /**< Array length for char_array types (0 if not array) */
        char *rec_member;       /**< Record member name for list access (optional) */
        char *op;               /**< Operation: "diff", "min", "max", etc. (optional) */
        char *func;             /**< Built-in function applied to the src value, e.g. "to_str" (optional) */
        /**
         * Fill value used if the metric is not found (optional), as
         * strings exactly as written in the config file.  A scalar fill
         * gives one string; a sequence of scalars gives one string per
         * element.  NULL if "fill" is absent or is not a scalar or a
         * sequence of scalars.  The decomposition converts the strings
         * to the column's \c type.
         */
        char **fill;
        int fill_count;         /**< Number of strings in \c fill */
        int fill_is_array;      /**< Non-zero if fill was a sequence */
        ldmsd_decomp_cfg_node_t node;   /**< Source location of this column */
} ldmsd_decomp_static_col_t;

/**
 * Index definition for static decomposition
 */
typedef struct decomp_static_index_s {
        char *name;             /**< Index name */
        char **cols;            /**< Array of column name strings */
        int col_count;          /**< Number of columns in index */
        ldmsd_decomp_cfg_node_t node;   /**< Source location of this index */
} ldmsd_decomp_static_index_t;

/**
 * Group configuration for static decomposition (required when using operations)
 */
typedef struct decomp_static_group_s {
        char **index;           /**< Array of column names defining grouping */
        int index_count;        /**< Number of index columns */
        char **order;           /**< Array of column names for ordering */
        int order_count;        /**< Number of order columns */
        int limit;              /**< Maximum rows to cache per group (0 = unlimited) */
        char *timeout;          /**< Inactivity timeout string (e.g., "60s") */
        ldmsd_decomp_cfg_node_t node;   /**< Source location of this group */
} ldmsd_decomp_static_group_t;

/**
 * Row definition for static decomposition
 */
typedef struct decomp_static_row_s {
        char *schema;                   /**< Output schema name */
        ldmsd_decomp_static_col_t *cols;      /**< Array of column definitions */
        int col_count;                  /**< Number of columns */
        ldmsd_decomp_static_index_t *indices; /**< Array of index definitions */
        int index_count;                /**< Number of indices */
        ldmsd_decomp_static_group_t *group;   /**< Group configuration (optional) */
        ldmsd_decomp_cfg_node_t node;   /**< Source location of this row */
} ldmsd_decomp_static_row_t;

/**
 * Static decomposition configuration
 */
struct decomp_static_config_s {
        char *type;                     /**< "static" */
        ldmsd_decomp_static_row_t *rows;      /**< Array of row definitions */
        int row_count;                  /**< Number of rows */
        ldmsd_decomp_cfg_node_t node;   /**< Source location of this config */
};

/*
 * As-Is Decomposition Structures
 */

/**
 * Index definition for as-is decomposition
 */
typedef struct decomp_as_is_index_s {
        char *name;             /**< Index name */
        char **cols;            /**< Array of column name strings */
        int col_count;          /**< Number of columns */
        ldmsd_decomp_cfg_node_t node;   /**< Source location of this index */
} ldmsd_decomp_as_is_index_t;

/**
 * As-is decomposition configuration
 */
struct decomp_as_is_config_s {
        char *type;                     /**< "as_is" */
        ldmsd_decomp_as_is_index_t *indices;  /**< Array of index definitions */
        int index_count;                /**< Number of indices */
        ldmsd_decomp_cfg_node_t node;   /**< Source location of this config */
};

/*
 * Flex Decomposition Structures
 */

/**
 * Reference to a nested decomposition in flex config
 */
typedef struct decomp_flex_decomp_ref_s {
        char *name;                     /**< Decomposition name/key */
        void *decomp_config;            /**< Pointer to nested config (static or as_is) */
        ldmsd_decomp_cfg_type_t decomp_type;  /**< Type of nested decomposition */
        ldmsd_decomp_cfg_node_t node;         /**< Source location of the nested decomposition */
} ldmsd_decomp_flex_decomp_ref_t;

/**
 * Match rule for flex decomposition (regex-based)
 */
typedef struct decomp_flex_match_s {
        char *schema;                   /**< Schema name regex pattern */
        char *instance;                 /**< Instance name regex pattern */
        char **apply;                   /**< Array of decomposition names to apply */
        int apply_count;                /**< Number of decompositions to apply */
        ldmsd_decomp_cfg_node_t node;   /**< Source location of this match rule */
} ldmsd_decomp_flex_match_t;

/**
 * Flex decomposition configuration
 */
struct decomp_flex_config_s {
        char *type;                             /**< "flex" */

        /* Nested decomposition definitions */
        ldmsd_decomp_flex_decomp_ref_t *decompositions;
        int decomp_count;

        /* Digest-based mapping: schema digest -> decomposition name */
        struct {
                char *digest;                   /**< Schema digest string */
                char *name;                     /**< Decomposition name */
                ldmsd_decomp_cfg_node_t digest_node;    /**< Location of the digest key */
                ldmsd_decomp_cfg_node_t name_node;      /**< Location of the name value */
        } *digest_map;
        int digest_count;

        /* Regex-based matches */
        ldmsd_decomp_flex_match_t *matches;
        int match_count;

        /* Default decomposition */
        char *default_decomp;                   /**< Default decomposition name (optional) */
        ldmsd_decomp_cfg_node_t node;           /**< Source location of this config */
};

/*
 * Union type for all decomposition configurations
 */

/**
 * Generic decomposition configuration container
 */
typedef struct decomp_config_s {
        ldmsd_decomp_cfg_type_t type;         /**< Configuration type */
        union {
                ldmsd_decomp_static_config_t *static_cfg;
                ldmsd_decomp_as_is_config_t *as_is_cfg;
                ldmsd_decomp_flex_config_t *flex_cfg;
        };
        ldmsd_decomp_cfg_node_t node;         /**< Source location of the root node */
        struct decomp_cfg_loc_tab_s *loc_tab; /**< Private: location table owning all nodes */
} ldmsd_decomp_config_t;

/*
 * Parser API
 */

/** Environment variable that selects the parser backend by name */
#define LDMSD_DECOMP_PARSER_ENV "LDMSD_DECOMP_PARSER"

/**
 * Number of parser backends built in.
 */
int ldmsd_decomp_parser_count(void);

/**
 * Name of parser backend \c idx, in order of default preference
 * ("libfyaml", "libyaml", "jansson").
 *
 * \return The name, or NULL if \c idx is out of range.
 */
const char *ldmsd_decomp_parser_name(int idx);

/**
 * Whether the named parser backend accepts YAML (and not only JSON).
 *
 * \return 1 if it does, 0 if it does not, -1 if \c name is not built in.
 */
int ldmsd_decomp_parser_supports_yaml(const char *name);

/**
 * Name of the parser backend ldmsd_decomp_config_parse_file() will use,
 * given the current value of LDMSD_DECOMP_PARSER.
 *
 * \return The name, or NULL if LDMSD_DECOMP_PARSER names a parser that
 *         is not built in.
 */
const char *ldmsd_decomp_parser_selected(void);

/**
 * Parse a decomposition configuration file (JSON or YAML format).
 *
 * The parser backend is chosen as described at the top of this file.  If
 * LDMSD_DECOMP_PARSER names a parser that is not built in, parsing fails.
 *
 * The caller is responsible for freeing the returned configuration
 * using ldmsd_decomp_config_free().
 *
 * \param path      Path to configuration file
 * \param err_buf   Buffer for error messages
 * \param err_sz    Size of error buffer
 *
 * \return Parsed configuration structure on success, NULL on error.
 *         On error, err_buf contains a detailed error message.
 */
ldmsd_decomp_config_t *ldmsd_decomp_config_parse_file(const char *path,
                                          char *err_buf,
                                          size_t err_sz);

/**
 * Free a parsed decomposition configuration.
 *
 * This function recursively frees all memory allocated for the
 * configuration structure, including nested decompositions in
 * flex configs.
 *
 * \param cfg  Configuration to free (NULL is safe)
 */
void ldmsd_decomp_config_free(ldmsd_decomp_config_t *cfg);

/**
 * Get string representation of decomposition type.
 *
 * \param type  Decomposition type enum
 * \return String name ("static", "as_is", "flex", or "unknown")
 */
const char *ldmsd_decomp_cfg_type_str(ldmsd_decomp_cfg_type_t type);

/*
 * Location API
 */

/**
 * Get the value node of \c key in a mapping node.
 *
 * \param node  A mapping node (e.g. \c col->node)
 * \param key   Key name
 * \return The value node, or NULL if \c node is NULL, is not a mapping,
 *         or does not contain \c key.
 */
ldmsd_decomp_cfg_node_t ldmsd_decomp_cfg_node_key(ldmsd_decomp_cfg_node_t node,
                                                  const char *key);

/**
 * Get item \c idx of a sequence node.
 *
 * Example: location of the 3rd column name in an index:
 *     ldmsd_decomp_cfg_node_item(ldmsd_decomp_cfg_node_key(idx->node, "cols"), 2)
 *
 * \return The item node, or NULL if \c node is NULL, is not a sequence,
 *         or \c idx is out of range.
 */
ldmsd_decomp_cfg_node_t ldmsd_decomp_cfg_node_item(ldmsd_decomp_cfg_node_t node,
                                                   int idx);

/**
 * Get the location of a node, or of the value of \c key within it.
 *
 * If \c key is NULL, the location of \c node itself is returned.  If
 * \c key is given but not present in \c node (e.g. a required attribute
 * is missing), \c loc is filled with the location of \c node itself and
 * ENOENT is returned, so the caller can still point the user at the
 * enclosing object.
 *
 * \param node  Node handle from a parsed structure (may be NULL)
 * \param key   Optional key name within a mapping node
 * \param loc   Output location
 *
 * \retval 0       \c loc describes the requested node
 * \retval ENOENT  \c key was not found; \c loc describes \c node
 * \retval EINVAL  \c node or \c loc is NULL; \c loc (if given) is zeroed
 */
int ldmsd_decomp_cfg_loc(ldmsd_decomp_cfg_node_t node, const char *key,
                         ldmsd_decomp_cfg_loc_t *loc);

/**
 * Format the location of a node (or of \c key within it) as
 * "file:line:column" for use in error messages.
 *
 * Follows the same fallback rules as ldmsd_decomp_cfg_loc().  If \c node
 * is NULL, "<unknown location>" is written.  If the parser does not
 * record line numbers (jansson), only the file name is written.
 *
 * \return \c buf
 */
const char *ldmsd_decomp_cfg_loc_str(ldmsd_decomp_cfg_node_t node,
                                     const char *key,
                                     char *buf, size_t sz);

/** Buffer size used by LDMSD_DECOMP_CFG_LOC() */
#define LDMSD_DECOMP_CFG_LOC_STR_MAX 512

/**
 * Convenience wrapper around ldmsd_decomp_cfg_loc_str() for use directly as
 * a printf-style argument, e.g.
 *
 *     ovis_log(log, OVIS_LERROR, "%s: column['src'] is required\n",
 *              LDMSD_DECOMP_CFG_LOC(col->node, "src"));
 *
 * The string lives in a compound literal, so it is only valid until the
 * end of the enclosing block.
 */
#define LDMSD_DECOMP_CFG_LOC(node, key) \
        ldmsd_decomp_cfg_loc_str((node), (key), \
                                 (char[LDMSD_DECOMP_CFG_LOC_STR_MAX]){0}, \
                                 LDMSD_DECOMP_CFG_LOC_STR_MAX)

#endif /* __LDMSD_DECOMP_CONFIG_PARSER_H */
