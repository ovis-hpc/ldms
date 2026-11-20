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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <yaml.h>
#include "decomp_config_parser.h"

/* Helper macros for error handling */
#define PARSE_ERROR(buf, sz, fmt, ...) \
        snprintf(buf, sz, fmt, ##__VA_ARGS__)

#define PARSE_ERROR_AT(buf, sz, node, fmt, ...) \
        snprintf(buf, sz, "Line %zu: " fmt, \
                 node->start_mark.line + 1, ##__VA_ARGS__)

/* Helper functions for YAML navigation */

/**
 * Get scalar value as string from a YAML node
 */
static char *yaml_node_get_scalar_string(yaml_document_t *doc, yaml_node_t *node,
                                         char *err_buf, size_t err_sz)
{
        if (!node) {
                PARSE_ERROR(err_buf, err_sz, "NULL node");
                return NULL;
        }

        if (node->type != YAML_SCALAR_NODE) {
                PARSE_ERROR_AT(err_buf, err_sz, node,
                              "Expected scalar value, got %d", node->type);
                return NULL;
        }

        char *str = strdup((char *)node->data.scalar.value);
        if (!str) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return NULL;
        }

        return str;
}

/**
 * Get scalar value as integer from a YAML node
 */
static int yaml_node_get_scalar_int(yaml_document_t *doc, yaml_node_t *node,
                                    int *out, char *err_buf, size_t err_sz)
{
        if (!node) {
                PARSE_ERROR(err_buf, err_sz, "NULL node");
                return -1;
        }

        if (node->type != YAML_SCALAR_NODE) {
                PARSE_ERROR_AT(err_buf, err_sz, node,
                              "Expected scalar value for integer");
                return -1;
        }

        char *endptr;
        long val = strtol((char *)node->data.scalar.value, &endptr, 10);
        if (*endptr != '\0') {
                PARSE_ERROR_AT(err_buf, err_sz, node,
                              "Invalid integer value: %s", node->data.scalar.value);
                return -1;
        }

        *out = (int)val;
        return 0;
}

/**
 * Get a value from a mapping node by key name
 */
static yaml_node_t *yaml_get_mapping_value(yaml_document_t *doc, yaml_node_t *map,
                                           const char *key)
{
        if (!map || map->type != YAML_MAPPING_NODE)
                return NULL;

        yaml_node_pair_t *pair;
        for (pair = map->data.mapping.pairs.start;
             pair < map->data.mapping.pairs.top; pair++) {
                yaml_node_t *key_node = yaml_document_get_node(doc, pair->key);
                if (key_node && key_node->type == YAML_SCALAR_NODE) {
                        if (strcmp((char *)key_node->data.scalar.value, key) == 0) {
                                return yaml_document_get_node(doc, pair->value);
                        }
                }
        }

        return NULL;
}

/**
 * Get string value from mapping by key
 */
static char *yaml_get_mapping_string(yaml_document_t *doc, yaml_node_t *map,
                                     const char *key, char *err_buf, size_t err_sz)
{
        yaml_node_t *value = yaml_get_mapping_value(doc, map, key);
        if (!value)
                return NULL;

        return yaml_node_get_scalar_string(doc, value, err_buf, err_sz);
}

/**
 * Get integer value from mapping by key
 */
static int yaml_get_mapping_int(yaml_document_t *doc, yaml_node_t *map,
                                const char *key, int *out, char *err_buf, size_t err_sz)
{
        yaml_node_t *value = yaml_get_mapping_value(doc, map, key);
        if (!value) {
                *out = 0;
                return 0;  /* Not found, but not an error */
        }

        return yaml_node_get_scalar_int(doc, value, out, err_buf, err_sz);
}

/* Forward declarations for recursive parsing */
static ldmsd_decomp_static_config_t *parse_static_config(yaml_document_t *doc,
                                                   yaml_node_t *root,
                                                   char *err_buf, size_t err_sz);
static ldmsd_decomp_as_is_config_t *parse_as_is_config(yaml_document_t *doc,
                                                 yaml_node_t *root,
                                                 char *err_buf, size_t err_sz);
static ldmsd_decomp_flex_config_t *parse_flex_config(yaml_document_t *doc,
                                               yaml_node_t *root,
                                               char *err_buf, size_t err_sz);

/*
 * Static decomposition parsing
 */

static ldmsd_decomp_static_col_t *parse_static_col(yaml_document_t *doc, yaml_node_t *col_node,
                                             char *err_buf, size_t err_sz)
{
        ldmsd_decomp_static_col_t *col = calloc(1, sizeof(*col));
        if (!col) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return NULL;
        }

        col->src = yaml_get_mapping_string(doc, col_node, "src", err_buf, err_sz);
        col->dst = yaml_get_mapping_string(doc, col_node, "dst", err_buf, err_sz);
        col->type = yaml_get_mapping_string(doc, col_node, "type", err_buf, err_sz);
        col->rec_member = yaml_get_mapping_string(doc, col_node, "rec_member", err_buf, err_sz);
        col->op = yaml_get_mapping_string(doc, col_node, "op", err_buf, err_sz);

        yaml_get_mapping_int(doc, col_node, "array_len", &col->array_len, err_buf, err_sz);

        /* Parse fill value if present */
        yaml_node_t *fill_node = yaml_get_mapping_value(doc, col_node, "fill");
        if (fill_node && fill_node->type == YAML_SCALAR_NODE) {
                col->fill = strdup((char *)fill_node->data.scalar.value);
                col->fill_len = strlen((char *)col->fill);
        }

        return col;
}

static ldmsd_decomp_static_index_t *parse_static_index(yaml_document_t *doc, yaml_node_t *idx_node,
                                                 char *err_buf, size_t err_sz)
{
        ldmsd_decomp_static_index_t *idx = calloc(1, sizeof(*idx));
        if (!idx) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return NULL;
        }

        idx->name = yaml_get_mapping_string(doc, idx_node, "name", err_buf, err_sz);

        yaml_node_t *cols_node = yaml_get_mapping_value(doc, idx_node, "cols");
        if (cols_node && cols_node->type == YAML_SEQUENCE_NODE) {
                idx->col_count = cols_node->data.sequence.items.top -
                                cols_node->data.sequence.items.start;
                idx->cols = calloc(idx->col_count, sizeof(char *));

                yaml_node_item_t *item;
                int i = 0;
                for (item = cols_node->data.sequence.items.start;
                     item < cols_node->data.sequence.items.top; item++, i++) {
                        yaml_node_t *col_node = yaml_document_get_node(doc, *item);
                        idx->cols[i] = yaml_node_get_scalar_string(doc, col_node, err_buf, err_sz);
                }
        }

        return idx;
}

static ldmsd_decomp_static_group_t *parse_static_group(yaml_document_t *doc, yaml_node_t *grp_node,
                                                 char *err_buf, size_t err_sz)
{
        if (!grp_node)
                return NULL;

        ldmsd_decomp_static_group_t *grp = calloc(1, sizeof(*grp));
        if (!grp) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return NULL;
        }

        /* Parse index array */
        yaml_node_t *idx_node = yaml_get_mapping_value(doc, grp_node, "index");
        if (idx_node && idx_node->type == YAML_SEQUENCE_NODE) {
                grp->index_count = idx_node->data.sequence.items.top -
                                  idx_node->data.sequence.items.start;
                grp->index = calloc(grp->index_count, sizeof(char *));

                yaml_node_item_t *item;
                int i = 0;
                for (item = idx_node->data.sequence.items.start;
                     item < idx_node->data.sequence.items.top; item++, i++) {
                        yaml_node_t *val_node = yaml_document_get_node(doc, *item);
                        grp->index[i] = yaml_node_get_scalar_string(doc, val_node, err_buf, err_sz);
                }
        }

        /* Parse order array */
        yaml_node_t *ord_node = yaml_get_mapping_value(doc, grp_node, "order");
        if (ord_node && ord_node->type == YAML_SEQUENCE_NODE) {
                grp->order_count = ord_node->data.sequence.items.top -
                                  ord_node->data.sequence.items.start;
                grp->order = calloc(grp->order_count, sizeof(char *));

                yaml_node_item_t *item;
                int i = 0;
                for (item = ord_node->data.sequence.items.start;
                     item < ord_node->data.sequence.items.top; item++, i++) {
                        yaml_node_t *val_node = yaml_document_get_node(doc, *item);
                        grp->order[i] = yaml_node_get_scalar_string(doc, val_node, err_buf, err_sz);
                }
        }

        yaml_get_mapping_int(doc, grp_node, "limit", &grp->limit, err_buf, err_sz);
        grp->timeout = yaml_get_mapping_string(doc, grp_node, "timeout", err_buf, err_sz);

        return grp;
}

static ldmsd_decomp_static_row_t *parse_static_row(yaml_document_t *doc, yaml_node_t *row_node,
                                             char *err_buf, size_t err_sz)
{
        ldmsd_decomp_static_row_t *row = calloc(1, sizeof(*row));
        if (!row) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return NULL;
        }

        row->schema = yaml_get_mapping_string(doc, row_node, "schema", err_buf, err_sz);

        /* Parse columns */
        yaml_node_t *cols_node = yaml_get_mapping_value(doc, row_node, "cols");
        if (cols_node && cols_node->type == YAML_SEQUENCE_NODE) {
                row->col_count = cols_node->data.sequence.items.top -
                                cols_node->data.sequence.items.start;
                row->cols = calloc(row->col_count, sizeof(ldmsd_decomp_static_col_t));

                yaml_node_item_t *item;
                int i = 0;
                for (item = cols_node->data.sequence.items.start;
                     item < cols_node->data.sequence.items.top; item++, i++) {
                        yaml_node_t *col_node = yaml_document_get_node(doc, *item);
                        ldmsd_decomp_static_col_t *col = parse_static_col(doc, col_node, err_buf, err_sz);
                        if (col) {
                                row->cols[i] = *col;
                                free(col);
                        }
                }
        }

        /* Parse indices */
        yaml_node_t *indices_node = yaml_get_mapping_value(doc, row_node, "indices");
        if (indices_node && indices_node->type == YAML_SEQUENCE_NODE) {
                row->index_count = indices_node->data.sequence.items.top -
                                  indices_node->data.sequence.items.start;
                row->indices = calloc(row->index_count, sizeof(ldmsd_decomp_static_index_t));

                yaml_node_item_t *item;
                int i = 0;
                for (item = indices_node->data.sequence.items.start;
                     item < indices_node->data.sequence.items.top; item++, i++) {
                        yaml_node_t *idx_node = yaml_document_get_node(doc, *item);
                        ldmsd_decomp_static_index_t *idx = parse_static_index(doc, idx_node, err_buf, err_sz);
                        if (idx) {
                                row->indices[i] = *idx;
                                free(idx);
                        }
                }
        }

        /* Parse group */
        yaml_node_t *grp_node = yaml_get_mapping_value(doc, row_node, "group");
        row->group = parse_static_group(doc, grp_node, err_buf, err_sz);

        return row;
}

static ldmsd_decomp_static_config_t *parse_static_config(yaml_document_t *doc, yaml_node_t *root,
                                                   char *err_buf, size_t err_sz)
{
        ldmsd_decomp_static_config_t *cfg = calloc(1, sizeof(*cfg));
        if (!cfg) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return NULL;
        }

        cfg->type = yaml_get_mapping_string(doc, root, "type", err_buf, err_sz);

        /* Parse rows */
        yaml_node_t *rows_node = yaml_get_mapping_value(doc, root, "rows");
        if (!rows_node || rows_node->type != YAML_SEQUENCE_NODE) {
                PARSE_ERROR(err_buf, err_sz, "Static decomposition requires 'rows' array");
                free(cfg);
                return NULL;
        }

        cfg->row_count = rows_node->data.sequence.items.top -
                        rows_node->data.sequence.items.start;
        cfg->rows = calloc(cfg->row_count, sizeof(ldmsd_decomp_static_row_t));

        yaml_node_item_t *item;
        int i = 0;
        for (item = rows_node->data.sequence.items.start;
             item < rows_node->data.sequence.items.top; item++, i++) {
                yaml_node_t *row_node = yaml_document_get_node(doc, *item);
                ldmsd_decomp_static_row_t *row = parse_static_row(doc, row_node, err_buf, err_sz);
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

static ldmsd_decomp_as_is_index_t *parse_as_is_index(yaml_document_t *doc, yaml_node_t *idx_node,
                                               char *err_buf, size_t err_sz)
{
        ldmsd_decomp_as_is_index_t *idx = calloc(1, sizeof(*idx));
        if (!idx) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return NULL;
        }

        idx->name = yaml_get_mapping_string(doc, idx_node, "name", err_buf, err_sz);

        yaml_node_t *cols_node = yaml_get_mapping_value(doc, idx_node, "cols");
        if (cols_node && cols_node->type == YAML_SEQUENCE_NODE) {
                idx->col_count = cols_node->data.sequence.items.top -
                                cols_node->data.sequence.items.start;
                idx->cols = calloc(idx->col_count, sizeof(char *));

                yaml_node_item_t *item;
                int i = 0;
                for (item = cols_node->data.sequence.items.start;
                     item < cols_node->data.sequence.items.top; item++, i++) {
                        yaml_node_t *col_node = yaml_document_get_node(doc, *item);
                        idx->cols[i] = yaml_node_get_scalar_string(doc, col_node, err_buf, err_sz);
                }
        }

        return idx;
}

static ldmsd_decomp_as_is_config_t *parse_as_is_config(yaml_document_t *doc, yaml_node_t *root,
                                                 char *err_buf, size_t err_sz)
{
        ldmsd_decomp_as_is_config_t *cfg = calloc(1, sizeof(*cfg));
        if (!cfg) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return NULL;
        }

        cfg->type = yaml_get_mapping_string(doc, root, "type", err_buf, err_sz);

        /* Parse indices */
        yaml_node_t *indices_node = yaml_get_mapping_value(doc, root, "indices");
        if (indices_node && indices_node->type == YAML_SEQUENCE_NODE) {
                cfg->index_count = indices_node->data.sequence.items.top -
                                  indices_node->data.sequence.items.start;
                cfg->indices = calloc(cfg->index_count, sizeof(ldmsd_decomp_as_is_index_t));

                yaml_node_item_t *item;
                int i = 0;
                for (item = indices_node->data.sequence.items.start;
                     item < indices_node->data.sequence.items.top; item++, i++) {
                        yaml_node_t *idx_node = yaml_document_get_node(doc, *item);
                        ldmsd_decomp_as_is_index_t *idx = parse_as_is_index(doc, idx_node, err_buf, err_sz);
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

static ldmsd_decomp_flex_config_t *parse_flex_config(yaml_document_t *doc, yaml_node_t *root,
                                               char *err_buf, size_t err_sz)
{
        ldmsd_decomp_flex_config_t *cfg = calloc(1, sizeof(*cfg));
        if (!cfg) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                return NULL;
        }

        cfg->type = yaml_get_mapping_string(doc, root, "type", err_buf, err_sz);

        /* Parse nested decomposition definitions */
        yaml_node_t *decomp_node = yaml_get_mapping_value(doc, root, "decomposition");
        if (decomp_node && decomp_node->type == YAML_MAPPING_NODE) {
                cfg->decomp_count = decomp_node->data.mapping.pairs.top -
                                   decomp_node->data.mapping.pairs.start;
                cfg->decompositions = calloc(cfg->decomp_count, sizeof(ldmsd_decomp_flex_decomp_ref_t));

                yaml_node_pair_t *pair;
                int i = 0;
                for (pair = decomp_node->data.mapping.pairs.start;
                     pair < decomp_node->data.mapping.pairs.top; pair++, i++) {
                        yaml_node_t *key_node = yaml_document_get_node(doc, pair->key);
                        yaml_node_t *val_node = yaml_document_get_node(doc, pair->value);

                        cfg->decompositions[i].name = yaml_node_get_scalar_string(doc, key_node, err_buf, err_sz);

                        /* Parse nested decomposition by type */
                        char *nested_type = yaml_get_mapping_string(doc, val_node, "type", err_buf, err_sz);
                        if (nested_type) {
                                if (strcmp(nested_type, "static") == 0) {
                                        cfg->decompositions[i].decomp_type = LDMSD_DECOMP_CFG_TYPE_STATIC;
                                        cfg->decompositions[i].decomp_config = parse_static_config(doc, val_node, err_buf, err_sz);
                                } else if (strcmp(nested_type, "as_is") == 0) {
                                        cfg->decompositions[i].decomp_type = LDMSD_DECOMP_CFG_TYPE_AS_IS;
                                        cfg->decompositions[i].decomp_config = parse_as_is_config(doc, val_node, err_buf, err_sz);
                                }
                                free(nested_type);
                        }
                }
        }

        /* Parse digest mapping */
        yaml_node_t *digest_node = yaml_get_mapping_value(doc, root, "digest");
        if (digest_node && digest_node->type == YAML_MAPPING_NODE) {
                cfg->digest_count = digest_node->data.mapping.pairs.top -
                                   digest_node->data.mapping.pairs.start;
                cfg->digest_map = calloc(cfg->digest_count, sizeof(*cfg->digest_map));

                yaml_node_pair_t *pair;
                int i = 0;
                for (pair = digest_node->data.mapping.pairs.start;
                     pair < digest_node->data.mapping.pairs.top; pair++, i++) {
                        yaml_node_t *key_node = yaml_document_get_node(doc, pair->key);
                        yaml_node_t *val_node = yaml_document_get_node(doc, pair->value);

                        cfg->digest_map[i].digest = yaml_node_get_scalar_string(doc, key_node, err_buf, err_sz);
                        cfg->digest_map[i].name = yaml_node_get_scalar_string(doc, val_node, err_buf, err_sz);
                }
        }

        /* Parse matches */
        yaml_node_t *matches_node = yaml_get_mapping_value(doc, root, "matches");
        if (matches_node && matches_node->type == YAML_SEQUENCE_NODE) {
                cfg->match_count = matches_node->data.sequence.items.top -
                                  matches_node->data.sequence.items.start;
                cfg->matches = calloc(cfg->match_count, sizeof(ldmsd_decomp_flex_match_t));

                yaml_node_item_t *item;
                int i = 0;
                for (item = matches_node->data.sequence.items.start;
                     item < matches_node->data.sequence.items.top; item++, i++) {
                        yaml_node_t *match_node = yaml_document_get_node(doc, *item);

                        cfg->matches[i].schema = yaml_get_mapping_string(doc, match_node, "schema", err_buf, err_sz);
                        cfg->matches[i].instance = yaml_get_mapping_string(doc, match_node, "instance", err_buf, err_sz);

                        yaml_node_t *apply_node = yaml_get_mapping_value(doc, match_node, "apply");
                        if (apply_node) {
                                if (apply_node->type == YAML_SCALAR_NODE) {
                                        cfg->matches[i].apply_count = 1;
                                        cfg->matches[i].apply = calloc(1, sizeof(char *));
                                        cfg->matches[i].apply[0] = yaml_node_get_scalar_string(doc, apply_node, err_buf, err_sz);
                                } else if (apply_node->type == YAML_SEQUENCE_NODE) {
                                        cfg->matches[i].apply_count = apply_node->data.sequence.items.top -
                                                                     apply_node->data.sequence.items.start;
                                        cfg->matches[i].apply = calloc(cfg->matches[i].apply_count, sizeof(char *));

                                        yaml_node_item_t *apply_item;
                                        int j = 0;
                                        for (apply_item = apply_node->data.sequence.items.start;
                                             apply_item < apply_node->data.sequence.items.top; apply_item++, j++) {
                                                yaml_node_t *apply_val = yaml_document_get_node(doc, *apply_item);
                                                cfg->matches[i].apply[j] = yaml_node_get_scalar_string(doc, apply_val, err_buf, err_sz);
                                        }
                                }
                        }
                }
        }

        /* Parse default */
        cfg->default_decomp = yaml_get_mapping_string(doc, root, "default", err_buf, err_sz);

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

ldmsd_decomp_config_t *ldmsd_decomp_config_parse_file(const char *path, char *err_buf, size_t err_sz)
{
        FILE *fp = NULL;
        yaml_parser_t parser;
        yaml_document_t document;
        ldmsd_decomp_config_t *cfg = NULL;
        int parser_initialized = 0;

        /* Open file */
        fp = fopen(path, "r");
        if (!fp) {
                PARSE_ERROR(err_buf, err_sz, "Cannot open file '%s': %s", path, strerror(errno));
                return NULL;
        }

        /* Initialize YAML parser */
        if (!yaml_parser_initialize(&parser)) {
                PARSE_ERROR(err_buf, err_sz, "Failed to initialize YAML parser");
                goto cleanup;
        }
        parser_initialized = 1;

        yaml_parser_set_input_file(&parser, fp);

        /* Load YAML document */
        if (!yaml_parser_load(&parser, &document)) {
                PARSE_ERROR(err_buf, err_sz, "YAML parse error at line %zu: %s",
                           parser.problem_mark.line + 1, parser.problem);
                goto cleanup;
        }

        /* Get root node */
        yaml_node_t *root = yaml_document_get_root_node(&document);
        if (!root || root->type != YAML_MAPPING_NODE) {
                PARSE_ERROR(err_buf, err_sz, "Root node must be a mapping");
                yaml_document_delete(&document);
                goto cleanup;
        }

        /* Get type field to determine decomposition type */
        char *type_str = yaml_get_mapping_string(&document, root, "type", err_buf, err_sz);
        if (!type_str) {
                PARSE_ERROR(err_buf, err_sz, "Missing required 'type' field");
                yaml_document_delete(&document);
                goto cleanup;
        }

        /* Allocate config structure */
        cfg = calloc(1, sizeof(*cfg));
        if (!cfg) {
                PARSE_ERROR(err_buf, err_sz, "Out of memory");
                free(type_str);
                yaml_document_delete(&document);
                goto cleanup;
        }

        /* Parse based on type */
        if (strcmp(type_str, "static") == 0) {
                cfg->type = LDMSD_DECOMP_CFG_TYPE_STATIC;
                cfg->static_cfg = parse_static_config(&document, root, err_buf, err_sz);
                if (!cfg->static_cfg) {
                        free(cfg);
                        cfg = NULL;
                }
        } else if (strcmp(type_str, "as_is") == 0) {
                cfg->type = LDMSD_DECOMP_CFG_TYPE_AS_IS;
                cfg->as_is_cfg = parse_as_is_config(&document, root, err_buf, err_sz);
                if (!cfg->as_is_cfg) {
                        free(cfg);
                        cfg = NULL;
                }
        } else if (strcmp(type_str, "flex") == 0) {
                cfg->type = LDMSD_DECOMP_CFG_TYPE_FLEX;
                cfg->flex_cfg = parse_flex_config(&document, root, err_buf, err_sz);
                if (!cfg->flex_cfg) {
                        free(cfg);
                        cfg = NULL;
                }
        } else {
                PARSE_ERROR(err_buf, err_sz, "Unknown decomposition type: %s", type_str);
                free(cfg);
                cfg = NULL;
        }

        free(type_str);
        yaml_document_delete(&document);

cleanup:
        if (parser_initialized)
                yaml_parser_delete(&parser);
        if (fp)
                fclose(fp);

        return cfg;
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
                        free(row->cols[j].fill);
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

        free(cfg);
}
