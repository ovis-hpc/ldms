/* -*- c-basic-offset: 8 -*- */
/* Copyright 2026 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */

/*
 * libyaml decomposition config parser backend (YAML and JSON)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <yaml.h>
#include "decomp_parser_priv.h"

/* Bound recursion on hostile input (also catches recursive aliases) */
#define MAX_DEPTH 256

struct conv_ctxt {
        struct decomp_cfg_loc_tab_s *tab;
        yaml_document_t *doc;
        /* Converted node for each YAML node, so that a node referenced by
         * several aliases is converted only once. */
        ldmsd_decomp_cfg_node_t *memo;
};

static ldmsd_decomp_cfg_node_t convert(struct conv_ctxt *c, yaml_node_t *yn,
                                       int depth, char *err_buf, size_t err_sz)
{
        struct decomp_cfg_loc_tab_s *tab = c->tab;
        yaml_document_t *doc = c->doc;
        ldmsd_decomp_cfg_node_t n, k, v;
        yaml_node_item_t *item;
        yaml_node_pair_t *pair;
        size_t idx;
        int line, column;

        if (!yn) {
                snprintf(err_buf, err_sz, "%s: invalid YAML node reference",
                         tab->file);
                return NULL;
        }
        idx = yn - doc->nodes.start;
        if (c->memo[idx])
                return c->memo[idx];
        line = yn->start_mark.line + 1;
        column = yn->start_mark.column + 1;
        if (depth > MAX_DEPTH) {
                snprintf(err_buf, err_sz, "%s:%d:%d: nesting is too deep",
                         tab->file, line, column);
                return NULL;
        }

        switch (yn->type) {
        case YAML_SCALAR_NODE:
                n = decomp_cfg_scalar_new(tab, (char *)yn->data.scalar.value,
                                          yn->data.scalar.length,
                                          line, column, err_buf, err_sz);
                break;
        case YAML_SEQUENCE_NODE:
                n = decomp_cfg_node_new(tab, DECOMP_CFG_NODE_SEQUENCE,
                                        line, column, err_buf, err_sz);
                if (!n)
                        return NULL;
                for (item = yn->data.sequence.items.start;
                     item < yn->data.sequence.items.top; item++) {
                        v = convert(c, yaml_document_get_node(doc, *item),
                                    depth + 1, err_buf, err_sz);
                        if (!v || decomp_cfg_seq_append(n, v, err_buf, err_sz))
                                return NULL;
                }
                break;
        case YAML_MAPPING_NODE:
                n = decomp_cfg_node_new(tab, DECOMP_CFG_NODE_MAPPING,
                                        line, column, err_buf, err_sz);
                if (!n)
                        return NULL;
                for (pair = yn->data.mapping.pairs.start;
                     pair < yn->data.mapping.pairs.top; pair++) {
                        k = convert(c, yaml_document_get_node(doc, pair->key),
                                    depth + 1, err_buf, err_sz);
                        if (!k)
                                return NULL;
                        v = convert(c, yaml_document_get_node(doc, pair->value),
                                    depth + 1, err_buf, err_sz);
                        if (!v || decomp_cfg_map_append(n, k, v, err_buf, err_sz))
                                return NULL;
                }
                break;
        default:
                snprintf(err_buf, err_sz, "%s:%d:%d: unsupported YAML node",
                         tab->file, line, column);
                return NULL;
        }
        c->memo[idx] = n;
        return n;
}

static ldmsd_decomp_cfg_node_t libyaml_load(struct decomp_cfg_loc_tab_s *tab,
                                            char *err_buf, size_t err_sz)
{
        FILE *fp;
        yaml_parser_t parser;
        yaml_document_t doc;
        yaml_node_t *yroot;
        ldmsd_decomp_cfg_node_t root = NULL;

        fp = fopen(tab->file, "r");
        if (!fp) {
                snprintf(err_buf, err_sz, "Cannot open file '%s': %s",
                         tab->file, strerror(errno));
                return NULL;
        }

        if (!yaml_parser_initialize(&parser)) {
                snprintf(err_buf, err_sz, "Failed to initialize YAML parser");
                fclose(fp);
                return NULL;
        }
        yaml_parser_set_input_file(&parser, fp);

        if (!yaml_parser_load(&parser, &doc)) {
                snprintf(err_buf, err_sz, "YAML parse error at %s:%zu:%zu: %s",
                         tab->file, parser.problem_mark.line + 1,
                         parser.problem_mark.column + 1,
                         parser.problem ? parser.problem : "unknown error");
                goto out;
        }

        yroot = yaml_document_get_root_node(&doc);
        if (!yroot) {
                snprintf(err_buf, err_sz, "%s: empty document", tab->file);
        } else {
                struct conv_ctxt c = { .tab = tab, .doc = &doc };
                c.memo = calloc(doc.nodes.top - doc.nodes.start, sizeof(*c.memo));
                if (!c.memo)
                        snprintf(err_buf, err_sz, "Out of memory");
                else
                        root = convert(&c, yroot, 0, err_buf, err_sz);
                free(c.memo);
        }
        yaml_document_delete(&doc);
out:
        yaml_parser_delete(&parser);
        fclose(fp);
        return root;
}

const struct decomp_parser_s decomp_parser_libyaml = {
        .name = "libyaml",
        .yaml = 1,
        .load = libyaml_load,
};
