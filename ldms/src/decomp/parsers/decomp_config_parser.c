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

/*
 * Decomposition config parser core.
 *
 * A parser backend (libfyaml, libyaml or jansson) loads the file into a
 * generic node tree (see decomp_parser_priv.h).  This file selects the
 * backend, builds the ldmsd_decomp_*_config_t structures from the tree,
 * and implements the location API.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include "ldmsd_decomp_config_parser.h"
#include "decomp_parser_priv.h"

/* Helper macros for error handling */
#define PARSE_ERROR(buf, sz, fmt, ...) \
        snprintf(buf, sz, fmt, ##__VA_ARGS__)

#define PARSE_ERROR_AT(buf, sz, node, fmt, ...) \
        snprintf(buf, sz, "Line %d, column %d: " fmt, \
                 (node)->line, (node)->column, ##__VA_ARGS__)

/*
 * Parser backends, in order of preference for the default
 */
static const struct decomp_parser_s *parsers[] = {
#ifdef DECOMP_PARSER_LIBFYAML
        &decomp_parser_libfyaml,
#endif
#ifdef DECOMP_PARSER_LIBYAML
        &decomp_parser_libyaml,
#endif
#ifdef DECOMP_PARSER_JANSSON
        &decomp_parser_jansson,
#endif
        NULL
};

#define NUM_PARSERS ((int)(sizeof(parsers) / sizeof(parsers[0])) - 1)

int ldmsd_decomp_parser_count(void)
{
        return NUM_PARSERS;
}

const char *ldmsd_decomp_parser_name(int idx)
{
        if (idx < 0 || idx >= NUM_PARSERS)
                return NULL;
        return parsers[idx]->name;
}

int ldmsd_decomp_parser_supports_yaml(const char *name)
{
        int i;

        for (i = 0; i < NUM_PARSERS; i++) {
                if (strcmp(parsers[i]->name, name) == 0)
                        return parsers[i]->yaml;
        }
        return -1;
}

/* Format the list of built parsers, for error messages */
static void parser_list(char *buf, size_t sz)
{
        size_t off = 0;
        int i;

        buf[0] = '\0';
        for (i = 0; i < NUM_PARSERS && off < sz; i++)
                off += snprintf(buf + off, sz - off, "%s%s",
                                i ? ", " : "", parsers[i]->name);
}

/* Select the parser named by LDMSD_DECOMP_PARSER, or the default */
static const struct decomp_parser_s *parser_select(char *err_buf, size_t err_sz)
{
        const char *name = getenv(LDMSD_DECOMP_PARSER_ENV);
        char list[128];
        int i;

        if (!name || !*name) {
                if (!NUM_PARSERS) {
                        PARSE_ERROR(err_buf, err_sz,
                                    "No decomposition config parser was built");
                        return NULL;
                }
                return parsers[0];
        }
        for (i = 0; i < NUM_PARSERS; i++) {
                if (strcmp(parsers[i]->name, name) == 0)
                        return parsers[i];
        }
        parser_list(list, sizeof(list));
        PARSE_ERROR(err_buf, err_sz,
                    "%s='%s' is not an available parser (available: %s)",
                    LDMSD_DECOMP_PARSER_ENV, name, list);
        return NULL;
}

const char *ldmsd_decomp_parser_selected(void)
{
        char err[256];
        const struct decomp_parser_s *p = parser_select(err, sizeof(err));
        return p ? p->name : NULL;
}

/*
 * Generic node tree
 */

static void loc_tab_free(struct decomp_cfg_loc_tab_s *tab)
{
        struct decomp_cfg_node_s *n, *next;

        if (!tab)
                return;
        for (n = tab->nodes; n; n = next) {
                next = n->next_alloc;
                free(n->scalar);
                free(n->kids);
                free(n);
        }
        free(tab->file);
        free(tab);
}

ldmsd_decomp_cfg_node_t decomp_cfg_node_new(struct decomp_cfg_loc_tab_s *tab,
                                            decomp_cfg_node_type_t type,
                                            int line, int column,
                                            char *err_buf, size_t err_sz)
{
        struct decomp_cfg_node_s *n = calloc(1, sizeof(*n));

        if (!n) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return NULL;
        }
        n->tab = tab;
        n->type = type;
        n->line = line;
        n->column = column;
        n->next_alloc = tab->nodes;
        tab->nodes = n;
        return n;
}

ldmsd_decomp_cfg_node_t decomp_cfg_scalar_new(struct decomp_cfg_loc_tab_s *tab,
                                              const char *str, size_t len,
                                              int line, int column,
                                              char *err_buf, size_t err_sz)
{
        struct decomp_cfg_node_s *n;

        n = decomp_cfg_node_new(tab, DECOMP_CFG_NODE_SCALAR, line, column,
                                err_buf, err_sz);
        if (!n)
                return NULL;
        n->scalar = malloc(len + 1);
        if (!n->scalar) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return NULL;    /* n is freed with the tab */
        }
        memcpy(n->scalar, str, len);
        n->scalar[len] = '\0';
        return n;
}

/* Make room for one more item or pair in \c n */
static int node_grow(ldmsd_decomp_cfg_node_t n, char *err_buf, size_t err_sz)
{
        struct decomp_cfg_node_s **kids;
        size_t need = (n->type == DECOMP_CFG_NODE_MAPPING ? 2 : 1) * (n->count + 1);
        size_t alloc;

        if (need <= n->alloc)
                return 0;
        alloc = n->alloc ? n->alloc * 2 : 8;
        while (alloc < need)
                alloc *= 2;
        kids = realloc(n->kids, alloc * sizeof(*kids));
        if (!kids) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return ENOMEM;
        }
        n->kids = kids;
        n->alloc = alloc;
        return 0;
}

int decomp_cfg_seq_append(ldmsd_decomp_cfg_node_t seq,
                          ldmsd_decomp_cfg_node_t item,
                          char *err_buf, size_t err_sz)
{
        int rc;

        if (seq->type != DECOMP_CFG_NODE_SEQUENCE || !item) {
                PARSE_ERROR(err_buf, err_sz, "Internal error: bad sequence append");
                return EINVAL;
        }
        rc = node_grow(seq, err_buf, err_sz);
        if (rc)
                return rc;
        seq->kids[seq->count++] = item;
        return 0;
}

static ldmsd_decomp_cfg_node_t map_get(ldmsd_decomp_cfg_node_t map, const char *key);

int decomp_cfg_map_append(ldmsd_decomp_cfg_node_t map,
                          ldmsd_decomp_cfg_node_t key,
                          ldmsd_decomp_cfg_node_t value,
                          char *err_buf, size_t err_sz)
{
        int rc;

        if (map->type != DECOMP_CFG_NODE_MAPPING || !key || !value) {
                PARSE_ERROR(err_buf, err_sz, "Internal error: bad mapping append");
                return EINVAL;
        }
        if (key->type != DECOMP_CFG_NODE_SCALAR) {
                PARSE_ERROR_AT(err_buf, err_sz, key, "Mapping keys must be scalars");
                return EINVAL;
        }
        if (map_get(map, key->scalar)) {
                PARSE_ERROR_AT(err_buf, err_sz, key, "Duplicate key '%s'",
                               key->scalar);
                return EEXIST;
        }
        rc = node_grow(map, err_buf, err_sz);
        if (rc)
                return rc;
        map->kids[2 * map->count] = key;
        map->kids[2 * map->count + 1] = value;
        map->count++;
        return 0;
}

/* Tree navigation helpers */

static int seq_count(ldmsd_decomp_cfg_node_t n)
{
        return (n && n->type == DECOMP_CFG_NODE_SEQUENCE) ? (int)n->count : 0;
}

static ldmsd_decomp_cfg_node_t seq_item(ldmsd_decomp_cfg_node_t n, int i)
{
        return n->kids[i];
}

static int map_count(ldmsd_decomp_cfg_node_t n)
{
        return (n && n->type == DECOMP_CFG_NODE_MAPPING) ? (int)n->count : 0;
}

static ldmsd_decomp_cfg_node_t map_key(ldmsd_decomp_cfg_node_t n, int i)
{
        return n->kids[2 * i];
}

static ldmsd_decomp_cfg_node_t map_value(ldmsd_decomp_cfg_node_t n, int i)
{
        return n->kids[2 * i + 1];
}

/**
 * Get scalar value as string from a node
 */
static char *scalar_string(ldmsd_decomp_cfg_node_t node,
                           char *err_buf, size_t err_sz)
{
        if (!node) {
                PARSE_ERROR(err_buf, err_sz, "NULL node");
                return NULL;
        }

        if (node->type != DECOMP_CFG_NODE_SCALAR) {
                PARSE_ERROR_AT(err_buf, err_sz, node,
                              "Expected scalar value, got %d", node->type);
                return NULL;
        }

        char *str = strdup(node->scalar);
        if (!str) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return NULL;
        }

        return str;
}

/**
 * Get scalar value as integer from a node
 */
static int scalar_int(ldmsd_decomp_cfg_node_t node,
                      int *out, char *err_buf, size_t err_sz)
{
        if (!node) {
                PARSE_ERROR(err_buf, err_sz, "NULL node");
                return -1;
        }

        if (node->type != DECOMP_CFG_NODE_SCALAR) {
                PARSE_ERROR_AT(err_buf, err_sz, node,
                              "Expected scalar value for integer");
                return -1;
        }

        char *endptr;
        long val = strtol(node->scalar, &endptr, 10);
        if (*endptr != '\0') {
                PARSE_ERROR_AT(err_buf, err_sz, node,
                              "Invalid integer value: %s", node->scalar);
                return -1;
        }

        *out = (int)val;
        return 0;
}

/**
 * Get a value from a mapping node by key name
 */
static ldmsd_decomp_cfg_node_t map_get(ldmsd_decomp_cfg_node_t map, const char *key)
{
        int i;

        for (i = 0; i < map_count(map); i++) {
                if (strcmp(map_key(map, i)->scalar, key) == 0)
                        return map_value(map, i);
        }
        return NULL;
}

/**
 * Get string value from mapping by key
 */
static char *map_get_string(ldmsd_decomp_cfg_node_t map,
                            const char *key, char *err_buf, size_t err_sz)
{
        ldmsd_decomp_cfg_node_t value = map_get(map, key);
        if (!value)
                return NULL;

        return scalar_string(value, err_buf, err_sz);
}

/**
 * Get integer value from mapping by key
 */
static int map_get_int(ldmsd_decomp_cfg_node_t map,
                       const char *key, int *out, char *err_buf, size_t err_sz)
{
        ldmsd_decomp_cfg_node_t value = map_get(map, key);
        if (!value) {
                *out = 0;
                return 0;  /* Not found, but not an error */
        }

        return scalar_int(value, out, err_buf, err_sz);
}

/* Forward declarations for recursive parsing */
static ldmsd_decomp_static_config_t *parse_static_config(ldmsd_decomp_cfg_node_t root,
                                                         char *err_buf, size_t err_sz);
static ldmsd_decomp_as_is_config_t *parse_as_is_config(ldmsd_decomp_cfg_node_t root,
                                                       char *err_buf, size_t err_sz);
static ldmsd_decomp_flex_config_t *parse_flex_config(ldmsd_decomp_cfg_node_t root,
                                                     char *err_buf, size_t err_sz);

/*
 * Static decomposition parsing
 */

static ldmsd_decomp_static_col_t *parse_static_col(ldmsd_decomp_cfg_node_t col_node,
                                             char *err_buf, size_t err_sz)
{
        ldmsd_decomp_static_col_t *col = calloc(1, sizeof(*col));
        if (!col) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return NULL;
        }

        col->src = map_get_string(col_node, "src", err_buf, err_sz);
        col->dst = map_get_string(col_node, "dst", err_buf, err_sz);
        col->type = map_get_string(col_node, "type", err_buf, err_sz);
        col->rec_member = map_get_string(col_node, "rec_member", err_buf, err_sz);
        col->op = map_get_string(col_node, "op", err_buf, err_sz);
        col->func = map_get_string(col_node, "func", err_buf, err_sz);

        col->node = (col_node);
        map_get_int(col_node, "array_len", &col->array_len, err_buf, err_sz);

        /* Parse fill value if present: a scalar or a sequence of scalars */
        ldmsd_decomp_cfg_node_t fill_node = map_get(col_node, "fill");
        if (fill_node && fill_node->type == DECOMP_CFG_NODE_SCALAR) {
                col->fill = calloc(2, sizeof(char *));
                if (col->fill)
                        col->fill[0] = strdup(fill_node->scalar);
                if (!col->fill || !col->fill[0]) {
                        free(col->fill);
                        col->fill = NULL;
                        PARSE_ERROR(err_buf, err_sz, "Out of memory");
                } else {
                        col->fill_count = 1;
                }
        } else if (fill_node && fill_node->type == DECOMP_CFG_NODE_SEQUENCE) {
                int i, n = seq_count(fill_node);

                for (i = 0; i < n; i++) {
                        if (seq_item(fill_node, i)->type != DECOMP_CFG_NODE_SCALAR)
                                break;
                }
                /* NULL-terminated, so an empty sequence still allocates */
                col->fill = (i == n) ? calloc(n + 1, sizeof(char *)) : NULL;
                if (col->fill) {
                        col->fill_count = n;
                        col->fill_is_array = 1;
                        for (i = 0; i < n; i++) {
                                col->fill[i] = strdup(seq_item(fill_node, i)->scalar);
                                if (!col->fill[i]) {
                                        PARSE_ERROR(err_buf, err_sz, "Out of memory");
                                        col->fill_count = i;
                                        break;
                                }
                        }
                }
        }

        return col;
}

static ldmsd_decomp_static_index_t *parse_static_index(ldmsd_decomp_cfg_node_t idx_node,
                                                 char *err_buf, size_t err_sz)
{
        ldmsd_decomp_static_index_t *idx = calloc(1, sizeof(*idx));
        if (!idx) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return NULL;
        }

        idx->node = (idx_node);
        idx->name = map_get_string(idx_node, "name", err_buf, err_sz);

        ldmsd_decomp_cfg_node_t cols_node = map_get(idx_node, "cols");
        if (cols_node && cols_node->type == DECOMP_CFG_NODE_SEQUENCE) {
                idx->col_count = seq_count(cols_node);
                idx->cols = calloc(idx->col_count, sizeof(char *));
                int i;
                for (i = 0; i < seq_count(cols_node); i++) {
                        ldmsd_decomp_cfg_node_t col_node = seq_item(cols_node, i);
                        idx->cols[i] = scalar_string(col_node, err_buf, err_sz);
                }
        }

        return idx;
}

static ldmsd_decomp_static_group_t *parse_static_group(ldmsd_decomp_cfg_node_t grp_node,
                                                 char *err_buf, size_t err_sz)
{
        if (!grp_node)
                return NULL;

        ldmsd_decomp_static_group_t *grp = calloc(1, sizeof(*grp));
        if (!grp) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return NULL;
        }

        grp->node = (grp_node);

        /* Parse index array */
        ldmsd_decomp_cfg_node_t idx_node = map_get(grp_node, "index");
        if (idx_node && idx_node->type == DECOMP_CFG_NODE_SEQUENCE) {
                grp->index_count = seq_count(idx_node);
                grp->index = calloc(grp->index_count, sizeof(char *));
                int i;
                for (i = 0; i < seq_count(idx_node); i++) {
                        ldmsd_decomp_cfg_node_t val_node = seq_item(idx_node, i);
                        grp->index[i] = scalar_string(val_node, err_buf, err_sz);
                }
        }

        /* Parse order array */
        ldmsd_decomp_cfg_node_t ord_node = map_get(grp_node, "order");
        if (ord_node && ord_node->type == DECOMP_CFG_NODE_SEQUENCE) {
                grp->order_count = seq_count(ord_node);
                grp->order = calloc(grp->order_count, sizeof(char *));
                int i;
                for (i = 0; i < seq_count(ord_node); i++) {
                        ldmsd_decomp_cfg_node_t val_node = seq_item(ord_node, i);
                        grp->order[i] = scalar_string(val_node, err_buf, err_sz);
                }
        }

        map_get_int(grp_node, "limit", &grp->limit, err_buf, err_sz);
        grp->timeout = map_get_string(grp_node, "timeout", err_buf, err_sz);

        return grp;
}

static ldmsd_decomp_static_row_t *parse_static_row(ldmsd_decomp_cfg_node_t row_node,
                                             char *err_buf, size_t err_sz)
{
        ldmsd_decomp_static_row_t *row = calloc(1, sizeof(*row));
        if (!row) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return NULL;
        }

        row->node = (row_node);
        row->schema = map_get_string(row_node, "schema", err_buf, err_sz);

        /* Parse columns */
        ldmsd_decomp_cfg_node_t cols_node = map_get(row_node, "cols");
        if (cols_node && cols_node->type == DECOMP_CFG_NODE_SEQUENCE) {
                row->col_count = seq_count(cols_node);
                row->cols = calloc(row->col_count, sizeof(ldmsd_decomp_static_col_t));
                int i;
                for (i = 0; i < seq_count(cols_node); i++) {
                        ldmsd_decomp_cfg_node_t col_node = seq_item(cols_node, i);
                        ldmsd_decomp_static_col_t *col = parse_static_col(col_node, err_buf, err_sz);
                        if (col) {
                                row->cols[i] = *col;
                                free(col);
                        }
                }
        }

        /* Parse indices */
        ldmsd_decomp_cfg_node_t indices_node = map_get(row_node, "indices");
        if (indices_node && indices_node->type == DECOMP_CFG_NODE_SEQUENCE) {
                row->index_count = seq_count(indices_node);
                row->indices = calloc(row->index_count, sizeof(ldmsd_decomp_static_index_t));
                int i;
                for (i = 0; i < seq_count(indices_node); i++) {
                        ldmsd_decomp_cfg_node_t idx_node = seq_item(indices_node, i);
                        ldmsd_decomp_static_index_t *idx = parse_static_index(idx_node, err_buf, err_sz);
                        if (idx) {
                                row->indices[i] = *idx;
                                free(idx);
                        }
                }
        }

        /* Parse group */
        ldmsd_decomp_cfg_node_t grp_node = map_get(row_node, "group");
        row->group = parse_static_group(grp_node, err_buf, err_sz);

        return row;
}

static ldmsd_decomp_static_config_t *parse_static_config(ldmsd_decomp_cfg_node_t root,
                                                   char *err_buf, size_t err_sz)
{
        ldmsd_decomp_static_config_t *cfg = calloc(1, sizeof(*cfg));
        if (!cfg) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return NULL;
        }

        cfg->node = (root);
        cfg->type = map_get_string(root, "type", err_buf, err_sz);

        /* Parse rows */
        ldmsd_decomp_cfg_node_t rows_node = map_get(root, "rows");
        if (!rows_node || rows_node->type != DECOMP_CFG_NODE_SEQUENCE) {
                PARSE_ERROR(err_buf, err_sz, "Static decomposition requires 'rows' array");
                free(cfg);
                return NULL;
        }

        cfg->row_count = seq_count(rows_node);
        cfg->rows = calloc(cfg->row_count, sizeof(ldmsd_decomp_static_row_t));
        int i;
        for (i = 0; i < seq_count(rows_node); i++) {
                ldmsd_decomp_cfg_node_t row_node = seq_item(rows_node, i);
                ldmsd_decomp_static_row_t *row = parse_static_row(row_node, err_buf, err_sz);
                if (row) {
                        cfg->rows[i] = *row;
                        free(row);
                }
        }

        return cfg;
}

/*
 * As-is decomposition parsing
 */

static ldmsd_decomp_as_is_index_t *parse_as_is_index(ldmsd_decomp_cfg_node_t idx_node,
                                               char *err_buf, size_t err_sz)
{
        ldmsd_decomp_as_is_index_t *idx = calloc(1, sizeof(*idx));
        if (!idx) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return NULL;
        }

        idx->node = (idx_node);
        idx->name = map_get_string(idx_node, "name", err_buf, err_sz);

        ldmsd_decomp_cfg_node_t cols_node = map_get(idx_node, "cols");
        if (cols_node && cols_node->type == DECOMP_CFG_NODE_SEQUENCE) {
                idx->col_count = seq_count(cols_node);
                idx->cols = calloc(idx->col_count, sizeof(char *));
                int i;
                for (i = 0; i < seq_count(cols_node); i++) {
                        ldmsd_decomp_cfg_node_t col_node = seq_item(cols_node, i);
                        idx->cols[i] = scalar_string(col_node, err_buf, err_sz);
                }
        }

        return idx;
}

static ldmsd_decomp_as_is_config_t *parse_as_is_config(ldmsd_decomp_cfg_node_t root,
                                                 char *err_buf, size_t err_sz)
{
        ldmsd_decomp_as_is_config_t *cfg = calloc(1, sizeof(*cfg));
        if (!cfg) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return NULL;
        }

        cfg->node = (root);
        cfg->type = map_get_string(root, "type", err_buf, err_sz);

        /* Parse indices */
        ldmsd_decomp_cfg_node_t indices_node = map_get(root, "indices");
        if (indices_node && indices_node->type == DECOMP_CFG_NODE_SEQUENCE) {
                cfg->index_count = seq_count(indices_node);
                cfg->indices = calloc(cfg->index_count, sizeof(ldmsd_decomp_as_is_index_t));
                int i;
                for (i = 0; i < seq_count(indices_node); i++) {
                        ldmsd_decomp_cfg_node_t idx_node = seq_item(indices_node, i);
                        ldmsd_decomp_as_is_index_t *idx = parse_as_is_index(idx_node, err_buf, err_sz);
                        if (idx) {
                                cfg->indices[i] = *idx;
                                free(idx);
                        }
                }
        }

        return cfg;
}

/*
 * Flex decomposition parsing
 */

static ldmsd_decomp_flex_config_t *parse_flex_config(ldmsd_decomp_cfg_node_t root,
                                               char *err_buf, size_t err_sz)
{
        ldmsd_decomp_flex_config_t *cfg = calloc(1, sizeof(*cfg));
        if (!cfg) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return NULL;
        }

        cfg->node = (root);
        cfg->type = map_get_string(root, "type", err_buf, err_sz);

        /* Parse nested decomposition definitions */
        ldmsd_decomp_cfg_node_t decomp_node = map_get(root, "decomposition");
        if (decomp_node && decomp_node->type == DECOMP_CFG_NODE_MAPPING) {
                cfg->decomp_count = map_count(decomp_node);
                cfg->decompositions = calloc(cfg->decomp_count, sizeof(ldmsd_decomp_flex_decomp_ref_t));
                int i;
                for (i = 0; i < map_count(decomp_node); i++) {
                        ldmsd_decomp_cfg_node_t key_node = map_key(decomp_node, i);
                        ldmsd_decomp_cfg_node_t val_node = map_value(decomp_node, i);

                        cfg->decompositions[i].name = scalar_string(key_node, err_buf, err_sz);
                        cfg->decompositions[i].node = (val_node);

                        /* Parse nested decomposition by type */
                        char *nested_type = map_get_string(val_node, "type", err_buf, err_sz);
                        if (nested_type) {
                                if (strcmp(nested_type, "static") == 0) {
                                        cfg->decompositions[i].decomp_type = LDMSD_DECOMP_CFG_TYPE_STATIC;
                                        cfg->decompositions[i].decomp_config = parse_static_config(val_node, err_buf, err_sz);
                                } else if (strcmp(nested_type, "as_is") == 0) {
                                        cfg->decompositions[i].decomp_type = LDMSD_DECOMP_CFG_TYPE_AS_IS;
                                        cfg->decompositions[i].decomp_config = parse_as_is_config(val_node, err_buf, err_sz);
                                }
                                free(nested_type);
                        }
                }
        }

        /* Parse digest mapping */
        ldmsd_decomp_cfg_node_t digest_node = map_get(root, "digest");
        if (digest_node && digest_node->type == DECOMP_CFG_NODE_MAPPING) {
                cfg->digest_count = map_count(digest_node);
                cfg->digest_map = calloc(cfg->digest_count, sizeof(*cfg->digest_map));
                int i;
                for (i = 0; i < map_count(digest_node); i++) {
                        ldmsd_decomp_cfg_node_t key_node = map_key(digest_node, i);
                        ldmsd_decomp_cfg_node_t val_node = map_value(digest_node, i);

                        cfg->digest_map[i].digest = scalar_string(key_node, err_buf, err_sz);
                        cfg->digest_map[i].name = scalar_string(val_node, err_buf, err_sz);
                        cfg->digest_map[i].digest_node = (key_node);
                        cfg->digest_map[i].name_node = (val_node);
                }
        }

        /* Parse matches */
        ldmsd_decomp_cfg_node_t matches_node = map_get(root, "matches");
        if (matches_node && matches_node->type == DECOMP_CFG_NODE_SEQUENCE) {
                cfg->match_count = seq_count(matches_node);
                cfg->matches = calloc(cfg->match_count, sizeof(ldmsd_decomp_flex_match_t));
                int i;
                for (i = 0; i < seq_count(matches_node); i++) {
                        ldmsd_decomp_cfg_node_t match_node = seq_item(matches_node, i);

                        cfg->matches[i].node = (match_node);
                        cfg->matches[i].schema = map_get_string(match_node, "schema", err_buf, err_sz);
                        cfg->matches[i].instance = map_get_string(match_node, "instance", err_buf, err_sz);

                        ldmsd_decomp_cfg_node_t apply_node = map_get(match_node, "apply");
                        if (apply_node) {
                                if (apply_node->type == DECOMP_CFG_NODE_SCALAR) {
                                        cfg->matches[i].apply_count = 1;
                                        cfg->matches[i].apply = calloc(1, sizeof(char *));
                                        cfg->matches[i].apply[0] = scalar_string(apply_node, err_buf, err_sz);
                                } else if (apply_node->type == DECOMP_CFG_NODE_SEQUENCE) {
                                        cfg->matches[i].apply_count = seq_count(apply_node);
                                        cfg->matches[i].apply = calloc(cfg->matches[i].apply_count, sizeof(char *));
                                        int j;
                                        for (j = 0; j < seq_count(apply_node); j++) {
                                                ldmsd_decomp_cfg_node_t apply_val = seq_item(apply_node, j);
                                                cfg->matches[i].apply[j] = scalar_string(apply_val, err_buf, err_sz);
                                        }
                                }
                        }
                }
        }

        /* Parse default */
        cfg->default_decomp = map_get_string(root, "default", err_buf, err_sz);

        return cfg;
}

/*
 * Public API implementation
 */

const char *ldmsd_decomp_cfg_type_str(ldmsd_decomp_cfg_type_t type)
{
        switch (type) {
        case LDMSD_DECOMP_CFG_TYPE_STATIC:
                return "static";
        case LDMSD_DECOMP_CFG_TYPE_AS_IS:
                return "as_is";
        case LDMSD_DECOMP_CFG_TYPE_FLEX:
                return "flex";
        default:
                return "unknown";
        }
}

static void free_static_config(ldmsd_decomp_static_config_t *scfg);
static void free_as_is_config(ldmsd_decomp_as_is_config_t *acfg);
static void free_flex_config(ldmsd_decomp_flex_config_t *fcfg);

ldmsd_decomp_config_t *ldmsd_decomp_config_parse_file(const char *path, char *err_buf, size_t err_sz)
{
        const struct decomp_parser_s *parser;
        struct decomp_cfg_loc_tab_s *tab = NULL;
        ldmsd_decomp_cfg_node_t root;
        ldmsd_decomp_config_t *cfg = NULL;
        char *type_str = NULL;

        parser = parser_select(err_buf, err_sz);
        if (!parser)
                return NULL;

        tab = calloc(1, sizeof(*tab));
        if (!tab || !(tab->file = strdup(path))) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                goto err;
        }

        /* Load the file into the generic node tree */
        root = parser->load(tab, err_buf, err_sz);
        if (!root)
                goto err;

        if (root->type != DECOMP_CFG_NODE_MAPPING) {
                PARSE_ERROR(err_buf, err_sz, "Root node must be a mapping");
                goto err;
        }

        /* Get type field to determine decomposition type */
        type_str = map_get_string(root, "type", err_buf, err_sz);
        if (!type_str) {
                PARSE_ERROR(err_buf, err_sz, "Missing required 'type' field");
                goto err;
        }

        /* Allocate config structure */
        cfg = calloc(1, sizeof(*cfg));
        if (!cfg) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                goto err;
        }
        cfg->node = root;

        /* Parse based on type */
        if (strcmp(type_str, "static") == 0) {
                cfg->type = LDMSD_DECOMP_CFG_TYPE_STATIC;
                cfg->static_cfg = parse_static_config(root, err_buf, err_sz);
                if (!cfg->static_cfg)
                        goto err;
        } else if (strcmp(type_str, "as_is") == 0) {
                cfg->type = LDMSD_DECOMP_CFG_TYPE_AS_IS;
                cfg->as_is_cfg = parse_as_is_config(root, err_buf, err_sz);
                if (!cfg->as_is_cfg)
                        goto err;
        } else if (strcmp(type_str, "flex") == 0) {
                cfg->type = LDMSD_DECOMP_CFG_TYPE_FLEX;
                cfg->flex_cfg = parse_flex_config(root, err_buf, err_sz);
                if (!cfg->flex_cfg)
                        goto err;
        } else {
                PARSE_ERROR(err_buf, err_sz, "Unknown decomposition type: %s", type_str);
                goto err;
        }

        /* The config now owns the node tree */
        cfg->loc_tab = tab;
        tab = NULL;
        goto out;

err:
        free(cfg);
        cfg = NULL;
out:
        free(type_str);
        loc_tab_free(tab);
        return cfg;
}

/*
 * Location API
 */

ldmsd_decomp_cfg_node_t ldmsd_decomp_cfg_node_key(ldmsd_decomp_cfg_node_t node,
                                                  const char *key)
{
        if (!node || !key)
                return NULL;
        return map_get(node, key);
}

ldmsd_decomp_cfg_node_t ldmsd_decomp_cfg_node_item(ldmsd_decomp_cfg_node_t node,
                                                   int idx)
{
        if (!node || idx < 0 || idx >= seq_count(node))
                return NULL;
        return seq_item(node, idx);
}

int ldmsd_decomp_cfg_loc(ldmsd_decomp_cfg_node_t node, const char *key,
                         ldmsd_decomp_cfg_loc_t *loc)
{
        ldmsd_decomp_cfg_node_t target;
        int rc = 0;

        if (!loc)
                return EINVAL;
        if (!node) {
                memset(loc, 0, sizeof(*loc));
                return EINVAL;
        }

        target = node;
        if (key) {
                target = ldmsd_decomp_cfg_node_key(node, key);
                if (!target) {
                        target = node;
                        rc = ENOENT;
                }
        }

        loc->file = target->tab->file;
        loc->line = target->line;
        loc->column = target->column;
        return rc;
}

const char *ldmsd_decomp_cfg_loc_str(ldmsd_decomp_cfg_node_t node,
                                     const char *key,
                                     char *buf, size_t sz)
{
        ldmsd_decomp_cfg_loc_t loc;

        if (!buf || !sz)
                return buf;
        if (ldmsd_decomp_cfg_loc(node, key, &loc) == EINVAL)
                snprintf(buf, sz, "<unknown location>");
        else if (loc.line <= 0)
                snprintf(buf, sz, "%s", loc.file);
        else
                snprintf(buf, sz, "%s:%d:%d", loc.file, loc.line, loc.column);
        return buf;
}

/*
 * Helper function to free a static config structure
 */
static void free_static_config(ldmsd_decomp_static_config_t *scfg)
{
        if (!scfg)
                return;

        free(scfg->type);
        for (int i = 0; i < scfg->row_count; i++) {
                ldmsd_decomp_static_row_t *row = &scfg->rows[i];
                free(row->schema);

                /* Free columns */
                for (int j = 0; j < row->col_count; j++) {
                        free(row->cols[j].src);
                        free(row->cols[j].dst);
                        free(row->cols[j].type);
                        free(row->cols[j].rec_member);
                        free(row->cols[j].op);
                        free(row->cols[j].func);
                        if (row->cols[j].fill) {
                                for (int k = 0; k < row->cols[j].fill_count; k++)
                                        free(row->cols[j].fill[k]);
                                free(row->cols[j].fill);
                        }
                }
                free(row->cols);

                /* Free indices */
                for (int j = 0; j < row->index_count; j++) {
                        free(row->indices[j].name);
                        for (int k = 0; k < row->indices[j].col_count; k++)
                                free(row->indices[j].cols[k]);
                        free(row->indices[j].cols);
                }
                free(row->indices);

                /* Free group if present */
                if (row->group) {
                        for (int j = 0; j < row->group->index_count; j++)
                                free(row->group->index[j]);
                        free(row->group->index);
                        for (int j = 0; j < row->group->order_count; j++)
                                free(row->group->order[j]);
                        free(row->group->order);
                        free(row->group->timeout);
                        free(row->group);
                }
        }
        free(scfg->rows);
        free(scfg);
}

/*
 * Helper function to free an as_is config structure
 */
static void free_as_is_config(ldmsd_decomp_as_is_config_t *acfg)
{
        if (!acfg)
                return;

        free(acfg->type);
        for (int i = 0; i < acfg->index_count; i++) {
                free(acfg->indices[i].name);
                for (int j = 0; j < acfg->indices[i].col_count; j++)
                        free(acfg->indices[i].cols[j]);
                free(acfg->indices[i].cols);
        }
        free(acfg->indices);
        free(acfg);
}

/*
 * Helper function to free a flex config structure
 */
static void free_flex_config(ldmsd_decomp_flex_config_t *fcfg)
{
        if (!fcfg)
                return;

        free(fcfg->type);

        /* Free nested decompositions */
        for (int i = 0; i < fcfg->decomp_count; i++) {
                free(fcfg->decompositions[i].name);

                /* Free nested config based on type */
                if (fcfg->decompositions[i].decomp_config) {
                        ldmsd_decomp_cfg_type_t type = fcfg->decompositions[i].decomp_type;
                        void *nested = fcfg->decompositions[i].decomp_config;

                        if (type == LDMSD_DECOMP_CFG_TYPE_STATIC)
                                free_static_config((ldmsd_decomp_static_config_t *)nested);
                        else if (type == LDMSD_DECOMP_CFG_TYPE_AS_IS)
                                free_as_is_config((ldmsd_decomp_as_is_config_t *)nested);
                }
        }
        free(fcfg->decompositions);

        /* Free digest map */
        for (int i = 0; i < fcfg->digest_count; i++) {
                free(fcfg->digest_map[i].digest);
                free(fcfg->digest_map[i].name);
        }
        free(fcfg->digest_map);

        /* Free matches */
        for (int i = 0; i < fcfg->match_count; i++) {
                free(fcfg->matches[i].schema);
                free(fcfg->matches[i].instance);
                for (int j = 0; j < fcfg->matches[i].apply_count; j++)
                        free(fcfg->matches[i].apply[j]);
                free(fcfg->matches[i].apply);
        }
        free(fcfg->matches);

        free(fcfg->default_decomp);
        free(fcfg);
}

/*
 * Free a decomposition config structure
 */
void ldmsd_decomp_config_free(ldmsd_decomp_config_t *cfg)
{
        if (!cfg)
                return;

        switch (cfg->type) {
        case LDMSD_DECOMP_CFG_TYPE_STATIC:
                free_static_config(cfg->static_cfg);
                break;
        case LDMSD_DECOMP_CFG_TYPE_AS_IS:
                free_as_is_config(cfg->as_is_cfg);
                break;
        case LDMSD_DECOMP_CFG_TYPE_FLEX:
                free_flex_config(cfg->flex_cfg);
                break;
        }

        loc_tab_free(cfg->loc_tab);
        free(cfg);
}
