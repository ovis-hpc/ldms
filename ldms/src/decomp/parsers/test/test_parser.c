/* -*- c-basic-offset: 8 -*- */
/* Copyright 2026 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 *
 * Unit tests for decomp_config_parser
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include "ldmsd_decomp_config_parser.h"

/* Directory containing test/ (set with -d, for VPATH builds) */
static const char *srcdir = ".";
/* Name of the parser under test, and whether it handles YAML */
static const char *parser;
static int parser_yaml;

/* Path of a test data file.  Rotates through a few buffers so that several
 * results may be used in one expression. */
static const char *P(const char *rel)
{
        static char bufs[4][4096];
        static int n;
        char *b = bufs[n++ % 4];
        snprintf(b, sizeof(bufs[0]), "%s/%s", srcdir, rel);
        return b;
}

#define SKIP_UNLESS_YAML(name) \
        do { \
                if (!parser_yaml) { \
                        printf("TEST: %s ... SKIP (%s does not parse YAML)\n", \
                               name, parser); \
                        return; \
                } \
        } while (0)

/* Test result tracking */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name) \
        do { \
                tests_run++; \
                printf("TEST: %s ... ", name); \
                fflush(stdout); \
        } while (0)

#define TEST_PASS() \
        do { \
                tests_passed++; \
                printf("PASS\n"); \
        } while (0)

#define TEST_FAIL(msg) \
        do { \
                tests_failed++; \
                printf("FAIL: %s\n", msg); \
        } while (0)

#define ASSERT_NOT_NULL(ptr, msg) \
        do { \
                if (!(ptr)) { \
                        TEST_FAIL(msg); \
                        return; \
                } \
        } while (0)

/* Failure messages are printed directly; they may contain long paths */
#define ASSERT_EQUAL(a, b, msg) \
        do { \
                if ((a) != (b)) { \
                        tests_failed++; \
                        printf("FAIL: %s (expected %d, got %d)\n", \
                               msg, (int)(b), (int)(a)); \
                        return; \
                } \
        } while (0)

#define ASSERT_STR_EQUAL(a, b, msg) \
        do { \
                if (strcmp((a), (b)) != 0) { \
                        tests_failed++; \
                        printf("FAIL: %s (expected '%s', got '%s')\n", \
                               msg, b, a); \
                        return; \
                } \
        } while (0)

/*
 * Test 1: Parse as_is config from JSON
 */
static void test_as_is_json(void)
{
        TEST_START("Parse as_is from JSON");

        char err_buf[512];
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file(P("test/test_as_is.json"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(cfg, err_buf);

        ASSERT_EQUAL(cfg->type, LDMSD_DECOMP_CFG_TYPE_AS_IS, "Wrong config type");
        ASSERT_NOT_NULL(cfg->as_is_cfg, "as_is_cfg is NULL");
        ASSERT_STR_EQUAL(cfg->as_is_cfg->type, "as_is", "Wrong type string");
        ASSERT_EQUAL(cfg->as_is_cfg->index_count, 1, "Wrong index count");

        ldmsd_decomp_as_is_index_t *idx = &cfg->as_is_cfg->indices[0];
        ASSERT_STR_EQUAL(idx->name, "test_index", "Wrong index name");
        ASSERT_EQUAL(idx->col_count, 3, "Wrong column count");
        ASSERT_STR_EQUAL(idx->cols[0], "timestamp", "Wrong column 0");
        ASSERT_STR_EQUAL(idx->cols[1], "producer", "Wrong column 1");
        ASSERT_STR_EQUAL(idx->cols[2], "component_id", "Wrong column 2");

        ldmsd_decomp_config_free(cfg);
        TEST_PASS();
}

/*
 * Test 2: Parse as_is config from YAML
 */
static void test_as_is_yaml(void)
{
        SKIP_UNLESS_YAML("Parse as_is from YAML");
        TEST_START("Parse as_is from YAML");

        char err_buf[512];
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file(P("test/test_as_is.yaml"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(cfg, err_buf);

        ASSERT_EQUAL(cfg->type, LDMSD_DECOMP_CFG_TYPE_AS_IS, "Wrong config type");
        ASSERT_NOT_NULL(cfg->as_is_cfg, "as_is_cfg is NULL");
        ASSERT_EQUAL(cfg->as_is_cfg->index_count, 1, "Wrong index count");

        ldmsd_decomp_as_is_index_t *idx = &cfg->as_is_cfg->indices[0];
        ASSERT_STR_EQUAL(idx->name, "test_index", "Wrong index name");
        ASSERT_EQUAL(idx->col_count, 3, "Wrong column count");

        ldmsd_decomp_config_free(cfg);
        TEST_PASS();
}

/*
 * Test 3: Parse static config from JSON
 */
static void test_static_json(void)
{
        TEST_START("Parse static from JSON");

        char err_buf[512];
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file(P("test/test_static.json"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(cfg, err_buf);

        ASSERT_EQUAL(cfg->type, LDMSD_DECOMP_CFG_TYPE_STATIC, "Wrong config type");
        ASSERT_NOT_NULL(cfg->static_cfg, "static_cfg is NULL");
        ASSERT_STR_EQUAL(cfg->static_cfg->type, "static", "Wrong type string");
        ASSERT_EQUAL(cfg->static_cfg->row_count, 1, "Wrong row count");

        ldmsd_decomp_static_row_t *row = &cfg->static_cfg->rows[0];
        ASSERT_STR_EQUAL(row->schema, "test_schema", "Wrong schema name");
        ASSERT_EQUAL(row->col_count, 4, "Wrong column count");

        /* Check first column */
        ldmsd_decomp_static_col_t *col = &row->cols[0];
        ASSERT_STR_EQUAL(col->src, "timestamp", "Wrong col src");
        ASSERT_STR_EQUAL(col->dst, "timestamp", "Wrong col dst");
        ASSERT_STR_EQUAL(col->type, "ts", "Wrong col type");

        /* Check column with array_len */
        col = &row->cols[1];
        ASSERT_STR_EQUAL(col->src, "producer", "Wrong col src");
        ASSERT_EQUAL(col->array_len, 256, "Wrong array_len");

        /* Check index */
        ASSERT_EQUAL(row->index_count, 1, "Wrong index count");
        ldmsd_decomp_static_index_t *idx = &row->indices[0];
        ASSERT_STR_EQUAL(idx->name, "idx_producer", "Wrong index name");
        ASSERT_EQUAL(idx->col_count, 1, "Wrong index col count");
        ASSERT_STR_EQUAL(idx->cols[0], "producer", "Wrong index column");

        ldmsd_decomp_config_free(cfg);
        TEST_PASS();
}

/*
 * Test 4: Parse static config from YAML
 */
static void test_static_yaml(void)
{
        SKIP_UNLESS_YAML("Parse static from YAML");
        TEST_START("Parse static from YAML");

        char err_buf[512];
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file(P("test/test_static.yaml"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(cfg, err_buf);

        ASSERT_EQUAL(cfg->type, LDMSD_DECOMP_CFG_TYPE_STATIC, "Wrong config type");
        ASSERT_NOT_NULL(cfg->static_cfg, "static_cfg is NULL");
        ASSERT_EQUAL(cfg->static_cfg->row_count, 1, "Wrong row count");

        ldmsd_decomp_static_row_t *row = &cfg->static_cfg->rows[0];
        ASSERT_EQUAL(row->col_count, 4, "Wrong column count");

        ldmsd_decomp_config_free(cfg);
        TEST_PASS();
}

/*
 * Test 5: Parse flex config from JSON
 */
static void test_flex_json(void)
{
        TEST_START("Parse flex from JSON");

        char err_buf[512];
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file(P("test/test_flex.json"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(cfg, err_buf);

        ASSERT_EQUAL(cfg->type, LDMSD_DECOMP_CFG_TYPE_FLEX, "Wrong config type");
        ASSERT_NOT_NULL(cfg->flex_cfg, "flex_cfg is NULL");
        ASSERT_STR_EQUAL(cfg->flex_cfg->type, "flex", "Wrong type string");
        ASSERT_EQUAL(cfg->flex_cfg->decomp_count, 1, "Wrong decomp count");

        /* Check nested decomposition */
        ldmsd_decomp_flex_decomp_ref_t *ref = &cfg->flex_cfg->decompositions[0];
        ASSERT_STR_EQUAL(ref->name, "as_is_test", "Wrong decomp name");
        ASSERT_EQUAL(ref->decomp_type, LDMSD_DECOMP_CFG_TYPE_AS_IS, "Wrong nested decomp type");
        ASSERT_NOT_NULL(ref->decomp_config, "Nested config is NULL");

        /* Check digest mapping */
        ASSERT_EQUAL(cfg->flex_cfg->digest_count, 1, "Wrong digest count");
        ASSERT_STR_EQUAL(cfg->flex_cfg->digest_map[0].digest, "*", "Wrong digest key");
        ASSERT_STR_EQUAL(cfg->flex_cfg->digest_map[0].name, "as_is_test", "Wrong digest value");

        ldmsd_decomp_config_free(cfg);
        TEST_PASS();
}

/*
 * Test 6: Error handling - missing file
 */
static void test_error_missing_file(void)
{
        TEST_START("Error handling - missing file");

        char err_buf[512];
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file("nonexistent.json", err_buf, sizeof(err_buf));

        if (cfg != NULL) {
                TEST_FAIL("Expected NULL for missing file");
                ldmsd_decomp_config_free(cfg);
                return;
        }

        /* Error buffer should contain error message */
        if (strlen(err_buf) == 0) {
                TEST_FAIL("Error buffer is empty");
                return;
        }

        TEST_PASS();
}

/*
 * Test 7: JSON/YAML equivalence
 */
static void test_json_yaml_equivalence(void)
{
        SKIP_UNLESS_YAML("JSON/YAML equivalence");
        TEST_START("JSON/YAML equivalence");

        char err_buf[512];
        ldmsd_decomp_config_t *json_cfg = ldmsd_decomp_config_parse_file(P("test/test_as_is.json"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(json_cfg, "Failed to parse JSON");

        ldmsd_decomp_config_t *yaml_cfg = ldmsd_decomp_config_parse_file(P("test/test_as_is.yaml"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(yaml_cfg, "Failed to parse YAML");

        /* Compare structures */
        ASSERT_EQUAL(json_cfg->type, yaml_cfg->type, "Type mismatch");
        ASSERT_EQUAL(json_cfg->as_is_cfg->index_count, yaml_cfg->as_is_cfg->index_count, "Index count mismatch");

        ldmsd_decomp_as_is_index_t *json_idx = &json_cfg->as_is_cfg->indices[0];
        ldmsd_decomp_as_is_index_t *yaml_idx = &yaml_cfg->as_is_cfg->indices[0];

        ASSERT_STR_EQUAL(json_idx->name, yaml_idx->name, "Index name mismatch");
        ASSERT_EQUAL(json_idx->col_count, yaml_idx->col_count, "Column count mismatch");
        ASSERT_STR_EQUAL(json_idx->cols[0], yaml_idx->cols[0], "Column 0 mismatch");

        ldmsd_decomp_config_free(json_cfg);
        ldmsd_decomp_config_free(yaml_cfg);
        TEST_PASS();
}

/*
 * Test 8: ldmsd_decomp_cfg_type_str helper
 */
static void test_type_str(void)
{
        TEST_START("ldmsd_decomp_cfg_type_str helper");

        ASSERT_STR_EQUAL(ldmsd_decomp_cfg_type_str(LDMSD_DECOMP_CFG_TYPE_STATIC), "static", "Wrong static string");
        ASSERT_STR_EQUAL(ldmsd_decomp_cfg_type_str(LDMSD_DECOMP_CFG_TYPE_AS_IS), "as_is", "Wrong as_is string");
        ASSERT_STR_EQUAL(ldmsd_decomp_cfg_type_str(LDMSD_DECOMP_CFG_TYPE_FLEX), "flex", "Wrong flex string");

        TEST_PASS();
}

/*
 * Test 9: Complex static decomposition with multiple indices
 */
static void test_static_complex_json(void)
{
        TEST_START("Parse complex static from JSON");

        char err_buf[512];
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file(P("test/test_static_complex.json"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(cfg, err_buf);

        ASSERT_EQUAL(cfg->type, LDMSD_DECOMP_CFG_TYPE_STATIC, "Wrong config type");
        ASSERT_NOT_NULL(cfg->static_cfg, "static_cfg is NULL");
        ASSERT_EQUAL(cfg->static_cfg->row_count, 1, "Wrong row count");

        ldmsd_decomp_static_row_t *row = &cfg->static_cfg->rows[0];
        ASSERT_STR_EQUAL(row->schema, "network_metrics", "Wrong schema");
        ASSERT_EQUAL(row->col_count, 14, "Wrong column count");
        ASSERT_EQUAL(row->index_count, 2, "Wrong index count");

        /* Check indices */
        ASSERT_STR_EQUAL(row->indices[0].name, "device_idx", "Wrong index 0 name");
        ASSERT_EQUAL(row->indices[0].col_count, 2, "Wrong index 0 col count");

        ldmsd_decomp_config_free(cfg);
        TEST_PASS();
}

/*
 * Test 10: Complex static decomposition YAML
 */
static void test_static_complex_yaml(void)
{
        SKIP_UNLESS_YAML("Parse complex static from YAML");
        TEST_START("Parse complex static from YAML");

        char err_buf[512];
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file(P("test/test_static_complex.yaml"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(cfg, err_buf);

        ASSERT_EQUAL(cfg->type, LDMSD_DECOMP_CFG_TYPE_STATIC, "Wrong config type");
        ASSERT_EQUAL(cfg->static_cfg->rows[0].index_count, 2, "Wrong index count");

        ldmsd_decomp_config_free(cfg);
        TEST_PASS();
}

/*
 * Test 11: Full flex decomposition with digest, matches, default
 */
static void test_flex_full_json(void)
{
        TEST_START("Parse full flex from JSON");

        char err_buf[512];
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file(P("test/test_flex_full.json"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(cfg, err_buf);

        ASSERT_EQUAL(cfg->type, LDMSD_DECOMP_CFG_TYPE_FLEX, "Wrong config type");
        ASSERT_NOT_NULL(cfg->flex_cfg, "flex_cfg is NULL");

        /* Check decompositions */
        ASSERT_EQUAL(cfg->flex_cfg->decomp_count, 3, "Wrong decomp count");
        ASSERT_STR_EQUAL(cfg->flex_cfg->decompositions[0].name, "network_decomp", "Wrong decomp 0 name");

        /* Check digest map */
        ASSERT_EQUAL(cfg->flex_cfg->digest_count, 3, "Wrong digest count");

        /* Check matches */
        ASSERT_EQUAL(cfg->flex_cfg->match_count, 3, "Wrong match count");
        ASSERT_STR_EQUAL(cfg->flex_cfg->matches[0].schema, "^network.*", "Wrong match 0 schema");

        /* Check default */
        ASSERT_NOT_NULL(cfg->flex_cfg->default_decomp, "default_decomp is NULL");
        ASSERT_STR_EQUAL(cfg->flex_cfg->default_decomp, "cpu_decomp", "Wrong default decomp");

        ldmsd_decomp_config_free(cfg);
        TEST_PASS();
}

/*
 * Test 12: Full flex decomposition YAML
 */
static void test_flex_full_yaml(void)
{
        SKIP_UNLESS_YAML("Parse full flex from YAML");
        TEST_START("Parse full flex from YAML");

        char err_buf[512];
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file(P("test/test_flex_full.yaml"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(cfg, err_buf);

        ASSERT_EQUAL(cfg->type, LDMSD_DECOMP_CFG_TYPE_FLEX, "Wrong config type");
        ASSERT_EQUAL(cfg->flex_cfg->decomp_count, 3, "Wrong decomp count");
        ASSERT_EQUAL(cfg->flex_cfg->match_count, 3, "Wrong match count");

        ldmsd_decomp_config_free(cfg);
        TEST_PASS();
}

/*
 * Test 13: JSON/YAML equivalence for complex configs
 */
static void test_complex_equivalence(void)
{
        SKIP_UNLESS_YAML("JSON/YAML equivalence (complex)");
        TEST_START("JSON/YAML equivalence (complex)");

        char err_buf[512];
        ldmsd_decomp_config_t *json_cfg, *yaml_cfg;

        /* Test static complex */
        json_cfg = ldmsd_decomp_config_parse_file(P("test/test_static_complex.json"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(json_cfg, "Failed to parse complex JSON");

        yaml_cfg = ldmsd_decomp_config_parse_file(P("test/test_static_complex.yaml"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(yaml_cfg, "Failed to parse complex YAML");

        ASSERT_EQUAL(json_cfg->type, yaml_cfg->type, "Type mismatch");
        ASSERT_EQUAL(json_cfg->static_cfg->rows[0].col_count,
                     yaml_cfg->static_cfg->rows[0].col_count, "Col count mismatch");

        ldmsd_decomp_config_free(json_cfg);
        ldmsd_decomp_config_free(yaml_cfg);

        /* Test flex full */
        json_cfg = ldmsd_decomp_config_parse_file(P("test/test_flex_full.json"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(json_cfg, "Failed to parse flex JSON");

        yaml_cfg = ldmsd_decomp_config_parse_file(P("test/test_flex_full.yaml"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(yaml_cfg, "Failed to parse flex YAML");

        ASSERT_EQUAL(json_cfg->flex_cfg->decomp_count,
                     yaml_cfg->flex_cfg->decomp_count, "Decomp count mismatch");

        ldmsd_decomp_config_free(json_cfg);
        ldmsd_decomp_config_free(yaml_cfg);

        TEST_PASS();
}

/*
 * Location API tests
 */
#define ASSERT_LOC(node, key, exp_rc, exp_line, exp_col, msg) \
        do { \
                ldmsd_decomp_cfg_loc_t _loc; \
                int _rc = ldmsd_decomp_cfg_loc((node), (key), &_loc); \
                ASSERT_EQUAL(_rc, (exp_rc), msg ": rc"); \
                ASSERT_EQUAL(_loc.line, (exp_line), msg ": line"); \
                ASSERT_EQUAL(_loc.column, (exp_col), msg ": column"); \
        } while (0)

static void test_loc_static_yaml(void)
{
        SKIP_UNLESS_YAML("Location lookup in static YAML");
        TEST_START("Location lookup in static YAML");

        char err_buf[512];
        char loc_buf[4096];
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file(P("test/test_static.yaml"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(cfg, err_buf);

        ldmsd_decomp_static_row_t *row = &cfg->static_cfg->rows[0];
        ASSERT_EQUAL(row->col_count, 4, "Wrong column count");

        /* Root mapping starts at "type:" on line 2 */
        ASSERT_LOC(cfg->node, NULL, 0, 2, 1, "root");
        ASSERT_LOC(cfg->static_cfg->node, "type", 0, 2, 7, "type value");

        /* Row mapping starts at "schema:" on line 4 */
        ASSERT_LOC(row->node, NULL, 0, 4, 5, "row");
        ASSERT_LOC(row->node, "schema", 0, 4, 13, "row schema");

        /* Column 3 ("value") starts at line 16; its "fill" is line 19 */
        ASSERT_LOC(row->cols[3].node, NULL, 0, 16, 9, "col 3");
        ASSERT_LOC(row->cols[3].node, "src", 0, 16, 14, "col 3 src");
        ASSERT_LOC(row->cols[3].node, "fill", 0, 19, 15, "col 3 fill");

        /* Missing key falls back to the enclosing node with ENOENT */
        ASSERT_LOC(row->cols[0].node, "op", ENOENT, 6, 9, "missing op");

        /* Sequence item lookup: index cols[0] is "producer" on line 23 */
        ldmsd_decomp_static_index_t *idx = &row->indices[0];
        ldmsd_decomp_cfg_node_t cols = ldmsd_decomp_cfg_node_key(idx->node, "cols");
        ASSERT_NOT_NULL(cols, "index cols node");
        ASSERT_LOC(ldmsd_decomp_cfg_node_item(cols, 0), NULL, 0, 23, 13, "index col 0");
        if (ldmsd_decomp_cfg_node_item(cols, 1) != NULL) {
                TEST_FAIL("Out of range item should be NULL");
                return;
        }
        if (ldmsd_decomp_cfg_node_item(idx->node, 0) != NULL) {
                TEST_FAIL("Item of a mapping should be NULL");
                return;
        }

        /* String form */
        ldmsd_decomp_cfg_loc_str(row->cols[3].node, "fill", loc_buf, sizeof(loc_buf));
        char exp[4096];
        snprintf(exp, sizeof(exp), "%s:19:15", P("test/test_static.yaml"));
        ASSERT_STR_EQUAL(loc_buf, exp, "loc string");
        snprintf(exp, sizeof(exp), "%s:4:13", P("test/test_static.yaml"));
        ASSERT_STR_EQUAL(LDMSD_DECOMP_CFG_LOC(row->node, "schema"),
                         exp, "loc macro");
        ASSERT_STR_EQUAL(LDMSD_DECOMP_CFG_LOC(NULL, "x"),
                         "<unknown location>", "NULL node string");

        /* NULL node */
        ldmsd_decomp_cfg_loc_t loc;
        ASSERT_EQUAL(ldmsd_decomp_cfg_loc(NULL, NULL, &loc), EINVAL, "NULL node rc");
        ASSERT_EQUAL(loc.line, 0, "NULL node line");

        ldmsd_decomp_config_free(cfg);
        TEST_PASS();
}

static void test_loc_flex_json(void)
{
        if (!parser_yaml) {
                printf("TEST: Location lookup in flex JSON ... SKIP (%s does not record locations)\n",
                       parser);
                return;
        }
        TEST_START("Location lookup in flex JSON");

        char err_buf[512];
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file(P("test/test_flex.json"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(cfg, err_buf);

        ldmsd_decomp_flex_config_t *f = cfg->flex_cfg;
        ASSERT_EQUAL(f->decomp_count, 1, "Wrong decomp count");
        ASSERT_EQUAL(f->digest_count, 1, "Wrong digest count");

        /* Nested decomposition mapping value starts at '{' on line 4 */
        ASSERT_LOC(f->decompositions[0].node, NULL, 0, 4, 31, "nested decomp");
        ASSERT_LOC(f->decompositions[0].node, "type", 0, 5, 33, "nested type");

        /* digest "*": "as_is_test" on line 10 */
        ASSERT_LOC(f->digest_map[0].digest_node, NULL, 0, 10, 17, "digest key");
        ASSERT_LOC(f->digest_map[0].name_node, NULL, 0, 10, 22, "digest name");

        /* No "default" key: fall back to the flex root */
        ASSERT_LOC(f->node, "default", ENOENT, 1, 1, "missing default");

        ldmsd_decomp_config_free(cfg);
        TEST_PASS();
}

/*
 * Tests below exercise behavior that must be identical across parsers
 */

/* Parse a file that must fail; err_buf must mention \c expect */
static int expect_parse_fail(const char *path, const char *expect,
                             char *err_buf, size_t err_sz)
{
        ldmsd_decomp_config_t *cfg;

        err_buf[0] = '\0';
        cfg = ldmsd_decomp_config_parse_file(path, err_buf, err_sz);
        if (cfg) {
                ldmsd_decomp_config_free(cfg);
                TEST_FAIL("Expected parse failure");
                return -1;
        }
        if (!strstr(err_buf, expect)) {
                char buf[1024];
                snprintf(buf, sizeof(buf), "error '%s' does not contain '%s'",
                         err_buf, expect);
                TEST_FAIL(buf);
                return -1;
        }
        return 0;
}

static void test_bad_syntax_json(void)
{
        char err_buf[512];
        char exp[4096];

        TEST_START("Syntax error in JSON");
        /* All parsers report the file and the line of the error (line 4) */
        snprintf(exp, sizeof(exp), "%s:4:", P("test/test_bad_syntax.json"));
        if (expect_parse_fail(P("test/test_bad_syntax.json"), exp,
                              err_buf, sizeof(err_buf)))
                return;
        TEST_PASS();
}

static void test_bad_syntax_yaml(void)
{
        char err_buf[512];

        TEST_START("Syntax error in YAML");
        /* Every parser must reject this, including JSON-only ones */
        if (expect_parse_fail(P("test/test_bad_syntax.yaml"),
                              P("test/test_bad_syntax.yaml"),
                              err_buf, sizeof(err_buf)))
                return;
        TEST_PASS();
}

static void test_yaml_rejected_by_json_parser(void)
{
        char err_buf[512];

        if (parser_yaml)
                return;
        TEST_START("JSON-only parser rejects YAML");
        if (expect_parse_fail(P("test/test_static.yaml"), "JSON parse error",
                              err_buf, sizeof(err_buf)))
                return;
        TEST_PASS();
}

static void test_dup_key(void)
{
        char err_buf[512];

        TEST_START("Duplicate key is an error");
        if (expect_parse_fail(P("test/test_dup_key.json"), "uplicate",
                              err_buf, sizeof(err_buf)))
                return;
        TEST_PASS();
}

static void test_alias_yaml(void)
{
        SKIP_UNLESS_YAML("YAML anchors and aliases");
        TEST_START("YAML anchors and aliases");

        char err_buf[512];
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file(P("test/test_alias.yaml"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(cfg, err_buf);

        ASSERT_EQUAL(cfg->static_cfg->row_count, 2, "Wrong row count");
        for (int r = 0; r < 2; r++) {
                ldmsd_decomp_static_row_t *row = &cfg->static_cfg->rows[r];
                ASSERT_EQUAL(row->col_count, 2, "Wrong column count");
                ASSERT_STR_EQUAL(row->cols[0].src, "timestamp", "Wrong col 0 src");
                ASSERT_STR_EQUAL(row->cols[1].type, "u64", "Wrong col 1 type");
                /* Aliased columns point at the anchored definition */
                ASSERT_LOC(row->cols[1].node, "src", 0, 8, 14, "aliased col");
        }
        ASSERT_STR_EQUAL(cfg->static_cfg->rows[1].schema, "second", "Wrong schema");

        ldmsd_decomp_config_free(cfg);
        TEST_PASS();
}

static void test_scalars_json(void)
{
        TEST_START("JSON scalar conversion");

        char err_buf[512];
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file(P("test/test_scalars.json"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(cfg, err_buf);

        ldmsd_decomp_static_row_t *row = &cfg->static_cfg->rows[0];
        ASSERT_EQUAL(row->col_count, 6, "Wrong column count");
        ASSERT_NOT_NULL(row->cols[0].fill, "int fill is NULL");
        ASSERT_STR_EQUAL(row->cols[0].fill[0], "0", "int fill");
        ASSERT_EQUAL(row->cols[0].fill_count, 1, "int fill_count");
        ASSERT_EQUAL(row->cols[0].fill_is_array, 0, "int fill_is_array");
        ASSERT_STR_EQUAL(row->cols[1].fill[0], "-42", "negative fill");
        ASSERT_STR_EQUAL(row->cols[2].fill[0], "abc", "string fill");
        ASSERT_EQUAL(row->cols[2].array_len, 16, "array_len");
        ASSERT_STR_EQUAL(row->cols[3].fill[0], "true", "bool fill");

        /* Array fill: one string per element */
        ASSERT_NOT_NULL(row->cols[4].fill, "array fill is NULL");
        ASSERT_EQUAL(row->cols[4].fill_is_array, 1, "array fill_is_array");
        ASSERT_EQUAL(row->cols[4].fill_count, 3, "array fill_count");
        ASSERT_STR_EQUAL(row->cols[4].fill[0], "1", "array fill[0]");
        ASSERT_STR_EQUAL(row->cols[4].fill[2], "3.5", "array fill[2]");

        /* No fill */
        if (row->cols[5].fill != NULL) {
                TEST_FAIL("fill should be NULL when absent");
                return;
        }

        ASSERT_NOT_NULL(row->group, "group is NULL");
        ASSERT_EQUAL(row->group->index_count, 1, "group index count");
        ASSERT_EQUAL(row->group->order_count, 2, "group order count");
        ASSERT_STR_EQUAL(row->group->order[1], "b", "group order[1]");
        ASSERT_EQUAL(row->group->limit, 5, "group limit");
        ASSERT_STR_EQUAL(row->group->timeout, "60s", "group timeout");

        ldmsd_decomp_config_free(cfg);
        TEST_PASS();
}

/*
 * Integers above the signed 64-bit maximum: jansson cannot represent them
 * and rejects the file ("too big integer"), while the YAML parsers keep
 * the digits as written.  The s64 maximum itself parses with every parser.
 */
static void test_s64_max_json(void)
{
        TEST_START("Integer at the s64 maximum");

        char err_buf[512];
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file(P("test/test_s64_max.json"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(cfg, err_buf);

        ldmsd_decomp_static_col_t *col = &cfg->static_cfg->rows[0].cols[0];
        ASSERT_NOT_NULL(col->fill, "fill is NULL");
        ASSERT_STR_EQUAL(col->fill[0], "9223372036854775807", "s64 max fill");

        ldmsd_decomp_config_free(cfg);
        TEST_PASS();
}

static void test_big_int_json(void)
{
        char err_buf[512];

        if (!parser_yaml) {
                TEST_START("Integer above the s64 maximum is rejected (jansson limit)");
                if (expect_parse_fail(P("test/test_big_int.json"), "too big integer",
                                      err_buf, sizeof(err_buf)))
                        return;
                TEST_PASS();
                return;
        }

        TEST_START("Integer above the s64 maximum");
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file(P("test/test_big_int.json"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(cfg, err_buf);

        ldmsd_decomp_static_row_t *row = &cfg->static_cfg->rows[0];
        ASSERT_EQUAL(row->col_count, 2, "Wrong column count");
        ASSERT_STR_EQUAL(row->cols[0].fill[0], "9223372036854775807", "s64 max fill");
        ASSERT_STR_EQUAL(row->cols[1].fill[0], "18446744073709551615", "u64 max fill");

        ldmsd_decomp_config_free(cfg);
        TEST_PASS();
}

static void test_loc_no_positions(void)
{
        if (parser_yaml)
                return;
        TEST_START("Location lookup without positions");

        char err_buf[512];
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file(P("test/test_flex.json"), err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(cfg, err_buf);

        ldmsd_decomp_flex_config_t *f = cfg->flex_cfg;
        /* The node tree is still navigable; only line/column are unknown */
        ASSERT_LOC(f->decompositions[0].node, "type", 0, 0, 0, "nested type");
        ASSERT_LOC(f->node, "default", ENOENT, 0, 0, "missing default");
        ASSERT_STR_EQUAL(LDMSD_DECOMP_CFG_LOC(f->node, "digest"),
                         P("test/test_flex.json"), "loc string is file only");

        ldmsd_decomp_config_free(cfg);
        TEST_PASS();
}

static void usage(const char *prog)
{
        printf("usage: %s [-d SRCDIR] [-e EXPECTED_PARSER] [-u] | --list\n"
               "  --list  print the names of the parsers that were built\n"
               "  -d      directory containing test/ data files\n"
               "  -e      fail unless EXPECTED_PARSER is the selected parser\n"
               "  -u      expect " LDMSD_DECOMP_PARSER_ENV " to name an unknown\n"
               "          parser: check that parsing fails, run no other tests\n",
               prog);
}

/* -u: LDMSD_DECOMP_PARSER names a parser that is not built */
static int test_unknown_parser(void)
{
        char err_buf[512];
        ldmsd_decomp_config_t *cfg;

        printf("TEST: Unknown parser is rejected ... ");
        if (ldmsd_decomp_parser_selected() != NULL) {
                printf("FAIL: a parser was selected\n");
                return 1;
        }
        err_buf[0] = '\0';
        cfg = ldmsd_decomp_config_parse_file(P("test/test_as_is.json"),
                                             err_buf, sizeof(err_buf));
        if (cfg) {
                ldmsd_decomp_config_free(cfg);
                printf("FAIL: parse succeeded\n");
                return 1;
        }
        if (!strstr(err_buf, LDMSD_DECOMP_PARSER_ENV) ||
            !strstr(err_buf, ldmsd_decomp_parser_name(0))) {
                printf("FAIL: unexpected error '%s'\n", err_buf);
                return 1;
        }
        printf("PASS (%s)\n", err_buf);
        return 0;
}

/*
 * Main test runner
 */
int main(int argc, char **argv)
{
        const char *expect = NULL;
        int unknown = 0;
        int i, opt;

        if (argc == 2 && strcmp(argv[1], "--list") == 0) {
                for (i = 0; i < ldmsd_decomp_parser_count(); i++)
                        printf("%s\n", ldmsd_decomp_parser_name(i));
                return 0;
        }
        while ((opt = getopt(argc, argv, "d:e:uh")) != -1) {
                switch (opt) {
                case 'd':
                        srcdir = optarg;
                        break;
                case 'e':
                        expect = optarg;
                        break;
                case 'u':
                        unknown = 1;
                        break;
                default:
                        usage(argv[0]);
                        return opt == 'h' ? 0 : 2;
                }
        }

        if (unknown)
                return test_unknown_parser();

        parser = ldmsd_decomp_parser_selected();
        if (!parser) {
                printf("FAIL: %s='%s' is not a built parser\n",
                       LDMSD_DECOMP_PARSER_ENV, getenv(LDMSD_DECOMP_PARSER_ENV));
                return 1;
        }
        if (expect && strcmp(parser, expect) != 0) {
                printf("FAIL: selected parser is '%s', expected '%s'\n",
                       parser, expect);
                return 1;
        }
        parser_yaml = ldmsd_decomp_parser_supports_yaml(parser);

        printf("=== Decomposition Config Parser Unit Tests (%s) ===\n\n", parser);

        /* Run all tests */
        test_as_is_json();
        test_as_is_yaml();
        test_static_json();
        test_static_yaml();
        test_flex_json();
        test_error_missing_file();
        test_json_yaml_equivalence();
        test_type_str();
        test_static_complex_json();
        test_static_complex_yaml();
        test_flex_full_json();
        test_flex_full_yaml();
        test_complex_equivalence();
        test_loc_static_yaml();
        test_loc_flex_json();
        test_bad_syntax_json();
        test_bad_syntax_yaml();
        test_yaml_rejected_by_json_parser();
        test_dup_key();
        test_alias_yaml();
        test_scalars_json();
        test_s64_max_json();
        test_big_int_json();
        test_loc_no_positions();

        /* Print summary */
        printf("\n=== Test Summary ===\n");
        printf("Total:  %d\n", tests_run);
        printf("Passed: %d\n", tests_passed);
        printf("Failed: %d\n", tests_failed);

        if (tests_failed == 0) {
                printf("\nAll tests PASSED!\n");
                return 0;
        } else {
                printf("\nSome tests FAILED!\n");
                return 1;
        }
}
