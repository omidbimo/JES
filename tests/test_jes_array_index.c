/**
 * test_jes_array_index.c
 *
 * Regression tests for negative/out-of-range/extreme index handling in
 * jes_get_array_value(), jes_add_array_value() and jes_update_array_value().
 *
 * Background: the original implementation converted a negative index to
 * "distance from end" via `-index <= array_size`, which is undefined
 * behaviour when index == INT32_MIN (and later, once the signature moved
 * to int64_t, when index == INT64_MIN). The fix replaced the negate-then-
 * compare pattern with an add-then-check pattern (`index = array_size +
 * index`, then check `index < 0`), which never negates the input and is
 * therefore safe at any index value, on an array of any size including
 * zero. These tests pin that behaviour down so a future change can't
 * silently reintroduce the negation pattern.
 *
 * Build (from repo root):
 *   gcc tests/test_jes_array_index.c src/jes.c src/jes_tokenizer.c src/jes_parser.c \
 *       src/jes_serializer.c src/jes_tree.c src/jes_hash_table.c \
 *       src/jes_logger.c -std=c99 -DNDEBUG -o test_jes_array_index
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../src/jes.h"

/* =========================================================================
 * Test harness
 * ========================================================================= */

static int g_passed = 0;
static int g_failed = 0;

#define PASS(id)         do { printf("  [PASS] %s\n", id); g_passed++; } while(0)
#define FAIL(id, reason) do { printf("  [FAIL] %s - %s\n", id, reason); g_failed++; } while(0)

/* Expect result == NULL and ctx status == expected_status */
#define CHECK_NULL_STATUS(id, result, ctx, expected_status)                \
    do {                                                                    \
        if ((result) != NULL) {                                             \
            FAIL(id, "expected NULL return, got non-NULL");                 \
        } else if (jes_get_status(ctx) != (expected_status)) {             \
            char _m[64];                                                    \
            snprintf(_m, sizeof(_m), "status %d, expected %d",             \
                     (int)jes_get_status(ctx), (int)(expected_status));     \
            FAIL(id, _m);                                                   \
        } else {                                                            \
            PASS(id);                                                       \
        }                                                                   \
    } while(0)

/* Expect result != NULL and its value matches expected_str (length-aware,
 * value is NOT null-terminated in the tree). */
#define CHECK_VALUE(id, result, expected_str)                              \
    do {                                                                    \
        if ((result) == NULL) {                                             \
            FAIL(id, "expected non-NULL, got NULL");                        \
        } else if (((result)->length != strlen(expected_str)) ||            \
                   (memcmp((result)->value, expected_str, (result)->length) != 0)) { \
            FAIL(id, "value mismatch");                                     \
        } else {                                                            \
            PASS(id);                                                       \
        }                                                                   \
    } while(0)

/* =========================================================================
 * Shared fixtures
 * ========================================================================= */

/* Three-element array: ["a","b","c"] */
static const char ARRAY3_JSON[] = "[\"a\",\"b\",\"c\"]";

/* Empty array: [] */
static const char ARRAY0_JSON[] = "[]";

static uint8_t  g_workspace[JES_REQUIRED_SIZE(128)];
static struct jes_context *g_ctx = NULL;
static struct jes_element *g_root = NULL;

static int setup(const char *json)
{
    memset(g_workspace, 0, sizeof(g_workspace));
    g_ctx = jes_init(g_workspace, sizeof(g_workspace), JES_SEARCH_LINEAR);
    if (!g_ctx) return -1;
    if (jes_load(g_ctx, json, strlen(json)) != JES_NO_ERROR) return -1;
    g_root = jes_get_root(g_ctx);
    return g_root ? 0 : -1;
}

/* =========================================================================
 * Group 1 - jes_get_array_value() on a populated array (["a","b","c"])
 * ========================================================================= */

static void test_group_get_populated(void)
{
    struct jes_element *v;

    printf("\nGroup 1: jes_get_array_value() on [\"a\",\"b\",\"c\"]\n");
    if (setup(ARRAY3_JSON) != 0) { FAIL("G1-setup", "load failed"); return; }

    v = jes_get_array_value(g_ctx, g_root, 0);
    CHECK_VALUE("G1-01 index 0 -> a", v, "a");

    v = jes_get_array_value(g_ctx, g_root, 2);
    CHECK_VALUE("G1-02 last valid index -> c", v, "c");

    v = jes_get_array_value(g_ctx, g_root, 3);
    CHECK_NULL_STATUS("G1-03 index == array_size -> not found", v, g_ctx, JES_ELEMENT_NOT_FOUND);

    v = jes_get_array_value(g_ctx, g_root, -1);
    CHECK_VALUE("G1-04 index -1 -> c (last)", v, "c");

    v = jes_get_array_value(g_ctx, g_root, -3);
    CHECK_VALUE("G1-05 index -array_size -> a (first)", v, "a");

    v = jes_get_array_value(g_ctx, g_root, -4);
    CHECK_NULL_STATUS("G1-06 index -(array_size+1) -> not found", v, g_ctx, JES_ELEMENT_NOT_FOUND);

    v = jes_get_array_value(g_ctx, g_root, INT64_MIN);
    CHECK_NULL_STATUS("G1-07 INT64_MIN does not crash, not found", v, g_ctx, JES_ELEMENT_NOT_FOUND);

    v = jes_get_array_value(g_ctx, g_root, INT64_MAX);
    CHECK_NULL_STATUS("G1-08 INT64_MAX does not crash, not found", v, g_ctx, JES_ELEMENT_NOT_FOUND);
}

/* =========================================================================
 * Group 2 - jes_get_array_value() on an empty array ([])
 * ========================================================================= */

static void test_group_get_empty(void)
{
    struct jes_element *v;

    printf("\nGroup 2: jes_get_array_value() on []\n");
    if (setup(ARRAY0_JSON) != 0) { FAIL("G2-setup", "load failed"); return; }

    v = jes_get_array_value(g_ctx, g_root, 0);
    CHECK_NULL_STATUS("G2-01 index 0 on empty array -> not found", v, g_ctx, JES_ELEMENT_NOT_FOUND);

    v = jes_get_array_value(g_ctx, g_root, -1);
    CHECK_NULL_STATUS("G2-02 index -1 on empty array -> not found", v, g_ctx, JES_ELEMENT_NOT_FOUND);

    v = jes_get_array_value(g_ctx, g_root, INT64_MIN);
    CHECK_NULL_STATUS("G2-03 INT64_MIN on empty array does not crash", v, g_ctx, JES_ELEMENT_NOT_FOUND);

    v = jes_get_array_value(g_ctx, g_root, INT64_MAX);
    CHECK_NULL_STATUS("G2-04 INT64_MAX on empty array does not crash", v, g_ctx, JES_ELEMENT_NOT_FOUND);
}

/* =========================================================================
 * Group 3 - jes_update_array_value() boundary and extreme indices
 * ========================================================================= */

static void test_group_update(void)
{
    struct jes_element *v;

    printf("\nGroup 3: jes_update_array_value() on [\"a\",\"b\",\"c\"]\n");
    if (setup(ARRAY3_JSON) != 0) { FAIL("G3-setup", "load failed"); return; }

    /* In-range update by positive index */
    v = jes_update_array_value(g_ctx, g_root, 1, JES_STRING, "B", 1);
    CHECK_VALUE("G3-01 update index 1", v, "B");
    v = jes_get_array_value(g_ctx, g_root, 1);
    CHECK_VALUE("G3-02 read-back index 1", v, "B");

    /* In-range update by negative index (same element) */
    v = jes_update_array_value(g_ctx, g_root, -2, JES_STRING, "BB", 2);
    CHECK_VALUE("G3-03 update index -2 (same slot as index 1)", v, "BB");

    /* Out-of-range positive index */
    v = jes_update_array_value(g_ctx, g_root, 3, JES_STRING, "x", 1);
    CHECK_NULL_STATUS("G3-04 index == array_size rejected", v, g_ctx, JES_ELEMENT_NOT_FOUND);

    /* Extreme values must not crash and must be rejected cleanly */
    v = jes_update_array_value(g_ctx, g_root, INT64_MIN, JES_STRING, "x", 1);
    CHECK_NULL_STATUS("G3-05 INT64_MIN does not crash", v, g_ctx, JES_ELEMENT_NOT_FOUND);

    v = jes_update_array_value(g_ctx, g_root, INT64_MAX, JES_STRING, "x", 1);
    CHECK_NULL_STATUS("G3-06 INT64_MAX does not crash", v, g_ctx, JES_ELEMENT_NOT_FOUND);
}

static void test_group_update_empty(void)
{
    struct jes_element *v;

    printf("\nGroup 4: jes_update_array_value() on []\n");
    if (setup(ARRAY0_JSON) != 0) { FAIL("G4-setup", "load failed"); return; }

    v = jes_update_array_value(g_ctx, g_root, 0, JES_STRING, "x", 1);
    CHECK_NULL_STATUS("G4-01 update index 0 on empty array rejected", v, g_ctx, JES_ELEMENT_NOT_FOUND);

    v = jes_update_array_value(g_ctx, g_root, INT64_MIN, JES_STRING, "x", 1);
    CHECK_NULL_STATUS("G4-02 INT64_MIN on empty array does not crash", v, g_ctx, JES_ELEMENT_NOT_FOUND);
}

/* =========================================================================
 * Group 5 - jes_add_array_value() clamp-to-prepend/append behaviour
 * ========================================================================= */

static void test_group_add(void)
{
    struct jes_element *v;
    size_t size_before, size_after;

    printf("\nGroup 5: jes_add_array_value() on [\"a\",\"b\",\"c\"]\n");
    if (setup(ARRAY3_JSON) != 0) { FAIL("G5-setup", "load failed"); return; }

    /* Negative index far below range clamps to prepend (front of array) */
    size_before = jes_get_array_size(g_ctx, g_root);
    v = jes_add_array_value(g_ctx, g_root, INT64_MIN, JES_STRING, "z", 1);
    size_after = jes_get_array_size(g_ctx, g_root);
    CHECK_VALUE("G5-01 INT64_MIN clamps to prepend, does not crash", v, "z");
    if (size_after == size_before + 1)
        PASS("G5-02 array grew by one element");
    else
        FAIL("G5-02 array grew by one element", "size mismatch");
    v = jes_get_array_value(g_ctx, g_root, 0);
    CHECK_VALUE("G5-03 prepended element is now first", v, "z");

    /* Positive index far above range clamps to append (back of array) */
    v = jes_add_array_value(g_ctx, g_root, INT64_MAX, JES_STRING, "zz", 2);
    CHECK_VALUE("G5-04 INT64_MAX clamps to append, does not crash", v, "zz");
    v = jes_get_array_value(g_ctx, g_root, -1);
    CHECK_VALUE("G5-05 appended element is now last", v, "zz");
}

static void test_group_add_middle(void)
{
    struct jes_element *v;

    printf("\nGroup 7: jes_add_array_value() insert at a middle index\n");
    if (setup(ARRAY3_JSON) != 0) { FAIL("G7-setup", "load failed"); return; }

    /* ["a","b","c"], insert "x" at index 1 -> expect ["a","x","b","c"] */
    v = jes_add_array_value(g_ctx, g_root, 1, JES_STRING, "x", 1);
    CHECK_VALUE("G7-01 insert returns new element", v, "x");

    v = jes_get_array_value(g_ctx, g_root, 0);
    CHECK_VALUE("G7-02 index 0 still a", v, "a");
    v = jes_get_array_value(g_ctx, g_root, 1);
    CHECK_VALUE("G7-03 index 1 is now x", v, "x");
    v = jes_get_array_value(g_ctx, g_root, 2);
    CHECK_VALUE("G7-04 index 2 is now b (shifted)", v, "b");
    v = jes_get_array_value(g_ctx, g_root, 3);
    CHECK_VALUE("G7-05 index 3 is now c (shifted)", v, "c");
}

static void test_group_add_empty(void)
{
    struct jes_element *v;

    printf("\nGroup 6: jes_add_array_value() on []\n");
    if (setup(ARRAY0_JSON) != 0) { FAIL("G6-setup", "load failed"); return; }

    v = jes_add_array_value(g_ctx, g_root, 0, JES_STRING, "only", 4);
    CHECK_VALUE("G6-01 add at index 0 into empty array", v, "only");

    if (jes_get_array_size(g_ctx, g_root) == 1)
        PASS("G6-02 empty array now has one element");
    else
        FAIL("G6-02 empty array now has one element", "size mismatch");
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    printf("=== JES array index arithmetic tests ===\n");

    test_group_get_populated();
    test_group_get_empty();
    test_group_update();
    test_group_update_empty();
    test_group_add();
    test_group_add_middle();
    test_group_add_empty();

    printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
