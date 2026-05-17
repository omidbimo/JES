/**
 * jes_key_test.c
 *
 * Tests for all key-related JES API functions:
 *
 *   Lookup:     jes_get_key(), jes_get_key_value(), jes_get_value(),
 *               jes_set_path_separator()
 *   Mutation:   jes_add_key(), jes_add_key_before(), jes_add_key_after(),
 *               jes_update_key_value(), jes_update_key_value_to_object(),
 *               jes_update_key_value_to_array(),  jes_update_key_value_to_true(),
 *               jes_update_key_value_to_false(),  jes_update_key_value_to_null()
 *   Deletion:   jes_delete_element() (key subtree)
 *
 * These functions are grouped together because they share the same subject
 * (the JES_KEY element) and their correctness is interdependent — add/update
 * rely on successful lookups, and lookup tests after mutation verify consistency.
 *
 * Out of scope here (covered in jes_array_test.c):
 *   jes_get_array_value/size, jes_append/add/update_array_value.
 *
 * Build (from repo root):
 *   gcc jes_key_test.c src/jes.c src/jes_tokenizer.c src/jes_parser.c \
 *       src/jes_serializer.c src/jes_tree.c src/jes_hash_table.c \
 *       src/jes_logger.c -std=c99 -DNDEBUG -o jes_key_test
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../src/jes.h"

/* =========================================================================
 * Harness
 * ========================================================================= */

static int g_passed = 0;
static int g_failed = 0;

static void pass(const char *id) { printf("  [PASS] %s\n", id); g_passed++; }
static void fail(const char *id, const char *reason)
{
    printf("  [FAIL] %s — %s\n", id, reason); g_failed++;
}

#define CHECK(id, cond) \
    do { if (cond) pass(id); else fail(id, #cond " was false"); } while(0)

#define CHECK_NULL(id, ptr) \
    do { if ((ptr) == NULL) pass(id); else fail(id, "expected NULL, got non-NULL"); } while(0)

#define CHECK_NOTNULL(id, ptr) \
    do { if ((ptr) != NULL) pass(id); else fail(id, "expected non-NULL, got NULL"); } while(0)

#define CHECK_STATUS(id, ctx, expected) \
    do { \
        jes_status _st = jes_get_status(ctx); \
        if (_st == (expected)) pass(id); \
        else { char _m[64]; snprintf(_m, sizeof(_m), "status=%d, expected=%d", \
               (int)_st, (int)(expected)); fail(id, _m); } \
    } while(0)

#define CHECK_TYPE(id, elem, expected) \
    do { \
        if ((elem) && (elem)->type == (expected)) pass(id); \
        else { char _m[64]; snprintf(_m, sizeof(_m), "type=%d, expected=%d", \
               (elem) ? (int)(elem)->type : -1, (int)(expected)); fail(id, _m); } \
    } while(0)

#define CHECK_VALUE(id, elem, str) \
    do { \
        size_t _len = strlen(str); \
        if ((elem) && (elem)->length == (uint16_t)_len \
                   && memcmp((elem)->value, (str), _len) == 0) pass(id); \
        else { char _m[128]; \
               snprintf(_m, sizeof(_m), "got \"%.*s\", expected \"%s\"", \
                        (elem) ? (int)(elem)->length : 0, \
                        (elem) ? (elem)->value : "", (str)); \
               fail(id, _m); } \
    } while(0)

/* =========================================================================
 * Workspace helpers
 * ========================================================================= */

static uint8_t g_ws[JES_REQUIRED_SIZE(64)];

static struct jes_context *make_ctx(void)
{
    return jes_init(g_ws, sizeof(g_ws), JES_SEARCH_LINEAR);
}

static struct jes_context *load(const char *json)
{
    struct jes_context *ctx = make_ctx();
    if (!ctx) return NULL;
    jes_set_path_separator(ctx, JES_DEFAULT_PATH_SEPARATOR);
    return jes_load(ctx, json, strlen(json)) == JES_NO_ERROR ? ctx : NULL;
}

/* Verify the tree still serializes cleanly after a mutation */
static int renders_ok(struct jes_context *ctx)
{
    char out[2048];
    return jes_render(ctx, out, sizeof(out), true) > 0;
}

/* =========================================================================
 * Group 1 — jes_get_key: invalid arguments
 * ========================================================================= */

static void test_get_key_invalid_args(void)
{
    printf("\nGroup 1: jes_get_key — invalid arguments\n");

    struct jes_element dummy = {0};
    char bad_buf[4] = {0};
    struct jes_context *bad_ctx = (struct jes_context *)bad_buf;

    struct jes_context *ctx = load("{\"a\":\"1\"}");
    if (!ctx) { fail("G1-setup", "load failed"); return; }
    struct jes_element *root = jes_get_root(ctx);

    /* G1-01 */ CHECK_NULL("G1-01 NULL ctx",     jes_get_key(NULL,    root, "a"));
    /* G1-02 */ CHECK_NULL("G1-02 bad ctx",      jes_get_key(bad_ctx, root, "a"));
    /* G1-03 */ CHECK_NULL("G1-03 NULL parent",  jes_get_key(ctx, NULL, "a"));
    /* G1-04 */ CHECK_STATUS("G1-04 NULL parent status", ctx, JES_INVALID_PARAMETER);
    /* G1-05 */ CHECK_NULL("G1-05 NULL path",    jes_get_key(ctx, root, NULL));
    /* G1-06 */ CHECK_STATUS("G1-06 NULL path status", ctx, JES_INVALID_PARAMETER);
    /* G1-07 */ CHECK_NULL("G1-07 dummy parent", jes_get_key(ctx, &dummy, "a"));
    /* G1-08 */ CHECK_STATUS("G1-08 dummy parent status", ctx, JES_INVALID_PARAMETER);

    /* Value element used as parent (not OBJECT or KEY) */
    struct jes_element *k = jes_get_key(ctx, root, "a");
    struct jes_element *v = jes_get_key_value(ctx, k);
    /* G1-09 */ CHECK_NULL("G1-09 value as parent",        jes_get_key(ctx, v, "a"));
    /* G1-10 */ CHECK_STATUS("G1-10 value as parent status", ctx, JES_INVALID_PARAMETER);
}

/* =========================================================================
 * Group 2 — jes_get_key: path format edge cases
 * ========================================================================= */

static void test_get_key_path_format(void)
{
    printf("\nGroup 2: jes_get_key — path format edge cases\n");

    struct jes_context *ctx = load("{\"a\":\"1\"}");
    if (!ctx) { fail("G2-setup", "load failed"); return; }
    struct jes_element *root = jes_get_root(ctx);

    char long_path[JES_MAX_PATH_LENGTH + 32];
    memset(long_path, 'x', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';

    /* G2-01 */ CHECK_NULL("G2-01 empty path",       jes_get_key(ctx, root, ""));
    /* G2-02 */ CHECK_NULL("G2-02 separator only",   jes_get_key(ctx, root, "."));
    /* G2-03 */ CHECK_NULL("G2-03 trailing sep",     jes_get_key(ctx, root, "a."));
    /* G2-04 */ CHECK_NULL("G2-04 leading sep",      jes_get_key(ctx, root, ".a"));
    /* G2-05 */ CHECK_NULL("G2-05 double sep",       jes_get_key(ctx, root, "a..b"));
    /* G2-06 */ CHECK_NULL("G2-06 path > MAX",       jes_get_key(ctx, root, long_path));
    /* G2-07 */ CHECK_NULL("G2-07 whitespace path",  jes_get_key(ctx, root, " "));
    /* G2-08 */ CHECK_NULL("G2-08 control chars",    jes_get_key(ctx, root, "\n\t\r"));
    /* G2-09 */ CHECK_NULL("G2-09 missing key",      jes_get_key(ctx, root, "no_such_key"));
    /* G2-10 */ CHECK_NULL("G2-10 too deep on flat", jes_get_key(ctx, root, "a.b"));
}

/* =========================================================================
 * Group 3 — jes_get_key: successful lookups and jes_set_path_separator
 * ========================================================================= */

static void test_get_key_lookup(void)
{
    printf("\nGroup 3: jes_get_key — successful lookups\n");

    /* Nested doc: {"a":{"b":{"c":"leaf"}}, "x":{"p":"1","q":"2"}} */
    struct jes_context *ctx = load(
        "{\"a\":{\"b\":{\"c\":\"leaf\"}},"
         "\"x\":{\"p\":\"1\",\"q\":\"2\"}}");
    if (!ctx) { fail("G3-setup", "load failed"); return; }
    struct jes_element *root = jes_get_root(ctx);

    struct jes_element *k;

    /* ── flat lookup ─────────────────────────────────────────────────── */
    k = jes_get_key(ctx, root, "a");
    /* G3-01 */ CHECK_NOTNULL("G3-01 found \"a\"",       k);
    /* G3-02 */ CHECK_TYPE("G3-02 type == JES_KEY",      k, JES_KEY);
    /* G3-03 */ CHECK_VALUE("G3-03 key name == \"a\"",   k, "a");
    /* G3-04 */ CHECK_STATUS("G3-04 status JES_NO_ERROR",ctx, JES_NO_ERROR);

    /* ── multi-level path ────────────────────────────────────────────── */
    k = jes_get_key(ctx, root, "a.b");
    /* G3-05 */ CHECK_NOTNULL("G3-05 found \"a.b\"",     k);

    k = jes_get_key(ctx, root, "a.b.c");
    /* G3-06 */ CHECK_NOTNULL("G3-06 found \"a.b.c\"",   k);
    /* G3-07 */ CHECK_VALUE("G3-07 leaf name == \"c\"",  k, "c");

    /* ── relative path from KEY element ─────────────────────────────── */
    struct jes_element *k_a = jes_get_key(ctx, root, "a");
    k = jes_get_key(ctx, k_a, "b.c");
    /* G3-08 */ CHECK_NOTNULL("G3-08 relative path from KEY", k);

    /* ── correct sibling disambiguation ─────────────────────────────── */
    k = jes_get_key(ctx, root, "x.p");
    struct jes_element *v = jes_get_key_value(ctx, k);
    /* G3-09 */ CHECK_NOTNULL("G3-09 found \"x.p\"",     k);
    /* G3-10 */ CHECK_VALUE("G3-10 x.p value == \"1\"",  v, "1");

    k = jes_get_key(ctx, root, "x.q");
    v = jes_get_key_value(ctx, k);
    /* G3-11 */ CHECK_NOTNULL("G3-11 found \"x.q\"",     k);
    /* G3-12 */ CHECK_VALUE("G3-12 x.q value == \"2\"",  v, "2");

    /* ── repeated lookup returns consistent result ───────────────────── */
    struct jes_element *k1 = jes_get_key(ctx, root, "a");
    struct jes_element *k2 = jes_get_key(ctx, root, "a");
    /* G3-13 */ CHECK("G3-13 repeated lookup == same ptr", k1 == k2);

    /* ── failed lookup does not break next lookup ────────────────────── */
    jes_get_key(ctx, root, "no_such_key");
    k = jes_get_key(ctx, root, "a");
    /* G3-14 */ CHECK_NOTNULL("G3-14 success after failure", k);

    /* ── custom path separator ───────────────────────────────────────── */
    jes_set_path_separator(ctx, '/');
    k = jes_get_key(ctx, root, "a/b/c");
    /* G3-15 */ CHECK_NOTNULL("G3-15 separator '/' works",  k);

    k = jes_get_key(ctx, root, "a.b.c");        /* dot is now a literal char */
    /* G3-16 */ CHECK_NULL("G3-16 old sep is literal",      k);

    jes_set_path_separator(ctx, JES_DEFAULT_PATH_SEPARATOR);
    k = jes_get_key(ctx, root, "a.b.c");
    /* G3-17 */ CHECK_NOTNULL("G3-17 restored default sep", k);
}

/* =========================================================================
 * Group 4 — jes_get_key_value and jes_get_value
 * ========================================================================= */

static void test_get_key_value(void)
{
    printf("\nGroup 4: jes_get_key_value and jes_get_value\n");

    struct jes_context *ctx = load(
        "{\"str\":\"hello\","
         "\"num\":42,"
         "\"flag\":true,"
         "\"obj\":{\"x\":1},"
         "\"arr\":[1,2]}");
    if (!ctx) { fail("G4-setup", "load failed"); return; }
    struct jes_element *root = jes_get_root(ctx);

    /* ── get_key_value for each value type ──────────────────────────── */
    struct jes_element *k, *v;

    k = jes_get_key(ctx, root, "str");
    v = jes_get_key_value(ctx, k);
    /* G4-01 */ CHECK_TYPE("G4-01 string value type",  v, JES_STRING);
    /* G4-02 */ CHECK_VALUE("G4-02 string value data", v, "hello");

    k = jes_get_key(ctx, root, "num");
    v = jes_get_key_value(ctx, k);
    /* G4-03 */ CHECK_TYPE("G4-03 number value type",  v, JES_NUMBER);
    /* G4-04 */ CHECK_VALUE("G4-04 number value data", v, "42");

    k = jes_get_key(ctx, root, "flag");
    v = jes_get_key_value(ctx, k);
    /* G4-05 */ CHECK_TYPE("G4-05 bool value type",    v, JES_TRUE);

    k = jes_get_key(ctx, root, "obj");
    v = jes_get_key_value(ctx, k);
    /* G4-06 */ CHECK_TYPE("G4-06 object value type",  v, JES_OBJECT);

    k = jes_get_key(ctx, root, "arr");
    v = jes_get_key_value(ctx, k);
    /* G4-07 */ CHECK_TYPE("G4-07 array value type",   v, JES_ARRAY);

    /* ── get_key_value with NULL/bad args ────────────────────────────── */
    /* G4-08 */ CHECK_NULL("G4-08 get_key_value NULL key", jes_get_key_value(ctx, NULL));

    struct jes_element dummy = {0};
    /* G4-09 */ CHECK_NULL("G4-09 get_key_value dummy key", jes_get_key_value(ctx, &dummy));

    /* ── jes_get_value convenience wrapper ───────────────────────────── */
    v = jes_get_value(ctx, root, "str");
    /* G4-10 */ CHECK_TYPE("G4-10 get_value type",     v, JES_STRING);
    /* G4-11 */ CHECK_VALUE("G4-11 get_value data",    v, "hello");

    v = jes_get_value(ctx, root, "obj.x");
    /* G4-12 */ CHECK_TYPE("G4-12 get_value nested",   v, JES_NUMBER);

    /* G4-13 */ CHECK_NULL("G4-13 get_value missing",  jes_get_value(ctx, root, "no_such"));
    /* G4-14 */ CHECK_NULL("G4-14 get_value NULL path",jes_get_value(ctx, root, NULL));
}

/* =========================================================================
 * Group 5 — jes_add_key, jes_add_key_before, jes_add_key_after
 * ========================================================================= */

static void test_add_key(void)
{
    printf("\nGroup 5: jes_add_key / jes_add_key_before / jes_add_key_after\n");

    struct jes_context *ctx = load("{\"b\":2}");
    if (!ctx) { fail("G5-setup", "load failed"); return; }
    struct jes_element *root = jes_get_root(ctx);

    /* ── jes_add_key appends ─────────────────────────────────────────── */
    struct jes_element *k_c = jes_add_key(ctx, root, "c", 1);
    /* G5-01 */ CHECK_NOTNULL("G5-01 add_key returns element", k_c);
    /* G5-02 */ CHECK_TYPE("G5-02 add_key type == JES_KEY", k_c, JES_KEY);
    /* G5-03 */ CHECK_VALUE("G5-03 add_key name == \"c\"",  k_c, "c");
    /* G5-04 */ CHECK("G5-04 tree doesn't render after add_key", renders_ok(ctx) == 0);
    jes_update_key_value(ctx, k_c, JES_STRING, "tmp", 3);  // give it a value first
    /* G5-05 */ CHECK("G5-05 tree renders after update_key_value", renders_ok(ctx));

    struct jes_element *found = jes_get_key(ctx, root, "c");
    /* G5-06 */ CHECK_NOTNULL("G5-06 new key findable", found);

    /* ── duplicate key must fail ─────────────────────────────────────── */
    /* G5-07 */ CHECK_NULL("G5-07 duplicate key returns NULL",
                           jes_add_key(ctx, root, "b", 1));
    /* G5-08 */ CHECK_STATUS("G5-08 duplicate key status", ctx, JES_DUPLICATE_KEY);

    /* ── jes_add_key_before ──────────────────────────────────────────── */
    struct jes_element *k_b = jes_get_key(ctx, root, "b");
    struct jes_element *k_a = jes_add_key_before(ctx, k_b, "a", 1);
    /* G5-09 */ CHECK_NOTNULL("G5-09 add_key_before returns element", k_a);
    /* G5-10 */ CHECK("G5-10 tree doesn't render after add_key_before", renders_ok(ctx) == 0);
    jes_update_key_value(ctx, k_a, JES_STRING, "tmp", 3);  // give it a value first
    /* G5-11 */ CHECK("G5-11 tree renders after update_key_value", renders_ok(ctx));
    /* Verify ordering: "a" must appear before "b" in the rendered output */
    char out[256];
    jes_render(ctx, out, sizeof(out), true);
    const char *pa = strstr(out, "\"a\"");
    const char *pb = strstr(out, "\"b\"");
    /* G5-12 */ CHECK("G5-12 a inserted before b", pa && pb && pa < pb);

    /* ── jes_add_key_after ───────────────────────────────────────────── */
    struct jes_element *k_bb = jes_add_key_after(ctx, k_b, "bb", 2);
    /* G5-13 */ CHECK_NOTNULL("G5-13 add_key_after returns element", k_bb);
    /* G5-14 */ CHECK("G5-14 tree doesn't render after add_key_after", renders_ok(ctx) == 0);
    jes_update_key_value(ctx, k_bb, JES_STRING, "tmp", 3);  // give it a value first
    /* G5-15 */ CHECK("G5-15 tree renders after update_key_value", renders_ok(ctx));

    jes_render(ctx, out, sizeof(out), true);
    pb = strstr(out, "\"b\"");
    const char *pbb = strstr(out, "\"bb\"");
    /* G5-16 */ CHECK("G5-16 bb inserted after b", pb && pbb && pb < pbb);

    /* ── invalid args ────────────────────────────────────────────────── */
    /* G5-17 */ CHECK_NULL("G5-17 add_key NULL ctx",    jes_add_key(NULL, root, "z", 1));
    /* G5-18 */ CHECK_NULL("G5-18 add_key NULL parent", jes_add_key(ctx, NULL, "z", 1));
    /* G5-19 */ CHECK_NULL("G5-19 add_key NULL name",   jes_add_key(ctx, root, NULL, 0));
    /* G5-20 */ CHECK_NULL("G5-20 add_key zero length", jes_add_key(ctx, root, "z", 0));
    /* G5-21 */ CHECK_NULL("G5-21 add_key_before NULL ref",
                           jes_add_key_before(ctx, NULL, "z", 1));
    /* G5-22 */ CHECK_NULL("G5-22 add_key_after NULL ref",
                           jes_add_key_after(ctx, NULL, "z", 1));
}

/* =========================================================================
 * Group 6 — jes_update_key_value and type-specific helpers
 * ========================================================================= */

static void test_update_key_value(void)
{
    printf("\nGroup 6: jes_update_key_value and _to_* helpers\n");

    struct jes_context *ctx = load(
        "{\"a\":\"old\","
         "\"b\":0,"
         "\"c\":false,"
         "\"d\":null,"
         "\"e\":\"keep\"}");
    if (!ctx) { fail("G6-setup", "load failed"); return; }
    struct jes_element *root = jes_get_root(ctx);

    struct jes_element *k, *v;

    /* ── update to string ────────────────────────────────────────────── */
    k = jes_get_key(ctx, root, "a");
    v = jes_update_key_value(ctx, k, JES_STRING, "new", 3);
    /* G6-01 */ CHECK_NOTNULL("G6-01 update_key_value returns value", v);
    /* G6-02 */ CHECK_TYPE("G6-02 updated type == JES_STRING",        v, JES_STRING);
    /* G6-03 */ CHECK_VALUE("G6-03 updated value == \"new\"",         v, "new");
    /* G6-04 */ CHECK("G6-04 renders after string update", renders_ok(ctx));

    /* ── update to number ────────────────────────────────────────────── */
    k = jes_get_key(ctx, root, "b");
    v = jes_update_key_value(ctx, k, JES_NUMBER, "99", 2);
    /* G6-05 */ CHECK_TYPE("G6-05 updated type == JES_NUMBER", v, JES_NUMBER);
    /* G6-06 */ CHECK_VALUE("G6-06 updated value == \"99\"",   v, "99");

    /* ── _to_true / _to_false / _to_null ────────────────────────────── */
    k = jes_get_key(ctx, root, "c");
    v = jes_update_key_value_to_true(ctx, k);
    /* G6-07 */ CHECK_TYPE("G6-07 _to_true type == JES_TRUE",  v, JES_TRUE);

    v = jes_update_key_value_to_false(ctx, k);
    /* G6-08 */ CHECK_TYPE("G6-08 _to_false type == JES_FALSE", v, JES_FALSE);

    k = jes_get_key(ctx, root, "d");
    v = jes_update_key_value_to_null(ctx, k);
    /* G6-09 */ CHECK_TYPE("G6-09 _to_null type == JES_NULL",  v, JES_NULL);

    /* ── _to_object: old value is replaced, new key can be nested ────── */
    k = jes_get_key(ctx, root, "a");
    v = jes_update_key_value_to_object(ctx, k);
    /* G6-10 */ CHECK_TYPE("G6-10 _to_object type == JES_OBJECT", v, JES_OBJECT);
    /* G6-11 */ CHECK("G6-11 renders after _to_object", renders_ok(ctx));

    /* Add a child key into the new object and verify lookup */
    struct jes_element *child_k = jes_add_key(ctx, k, "inner", 5);
    jes_update_key_value(ctx, child_k, JES_STRING, "val", 3);
    struct jes_element *found = jes_get_key(ctx, root, "a.inner");
    /* G6-12 */ CHECK_NOTNULL("G6-12 child key findable after _to_object", found);

    /* ── _to_array ───────────────────────────────────────────────────── */
    k = jes_get_key(ctx, root, "b");
    v = jes_update_key_value_to_array(ctx, k);
    /* G6-13 */ CHECK_TYPE("G6-13 _to_array type == JES_ARRAY", v, JES_ARRAY);
    /* G6-14 */ CHECK("G6-14 renders after _to_array", renders_ok(ctx));

    /* ── unaffected sibling keeps its value ──────────────────────────── */
    struct jes_element *k_e = jes_get_key(ctx, root, "e");
    struct jes_element *v_e = jes_get_key_value(ctx, k_e);
    /* G6-15 */ CHECK_VALUE("G6-15 sibling \"e\" unchanged", v_e, "keep");

    /* ── invalid args ────────────────────────────────────────────────── */
    /* G6-16 */ CHECK_NULL("G6-16 update NULL ctx",
                           jes_update_key_value(NULL, k, JES_STRING, "x", 1));
    /* G6-17 */ CHECK_NULL("G6-17 update NULL key",
                           jes_update_key_value(ctx, NULL, JES_STRING, "x", 1));
    /* G6-18 */ CHECK_NULL("G6-18 _to_true NULL key",
                           jes_update_key_value_to_true(ctx, NULL));
    /* G6-19 */ CHECK_NULL("G6-19 _to_false NULL key",
                           jes_update_key_value_to_false(ctx, NULL));
    /* G6-20 */ CHECK_NULL("G6-20 _to_null NULL key",
                           jes_update_key_value_to_null(ctx, NULL));
    /* G6-21 */ CHECK_NULL("G6-21 _to_object NULL key",
                           jes_update_key_value_to_object(ctx, NULL));
    /* G6-22 */ CHECK_NULL("G6-22 _to_array NULL key",
                           jes_update_key_value_to_array(ctx, NULL));
}

/* =========================================================================
 * Group 7 — jes_delete_element on keys
 * ========================================================================= */

static void test_delete_key(void)
{
    printf("\nGroup 7: jes_delete_element — key subtrees\n");

    struct jes_context *ctx = load(
        "{\"keep1\":1,"
         "\"del\":\"gone\","
         "\"keep2\":2,"
         "\"nested\":{\"child\":\"x\"}}");
    if (!ctx) { fail("G7-setup", "load failed"); return; }
    struct jes_element *root = jes_get_root(ctx);

    /* ── delete a leaf key ───────────────────────────────────────────── */
    struct jes_element *k_del = jes_get_key(ctx, root, "del");
    jes_status st = jes_delete_element(ctx, k_del);
    /* G7-01 */ CHECK("G7-01 delete returns JES_NO_ERROR", st == JES_NO_ERROR);
    /* G7-02 */ CHECK_NULL("G7-02 deleted key not found",
                           jes_get_key(ctx, root, "del"));
    /* G7-03 */ CHECK("G7-03 renders after leaf delete", renders_ok(ctx));
    /* Siblings must still be present */
    /* G7-04 */ CHECK_NOTNULL("G7-04 keep1 still present",
                              jes_get_key(ctx, root, "keep1"));
    /* G7-05 */ CHECK_NOTNULL("G7-05 keep2 still present",
                              jes_get_key(ctx, root, "keep2"));

    /* ── delete a key with a nested object (subtree) ─────────────────── */
    struct jes_element *k_nested = jes_get_key(ctx, root, "nested");
    st = jes_delete_element(ctx, k_nested);
    /* G7-06 */ CHECK("G7-06 delete subtree returns JES_NO_ERROR", st == JES_NO_ERROR);
    /* G7-07 */ CHECK_NULL("G7-07 deleted subtree key not found",
                           jes_get_key(ctx, root, "nested"));
    /* G7-08 */ CHECK_NULL("G7-08 deleted child not found",
                           jes_get_key(ctx, root, "nested.child"));
    /* G7-09 */ CHECK("G7-09 renders after subtree delete", renders_ok(ctx));

    /* ── delete then re-add same name ────────────────────────────────── */
    struct jes_element *k_new = jes_add_key(ctx, root, "del", 3);
    jes_update_key_value(ctx, k_new, JES_STRING, "back", 4);
    struct jes_element *k_found = jes_get_key(ctx, root, "del");
    /* G7-10 */ CHECK_NOTNULL("G7-10 re-added key findable", k_found);
    struct jes_element *v_found = jes_get_key_value(ctx, k_found);
    /* G7-11 */ CHECK_VALUE("G7-11 re-added key value correct", v_found, "back");

    /* ── invalid args ────────────────────────────────────────────────── */
    /* G7-12 */ CHECK("G7-12 delete NULL ctx returns error",
                      jes_delete_element(NULL, k_new) != JES_NO_ERROR);
    /* G7-13 */ CHECK("G7-13 delete NULL element returns error",
                      jes_delete_element(ctx, NULL) != JES_NO_ERROR);
}

/* =========================================================================
 * Group 8 — Search mode: JES_SEARCH_HASHED
 * ========================================================================= */

static void test_hashed_search(void)
{
    printf("\nGroup 8: jes_get_key — hash table search mode\n");

    /* Use a larger workspace for the hash table partition */
    static uint8_t ws_hashed[JES_REQUIRED_SIZE(64)];
    struct jes_context *ctx = jes_init(ws_hashed, sizeof(ws_hashed), JES_SEARCH_HASHED);
    if (!ctx) { fail("G8-setup", "ctx init failed"); return; }

    const char *json =
        "{\"a\":{\"b\":{\"c\":\"leaf\"}},"
         "\"x\":\"1\","
         "\"y\":\"2\"}";

    if (jes_load(ctx, json, strlen(json)) != JES_NO_ERROR) {
        fail("G8-setup", "load failed"); return;
    }

    struct jes_element *root = jes_get_root(ctx);
    struct jes_element *k;

    k = jes_get_key(ctx, root, "a.b.c");
    /* G8-01 */ CHECK_NOTNULL("G8-01 hashed: deep path found",    k);
    /* G8-02 */ CHECK_VALUE("G8-02 hashed: key name == \"c\"",    k, "c");

    k = jes_get_key(ctx, root, "x");
    /* G8-03 */ CHECK_NOTNULL("G8-03 hashed: flat key found",     k);

    k = jes_get_key(ctx, root, "no_such_key");
    /* G8-04 */ CHECK_NULL("G8-04 hashed: missing key returns NULL", k);
    /* G8-05 */ CHECK_STATUS("G8-05 hashed: missing key status",  ctx, JES_ELEMENT_NOT_FOUND);

    /* Results must match linear mode */
    static uint8_t ws_linear[JES_REQUIRED_SIZE(64)];
    struct jes_context *ctx_lin = jes_init(ws_linear, sizeof(ws_linear), JES_SEARCH_LINEAR);
    jes_load(ctx_lin, json, strlen(json));
    struct jes_element *root_lin = jes_get_root(ctx_lin);

    struct jes_element *k_lin  = jes_get_key(ctx_lin, root_lin, "a.b.c");
    struct jes_element *k_hash = jes_get_key(ctx,     root,     "a.b.c");
    /* G8-06 */ CHECK("G8-06 hashed and linear agree on found key",
                      k_lin  && k_hash &&
                      k_lin->length == k_hash->length &&
                      memcmp(k_lin->value, k_hash->value, k_lin->length) == 0);

    k_lin  = jes_get_key(ctx_lin, root_lin, "y");
    k_hash = jes_get_key(ctx,     root,     "y");
    /* G8-07 */ CHECK("G8-07 hashed and linear agree on sibling",
                      k_lin  && k_hash &&
                      k_lin->length == k_hash->length &&
                      memcmp(k_lin->value, k_hash->value, k_lin->length) == 0);
}

/* =========================================================================
 * Group 9 — delete a key whose value is an object with multiple children
 * ========================================================================= */
static void test_delete_key_with_multiple_values(void)
{
    printf("\nGroup 9: jes_delete_key subtree deletion\n");

    struct jes_context *ctx = load(
        "{\"keep\":0,"
         "\"del\":{\"a\":1,\"b\":2,\"c\":3},"
         "\"also_keep\":2}");
    if (!ctx) { fail("G9-setup", "load failed"); return; }
    struct jes_element *root = jes_get_root(ctx);
    struct jes_element *key = jes_get_key(ctx, root, "del");
    /* G9-01: baseline renders */
    /* G9-01 */ CHECK("G9-01 baseline renders", renders_ok(ctx));

    jes_delete_element(ctx, key);

    /* G9-02: tree still renders after deleting a multi-child subtree */
    /* G9-02 */ CHECK("G9-02 renders after delete", renders_ok(ctx));

    /* G9-03: deleted key is gone */
    /* G9-03 */ CHECK_NULL("G9-03 del key not found", jes_get_key(ctx, root, "del"));

    /* G9-04: sibling keys survive */
    /* G9-04 */ CHECK_NOTNULL("G9-04 keep still found",      jes_get_key(ctx, root, "keep"));
    /* G9-05 */ CHECK_NOTNULL("G9-05 also_keep still found", jes_get_key(ctx, root, "also_keep"));

    /* G9-06: children of deleted key are also gone */
    /* G9-06 */ CHECK_NULL("G9-06 del.a not found", jes_get_key(ctx, root, "del.a"));
    /* G9-07 */ CHECK_NULL("G9-07 del.b not found", jes_get_key(ctx, root, "del.b"));
    /* G9-08 */ CHECK_NULL("G9-08 del.c not found", jes_get_key(ctx, root, "del.c"));

    /* G9-09: node count is consistent (no leaked nodes) */
    uint32_t count = jes_get_element_count(ctx);
    /* G9-09 */ CHECK("G9-09 node count == 5", count == 5); /* root + keep + 1 + also_keep + 2 */
}
/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    printf("=== JES Key API Tests ===\n");

    test_get_key_invalid_args();
    test_get_key_path_format();
    test_get_key_lookup();
    test_get_key_value();
    test_add_key();
    test_update_key_value();
    test_delete_key();
    test_hashed_search();
    test_delete_key_with_multiple_values();

    printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
