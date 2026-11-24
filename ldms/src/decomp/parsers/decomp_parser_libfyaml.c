/* -*- c-basic-offset: 8 -*- */
/* Copyright 2026 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */

/*
 * libfyaml decomposition config parser backend (YAML and JSON)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libfyaml.h>
#include "decomp_parser_priv.h"

/* Bound recursion on hostile input (also catches recursive aliases) */
#define MAX_DEPTH 256

static void node_mark(struct fy_node *fyn, int *line, int *column)
{
        const struct fy_mark *m = NULL;
        struct fy_token *fyt = fy_node_get_start_token(fyn);

        if (fyt)
                m = fy_token_start_mark(fyt);
        if (m) {
                *line = m->line + 1;
                *column = m->column + 1;
                /* libfyaml marks quoted scalars after the opening quote;
                 * point at the quote, as libyaml does */
                switch (fy_node_get_style(fyn)) {
                case FYNS_SINGLE_QUOTED:
                case FYNS_DOUBLE_QUOTED:
                        if (*column > 1)
                                (*column)--;
                        break;
                default:
                        break;
                }
        } else {
                *line = 0;
                *column = 0;
        }
}

/*
 * Convert \c fyn to a generic node.  Converted nodes are remembered in the
 * libfyaml node's meta pointer, so a node referenced by several aliases is
 * converted only once (like libyaml, aliases share the anchored node).
 */
static ldmsd_decomp_cfg_node_t convert(struct decomp_cfg_loc_tab_s *tab,
                                       struct fy_node *fyn, int depth,
                                       char *err_buf, size_t err_sz)
{
        ldmsd_decomp_cfg_node_t n, k, v;
        struct fy_node_pair *pair;
        struct fy_node *target;
        const char *str;
        size_t len;
        int line, column, i, count;

        node_mark(fyn, &line, &column);
        if (depth > MAX_DEPTH) {
                snprintf(err_buf, err_sz, "%s:%d:%d: nesting is too deep",
                         tab->file, line, column);
                return NULL;
        }

        if (fy_node_is_alias(fyn)) {
                target = fy_node_resolve_alias(fyn);
                if (!target) {
                        snprintf(err_buf, err_sz, "%s:%d:%d: cannot resolve alias",
                                 tab->file, line, column);
                        return NULL;
                }
                return convert(tab, target, depth + 1, err_buf, err_sz);
        }

        n = fy_node_get_meta(fyn);
        if (n)
                return n;

        switch (fy_node_get_type(fyn)) {
        case FYNT_SCALAR:
                str = fy_node_get_scalar(fyn, &len);
                if (!str) {
                        str = "";
                        len = 0;
                }
                n = decomp_cfg_scalar_new(tab, str, len, line, column,
                                          err_buf, err_sz);
                break;
        case FYNT_SEQUENCE:
                n = decomp_cfg_node_new(tab, DECOMP_CFG_NODE_SEQUENCE,
                                        line, column, err_buf, err_sz);
                if (!n)
                        return NULL;
                count = fy_node_sequence_item_count(fyn);
                for (i = 0; i < count; i++) {
                        v = convert(tab, fy_node_sequence_get_by_index(fyn, i),
                                    depth + 1, err_buf, err_sz);
                        if (!v || decomp_cfg_seq_append(n, v, err_buf, err_sz))
                                return NULL;
                }
                break;
        case FYNT_MAPPING:
                n = decomp_cfg_node_new(tab, DECOMP_CFG_NODE_MAPPING,
                                        line, column, err_buf, err_sz);
                if (!n)
                        return NULL;
                count = fy_node_mapping_item_count(fyn);
                for (i = 0; i < count; i++) {
                        pair = fy_node_mapping_get_by_index(fyn, i);
                        if (!fy_node_pair_key(pair)) {
                                snprintf(err_buf, err_sz,
                                         "%s:%d:%d: mapping has an empty key",
                                         tab->file, line, column);
                                return NULL;
                        }
                        k = convert(tab, fy_node_pair_key(pair), depth + 1,
                                    err_buf, err_sz);
                        if (!k)
                                return NULL;
                        if (fy_node_pair_value(pair)) {
                                v = convert(tab, fy_node_pair_value(pair),
                                            depth + 1, err_buf, err_sz);
                        } else {
                                /* "key:" with no value is an empty scalar */
                                v = decomp_cfg_scalar_new(tab, "", 0,
                                                          k->line, k->column,
                                                          err_buf, err_sz);
                        }
                        if (!v || decomp_cfg_map_append(n, k, v, err_buf, err_sz))
                                return NULL;
                }
                break;
        default:
                snprintf(err_buf, err_sz, "%s:%d:%d: unsupported YAML node",
                         tab->file, line, column);
                return NULL;
        }
        if (n)
                fy_node_set_meta(fyn, n);
        return n;
}

static ldmsd_decomp_cfg_node_t libfyaml_load(struct decomp_cfg_loc_tab_s *tab,
                                             char *err_buf, size_t err_sz)
{
        struct fy_diag_cfg dcfg;
        struct fy_diag *diag;
        struct fy_parse_cfg pcfg;
        struct fy_document *fyd;
        struct fy_diag_error *derr;
        struct fy_node *fyroot;
        ldmsd_decomp_cfg_node_t root = NULL;
        void *iter = NULL;
        FILE *fp;

        /* libfyaml's own error for a missing file is not very helpful */
        fp = fopen(tab->file, "r");
        if (!fp) {
                snprintf(err_buf, err_sz, "Cannot open file '%s': %s",
                         tab->file, strerror(errno));
                return NULL;
        }
        fclose(fp);

        /* Collect errors instead of printing them to stderr */
        fy_diag_cfg_default(&dcfg);
        dcfg.fp = NULL;
        diag = fy_diag_create(&dcfg);
        if (!diag) {
                snprintf(err_buf, err_sz, "Failed to create libfyaml diag object");
                return NULL;
        }
        fy_diag_set_collect_errors(diag, true);

        memset(&pcfg, 0, sizeof(pcfg));
        pcfg.flags = FYPCF_QUIET | FYPCF_JSON_AUTO;
        pcfg.diag = diag;

        fyd = fy_document_build_from_file(&pcfg, tab->file);
        if (!fyd) {
                derr = fy_diag_errors_iterate(diag, &iter);
                if (derr)
                        snprintf(err_buf, err_sz, "YAML parse error at %s:%d:%d: %s",
                                 tab->file, derr->line, derr->column,
                                 derr->msg ? derr->msg : "unknown error");
                else
                        snprintf(err_buf, err_sz, "%s: empty document", tab->file);
                goto out;
        }

        fyroot = fy_document_root(fyd);
        if (!fyroot)
                snprintf(err_buf, err_sz, "%s: empty document", tab->file);
        else
                root = convert(tab, fyroot, 0, err_buf, err_sz);
        fy_document_destroy(fyd);
out:
        fy_diag_destroy(diag);
        return root;
}

const struct decomp_parser_s decomp_parser_libfyaml = {
        .name = "libfyaml",
        .yaml = 1,
        .load = libfyaml_load,
};
