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
#include "decomp_config_parser.h"

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

#define ASSERT_EQUAL(a, b, msg) \
        do { \
                if ((a) != (b)) { \
                        char buf[256]; \
                        snprintf(buf, sizeof(buf), "%s (expected %d, got %d)", msg, (int)(b), (int)(a)); \
                        TEST_FAIL(buf); \
                        return; \
                } \
        } while (0)

#define ASSERT_STR_EQUAL(a, b, msg) \
        do { \
                if (strcmp((a), (b)) != 0) { \
                        char buf[256]; \
                        snprintf(buf, sizeof(buf), "%s (expected '%s', got '%s')", msg, b, a); \
                        TEST_FAIL(buf); \
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
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file("test/test_as_is.json", err_buf, sizeof(err_buf));
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
        TEST_START("Parse as_is from YAML");

        char err_buf[512];
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file("test/test_as_is.yaml", err_buf, sizeof(err_buf));
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
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file("test/test_static.json", err_buf, sizeof(err_buf));
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
        TEST_START("Parse static from YAML");

        char err_buf[512];
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file("test/test_static.yaml", err_buf, sizeof(err_buf));
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
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file("test/test_flex.json", err_buf, sizeof(err_buf));
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
        TEST_START("JSON/YAML equivalence");

        char err_buf[512];
        ldmsd_decomp_config_t *json_cfg = ldmsd_decomp_config_parse_file("test/test_as_is.json", err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(json_cfg, "Failed to parse JSON");

        ldmsd_decomp_config_t *yaml_cfg = ldmsd_decomp_config_parse_file("test/test_as_is.yaml", err_buf, sizeof(err_buf));
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
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file("test/test_static_complex.json", err_buf, sizeof(err_buf));
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
        TEST_START("Parse complex static from YAML");

        char err_buf[512];
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file("test/test_static_complex.yaml", err_buf, sizeof(err_buf));
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
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file("test/test_flex_full.json", err_buf, sizeof(err_buf));
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
        TEST_START("Parse full flex from YAML");

        char err_buf[512];
        ldmsd_decomp_config_t *cfg = ldmsd_decomp_config_parse_file("test/test_flex_full.yaml", err_buf, sizeof(err_buf));
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
        TEST_START("JSON/YAML equivalence (complex)");

        char err_buf[512];
        ldmsd_decomp_config_t *json_cfg, *yaml_cfg;

        /* Test static complex */
        json_cfg = ldmsd_decomp_config_parse_file("test/test_static_complex.json", err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(json_cfg, "Failed to parse complex JSON");

        yaml_cfg = ldmsd_decomp_config_parse_file("test/test_static_complex.yaml", err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(yaml_cfg, "Failed to parse complex YAML");

        ASSERT_EQUAL(json_cfg->type, yaml_cfg->type, "Type mismatch");
        ASSERT_EQUAL(json_cfg->static_cfg->rows[0].col_count,
                     yaml_cfg->static_cfg->rows[0].col_count, "Col count mismatch");

        ldmsd_decomp_config_free(json_cfg);
        ldmsd_decomp_config_free(yaml_cfg);

        /* Test flex full */
        json_cfg = ldmsd_decomp_config_parse_file("test/test_flex_full.json", err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(json_cfg, "Failed to parse flex JSON");

        yaml_cfg = ldmsd_decomp_config_parse_file("test/test_flex_full.yaml", err_buf, sizeof(err_buf));
        ASSERT_NOT_NULL(yaml_cfg, "Failed to parse flex YAML");

        ASSERT_EQUAL(json_cfg->flex_cfg->decomp_count,
                     yaml_cfg->flex_cfg->decomp_count, "Decomp count mismatch");

        ldmsd_decomp_config_free(json_cfg);
        ldmsd_decomp_config_free(yaml_cfg);

        TEST_PASS();
}

/*
 * Main test runner
 */
int main(int argc, char **argv)
{
        printf("=== Decomposition Config Parser Unit Tests ===\n\n");

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
