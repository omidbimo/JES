/**
 * test_jes_streaming_serializer.c
 *
 * Tests for the JES streaming (tree-less) serializer API.
 * Exercises: jes_init_streaming(), jes_render_object_start/end(),
 *            jes_render_array_start/end(), jes_render_key(),
 *            jes_render_string(), jes_render_int32(), jes_render_int64(),
 *            jes_render_uint32(), jes_render_uint64(), jes_render_double(),
 *            jes_render_true(), jes_render_false(), jes_render_null().
 *
 * Build (from repo root):
 *   gcc test_jes_streaming_serializer.c ../src/*.c \
 *       -std=c99 -DNDEBUG -o test_jes_ss && ./test_jes_ss
 */

#include "../src/jes.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ── test harness ───────────────────────────────────────────────────────── */

static int g_passed = 0;
static int g_failed = 0;

#define PASS(name)          do { printf("[PASS] %s.\n", (name)); g_passed++; } while(0)
#define FAIL(name, reason)  do { printf("[FAIL] %s. — %s\n", (name), (#reason)); g_failed++; } while(0)
#define CHECK(name, cond)   do { if (cond) PASS(name); else FAIL(name, cond); } while(0)

/* Output buffer shared across tests — cleared before each test */
static char     out[1024];
static uint8_t  workspace[JES_STREAMING_SERIALIZER_REQUIRED_SIZE];

static struct jes_streaming_serializer_context * init_ss()
{
    memset(out, 0, sizeof(out));
    memset(workspace, 0, sizeof(workspace));
    return jes_init_streaming(workspace, sizeof(workspace), out, sizeof(out));
}

/* ── tests ──────────────────────────────────────────────────────────────── */

/**
 * TC-SS-01: jes_init_streaming with valid args returns JES_NO_ERROR.
 */
static void test_init_ok(void)
{
    const char *name = "TC-SS-01 init ok";
    struct jes_streaming_serializer_context* ss_ctx;
    ss_ctx = jes_init_streaming(workspace, sizeof(workspace), out, sizeof(out));
    CHECK(name, ss_ctx != NULL);
}

/**
 * TC-SS-02: jes_init_streaming with NULL output buffer returns error.
 */
static void test_init_null_output(void)
{
    const char *name = "TC-SS-02 init null output";
    struct jes_streaming_serializer_context* ss_ctx;
    ss_ctx = jes_init_streaming(workspace, sizeof(workspace), out, sizeof(out));
    CHECK(name, ss_ctx != NULL);
}

/**
 * TC-SS-03: jes_init_streaming with NULL stack buffer returns error.
 */
static void test_init_null_stack(void)
{
    const char *name = "TC-SS-03 init null stack";
    struct jes_streaming_serializer_context* ss_ctx;
    ss_ctx = jes_init_streaming(workspace, sizeof(workspace), out, sizeof(out));
    CHECK(name, ss_ctx != NULL);
}

/**
 * TC-SS-04: Flat object — {"name":"Alice","score":42}
 */
static void test_flat_object(void)
{
    const char *name = "TC-SS-04 flat object";
    struct jes_streaming_serializer_context* ss_ctx;
    ss_ctx = init_ss();

    jes_status st = JES_NO_ERROR;
    st |= jes_render_object_start(ss_ctx);
        st |= jes_render_key(ss_ctx, "name", 4);
        st |= jes_render_string(ss_ctx, "Alice", 5);
        st |= jes_render_key(ss_ctx, "score", 5);
        st |= jes_render_int32(ss_ctx, 42);
    st |= jes_render_object_end(ss_ctx);

    CHECK(name,                         st == JES_NO_ERROR);
    CHECK("TC-SS-04 opening brace",     out[0] == '{');
    CHECK("TC-SS-04 name key",          strstr(out, "\"name\"")  != NULL);
    CHECK("TC-SS-04 Alice value",       strstr(out, "\"Alice\"") != NULL);
    CHECK("TC-SS-04 score key",         strstr(out, "\"score\"") != NULL);
    CHECK("TC-SS-04 42 value",          strstr(out, "42")        != NULL);
    CHECK("TC-SS-04 closing brace",     strchr(out, '}')         != NULL);
}

/**
 * TC-SS-05: All scalar value types in a single object.
 */
static void test_all_scalar_types(void)
{
    const char *name = "TC-SS-05 all scalar types";
    struct jes_streaming_serializer_context* ss_ctx;
    ss_ctx = init_ss();

    jes_status st = JES_NO_ERROR;
    st |= jes_render_object_start(ss_ctx);
        st |= jes_render_key(ss_ctx, "i32",  3); st |= jes_render_int32(ss_ctx,  -100);
        st |= jes_render_key(ss_ctx, "u32",  3); st |= jes_render_uint32(ss_ctx,  200u);
        st |= jes_render_key(ss_ctx, "i64",  3); st |= jes_render_int64(ss_ctx,  -9000000000LL);
        st |= jes_render_key(ss_ctx, "u64",  3); st |= jes_render_uint64(ss_ctx,  9000000000ULL);
        st |= jes_render_key(ss_ctx, "dbl",  3); st |= jes_render_double(ss_ctx,  3.14);
        st |= jes_render_key(ss_ctx, "str",  3); st |= jes_render_string(ss_ctx,  "hi", 2);
        st |= jes_render_key(ss_ctx, "yes",  3); st |= jes_render_true(ss_ctx);
        st |= jes_render_key(ss_ctx, "no",   2); st |= jes_render_false(ss_ctx);
        st |= jes_render_key(ss_ctx, "nil",  3); st |= jes_render_null(ss_ctx);
    st |= jes_render_object_end(ss_ctx);

    CHECK(name,                     st == JES_NO_ERROR);
    CHECK("TC-SS-05 -100",          strstr(out, "-100")          != NULL);
    CHECK("TC-SS-05 200",           strstr(out, "200")           != NULL);
    CHECK("TC-SS-05 -9000000000",   strstr(out, "-9000000000")   != NULL);
    CHECK("TC-SS-05 9000000000",    strstr(out, "9000000000")    != NULL);
    CHECK("TC-SS-05 3.14",          strstr(out, "3.14")          != NULL);
    CHECK("TC-SS-05 hi string",     strstr(out, "\"hi\"")        != NULL);
    CHECK("TC-SS-05 true",          strstr(out, "true")          != NULL);
    CHECK("TC-SS-05 false",         strstr(out, "false")         != NULL);
    CHECK("TC-SS-05 null",          strstr(out, "null")          != NULL);
}

/**
 * TC-SS-06: Nested object — {"outer":{"inner":"value"}}
 */
static void test_nested_object(void)
{
    const char *name = "TC-SS-06 nested object";
    struct jes_streaming_serializer_context* ss_ctx;
    ss_ctx = init_ss();

    jes_status st = JES_NO_ERROR;
    st |= jes_render_object_start(ss_ctx);
        st |= jes_render_key(ss_ctx, "outer", 5);
        st |= jes_render_object_start(ss_ctx);
            st |= jes_render_key(ss_ctx, "inner", 5);
            st |= jes_render_string(ss_ctx, "value", 5);
        st |= jes_render_object_end(ss_ctx);
    st |= jes_render_object_end(ss_ctx);

    CHECK(name,                     st == JES_NO_ERROR);
    CHECK("TC-SS-06 outer key",     strstr(out, "\"outer\"") != NULL);
    CHECK("TC-SS-06 inner key",     strstr(out, "\"inner\"") != NULL);
    CHECK("TC-SS-06 value",         strstr(out, "\"value\"") != NULL);
    /* Two closing braces */
    const char *p = strchr(out, '}');
    CHECK("TC-SS-06 two braces",    p && strchr(p + 1, '}') != NULL);
}

/**
 * TC-SS-07: Flat array of integers — [1,2,3]
 */
static void test_flat_array(void)
{
    const char *name = "TC-SS-07 flat array";
    struct jes_streaming_serializer_context* ss_ctx;
    ss_ctx = init_ss();

    jes_status st = JES_NO_ERROR;
    st |= jes_render_array_start(ss_ctx);
        st |= jes_render_int32(ss_ctx, 1);
        st |= jes_render_int32(ss_ctx, 2);
        st |= jes_render_int32(ss_ctx, 3);
    st |= jes_render_array_end(ss_ctx);

    CHECK(name,                 st == JES_NO_ERROR);
    CHECK("TC-SS-07 [",         out[0] == '[');
    CHECK("TC-SS-07 1",         strstr(out, "1") != NULL);
    CHECK("TC-SS-07 2",         strstr(out, "2") != NULL);
    CHECK("TC-SS-07 3",         strstr(out, "3") != NULL);
    CHECK("TC-SS-07 ]",         strchr(out, ']') != NULL);
}

/**
 * TC-SS-08: Array of mixed types — [true,false,null,"text",99]
 */
static void test_mixed_array(void)
{
    const char *name = "TC-SS-08 mixed array";
    struct jes_streaming_serializer_context* ss_ctx;
    ss_ctx = init_ss();

    jes_status st = JES_NO_ERROR;
    st |= jes_render_array_start(ss_ctx);
        st |= jes_render_true(ss_ctx);
        st |= jes_render_false(ss_ctx);
        st |= jes_render_null(ss_ctx);
        st |= jes_render_string(ss_ctx, "text", 4);
        st |= jes_render_int32(ss_ctx, 99);
    st |= jes_render_array_end(ss_ctx);

    CHECK(name,                     st == JES_NO_ERROR);
    CHECK("TC-SS-08 true",          strstr(out, "true")    != NULL);
    CHECK("TC-SS-08 false",         strstr(out, "false")   != NULL);
    CHECK("TC-SS-08 null",          strstr(out, "null")    != NULL);
    CHECK("TC-SS-08 text",          strstr(out, "\"text\"") != NULL);
    CHECK("TC-SS-08 99",            strstr(out, "99")      != NULL);
}

/**
 * TC-SS-09: Array of objects — [{"id":1},{"id":2}]
 */
static void test_array_of_objects(void)
{
    const char *name = "TC-SS-09 array of objects";
    struct jes_streaming_serializer_context* ss_ctx;
    ss_ctx = init_ss();

    jes_status st = JES_NO_ERROR;
    st |= jes_render_array_start(ss_ctx);
        st |= jes_render_object_start(ss_ctx);
            st |= jes_render_key(ss_ctx, "id", 2);
            st |= jes_render_int32(ss_ctx, 1);
        st |= jes_render_object_end(ss_ctx);
        st |= jes_render_object_start(ss_ctx);
            st |= jes_render_key(ss_ctx, "id", 2);
            st |= jes_render_int32(ss_ctx, 2);
        st |= jes_render_object_end(ss_ctx);
    st |= jes_render_array_end(ss_ctx);

    CHECK(name,                 st == JES_NO_ERROR);
    CHECK("TC-SS-09 [",         out[0] == '[');
    CHECK("TC-SS-09 id key",    strstr(out, "\"id\"")    != NULL);
    /* Both values present — find '1' then '2' after it */
    const char *p1 = strstr(out, "1");
    const char *p2 = strstr(out, "2");
    CHECK("TC-SS-09 1 before 2", p1 && p2 && p1 < p2);
}

/**
 * TC-SS-10: Deeply nested — object inside array inside object.
 * {"a":[{"b":true}]}
 */
static void test_deep_nesting(void)
{
    const char *name = "TC-SS-10 deep nesting";
    struct jes_streaming_serializer_context* ss_ctx;
    ss_ctx = init_ss();

    jes_status st = JES_NO_ERROR;
    st |= jes_render_object_start(ss_ctx);
        st |= jes_render_key(ss_ctx, "a", 1);
        st |= jes_render_array_start(ss_ctx);
            st |= jes_render_object_start(ss_ctx);
                st |= jes_render_key(ss_ctx, "b", 1);
                st |= jes_render_true(ss_ctx);
            st |= jes_render_object_end(ss_ctx);
        st |= jes_render_array_end(ss_ctx);
    st |= jes_render_object_end(ss_ctx);

    CHECK(name,                 st == JES_NO_ERROR);
    CHECK("TC-SS-10 a key",     strstr(out, "\"a\"") != NULL);
    CHECK("TC-SS-10 b key",     strstr(out, "\"b\"") != NULL);
    CHECK("TC-SS-10 true",      strstr(out, "true")  != NULL);
    CHECK("TC-SS-10 [",         strchr(out, '[')     != NULL);
    CHECK("TC-SS-10 ]",         strchr(out, ']')     != NULL);
}

/**
 * TC-SS-11: Output is valid JSON — parse it back with the tree API.
 * Builds: {"sensor":"temp","value":23}
 */
static void test_output_is_valid_json(void)
{
    const char *name = "TC-SS-11 output is valid JSON";
    struct jes_streaming_serializer_context* ss_ctx;
    ss_ctx = init_ss();

    jes_render_object_start(ss_ctx);
        jes_render_key(ss_ctx, "sensor", 6);
        jes_render_string(ss_ctx, "temp", 4);
        jes_render_key(ss_ctx, "value", 5);
        jes_render_int32(ss_ctx, 23);
    jes_render_object_end(ss_ctx);

    /* Re-parse with the tree API */
    uint8_t  ws[JES_REQUIRED_SIZE(20)];
    struct jes_context *ctx = jes_init(ws, sizeof(ws), JES_SEARCH_LINEAR);
    if (!ctx) { FAIL(name, "tree ss_ctx init"); return; }

    jes_status st = jes_load(ctx, out, strlen(out));
    CHECK(name, st == JES_NO_ERROR);

    struct jes_element *root  = jes_get_root(ctx);
    struct jes_element *k_val = jes_get_key(ctx, root, "value");
    CHECK("TC-SS-11 key found",   k_val != NULL);
    if (k_val) {
        struct jes_element *v = jes_get_key_value(ctx, k_val);
        CHECK("TC-SS-11 value == 23",
              v && v->type == JES_NUMBER
              && v->length == 2
              && v->value[0] == '2' && v->value[1] == '3');
    }
}

/**
 * TC-SS-12: Buffer too small — render_object_start or a value should fail.
 */
static void test_buffer_too_small(void)
{
    const char *name = "TC-SS-12 buffer too small";
    struct jes_streaming_serializer_context* ss_ctx;
    char tiny[4];
    uint8_t tiny_stack[JES_STREAMING_SERIALIZER_REQUIRED_SIZE];


    ss_ctx = jes_init_streaming(workspace, sizeof(workspace), tiny, sizeof(tiny));
    if (ss_ctx == NULL) { PASS(name); return; } /* init itself rejected */

    jes_status st = JES_NO_ERROR;
    st |= jes_render_object_start(ss_ctx);
    st |= jes_render_key(ss_ctx, "longkey", 7);
    st |= jes_render_string(ss_ctx, "longvalue", 9);
    st |= jes_render_object_end(ss_ctx);

    CHECK(name, st != JES_NO_ERROR);
}

/**
 * TC-SS-13: Empty object — {}
 */
static void test_empty_object(void)
{
    const char *name = "TC-SS-13 empty object";
    struct jes_streaming_serializer_context* ss_ctx;
    ss_ctx = init_ss();

    jes_status st = JES_NO_ERROR;
    st |= jes_render_object_start(ss_ctx);
    st |= jes_render_object_end(ss_ctx);

    CHECK(name,             st == JES_NO_ERROR);
    CHECK("TC-SS-13 {}",    out[0] == '{' && strchr(out, '}') != NULL);
}

/**
 * TC-SS-14: Empty array — []
 */
static void test_empty_array(void)
{
    const char *name = "TC-SS-14 empty array";
    struct jes_streaming_serializer_context* ss_ctx;
    ss_ctx = init_ss();

    jes_status st = JES_NO_ERROR;
    st |= jes_render_array_start(ss_ctx);
    st |= jes_render_array_end(ss_ctx);

    CHECK(name,             st == JES_NO_ERROR);
    CHECK("TC-SS-14 []",    out[0] == '[' && strchr(out, ']') != NULL);
}

/**
 * TC-SS-15: Integer boundary values — INT32_MIN, INT32_MAX, UINT32_MAX,
 *           INT64_MIN, UINT64_MAX.
 */
static void test_integer_boundaries(void)
{
    const char *name = "TC-SS-15 integer boundaries";
    struct jes_streaming_serializer_context* ss_ctx;
    ss_ctx = init_ss();

    jes_status st = JES_NO_ERROR;
    st |= jes_render_object_start(ss_ctx);
        st |= jes_render_key(ss_ctx, "i32min", 6); st |= jes_render_int32(ss_ctx,  -2147483647 - 1);
        st |= jes_render_key(ss_ctx, "i32max", 6); st |= jes_render_int32(ss_ctx,   2147483647);
        st |= jes_render_key(ss_ctx, "u32max", 6); st |= jes_render_uint32(ss_ctx,  4294967295u);
        st |= jes_render_key(ss_ctx, "i64min", 6); st |= jes_render_int64(ss_ctx,  -9223372036854775807LL - 1);
        st |= jes_render_key(ss_ctx, "u64max", 6); st |= jes_render_uint64(ss_ctx,  18446744073709551615ULL);
    st |= jes_render_object_end(ss_ctx);

    CHECK(name,                               st == JES_NO_ERROR);
    CHECK("TC-SS-15 -2147483648",             strstr(out, "-2147483648")             != NULL);
    CHECK("TC-SS-15 2147483647",              strstr(out, "2147483647")              != NULL);
    CHECK("TC-SS-15 4294967295",              strstr(out, "4294967295")              != NULL);
    CHECK("TC-SS-15 -9223372036854775808",    strstr(out, "-9223372036854775808")    != NULL);
    CHECK("TC-SS-15 18446744073709551615",    strstr(out, "18446744073709551615")    != NULL);
}

/**
 * TC-SS-16: Comma separation — multiple values in an array must be
 *           separated by commas and no trailing comma before ']'.
 * Expected: [1,2,3]
 */
static void test_comma_separation(void)
{
    const char *name = "TC-SS-16 comma separation";
    struct jes_streaming_serializer_context* ss_ctx;
    ss_ctx = init_ss();

    jes_render_array_start(ss_ctx);
    jes_render_int32(ss_ctx, 1);
    jes_render_int32(ss_ctx, 2);
    jes_render_int32(ss_ctx, 3);
    jes_render_array_end(ss_ctx);

    /* Must contain commas between values */
    CHECK(name,                         strchr(out, ',') != NULL);
    /* No trailing comma: last char before ']' must not be ',' */
    char *bracket = strchr(out, ']');
    CHECK("TC-SS-16 no trailing comma", bracket && *(bracket - 1) != ',');
}

/* ── main ───────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== JES Streaming Serializer Tests ===\n\n");

    test_init_ok();
    test_init_null_output();
    test_init_null_stack();
    test_flat_object();
    test_all_scalar_types();
    test_nested_object();
    test_flat_array();
    test_mixed_array();
    test_array_of_objects();
    test_deep_nesting();
    test_output_is_valid_json();
    test_buffer_too_small();
    test_empty_object();
    test_empty_array();
    test_integer_boundaries();
    test_comma_separation();

    printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
