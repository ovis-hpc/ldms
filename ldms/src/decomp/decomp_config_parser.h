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
#ifndef __DECOMP_CONFIG_PARSER_H
#define __DECOMP_CONFIG_PARSER_H

#include <stddef.h>

/**
 * \file decomp_config_parser.h
 * \brief Centralized parser for decomposition configuration files (JSON/YAML)
 *
 * This module provides a unified interface for parsing decomposition
 * configuration files in either JSON or YAML format (YAML is a superset
 * of JSON, so libyaml handles both).
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
        void *fill;             /**< Fill value if metric not found (optional) */
        int fill_len;           /**< Length of fill value */
} ldmsd_decomp_static_col_t;

/**
 * Index definition for static decomposition
 */
typedef struct decomp_static_index_s {
        char *name;             /**< Index name */
        char **cols;            /**< Array of column name strings */
        int col_count;          /**< Number of columns in index */
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
} ldmsd_decomp_static_row_t;

/**
 * Static decomposition configuration
 */
struct decomp_static_config_s {
        char *type;                     /**< "static" */
        ldmsd_decomp_static_row_t *rows;      /**< Array of row definitions */
        int row_count;                  /**< Number of rows */
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
} ldmsd_decomp_as_is_index_t;

/**
 * As-is decomposition configuration
 */
struct decomp_as_is_config_s {
        char *type;                     /**< "as_is" */
        ldmsd_decomp_as_is_index_t *indices;  /**< Array of index definitions */
        int index_count;                /**< Number of indices */
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
} ldmsd_decomp_flex_decomp_ref_t;

/**
 * Match rule for flex decomposition (regex-based)
 */
typedef struct decomp_flex_match_s {
        char *schema;                   /**< Schema name regex pattern */
        char *instance;                 /**< Instance name regex pattern */
        char **apply;                   /**< Array of decomposition names to apply */
        int apply_count;                /**< Number of decompositions to apply */
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
        } *digest_map;
        int digest_count;

        /* Regex-based matches */
        ldmsd_decomp_flex_match_t *matches;
        int match_count;

        /* Default decomposition */
        char *default_decomp;                   /**< Default decomposition name (optional) */
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
} ldmsd_decomp_config_t;

/*
 * Parser API
 */

/**
 * Parse a decomposition configuration file (JSON or YAML format).
 *
 * This function automatically handles both JSON and YAML files since
 * libyaml can parse JSON natively (YAML is a superset of JSON).
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

#endif /* __DECOMP_CONFIG_PARSER_H */
