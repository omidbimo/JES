/**
 * jes_get_key_test.c
 *
 * Tests for jes_get_key() and jes_set_path_separator().
 *
 * Build (from repo root):
 *   gcc jes_get_key_test.c src/jes.c src/jes_tokenizer.c src/jes_parser.c \
 *       src/jes_serializer.c src/jes_tree.c src/jes_hash_table.c \
 *       src/jes_logger.c -std=c99 -DNDEBUG -o jes_get_key_test
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
#define FAIL(id, reason) do { printf("  [FAIL] %s — %s\n", id, reason); g_failed++; } while(0)

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

/* Expect result != NULL */
#define CHECK_FOUND(id, result)                                             \
    do {                                                                    \
        if ((result) == NULL) FAIL(id, "expected non-NULL, got NULL");      \
        else PASS(id);                                                      \
    } while(0)

/* =========================================================================
 * Shared fixtures
 * ========================================================================= */

/*
 * Flat JSON used for invalid-arg and simple path tests:
 *   {"key1":"value1","key2":"value2"}
 */
static const char FLAT_JSON[] =
    "{\"key1\":\"value1\","
     "\"key2\":\"value2\"}";

/*
 * Nested JSON used for path traversal tests:
 *   {"a":{"b":{"c":"leaf"}}}
 */
static const char NESTED_JSON[] =
    "{\"a\":{\"b\":{\"c\":\"leaf\"}}}";

/*
 * Wide JSON with several siblings at each level:
 *   {"x":{"p":"1","q":"2"},"y":{"p":"3","q":"4"}}
 */
static const char WIDE_JSON[] =
    "{\"x\":{\"p\":\"1\",\"q\":\"2\"},"
     "\"y\":{\"p\":\"3\",\"q\":\"4\"}}";

static uint8_t  g_workspace[JES_REQUIRED_SIZE(128)];
static struct jes_context *g_ctx = NULL;

static int setup(const char *json)
{
    memset(g_workspace, 0, sizeof(g_workspace));
    g_ctx = jes_init(g_workspace, sizeof(g_workspace), JES_SEARCH_LINEAR);
    if (!g_ctx) return -1;
    /* Reset path separator to default before each group */
    jes_set_path_separator(g_ctx, JES_DEFAULT_PATH_SEPARATOR);
    return jes_load(g_ctx, json, strlen(json)) == JES_NO_ERROR ? 0 : -1;
}

/* =========================================================================
 * Group 1 — Invalid context / NULL arguments
 * ========================================================================= */

static void test_group_invalid_context(void)
{
    struct jes_element dummy = {0};
    struct jes_element *key;
    char bad_buf[4] = {0};
    struct jes_context *bad_ctx = (struct jes_context *)bad_buf;

    printf("\nGroup 1: Invalid context / NULL arguments\n");

    if (setup(FLAT_JSON) != 0) { FAIL("G1-setup", "context init/load failed"); return; }

    /* G1-01: NULL context */
    key = jes_get_key(NULL, NULL, "key1");
    if (key != NULL) FAIL("G1-01 NULL ctx", "expected NULL");
    else PASS("G1-01 NULL ctx");

    /* G1-02: Uninitialised (garbage) context */
    key = jes_get_key(bad_ctx, NULL, "key1");
    if (key != NULL) FAIL("G1-02 bad ctx", "expected NULL");
    else PASS("G1-02 bad ctx");

    /* G1-03: NULL path → JES_INVALID_PARAMETER */
    key = jes_get_key(g_ctx, jes_get_root(g_ctx), NULL);
    CHECK_NULL_STATUS("G1-03 NULL path", key, g_ctx, JES_INVALID_PARAMETER);

    /* G1-04: NULL parent with valid path → JES_INVALID_PARAMETER */
    key = jes_get_key(g_ctx, NULL, "key1");
    CHECK_NULL_STATUS("G1-04 NULL parent", key, g_ctx, JES_INVALID_PARAMETER);

    /* G1-05: Dummy (unregistered) element as parent */
    key = jes_get_key(g_ctx, &dummy, "key1");
    CHECK_NULL_STATUS("G1-05 dummy parent", key, g_ctx, JES_INVALID_PARAMETER);

    /* G1-06: Value element (JES_STRING) used as parent — not OBJECT or KEY */
    struct jes_element *k1 = jes_get_key(g_ctx, jes_get_root(g_ctx), "key1");
    struct jes_element *v1 = jes_get_key_value(g_ctx, k1);
    key = jes_get_key(g_ctx, v1, "key1");
    CHECK_NULL_STATUS("G1-06 value as parent", key, g_ctx, JES_INVALID_PARAMETER);
}

/* =========================================================================
 * Group 2 — Path format edge cases (key not found expected)
 * ========================================================================= */

static void test_group_path_format(void)
{
    struct jes_element *key;
    char long_path[JES_MAX_PATH_LENGTH + 32];

    printf("\nGroup 2: Path format edge cases\n");

    if (setup(FLAT_JSON) != 0) { FAIL("G2-setup", "context init/load failed"); return; }

    struct jes_element *root = jes_get_root(g_ctx);

    /* G2-01: Empty string path */
    key = jes_get_key(g_ctx, root, "");
    CHECK_NULL_STATUS("G2-01 empty path", key, g_ctx, JES_ELEMENT_NOT_FOUND);

    /* G2-02: Separator only */
    key = jes_get_key(g_ctx, root, ".");
    CHECK_NULL_STATUS("G2-02 separator only", key, g_ctx, JES_ELEMENT_NOT_FOUND);

    /* G2-03: Trailing separator — "key1." */
    key = jes_get_key(g_ctx, root, "key1.");
    CHECK_NULL_STATUS("G2-03 trailing sep", key, g_ctx, JES_ELEMENT_NOT_FOUND);

    /* G2-04: Leading separator — ".key1" */
    key = jes_get_key(g_ctx, root, ".key1");
    CHECK_NULL_STATUS("G2-04 leading sep", key, g_ctx, JES_ELEMENT_NOT_FOUND);

    /* G2-05: Double separator — "key1..key2" */
    key = jes_get_key(g_ctx, root, "key1..key2");
    CHECK_NULL_STATUS("G2-05 double sep", key, g_ctx, JES_ELEMENT_NOT_FOUND);

    /* G2-06: Path that exceeds JES_MAX_PATH_LENGTH */
    memset(long_path, 'a', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';
    key = jes_get_key(g_ctx, root, long_path);
    /* Expect either JES_PATH_TOO_LONG or JES_ELEMENT_NOT_FOUND */
    if (key != NULL) {
        FAIL("G2-06 path too long", "expected NULL");
    } else {
        jes_status st = jes_get_status(g_ctx);
        if (st == JES_PATH_TOO_LONG || st == JES_ELEMENT_NOT_FOUND)
            PASS("G2-06 path too long");
        else {
            char m[64];
            snprintf(m, sizeof(m), "unexpected status %d", (int)st);
            FAIL("G2-06 path too long", m);
        }
    }

    /* G2-07: Whitespace-only path */
    key = jes_get_key(g_ctx, root, " ");
    CHECK_NULL_STATUS("G2-07 whitespace path", key, g_ctx, JES_ELEMENT_NOT_FOUND);

    /* G2-08: Control characters in path */
    key = jes_get_key(g_ctx, root, "\n\t\r");
    CHECK_NULL_STATUS("G2-08 control chars", key, g_ctx, JES_ELEMENT_NOT_FOUND);

    /* G2-09: Non-existing key name */
    key = jes_get_key(g_ctx, root, "no_such_key");
    CHECK_NULL_STATUS("G2-09 missing key", key, g_ctx, JES_ELEMENT_NOT_FOUND);

    /* G2-10: Path deeper than tree allows */
    key = jes_get_key(g_ctx, root, "key1.key2");
    CHECK_NULL_STATUS("G2-10 too deep on flat", key, g_ctx, JES_ELEMENT_NOT_FOUND);
}

/* =========================================================================
 * Group 3 — Successful flat lookups
 * ========================================================================= */

static void test_group_flat_lookup(void)
{
    struct jes_element *key;

    printf("\nGroup 3: Successful flat lookups\n");

    if (setup(FLAT_JSON) != 0) { FAIL("G3-setup", "context init/load failed"); return; }

    struct jes_element *root = jes_get_root(g_ctx);

    /* G3-01: Find first key */
    key = jes_get_key(g_ctx, root, "key1");
    CHECK_FOUND("G3-01 find key1", key);
    if (key) {
        if (key->type != JES_KEY)
            FAIL("G3-01 type check", "expected JES_KEY");
        else
            PASS("G3-01 type == JES_KEY");
        if (key->length == 4 && memcmp(key->value, "key1", 4) == 0)
            PASS("G3-01 value == \"key1\"");
        else
            FAIL("G3-01 value check", "wrong key name in element");
    }

    /* G3-02: Find second key */
    key = jes_get_key(g_ctx, root, "key2");
    CHECK_FOUND("G3-02 find key2", key);

    /* G3-03: Status is JES_NO_ERROR after successful lookup */
    if (jes_get_status(g_ctx) == JES_NO_ERROR)
        PASS("G3-03 status after success");
    else
        FAIL("G3-03 status after success", "expected JES_NO_ERROR");
}

/* =========================================================================
 * Group 4 — Nested / multi-level path lookups
 * ========================================================================= */

static void test_group_nested_lookup(void)
{
    struct jes_element *key;

    printf("\nGroup 4: Nested / multi-level path lookups\n");

    if (setup(NESTED_JSON) != 0) { FAIL("G4-setup", "context init/load failed"); return; }

    struct jes_element *root = jes_get_root(g_ctx);

    /* G4-01: One level deep */
    key = jes_get_key(g_ctx, root, "a");
    CHECK_FOUND("G4-01 path \"a\"", key);

    /* G4-02: Two levels deep */
    key = jes_get_key(g_ctx, root, "a.b");
    CHECK_FOUND("G4-02 path \"a.b\"", key);

    /* G4-03: Three levels deep (leaf) */
    key = jes_get_key(g_ctx, root, "a.b.c");
    CHECK_FOUND("G4-03 path \"a.b.c\"", key);
    if (key) {
        struct jes_element *val = jes_get_key_value(g_ctx, key);
        if (val && val->type == JES_STRING && val->length == 4
                && memcmp(val->value, "leaf", 4) == 0)
            PASS("G4-03 leaf value == \"leaf\"");
        else
            FAIL("G4-03 leaf value", "wrong value");
    }

    /* G4-04: Lookup starting from an intermediate KEY element */
    struct jes_element *k_a = jes_get_key(g_ctx, root, "a");
    key = jes_get_key(g_ctx, k_a, "b.c");
    CHECK_FOUND("G4-04 relative path from KEY", key);

    /* G4-05: Sibling that does not exist at intermediate level */
    key = jes_get_key(g_ctx, root, "a.x");
    CHECK_NULL_STATUS("G4-05 missing sibling", key, g_ctx, JES_ELEMENT_NOT_FOUND);

    /* G4-06: Correct sibling chosen in wide tree */
    if (setup(WIDE_JSON) != 0) { FAIL("G4-wide-setup", "failed"); return; }
    root = jes_get_root(g_ctx);

    key = jes_get_key(g_ctx, root, "x.p");
    CHECK_FOUND("G4-06 x.p found", key);
    if (key) {
        struct jes_element *val = jes_get_key_value(g_ctx, key);
        if (val && val->length == 1 && val->value[0] == '1')
            PASS("G4-06 x.p == \"1\"");
        else
            FAIL("G4-06 x.p value", "wrong value");
    }

    key = jes_get_key(g_ctx, root, "y.q");
    CHECK_FOUND("G4-07 y.q found", key);
    if (key) {
        struct jes_element *val = jes_get_key_value(g_ctx, key);
        if (val && val->length == 1 && val->value[0] == '4')
            PASS("G4-07 y.q == \"4\"");
        else
            FAIL("G4-07 y.q value", "wrong value");
    }
}

/* =========================================================================
 * Group 5 — Custom path separator
 * ========================================================================= */

static void test_group_path_separator(void)
{
    struct jes_element *key;

    printf("\nGroup 5: Custom path separator\n");

    if (setup(NESTED_JSON) != 0) { FAIL("G5-setup", "context init/load failed"); return; }

    struct jes_element *root = jes_get_root(g_ctx);

    /* G5-01: Switch to '/' — deep path works */
    jes_set_path_separator(g_ctx, '/');
    key = jes_get_key(g_ctx, root, "a/b/c");
    CHECK_FOUND("G5-01 separator '/' deep path", key);

    /* G5-02: Old separator '.' no longer splits — treated as literal char */
    key = jes_get_key(g_ctx, root, "a.b.c");
    CHECK_NULL_STATUS("G5-02 old sep is literal", key, g_ctx, JES_ELEMENT_NOT_FOUND);

    /* G5-03: Switch to '\\' */
    jes_set_path_separator(g_ctx, '\\');
    key = jes_get_key(g_ctx, root, "a\\b\\c");
    CHECK_FOUND("G5-03 separator '\\\\' deep path", key);

    /* G5-04: Restore default '.' and verify it works again */
    jes_set_path_separator(g_ctx, JES_DEFAULT_PATH_SEPARATOR);
    key = jes_get_key(g_ctx, root, "a.b.c");
    CHECK_FOUND("G5-04 restored default separator", key);
}

/* =========================================================================
 * Group 6 — Repeated / sequential lookups
 * ========================================================================= */

static void test_group_repeated_lookups(void)
{
    struct jes_element *key;

    printf("\nGroup 6: Repeated and sequential lookups\n");

    if (setup(FLAT_JSON) != 0) { FAIL("G6-setup", "context init/load failed"); return; }

    struct jes_element *root = jes_get_root(g_ctx);

    /* G6-01: Same key looked up twice returns consistent results */
    struct jes_element *k1a = jes_get_key(g_ctx, root, "key1");
    struct jes_element *k1b = jes_get_key(g_ctx, root, "key1");
    if (k1a && k1a == k1b)
        PASS("G6-01 repeated lookup same pointer");
    else if (k1a && k1b && k1a->value == k1b->value)
        PASS("G6-01 repeated lookup same value ptr");
    else
        FAIL("G6-01 repeated lookup", "inconsistent results");

    /* G6-02: Alternating lookups between two keys */
    struct jes_element *k2 = jes_get_key(g_ctx, root, "key2");
    key = jes_get_key(g_ctx, root, "key1");
    CHECK_FOUND("G6-02 key1 after key2 lookup", key);

    /* G6-03: Failed lookup does not corrupt subsequent successful one */
    jes_get_key(g_ctx, root, "no_such_key");
    key = jes_get_key(g_ctx, root, "key2");
    CHECK_FOUND("G6-03 success after failure", key);
    (void)k2; /* suppress unused warning */
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    printf("=== jes_get_key() Tests ===\n");

    test_group_invalid_context();
    test_group_path_format();
    test_group_flat_lookup();
    test_group_nested_lookup();
    test_group_path_separator();
    test_group_repeated_lookups();

    printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
