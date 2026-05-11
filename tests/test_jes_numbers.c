/**
 * jes_numbers_test.c
 *
 * Parser tests for JSON number literals.
 *
 * Groups:
 *   1. Valid numbers   — jes_load() must succeed, element type must be JES_NUMBER,
 *                        stored value must match the original token.
 *   2. Invalid numbers — jes_load() must fail with JES_INVALID_NUMBER or
 *                        JES_UNEXPECTED_EOF (truncated input) or JES_UNEXPECTED_TOKEN.
 *
 * Build (from repo root):
 *   gcc jes_numbers_test.c src/jes.c src/jes_tokenizer.c src/jes_parser.c \
 *       src/jes_serializer.c src/jes_tree.c src/jes_hash_table.c \
 *       src/jes_logger.c -std=c99 -DNDEBUG -o jes_numbers_test
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

/* =========================================================================
 * Workspace — re-used across every sub-test via jes_init()
 * ========================================================================= */

static uint8_t g_ws[JES_REQUIRED_SIZE(32)]; /* 1 object + 1 key + 1 number */

/* =========================================================================
 * Positive test cases
 *
 * Each entry is a full JSON document {"number": <value>}.
 * The "expected_token" field is the exact bytes the tokenizer should store
 * (i.e. the number literal as it appears in the source, without surrounding
 * whitespace or quotes).
 * ========================================================================= */

typedef struct {
    const char *json;           /* Full JSON string (null-terminated)  */
    const char *expected_token; /* Expected stored value in the element */
} positive_case;

static const positive_case POSITIVE[] = {
    /* ── integers ─────────────────────────────────────────────────────── */
    { "{\"n\":0}",                          "0"                          },
    { "{\"n\":1}",                          "1"                          },
    { "{\"n\":12}",                         "12"                         },
    { "{\"n\":123}",                        "123"                        },
    { "{\"n\":1234567890}",                 "1234567890"                 },
    { "{\"n\":-1}",                         "-1"                         },
    { "{\"n\":-1234567890}",               "-1234567890"                },

    /* ── decimals ──────────────────────────────────────────────────────── */
    { "{\"n\":0.0}",                        "0.0"                        },
    { "{\"n\":0.123456789}",               "0.123456789"                },
    { "{\"n\":123456789.123456789}",        "123456789.123456789"        },
    { "{\"n\":-0.0}",                       "-0.0"                       },
    { "{\"n\":-0.123456789}",              "-0.123456789"               },
    { "{\"n\":-123456789.123456789}",       "-123456789.123456789"       },

    /* ── very long mantissa ───────────────────────────────────────────── */
    { "{\"n\":0.11111111111111111111111111111111111111111111111111111111111"
        "11111111111111111111111111111111111111111111111111111111111111111"
        "11111111111111111111111111}",
      "0.11111111111111111111111111111111111111111111111111111111111111111"
        "11111111111111111111111111111111111111111111111111111111111111111"
        "11111111111111111111"                                      },

    /* ── exponent — lowercase 'e' ─────────────────────────────────────── */
    { "{\"n\":0e0}",                        "0e0"                        },
    { "{\"n\":123456789.123456789e0}",      "123456789.123456789e0"      },
    { "{\"n\":123456789.123456789e1}",      "123456789.123456789e1"      },
    { "{\"n\":123456789.123456789e123456789}", "123456789.123456789e123456789" },
    { "{\"n\":-123456789.123456789e0}",     "-123456789.123456789e0"     },
    { "{\"n\":-123456789.123456789e1}",     "-123456789.123456789e1"     },
    { "{\"n\":-123456789.123456789e123456789}", "-123456789.123456789e123456789" },

    /* ── exponent — negative exponent ─────────────────────────────────── */
    { "{\"n\":123456789.123456789e-0}",     "123456789.123456789e-0"     },
    { "{\"n\":123456789.123456789e-1}",     "123456789.123456789e-1"     },
    { "{\"n\":123456789.123456789e-123456789}", "123456789.123456789e-123456789" },
    { "{\"n\":-123456789.123456789e-0}",    "-123456789.123456789e-0"    },
    { "{\"n\":-123456789.123456789e-1}",    "-123456789.123456789e-1"    },
    { "{\"n\":-123456789.123456789e-123456789}", "-123456789.123456789e-123456789" },

    /* ── exponent — positive exponent ─────────────────────────────────── */
    { "{\"n\":123456789.123456789e+0}",     "123456789.123456789e+0"     },
    { "{\"n\":123456789.123456789e+1}",     "123456789.123456789e+1"     },
    { "{\"n\":123456789.123456789e+123456789}", "123456789.123456789e+123456789" },
    { "{\"n\":-123456789.123456789e+0}",    "-123456789.123456789e+0"    },
    { "{\"n\":-123456789.123456789e+1}",    "-123456789.123456789e+1"    },
    { "{\"n\":-123456789.123456789e+123456789}", "-123456789.123456789e+123456789" },

    /* ── exponent — uppercase 'E' ─────────────────────────────────────── */
    { "{\"n\":123456789.123456789E0}",      "123456789.123456789E0"      },
    { "{\"n\":123456789.123456789E1}",      "123456789.123456789E1"      },
    { "{\"n\":123456789.123456789E123456789}", "123456789.123456789E123456789" },
    { "{\"n\":-123456789.123456789E0}",     "-123456789.123456789E0"     },
    { "{\"n\":-123456789.123456789E1}",     "-123456789.123456789E1"     },
    { "{\"n\":-123456789.123456789E-1}",    "-123456789.123456789E-1"    },
    { "{\"n\":123456789.123456789E-0}",     "123456789.123456789E-0"     },
    { "{\"n\":123456789.123456789E-1}",     "123456789.123456789E-1"     },
    { "{\"n\":123456789.123456789E-123456789}", "123456789.123456789E-123456789" },
    { "{\"n\":-123456789.123456789E-0}",    "-123456789.123456789E-0"    },
    { "{\"n\":-123456789.123456789E-123456789}", "-123456789.123456789E-123456789" },
    { "{\"n\":123456789.123456789E+0}",     "123456789.123456789E+0"     },
    { "{\"n\":123456789.123456789E+1}",     "123456789.123456789E+1"     },
    { "{\"n\":123456789.123456789E+123456789}", "123456789.123456789E+123456789" },
    { "{\"n\":-123456789.123456789E+0}",    "-123456789.123456789E+0"    },
    { "{\"n\":-123456789.123456789E+1}",    "-123456789.123456789E+1"    },
    { "{\"n\":-123456789.123456789E+123456789}", "-123456789.123456789E+123456789" },

    /* ── trailing whitespace inside object ────────────────────────────── */
    { "{\"n\":1 }",                         "1"                          },
    { "{\"n\":-1 }",                        "-1"                         },
    { "{\"n\":0.5 }",                       "0.5"                        },
    { "{\"n\":1e2 }",                       "1e2"                        },
};

/* =========================================================================
 * Negative test cases
 *
 * Each entry is a JSON string that must be rejected by the tokenizer/parser.
 * expected_status lists the acceptable error codes; the first matching one wins.
 * ========================================================================= */

typedef struct {
    const char *json;
    const char *description;
    /* Two acceptable status codes (set second to -1 if only one applies) */
    jes_status  s1;
    jes_status  s2;
} negative_case;

#define ONLY(s) (s), (jes_status)(-1)

static const negative_case NEGATIVE[] = {
    /* ── leading zeros ────────────────────────────────────────────────── */
    { "{\"n\":00}",        "leading zero",              ONLY(JES_INVALID_NUMBER)  },
    { "{\"n\":01}",        "leading zero + digit",      ONLY(JES_INVALID_NUMBER)  },
    { "{\"n\":01.0}",      "leading zero + decimal",    ONLY(JES_INVALID_NUMBER)  },
    { "{\"n\":00.}",       "double leading zero",       ONLY(JES_INVALID_NUMBER)  },

    /* ── missing integer part ─────────────────────────────────────────── */
    { "{\"n\":.0}",        "no integer part",           ONLY(JES_UNEXPECTED_SYMBOL) },
    { "{\"n\":+0}",        "leading plus",              ONLY(JES_UNEXPECTED_SYMBOL) },
    { "{\"n\":e}",         "bare 'e'",                  ONLY(JES_UNEXPECTED_SYMBOL) },
    { "{\"n\":e1}",        "bare 'e1'",                 ONLY(JES_UNEXPECTED_SYMBOL) },
    { "{\"n\":............}","only dots",               ONLY(JES_UNEXPECTED_SYMBOL) },

    /* ── sign without digits ──────────────────────────────────────────── */
    { "{\"n\":-}",         "lone minus",                ONLY(JES_UNEXPECTED_SYMBOL) },
    { "{\"n\":-.}",        "minus dot",                 ONLY(JES_UNEXPECTED_SYMBOL) },
    { "{\"n\":-+}",        "minus plus",                ONLY(JES_UNEXPECTED_SYMBOL) },
    { "{\"n\":-e}",        "minus e",                   ONLY(JES_UNEXPECTED_SYMBOL) },

    /* ── bad decimal ──────────────────────────────────────────────────── */
    { "{\"n\":1.}",        "trailing dot",              ONLY(JES_UNEXPECTED_SYMBOL)  },
    { "{\"n\":0..}",       "double dot",                ONLY(JES_UNEXPECTED_SYMBOL)  },
    { "{\"n\":1..}",       "double dot",                ONLY(JES_UNEXPECTED_SYMBOL)  },
    { "{\"n\":0.0.}",      "two decimal points",        ONLY(JES_UNEXPECTED_SYMBOL)  },
    { "{\"n\":-0.0.}",     "two decimal points (neg)",  ONLY(JES_UNEXPECTED_SYMBOL)  },
    { "{\"n\":-0.123456789.}", "trailing dot on decimal", ONLY(JES_UNEXPECTED_SYMBOL) },
    { "{\"n\":0.1111111111111111111111111111111111111111.0}",
                           "second decimal point",      ONLY(JES_UNEXPECTED_SYMBOL)  },

    /* ── bad exponent ─────────────────────────────────────────────────── */
    { "{\"n\":1e}",        "e without digits",          JES_INVALID_NUMBER, JES_UNEXPECTED_EOF },
    { "{\"n\":0e.}",       "e then dot",                ONLY(JES_INVALID_NUMBER)  },
    { "{\"n\":0e+}",       "e+ without digits",         JES_INVALID_NUMBER, JES_UNEXPECTED_EOF },
    { "{\"n\":0e-}",       "e- without digits",         JES_INVALID_NUMBER, JES_UNEXPECTED_EOF },
    { "{\"n\":0E.}",       "E then dot",                ONLY(JES_INVALID_NUMBER)  },
    { "{\"n\":0E+}",       "E+ without digits",         JES_INVALID_NUMBER, JES_UNEXPECTED_EOF },
    { "{\"n\":0E-}",       "E- without digits",         JES_INVALID_NUMBER, JES_UNEXPECTED_EOF },
    { "{\"n\":0e-1-}",     "second minus in exponent",  ONLY(JES_UNEXPECTED_SYMBOL)  },
    { "{\"n\":0e+1+}",     "second plus in exponent",   ONLY(JES_UNEXPECTED_SYMBOL)  },
    { "{\"n\":0e-1-2}",    "extra minus after exp",     ONLY(JES_UNEXPECTED_TOKEN)   },
    { "{\"n\":0e-1+2}",    "sign change mid-exponent",  ONLY(JES_UNEXPECTED_SYMBOL)  },
    { "{\"n\":0e-1.2}",    "decimal in exponent",       ONLY(JES_UNEXPECTED_SYMBOL)  },
    { "{\"n\":0eeeee}",    "multiple e's",              ONLY(JES_INVALID_NUMBER)     },
    { "{\"n\":0.123456789e1.1}", "dot after exp digit", ONLY(JES_UNEXPECTED_SYMBOL)  },

    /* ── stray signs / operators ──────────────────────────────────────── */
    { "{\"n\":0-}",        "trailing minus",            ONLY(JES_UNEXPECTED_SYMBOL)  },
    { "{\"n\":0+}",        "trailing plus",             ONLY(JES_UNEXPECTED_SYMBOL)  },
    { "{\"n\":0+0}",       "plus between digits",       ONLY(JES_UNEXPECTED_SYMBOL)  },
    { "{\"n\":0-0}",       "minus between digits",      ONLY(JES_UNEXPECTED_TOKEN)  },

    /* ── non-numeric characters ───────────────────────────────────────── */
    { "{\"n\":0x}",        "hex prefix",                ONLY(JES_UNEXPECTED_SYMBOL)  },
    { "{\"n\":x0}",        "leading x",                 ONLY(JES_UNEXPECTED_SYMBOL)  },
    { "{\"n\":0ab}",       "alpha suffix",              ONLY(JES_UNEXPECTED_SYMBOL)  },

    /* ── whitespace mid-number ────────────────────────────────────────── */
    { "{\"n\":0 1}",       "space between digits",      ONLY(JES_UNEXPECTED_TOKEN) },
    { "{\"n\":- 1}",       "space after minus",         ONLY(JES_UNEXPECTED_SYMBOL)  },

    /* ── truncated (no closing brace / EOF mid-number) ────────────────── */
    { "{\"n\":0",          "truncated integer",         ONLY(JES_UNEXPECTED_EOF)  },
    { "{\"n\":1",          "truncated integer 1",       ONLY(JES_UNEXPECTED_EOF)  },
    { "{\"n\":.",          "truncated dot",             ONLY(JES_UNEXPECTED_SYMBOL) },
    { "{\"n\":0.",         "truncated after dot",       ONLY(JES_UNEXPECTED_SYMBOL)  },
    { "{\"n\":1.",         "truncated after dot 1",     ONLY(JES_UNEXPECTED_SYMBOL)  },
    { "{\"n\":0.1",        "truncated decimal",         ONLY(JES_UNEXPECTED_EOF)  },
    { "{\"n\":1.123456789e1", "truncated with exp",     ONLY(JES_UNEXPECTED_EOF)  },
    { "{\"n\":1.1e+",      "truncated after e+",        ONLY(JES_INVALID_NUMBER)  },
    { "{\"n\":-",          "truncated after minus",     ONLY(JES_UNEXPECTED_SYMBOL)  },
};

/* =========================================================================
 * Helpers
 * ========================================================================= */

/* Extract the stored number token for the key "n" from a freshly parsed doc. */
static struct jes_element *parse_and_get_value(const char *json)
{
    struct jes_context *ctx = jes_init(g_ws, sizeof(g_ws), JES_SEARCH_LINEAR);
    if (!ctx) return NULL;
    if (jes_load(ctx, json, strlen(json)) != JES_NO_ERROR) return NULL;
    struct jes_element *root = jes_get_root(ctx);
    struct jes_element *key  = jes_get_key(ctx, root, "n");
    return key ? jes_get_key_value(ctx, key) : NULL;
}

static int is_acceptable_error(jes_status actual, jes_status s1, jes_status s2)
{
    return actual == s1 || ((int)s2 != -1 && actual == s2);
}

/* =========================================================================
 * Test groups
 * ========================================================================= */

static void test_group_valid_numbers(void)
{
    const size_t N = sizeof(POSITIVE) / sizeof(POSITIVE[0]);
    char label[128];

    printf("\nGroup 1: Valid number literals (%zu cases)\n", N);

    for (size_t i = 0; i < N; i++) {
        const positive_case *tc = &POSITIVE[i];
        snprintf(label, sizeof(label), "G1-%02zu  %s", i + 1, tc->json);

        /* ── parse must succeed ─────────────────────────────────────── */
        struct jes_context *ctx = jes_init(g_ws, sizeof(g_ws), JES_SEARCH_LINEAR);
        if (!ctx) { fail(label, "ctx init"); continue; }

        if (jes_load(ctx, tc->json, strlen(tc->json)) != JES_NO_ERROR) {
            char m[64];
            snprintf(m, sizeof(m), "parse failed, status=%d", (int)jes_get_status(ctx));
            fail(label, m);
            continue;
        }

        /* ── element type must be JES_NUMBER ────────────────────────── */
        struct jes_element *root = jes_get_root(ctx);
        struct jes_element *key  = jes_get_key(ctx, root, "n");
        struct jes_element *val  = key ? jes_get_key_value(ctx, key) : NULL;

        if (!val) { fail(label, "value element not found"); continue; }

        if (val->type != JES_NUMBER) {
            char m[64];
            snprintf(m, sizeof(m), "expected JES_NUMBER, got %d", (int)val->type);
            fail(label, m);
            continue;
        }

        /* ── stored token must match expected literal ────────────────── */
        size_t exp_len = strlen(tc->expected_token);
        if (val->length != (uint16_t)exp_len ||
            memcmp(val->value, tc->expected_token, exp_len) != 0) {
            char m[128];
            snprintf(m, sizeof(m), "value mismatch: got \"%.*s\", expected \"%s\"",
                     (int)val->length, val->value, tc->expected_token);
            fail(label, m);
            continue;
        }

        /* ── render round-trip must succeed ─────────────────────────── */
        char out[512];
        if (jes_render(ctx, out, sizeof(out), true) == 0) {
            fail(label, "render failed");
            continue;
        }

        pass(label);
    }
}

static void test_group_invalid_numbers(void)
{
    const size_t N = sizeof(NEGATIVE) / sizeof(NEGATIVE[0]);
    char label[128];

    printf("\nGroup 2: Invalid number literals (%zu cases)\n", N);

    for (size_t i = 0; i < N; i++) {
        const negative_case *tc = &NEGATIVE[i];
        snprintf(label, sizeof(label), "G2-%02zu  %s", i + 1, tc->description);

        struct jes_context *ctx = jes_init(g_ws, sizeof(g_ws), JES_SEARCH_LINEAR);
        if (!ctx) { fail(label, "ctx init"); continue; }

        jes_status st = jes_load(ctx, tc->json, strlen(tc->json));

        if (st == JES_NO_ERROR) {
            fail(label, "expected parse failure, got JES_NO_ERROR");
            continue;
        }

        if (!is_acceptable_error(st, tc->s1, tc->s2)) {
            char m[128];
            snprintf(m, sizeof(m), "wrong status %d (expected %d%s%s)",
                     (int)st, (int)tc->s1,
                     (int)tc->s2 != -1 ? " or " : "",
                     (int)tc->s2 != -1 ? (char[]){(char)('0'+(int)tc->s2), '\0'} : "");
            /* Rebuild with proper status name */
            snprintf(m, sizeof(m), "status=%d, expected %d%s%d",
                     (int)st, (int)tc->s1,
                     (int)tc->s2 != -1 ? " or " : "",
                     (int)tc->s2 != -1 ? (int)tc->s2 : (int)tc->s1);
            fail(label, m);
            continue;
        }

        pass(label);
    }
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    printf("=== JES Number Tokenizer Tests ===\n");

    test_group_valid_numbers();
    test_group_invalid_numbers();

    printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
