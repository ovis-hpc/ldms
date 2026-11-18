/* -*- c-basic-offset: 8 -*- */
/* Copyright 2026 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */

/*
 * jansson decomposition config parser backend (JSON only)
 *
 * jansson does not record source positions for parsed values, so nodes
 * from this backend have no line/column (only syntax errors carry one).
 * Non-string JSON scalars are converted to their JSON text, matching what
 * the YAML parsers produce for the same input.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <jansson.h>
#include "decomp_parser_priv.h"

/* Bound recursion (jansson itself limits parse depth as well) */
#define MAX_DEPTH 256

static ldmsd_decomp_cfg_node_t convert(struct decomp_cfg_loc_tab_s *tab,
                                       json_t *j, int depth,
                                       char *err_buf, size_t err_sz)
{
        ldmsd_decomp_cfg_node_t n, k, v;
        const char *key;
        json_t *val;
        size_t i;
        char buf[64];
        const char *str;
        size_t len;

        if (depth > MAX_DEPTH) {
                snprintf(err_buf, err_sz, "%s: nesting is too deep", tab->file);
                return NULL;
        }

        switch (json_typeof(j)) {
        case JSON_OBJECT:
                n = decomp_cfg_node_new(tab, DECOMP_CFG_NODE_MAPPING, 0, 0,
                                        err_buf, err_sz);
                if (!n)
                        return NULL;
                json_object_foreach(j, key, val) {
                        k = decomp_cfg_scalar_new(tab, key, strlen(key), 0, 0,
                                                  err_buf, err_sz);
                        if (!k)
                                return NULL;
                        v = convert(tab, val, depth + 1, err_buf, err_sz);
                        if (!v || decomp_cfg_map_append(n, k, v, err_buf, err_sz))
                                return NULL;
                }
                return n;
        case JSON_ARRAY:
                n = decomp_cfg_node_new(tab, DECOMP_CFG_NODE_SEQUENCE, 0, 0,
                                        err_buf, err_sz);
                if (!n)
                        return NULL;
                json_array_foreach(j, i, val) {
                        v = convert(tab, val, depth + 1, err_buf, err_sz);
                        if (!v || decomp_cfg_seq_append(n, v, err_buf, err_sz))
                                return NULL;
                }
                return n;
        case JSON_STRING:
                str = json_string_value(j);
                len = json_string_length(j);
                break;
        case JSON_INTEGER:
                snprintf(buf, sizeof(buf), "%" JSON_INTEGER_FORMAT,
                         json_integer_value(j));
                str = buf;
                len = strlen(buf);
                break;
        case JSON_REAL:
                snprintf(buf, sizeof(buf), "%.17g", json_real_value(j));
                str = buf;
                len = strlen(buf);
                break;
        case JSON_TRUE:
                str = "true";
                len = 4;
                break;
        case JSON_FALSE:
                str = "false";
                len = 5;
                break;
        case JSON_NULL:
        default:
                str = "null";
                len = 4;
                break;
        }
        return decomp_cfg_scalar_new(tab, str, len, 0, 0, err_buf, err_sz);
}

static ldmsd_decomp_cfg_node_t jansson_load(struct decomp_cfg_loc_tab_s *tab,
                                            char *err_buf, size_t err_sz)
{
        json_error_t jerr;
        json_t *jroot;
        ldmsd_decomp_cfg_node_t root;
        FILE *fp;

        fp = fopen(tab->file, "r");
        if (!fp) {
                snprintf(err_buf, err_sz, "Cannot open file '%s': %s",
                         tab->file, strerror(errno));
                return NULL;
        }
        jroot = json_loadf(fp, JSON_REJECT_DUPLICATES, &jerr);
        fclose(fp);
        if (!jroot) {
                snprintf(err_buf, err_sz, "JSON parse error at %s:%d:%d: %s",
                         tab->file, jerr.line, jerr.column, jerr.text);
                return NULL;
        }
        root = convert(tab, jroot, 0, err_buf, err_sz);
        json_decref(jroot);
        return root;
}

const struct decomp_parser_s decomp_parser_jansson = {
        .name = "jansson",
        .yaml = 0,
        .load = jansson_load,
};
