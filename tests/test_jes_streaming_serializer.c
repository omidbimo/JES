/**
 * test_jes_streaming_serializer.c
 *
 * Tests for the JES streaming (tree-less) serializer API.
 *
 * Exercises:
 *   jes_init_streaming(), jes_take_streaming_status(),
 *   jes_render_object_start/end(), jes_render_array_start/end(),
 *   jes_render_key(), jes_render_string(),
 *   jes_render_int32(), jes_render_int64(),
 *   jes_render_uint32(), jes_render_uint64(), jes_render_double(),
 *   jes_render_true(), jes_render_false(), jes_render_null().
 *
 * Error checking strategy:
 *   Positive tests cast jes_render_*() calls to (void) to explicitly signal
 *   that individual return values are intentionally ignored. The entire
 *   sequence is verified with a single jes_take_streaming_status() call at
 *   the end — the sticky-error pattern.
 *   Negative tests use the per-call return value only where the exact status
 *   of a specific call is under test. No-op calls after a triggered error are
 *   also cast to (void).
 *
 * Build (from repo root):
 *   gcc test_jes_streaming_serializer.c \
 *       src/jes.c src/jes_tokenizer.c src/jes_parser.c \
 *       src/jes_serializer.c src/jes_tree.c \
 *       src/jes_hash_table.c src/jes_logger.c \
 *       -std=c99 -DNDEBUG -o test_jes_ss
 */

#include "../src/jes.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

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

#define CHECK_STATUS(id, ss, expected) \
    do { \
        jes_status _st = jes_take_streaming_status(ss); \
        if (_st == (expected)) pass(id); \
        else { char _m[64]; \
               snprintf(_m, sizeof(_m), "status=%d, expected=%d", \
                        (int)_st, (int)(expected)); \
               fail(id, _m); } \
    } while(0)

#define CHECK_OK(id, ss)    CHECK_STATUS(id, ss, JES_NO_ERROR)
#define CHECK_ERR(id, ss)   \
    do { \
        jes_status _st = jes_take_streaming_status(ss); \
        if (_st != JES_NO_ERROR) pass(id); \
        else fail(id, "expected error, got JES_NO_ERROR"); \
    } while(0)

/* =========================================================================
 * Workspace helpers
 *
 * jes_init_streaming() takes a caller-provided workspace that holds both
 * the context struct and the container stack. Size it with
 * JES_STREAMING_SERIALIZER_REQUIRED_SIZE.
 * ========================================================================= */

static uint8_t  g_workspace[JES_STREAMING_SERIALIZER_REQUIRED_SIZE];
static char     g_out[1024];

static struct jes_streaming_serializer_context *fresh_ss(void)
{
    memset(g_workspace, 0, sizeof(g_workspace));
    memset(g_out,       0, sizeof(g_out));
    return jes_init_streaming(g_workspace, sizeof(g_workspace),
                              g_out,       sizeof(g_out));
}

/* =========================================================================
 * Group 1 — jes_init_streaming
 * ========================================================================= */

static void test_group_init(void)
{
    printf("\nGroup 1: jes_init_streaming\n");

    static uint8_t ws[JES_STREAMING_SERIALIZER_REQUIRED_SIZE];
    static char    out[64];
    struct jes_streaming_serializer_context *ss;

    /* G1-01: Valid arguments → non-NULL context */
    ss = jes_init_streaming(ws, sizeof(ws), out, sizeof(out));
    CHECK("G1-01 valid args returns non-NULL", ss != NULL);

    /* G1-02: NULL workspace → NULL */
    ss = jes_init_streaming(NULL, sizeof(ws), out, sizeof(out));
    CHECK("G1-02 NULL workspace returns NULL", ss == NULL);

    /* G1-03: NULL output buffer → NULL */
    ss = jes_init_streaming(ws, sizeof(ws), NULL, sizeof(out));
    CHECK("G1-03 NULL output returns NULL", ss == NULL);

    /* G1-04: Workspace too small → NULL */
    uint8_t tiny[4];
    ss = jes_init_streaming(tiny, sizeof(tiny), out, sizeof(out));
    CHECK("G1-04 workspace too small returns NULL", ss == NULL);

    /* G1-05: Sticky error is clear after successful init */
    ss = jes_init_streaming(ws, sizeof(ws), out, sizeof(out));
    if (!ss) { fail("G1-05 init for sticky check", "init failed"); return; }
    CHECK_OK("G1-05 sticky error clear after init", ss);
}

/* =========================================================================
 * Group 2 — Positive: flat object
 * ========================================================================= */

static void test_group_flat_object(void)
{
    printf("\nGroup 2: Flat object\n");

    struct jes_streaming_serializer_context *ss = fresh_ss();
    if (!ss) { fail("G2-setup", "init failed"); return; }

    /* Build {"name":"Alice","score":42} — return values intentionally ignored */
    (void)jes_render_object_start(ss);
        (void)jes_render_key(ss, "name",  4); (void)jes_render_string(ss, "Alice", 5);
        (void)jes_render_key(ss, "score", 5); (void)jes_render_int32(ss, 42);
    (void)jes_render_object_end(ss);

    CHECK_OK("G2-01 sequence succeeds",      ss);
    CHECK("G2-02 opening brace",             g_out[0] == '{');
    CHECK("G2-03 name key present",          strstr(g_out, "\"name\"")  != NULL);
    CHECK("G2-04 Alice value present",       strstr(g_out, "\"Alice\"") != NULL);
    CHECK("G2-05 score key present",         strstr(g_out, "\"score\"") != NULL);
    CHECK("G2-06 42 value present",          strstr(g_out, "42")        != NULL);
    CHECK("G2-07 closing brace",             strchr(g_out, '}')         != NULL);
}

/* =========================================================================
 * Group 3 — Positive: all scalar value types
 * ========================================================================= */

static void test_group_scalar_types(void)
{
    printf("\nGroup 3: All scalar value types\n");

    struct jes_streaming_serializer_context *ss = fresh_ss();
    if (!ss) { fail("G3-setup", "init failed"); return; }

    (void)jes_render_object_start(ss);
        (void)jes_render_key(ss, "i32", 3); (void)jes_render_int32(ss,  -100);
        (void)jes_render_key(ss, "u32", 3); (void)jes_render_uint32(ss,  200u);
        (void)jes_render_key(ss, "i64", 3); (void)jes_render_int64(ss,  -9000000000LL);
        (void)jes_render_key(ss, "u64", 3); (void)jes_render_uint64(ss,  9000000000ULL);
        (void)jes_render_key(ss, "dbl", 3); (void)jes_render_double(ss,  3.14);
        (void)jes_render_key(ss, "str", 3); (void)jes_render_string(ss,  "hi", 2);
        (void)jes_render_key(ss, "yes", 3); (void)jes_render_true(ss);
        (void)jes_render_key(ss, "no",  2); (void)jes_render_false(ss);
        (void)jes_render_key(ss, "nil", 3); (void)jes_render_null(ss);
    (void)jes_render_object_end(ss);

    CHECK_OK("G3-01 sequence succeeds",      ss);
    CHECK("G3-02 int32  -100",               strstr(g_out, "-100")        != NULL);
    CHECK("G3-03 uint32  200",               strstr(g_out, "200")         != NULL);
    CHECK("G3-04 int64  -9000000000",        strstr(g_out, "-9000000000") != NULL);
    CHECK("G3-05 uint64  9000000000",        strstr(g_out, "9000000000")  != NULL);
    CHECK("G3-06 double  3.14",              strstr(g_out, "3.14")        != NULL);
    CHECK("G3-07 string \"hi\"",             strstr(g_out, "\"hi\"")      != NULL);
    CHECK("G3-08 true",                      strstr(g_out, "true")        != NULL);
    CHECK("G3-09 false",                     strstr(g_out, "false")       != NULL);
    CHECK("G3-10 null",                      strstr(g_out, "null")        != NULL);
}

/* =========================================================================
 * Group 4 — Positive: nesting
 * ========================================================================= */

static void test_group_nesting(void)
{
    printf("\nGroup 4: Nesting\n");

    /* ── nested object ───────────────────────────────────────────────── */
    {
        struct jes_streaming_serializer_context *ss = fresh_ss();
        if (!ss) { fail("G4-nested-obj-setup", "init failed"); return; }

        (void)jes_render_object_start(ss);
            (void)jes_render_key(ss, "outer", 5);
            (void)jes_render_object_start(ss);
                (void)jes_render_key(ss, "inner", 5);
                (void)jes_render_string(ss, "value", 5);
            (void)jes_render_object_end(ss);
        (void)jes_render_object_end(ss);

        CHECK_OK("G4-01 nested object succeeds",      ss);
        CHECK("G4-02 outer key",                      strstr(g_out, "\"outer\"") != NULL);
        CHECK("G4-03 inner key",                      strstr(g_out, "\"inner\"") != NULL);
        const char *p = strchr(g_out, '}');
        CHECK("G4-04 two closing braces",             p && strchr(p + 1, '}') != NULL);
    }

    /* ── flat array ──────────────────────────────────────────────────── */
    {
        struct jes_streaming_serializer_context *ss = fresh_ss();
        if (!ss) { fail("G4-flat-arr-setup", "init failed"); return; }

        (void)jes_render_array_start(ss);
            (void)jes_render_int32(ss, 1);
            (void)jes_render_int32(ss, 2);
            (void)jes_render_int32(ss, 3);
        (void)jes_render_array_end(ss);

        CHECK_OK("G4-05 flat array succeeds",         ss);
        CHECK("G4-06 opening bracket",                g_out[0] == '[');
        CHECK("G4-07 closing bracket",                strchr(g_out, ']') != NULL);
    }

    /* ── array of objects ────────────────────────────────────────────── */
    {
        struct jes_streaming_serializer_context *ss = fresh_ss();
        if (!ss) { fail("G4-arr-obj-setup", "init failed"); return; }

        (void)jes_render_array_start(ss);
            (void)jes_render_object_start(ss);
                (void)jes_render_key(ss, "id", 2); (void)jes_render_int32(ss, 1);
            (void)jes_render_object_end(ss);
            (void)jes_render_object_start(ss);
                (void)jes_render_key(ss, "id", 2); (void)jes_render_int32(ss, 2);
            (void)jes_render_object_end(ss);
        (void)jes_render_array_end(ss);

        CHECK_OK("G4-08 array of objects succeeds",   ss);
        const char *p1 = strstr(g_out, "1");
        const char *p2 = strstr(g_out, "2");
        CHECK("G4-09 element order preserved",        p1 && p2 && p1 < p2);
    }

    /* ── object → array → object (3 levels) ─────────────────────────── */
    {
        struct jes_streaming_serializer_context *ss = fresh_ss();
        if (!ss) { fail("G4-deep-setup", "init failed"); return; }

        (void)jes_render_object_start(ss);
            (void)jes_render_key(ss, "a", 1);
            (void)jes_render_array_start(ss);
                (void)jes_render_object_start(ss);
                    (void)jes_render_key(ss, "b", 1);
                    (void)jes_render_true(ss);
                (void)jes_render_object_end(ss);
            (void)jes_render_array_end(ss);
        (void)jes_render_object_end(ss);

        CHECK_OK("G4-10 deep nesting succeeds",       ss);
        CHECK("G4-11 array bracket present",          strchr(g_out, '[') != NULL);
    }
}

/* =========================================================================
 * Group 5 — Positive: edge cases
 * ========================================================================= */

static void test_group_edge_cases(void)
{
    printf("\nGroup 5: Edge cases\n");

    /* ── empty object ────────────────────────────────────────────────── */
    {
        struct jes_streaming_serializer_context *ss = fresh_ss();
        if (!ss) { fail("G5-empty-obj-setup", "init failed"); return; }

        (void)jes_render_object_start(ss);
        (void)jes_render_object_end(ss);

        CHECK_OK("G5-01 empty object succeeds",       ss);
        CHECK("G5-02 empty object output",            g_out[0] == '{' && strchr(g_out, '}') != NULL);
    }

    /* ── empty array ─────────────────────────────────────────────────── */
    {
        struct jes_streaming_serializer_context *ss = fresh_ss();
        if (!ss) { fail("G5-empty-arr-setup", "init failed"); return; }

        (void)jes_render_array_start(ss);
        (void)jes_render_array_end(ss);

        CHECK_OK("G5-03 empty array succeeds",        ss);
        CHECK("G5-04 empty array output",             g_out[0] == '[' && strchr(g_out, ']') != NULL);
    }

    /* ── comma separation and no trailing comma ──────────────────────── */
    {
        struct jes_streaming_serializer_context *ss = fresh_ss();
        if (!ss) { fail("G5-comma-setup", "init failed"); return; }

        (void)jes_render_array_start(ss);
            (void)jes_render_int32(ss, 1);
            (void)jes_render_int32(ss, 2);
            (void)jes_render_int32(ss, 3);
        (void)jes_render_array_end(ss);

        CHECK_OK("G5-05 comma array succeeds",        ss);
        CHECK("G5-06 commas present",                 strchr(g_out, ',') != NULL);
        char *bracket = strchr(g_out, ']');
        CHECK("G5-07 no trailing comma",              bracket && *(bracket - 1) != ',');
    }

    /* ── integer boundary values ─────────────────────────────────────── */
    {
        struct jes_streaming_serializer_context *ss = fresh_ss();
        if (!ss) { fail("G5-boundary-setup", "init failed"); return; }

        (void)jes_render_object_start(ss);
            (void)jes_render_key(ss, "i32min", 6); (void)jes_render_int32(ss,  -2147483647 - 1);
            (void)jes_render_key(ss, "i32max", 6); (void)jes_render_int32(ss,   2147483647);
            (void)jes_render_key(ss, "u32max", 6); (void)jes_render_uint32(ss,  4294967295u);
            (void)jes_render_key(ss, "i64min", 6); (void)jes_render_int64(ss,  -9223372036854775807LL - 1);
            (void)jes_render_key(ss, "u64max", 6); (void)jes_render_uint64(ss,  18446744073709551615ULL);
        (void)jes_render_object_end(ss);

        CHECK_OK("G5-08 boundary values succeed",         ss);
        CHECK("G5-09 INT32_MIN",   strstr(g_out, "-2147483648")             != NULL);
        CHECK("G5-10 INT32_MAX",   strstr(g_out, "2147483647")              != NULL);
        CHECK("G5-11 UINT32_MAX",  strstr(g_out, "4294967295")              != NULL);
        CHECK("G5-12 INT64_MIN",   strstr(g_out, "-9223372036854775808")    != NULL);
        CHECK("G5-13 UINT64_MAX",  strstr(g_out, "18446744073709551615")    != NULL);
    }
}

/* =========================================================================
 * Group 6 — Positive: round-trip through tree parser
 * ========================================================================= */

static void test_group_roundtrip(void)
{
    printf("\nGroup 6: Round-trip through tree parser\n");

    struct jes_streaming_serializer_context *ss = fresh_ss();
    if (!ss) { fail("G6-setup", "init failed"); return; }

    (void)jes_render_object_start(ss);
        (void)jes_render_key(ss, "sensor", 6); (void)jes_render_string(ss, "temp", 4);
        (void)jes_render_key(ss, "value",  5); (void)jes_render_int32(ss, 23);
    (void)jes_render_object_end(ss);

    CHECK_OK("G6-01 stream sequence succeeds", ss);

    /* Parse the output with the tree API */
    uint8_t ws[JES_REQUIRED_SIZE(20)];
    struct jes_context *ctx = jes_init(ws, sizeof(ws), JES_SEARCH_LINEAR);
    if (!ctx) { fail("G6-02 tree ctx init", "jes_init returned NULL"); return; }

    CHECK("G6-02 tree parse succeeds",
          jes_load(ctx, g_out, strlen(g_out)) == JES_NO_ERROR);

    struct jes_element *root  = jes_get_root(ctx);
    struct jes_element *k_val = jes_get_key(ctx, root, "value");
    CHECK("G6-03 key \"value\" found", k_val != NULL);

    if (k_val) {
        struct jes_element *v = jes_get_key_value(ctx, k_val);
        CHECK("G6-04 value type == JES_NUMBER", v && v->type == JES_NUMBER);
        CHECK("G6-05 value data == \"23\"",
              v && v->length == 2 && v->value[0] == '2' && v->value[1] == '3');
    }
}

/* =========================================================================
 * Group 7 — Sticky error behaviour
 * ========================================================================= */

static void test_group_sticky_error(void)
{
    printf("\nGroup 7: Sticky error behaviour\n");

    /* ── jes_take_streaming_status clears the error ───────────────────── */
    {
        struct jes_streaming_serializer_context *ss = fresh_ss();
        if (!ss) { fail("G7-clear-setup", "init failed"); return; }

        /* Provoke an error: value without opening container */
        jes_render_int32(ss, 1);

        jes_status first = jes_take_streaming_status(ss);
        CHECK("G7-01 first take returns error",     first != JES_NO_ERROR);
        CHECK_OK("G7-02 second take is cleared",    ss);
    }

    /* ── calls after an error are no-ops ─────────────────────────────── */
    {
        struct jes_streaming_serializer_context *ss = fresh_ss();
        if (!ss) { fail("G7-noop-setup", "init failed"); return; }

        /* Provoke first error: render_key outside any object */
        jes_render_key(ss, "k", 1);
        jes_status after_first_error = jes_take_streaming_status(ss);
        CHECK("G7-03 error recorded after bad call", after_first_error != JES_NO_ERROR);

        /* Re-init for the no-op sequence test */
        ss = fresh_ss();
        if (!ss) { fail("G7-noop-reinit", "init failed"); return; }

        /* First bad call sets the sticky error */
        (void)jes_render_key(ss, "k", 1);

        /* Snapshot the output length — subsequent calls must not advance it */
        size_t len_after_error = strlen(g_out);

        /* These should all be no-ops */
        (void)jes_render_string(ss, "v", 1);
        (void)jes_render_int32(ss, 99);
        (void)jes_render_object_end(ss);

        CHECK("G7-04 no-op calls do not write output",
              strlen(g_out) == len_after_error);
        CHECK_ERR("G7-05 sticky error preserved through no-ops", ss);
    }

    /* ── first error is preserved, not overwritten by later errors ────── */
    {
        struct jes_streaming_serializer_context *ss = fresh_ss();
        if (!ss) { fail("G7-first-wins-setup", "init failed"); return; }

        /* State machine in JES_START — render_key is invalid here.
         * Capture the return value of the first bad call directly
         * instead of peeking at the internal field. */
        jes_status first_error = jes_render_key(ss, "k", 1);
        CHECK("G7-06 first call returned error",  first_error != JES_NO_ERROR);

        /* More bad calls — these are no-ops but still return the sticky error */
        (void)jes_render_string(ss, "v", 1);
        (void)jes_render_object_end(ss);

        jes_status taken = jes_take_streaming_status(ss);
        CHECK("G7-07 taken error matches first",  taken == first_error);
        CHECK_OK("G7-08 cleared after take",      ss);
    }

    /* ── buffer overflow sets sticky error ───────────────────────────── */
    {
        static uint8_t ws[JES_STREAMING_SERIALIZER_REQUIRED_SIZE];
        char tiny[4];
        memset(ws,   0, sizeof(ws));
        memset(tiny, 0, sizeof(tiny));

        struct jes_streaming_serializer_context *ss =
            jes_init_streaming(ws, sizeof(ws), tiny, sizeof(tiny));
        if (!ss) { fail("G7-overflow-setup", "init failed"); return; }

        (void)jes_render_object_start(ss);
        (void)jes_render_key(ss, "longkey", 7);
        (void)jes_render_string(ss, "longvalue", 9);
        (void)jes_render_object_end(ss);

        CHECK_ERR("G7-08 overflow sets sticky error", ss);
    }
}

/* =========================================================================
 * Group 8 — Negative: invalid operation sequences
 * ========================================================================= */

static void test_group_invalid_sequences(void)
{
    printf("\nGroup 8: Invalid operation sequences\n");

    /* G8-01: Value without any container */
    {
        struct jes_streaming_serializer_context *ss = fresh_ss();
        if (!ss) { fail("G8-01-setup", "init failed"); return; }
        jes_render_int32(ss, 1);
        CHECK_ERR("G8-01 value without container", ss);
    }

    /* G8-02: Key without any container */
    {
        struct jes_streaming_serializer_context *ss = fresh_ss();
        if (!ss) { fail("G8-02-setup", "init failed"); return; }
        jes_render_key(ss, "k", 1);
        CHECK_ERR("G8-02 key without container", ss);
    }

    /* G8-03: Key inside array (arrays don't have keys) */
    {
        struct jes_streaming_serializer_context *ss = fresh_ss();
        if (!ss) { fail("G8-03-setup", "init failed"); return; }
        jes_render_array_start(ss);
        jes_render_key(ss, "k", 1);
        CHECK_ERR("G8-03 key inside array", ss);
    }

    /* G8-04: Value directly inside object (missing key) */
    {
        struct jes_streaming_serializer_context *ss = fresh_ss();
        if (!ss) { fail("G8-04-setup", "init failed"); return; }
        jes_render_object_start(ss);
        jes_render_int32(ss, 1);
        CHECK_ERR("G8-04 value without key in object", ss);
    }

    /* G8-05: Closing object when inside array */
    {
        struct jes_streaming_serializer_context *ss = fresh_ss();
        if (!ss) { fail("G8-05-setup", "init failed"); return; }
        jes_render_array_start(ss);
        jes_render_object_end(ss);
        CHECK_ERR("G8-05 object_end inside array", ss);
    }

    /* G8-06: Closing array when inside object */
    {
        struct jes_streaming_serializer_context *ss = fresh_ss();
        if (!ss) { fail("G8-06-setup", "init failed"); return; }
        jes_render_object_start(ss);
        jes_render_array_end(ss);
        CHECK_ERR("G8-06 array_end inside object", ss);
    }

    /* G8-07: NULL context passed to render function */
    {
        CHECK("G8-07 render_int32 NULL ctx returns error",
              jes_render_int32(NULL, 1) != JES_NO_ERROR);
        CHECK("G8-08 render_key NULL ctx returns error",
              jes_render_key(NULL, "k", 1) != JES_NO_ERROR);
        CHECK("G8-09 render_object_start NULL ctx returns error",
              jes_render_object_start(NULL) != JES_NO_ERROR);
    }
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    printf("=== JES Streaming Serializer Tests ===\n");

    test_group_init();
    test_group_flat_object();
    test_group_scalar_types();
    test_group_nesting();
    test_group_edge_cases();
    test_group_roundtrip();
    test_group_sticky_error();
    test_group_invalid_sequences();

    printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
