/**
 * jes_load_test.c
 *
 * Tests for jes_load() — the JSON parser entry point.
 *
 * Groups:
 *   1. Invalid arguments     — NULL/bad context, NULL input, zero length
 *   2. Valid JSON structures  — parse succeeds, render round-trip succeeds,
 *                              root element type verified
 *   3. Valid escape sequences — string literals with all recognised escapes
 *   4. Structural errors      — mismatched brackets, missing tokens, etc.
 *   5. Token-level errors     — bad literals, invalid escapes, invalid unicode
 *   6. Truncated input        — every prefix of a valid document must fail
 *
 * Build (from repo root):
 *   gcc jes_load_test.c src/jes.c src/jes_tokenizer.c src/jes_parser.c \
 *       src/jes_serializer.c src/jes_tree.c src/jes_hash_table.c \
 *       src/jes_logger.c -std=c99 -DNDEBUG -o jes_load_test
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

static void pass(const char *label) { printf("  [PASS] %s\n", label); g_passed++; }
static void fail(const char *label, const char *reason)
{
    printf("  [FAIL] %s — %s\n", label, reason);
    g_failed++;
}

static int status_matches(jes_status actual, jes_status s1, jes_status s2)
{
    return actual == s1 || ((int)s2 != -1 && actual == s2);
}

/* =========================================================================
 * Workspace
 * ========================================================================= */

static uint8_t g_ws[JES_REQUIRED_SIZE(64)];

static struct jes_context *fresh_ctx(void)
{
    return jes_init(g_ws, sizeof(g_ws), JES_SEARCH_LINEAR);
}

/* =========================================================================
 * Group 1 — Invalid arguments
 * ========================================================================= */

static void test_group_invalid_args(void)
{
    printf("\nGroup 1: Invalid arguments\n");

    struct jes_context *ctx;
    char bad_buf[4] = {0};
    struct jes_context *bad_ctx = (struct jes_context *)bad_buf;
    const char *json = "{\"k\":\"v\"}";

    /* G1-01: NULL context */
    {
        jes_status st = jes_load(NULL, json, strlen(json));
        if (st != JES_NO_ERROR) pass("G1-01 NULL ctx");
        else fail("G1-01 NULL ctx", "expected error, got JES_NO_ERROR");
    }

    /* G1-02: Uninitialised (garbage) context */
    {
        jes_status st = jes_load(bad_ctx, json, strlen(json));
        if (st != JES_NO_ERROR) pass("G1-02 bad ctx");
        else fail("G1-02 bad ctx", "expected error, got JES_NO_ERROR");
    }

    /* G1-03: NULL json_data */
    {
        ctx = fresh_ctx();
        jes_status st = jes_load(ctx, NULL, 10);
        if (st == JES_INVALID_PARAMETER) pass("G1-03 NULL json_data");
        else {
            char m[64]; snprintf(m, sizeof(m), "status=%d", (int)st);
            fail("G1-03 NULL json_data", m);
        }
    }

    /* G1-04: Zero length */
    {
        ctx = fresh_ctx();
        jes_status st = jes_load(ctx, json, 0);
        if (st == JES_INVALID_PARAMETER || st == JES_UNEXPECTED_EOF)
            pass("G1-04 zero length");
        else {
            char m[64]; snprintf(m, sizeof(m), "status=%d", (int)st);
            fail("G1-04 zero length", m);
        }
    }

    /* G1-05: Load called twice without reset */
    {
        ctx = fresh_ctx();
        jes_load(ctx, json, strlen(json));
        jes_status st = jes_load(ctx, json, strlen(json));
        if (st == JES_NO_ERROR || st == JES_INVALID_OPERATION)
            pass("G1-05 double load without reset");
        else {
            char m[64]; snprintf(m, sizeof(m), "status=%d", (int)st);
            fail("G1-05 double load without reset", m);
        }
    }
}

/* =========================================================================
 * Group 2 — Valid JSON structures
 * ========================================================================= */

typedef struct {
    const char   *json;
    const char   *description;
    enum jes_type root_type;
} valid_case;

static const valid_case VALID[] = {
    /* ── objects ──────────────────────────────────────────────────────── */
    { "{\"key\":\"value\"}",
      "simple object",                                              JES_OBJECT },
    { "{\"key\":[\"value\",{}]}",
      "object with array value",                                    JES_OBJECT },
    { "     {    \"key\"   :   \"value\"    }   ",
      "extra whitespace",                                           JES_OBJECT },
    { "{\"key\":{\"key\":[\"value1\",[{\"key\":\"value\"},\"value\",[{}]]]}}",
      "deeply nested",                                              JES_OBJECT },
    { "{\"\": \"value\"}",
      "empty-string key",                                           JES_OBJECT },
    { "{\"\": \"\"}",
      "empty key and value",                                        JES_OBJECT },

    /* ── arrays ───────────────────────────────────────────────────────── */
    { "[{\"\": \"\"}]",
      "array of objects",                                           JES_ARRAY  },
    { "[1, null, true, false, \"string\", [], {}]",
      "mixed-type array",                                           JES_ARRAY  },

    /* ── standalone scalars ───────────────────────────────────────────── */
    { "null",                    "standalone null",                 JES_NULL   },
    { "false",                   "standalone false",                JES_FALSE  },
    { "true",                    "standalone true",                 JES_TRUE   },
    { "1",                       "standalone integer",              JES_NUMBER },
    { "0",                       "standalone zero",                 JES_NUMBER },
    { "1.1",                     "standalone decimal",              JES_NUMBER },
    { "-1.0",                    "standalone negative decimal",     JES_NUMBER },
    { "0.6",                     "standalone 0.6",                  JES_NUMBER },
    { "-0.6",                    "standalone -0.6",                 JES_NUMBER },
    { "435000000000.1234567890", "standalone large number",         JES_NUMBER },
    { "4.35e-10",                "standalone scientific notation",  JES_NUMBER },
    { "\"string\"",              "standalone string",               JES_STRING },
};

static void test_group_valid_structures(void)
{
    const size_t N = sizeof(VALID) / sizeof(VALID[0]);
    char label[128];
    char out[4096];

    printf("\nGroup 2: Valid JSON structures (%zu cases)\n", N);

    for (size_t i = 0; i < N; i++) {
        const valid_case *tc = &VALID[i];
        memset(label, 0, sizeof(label));
        snprintf(label, sizeof(label), "G2-%02zu  %s", i + 1, tc->description);

        struct jes_context *ctx = fresh_ctx();
        if (!ctx) { fail(label, "ctx init"); continue; }

        if (jes_load(ctx, tc->json, strlen(tc->json)) != JES_NO_ERROR) {
            char m[64];
            snprintf(m, sizeof(m), "parse failed, status=%d", (int)jes_get_status(ctx));
            fail(label, m);
            continue;
        }

        struct jes_element *root = jes_get_root(ctx);
        if (!root) { fail(label, "root is NULL"); continue; }

        if (root->type != tc->root_type) {
            char m[64];
            snprintf(m, sizeof(m), "root type %d, expected %d",
                     (int)root->type, (int)tc->root_type);
            fail(label, m);
            continue;
        }

        if (jes_render(ctx, out, sizeof(out), true) == 0) {
            char m[64];
            snprintf(m, sizeof(m), "render failed, status=%d", (int)jes_get_status(ctx));
            fail(label, m);
            continue;
        }

        pass(label);
    }
}

/* =========================================================================
 * Group 3 — Valid escape sequences in strings
 * ========================================================================= */

static const valid_case ESCAPES[] = {
    { "\"Hello\\tWorld\"",            "\\t in string",              JES_STRING },
    { "\"My name is \\\"Hope\\\"\"",  "escaped quotes in string",   JES_STRING },
    { "\"Escaped \\\" \"",            "escaped double-quote",       JES_STRING },
    { "\"Escaped \\\\ \"",            "escaped backslash",          JES_STRING },
    { "\"Escaped \\/ \"",             "escaped forward-slash",      JES_STRING },
    { "\"Escaped \\b \"",             "escaped backspace",          JES_STRING },
    { "\"Escaped \\f \"",             "escaped form-feed",          JES_STRING },
    { "\"Escaped \\n \"",             "escaped newline",            JES_STRING },
    { "\"Escaped \\r \"",             "escaped carriage-return",    JES_STRING },
    { "\"Escaped \\t \"",             "escaped tab",                JES_STRING },
    { "\"unicode: \\u4f60\"",         "basic-plane unicode",        JES_STRING },
    { "\"unicode: \\u4f60\\u597d\"",  "two basic-plane codepoints", JES_STRING },
    { "\"unicode: \\ud801\\udc01\"",  "surrogate pair",             JES_STRING },
};

static void test_group_escape_sequences(void)
{
    const size_t N = sizeof(ESCAPES) / sizeof(ESCAPES[0]);
    char label[128];
    char out[512];

    printf("\nGroup 3: Valid escape sequences (%zu cases)\n", N);

    for (size_t i = 0; i < N; i++) {
        const valid_case *tc = &ESCAPES[i];
        snprintf(label, sizeof(label), "G3-%02zu  %s", i + 1, tc->description);

        struct jes_context *ctx = fresh_ctx();
        if (!ctx) { fail(label, "ctx init"); continue; }

        if (jes_load(ctx, tc->json, strlen(tc->json)) != JES_NO_ERROR) {
            char m[64];
            snprintf(m, sizeof(m), "parse failed, status=%d", (int)jes_get_status(ctx));
            fail(label, m);
            continue;
        }

        struct jes_element *root = jes_get_root(ctx);
        if (!root || root->type != JES_STRING) {
            fail(label, "root not JES_STRING"); continue;
        }

        if (jes_render(ctx, out, sizeof(out), true) == 0) {
            fail(label, "render failed"); continue;
        }

        pass(label);
    }
}

/* =========================================================================
 * Group 4 — Structural errors
 * ========================================================================= */

typedef struct {
    const char  *json;
    const char  *description;
    jes_status   s1;
    jes_status   s2;
} invalid_case;

static const invalid_case STRUCTURAL[] = {
    { "{",                 "lone opening brace",          JES_UNEXPECTED_EOF,   (jes_status)-1 },
    { "}",                 "lone closing brace",          JES_UNEXPECTED_TOKEN, (jes_status)-1 },
    { "[",                 "lone opening bracket",        JES_UNEXPECTED_EOF,   (jes_status)-1 },
    { "]",                 "lone closing bracket",        JES_UNEXPECTED_TOKEN, (jes_status)-1 },
    { "{1}",               "integer as key",              JES_UNEXPECTED_TOKEN, (jes_status)-1 },
    { "{\"key\"}",         "key without value",           JES_UNEXPECTED_TOKEN, (jes_status)-1 },
    { "{\"key\": }",       "empty value",                 JES_UNEXPECTED_TOKEN, (jes_status)-1 },
    { "{\"key\": \"value\"",
                           "missing closing brace",       JES_UNEXPECTED_EOF,   (jes_status)-1 },
    { "{\"key: \"value\"", "non-terminated key string",   JES_UNEXPECTED_SYMBOL, JES_UNEXPECTED_TOKEN },
    { "{\"key\": \"value}","non-terminated value string", JES_UNEXPECTED_EOF,   (jes_status)-1 },
    { "\"key\": \"value\"","bare key-value (no object)",  JES_UNEXPECTED_TOKEN, (jes_status)-1 },
    { "{key: \"value\"}",  "unquoted key",                JES_UNEXPECTED_SYMBOL, (jes_status)-1 },
    { "{key\": \"value\"", "partially quoted key",        JES_UNEXPECTED_SYMBOL, (jes_status)-1 },
    { "{'key': \"value\"}", "single-quoted key",          JES_UNEXPECTED_SYMBOL, (jes_status)-1 },
    { "{\"key\" \"value\"}","missing colon",              JES_UNEXPECTED_TOKEN, (jes_status)-1 },
    { "{\"key\": {\"a\":1}","missing outer closing brace",JES_UNEXPECTED_EOF,   (jes_status)-1 },
    { "{\"key\": \"value\"}}", "extra closing brace",     JES_UNEXPECTED_TOKEN, (jes_status)-1 },
    { "{\"key\": \"value\",}","trailing comma in object", JES_UNEXPECTED_TOKEN, (jes_status)-1 },
    { "{\"key\": [\"v\",]}", "trailing comma in array",   JES_UNEXPECTED_TOKEN, (jes_status)-1 },
    { "{\"k1\":\"v1\" \"k2\":\"v2\"}", "missing comma",   JES_UNEXPECTED_TOKEN, (jes_status)-1 },
    { "{\"key\": value}",  "unquoted string value",       JES_UNEXPECTED_SYMBOL, (jes_status)-1 },
    { "{\"key\": []]}",    "extra closing bracket",       JES_UNEXPECTED_TOKEN, (jes_status)-1 },
    { "{\"key\": [[]}",    "missing closing bracket",     JES_UNEXPECTED_EOF,   JES_UNEXPECTED_TOKEN },
    { "{\"key\": [{,}]}",  "comma in empty object",       JES_UNEXPECTED_TOKEN, (jes_status)-1 },
    { "{\"key\": [,]}",    "leading comma in array",      JES_UNEXPECTED_TOKEN, (jes_status)-1 },
    { "{\"key\": ,[]}",    "comma before value",          JES_UNEXPECTED_TOKEN, (jes_status)-1 },
    { "{\"key\": ][}",     "wrong bracket order",         JES_UNEXPECTED_TOKEN, (jes_status)-1 },
    { "{\"key\": [][]}",   "two arrays as one value",     JES_UNEXPECTED_TOKEN, (jes_status)-1 },
    { "{\"key\": [[[[[[[[[[]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]}",
                           "extra closing brackets",      JES_UNEXPECTED_TOKEN, (jes_status)-1 },
    { "{\"k\":\"v\", \"k\":\"v2\"}",
                           "duplicate keys",              JES_DUPLICATE_KEY,    (jes_status)-1 },
};

static void test_group_structural_errors(void)
{
    const size_t N = sizeof(STRUCTURAL) / sizeof(STRUCTURAL[0]);
    char label[128];

    printf("\nGroup 4: Structural errors (%zu cases)\n", N);

    for (size_t i = 0; i < N; i++) {
        const invalid_case *tc = &STRUCTURAL[i];
        snprintf(label, sizeof(label), "G4-%02zu  %s", i + 1, tc->description);

        struct jes_context *ctx = fresh_ctx();
        if (!ctx) { fail(label, "ctx init"); continue; }

        jes_status st = jes_load(ctx, tc->json, strlen(tc->json));
        if (st == JES_NO_ERROR) {
            fail(label, "expected failure, got JES_NO_ERROR");
        } else if (!status_matches(st, tc->s1, tc->s2)) {
            char m[80];
            snprintf(m, sizeof(m), "status=%d, expected %d%s%d",
                     (int)st, (int)tc->s1,
                     (int)tc->s2 != -1 ? " or " : "",
                     (int)tc->s2 != -1 ? (int)tc->s2 : (int)tc->s1);
            fail(label, m);
        } else {
            pass(label);
        }
    }
}

/* =========================================================================
 * Group 5 — Token-level errors
 * ========================================================================= */

static const invalid_case TOKEN_ERRORS[] = {
    /* ── unexpected raw control symbols ───────────────────────────────── */
    { "{\\f}",  "raw \\f outside string",      JES_UNEXPECTED_SYMBOL,      (jes_status)-1 },
    { "{\\t}",  "raw \\t outside string",      JES_UNEXPECTED_SYMBOL,      (jes_status)-1 },
    { "{\\r}",  "raw \\r outside string",      JES_UNEXPECTED_SYMBOL,      (jes_status)-1 },

    /* ── incorrectly cased literals ────────────────────────────────────── */
    { "{\"key\": True}",  "capitalised True",  JES_UNEXPECTED_SYMBOL,       (jes_status)-1 },
    { "{\"key\": False}", "capitalised False", JES_UNEXPECTED_SYMBOL,       (jes_status)-1 },
    { "{\"key\": Null}",  "capitalised Null",  JES_UNEXPECTED_SYMBOL,       (jes_status)-1 },
    { "tru",   "truncated true",               JES_UNEXPECTED_SYMBOL,        JES_UNEXPECTED_TOKEN },
    { "fals",  "truncated false",              JES_UNEXPECTED_SYMBOL,        JES_UNEXPECTED_TOKEN },
    { "nul",   "truncated null",               JES_UNEXPECTED_SYMBOL,        JES_UNEXPECTED_TOKEN },
    { "trues", "true with extra char",         JES_UNEXPECTED_SYMBOL,       (jes_status)-1 },

    /* ── invalid escape sequences ──────────────────────────────────────── */
    { "\"Escaped \\a \"", "invalid escape \\a",       JES_INVALID_ESCAPED_SYMBOL, (jes_status)-1 },
    { "\"Escaped \\1 \"", "invalid escape \\1",       JES_INVALID_ESCAPED_SYMBOL, (jes_status)-1 },
    { "\"Escaped \\  \"", "invalid escape \\ space",  JES_INVALID_ESCAPED_SYMBOL, (jes_status)-1 },
    { "\"Escaped \\. \"", "invalid escape \\.",       JES_INVALID_ESCAPED_SYMBOL, (jes_status)-1 },

    /* ── invalid unicode escapes ───────────────────────────────────────── */
    { "\"unicode: \\u4f60\\g597d\"", "non-hex digit in \\u", JES_INVALID_ESCAPED_SYMBOL, (jes_status)-1 },

    /* ── standalone invalid number ─────────────────────────────────────── */
    { "01",  "standalone leading-zero integer", JES_INVALID_NUMBER,        (jes_status)-1 },

    /* ── EOF inside string ─────────────────────────────────────────────── */
    { "{\"key\": \"val",  "EOF inside value string",  JES_UNEXPECTED_EOF,  (jes_status)-1 },
};

static void test_group_token_errors(void)
{
    const size_t N = sizeof(TOKEN_ERRORS) / sizeof(TOKEN_ERRORS[0]);
    char label[128];

    printf("\nGroup 5: Token-level errors (%zu cases)\n", N);

    for (size_t i = 0; i < N; i++) {
        const invalid_case *tc = &TOKEN_ERRORS[i];
        snprintf(label, sizeof(label), "G5-%02zu  %s", i + 1, tc->description);

        struct jes_context *ctx = fresh_ctx();
        if (!ctx) { fail(label, "ctx init"); continue; }

        jes_status st = jes_load(ctx, tc->json, strlen(tc->json));
        if (st == JES_NO_ERROR) {
            fail(label, "expected failure, got JES_NO_ERROR");
        } else if (!status_matches(st, tc->s1, tc->s2)) {
            char m[80];
            snprintf(m, sizeof(m), "status=%d, expected %d%s%d",
                     (int)st, (int)tc->s1,
                     (int)tc->s2 != -1 ? " or " : "",
                     (int)tc->s2 != -1 ? (int)tc->s2 : (int)tc->s1);
            fail(label, m);
        } else {
            pass(label);
        }
    }
}

/* =========================================================================
 * Group 6 — Truncated input
 *
 * Every strict prefix of a complete JSON document must be rejected.
 * ========================================================================= */

static void test_group_truncated_input(void)
{
    static const struct { const char *json; const char *desc; } DOCS[] = {
        { "{\"key\":\"value\"}",      "flat object"    },
        { "{\"a\":[1,true,\"s\"]}",   "nested object"  },
    };
    const size_t NDOCS = sizeof(DOCS) / sizeof(DOCS[0]);

    for (size_t d = 0; d < NDOCS; d++) {
        const char *full     = DOCS[d].json;
        const size_t full_len = strlen(full);

        printf("\nGroup 6: Truncated \"%s\" (%zu prefixes)\n",
               DOCS[d].desc, full_len - 1);

        for (size_t cut = 1; cut < full_len; cut++) {
            char label[64];
            snprintf(label, sizeof(label), "G6-%s-%02zu  first %zu byte(s)",
                     DOCS[d].desc, cut, cut);

            struct jes_context *ctx = fresh_ctx();
            if (!ctx) { fail(label, "ctx init"); continue; }

            jes_status st = jes_load(ctx, full, cut);
            if (st == JES_NO_ERROR)
                fail(label, "expected failure, got JES_NO_ERROR");
            else
                pass(label);
        }
    }
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    printf("=== jes_load() Tests ===\n");

    test_group_invalid_args();
    test_group_valid_structures();
    test_group_escape_sequences();
    test_group_structural_errors();
    test_group_token_errors();
    test_group_truncated_input();

    printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
