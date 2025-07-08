
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "..\src\jes.h"
#include "..\src\jes_logger.h"


#define BUFFER_SIZE 0xFFF

int main(void)
{
  uint32_t idx;

  struct jes_context *doc = NULL;
  size_t out_size;

  char err_msg[250] = {'\0'};
  uint8_t work_buffer[BUFFER_SIZE];
  char output[0xFFFF];
  char keywords[10][10];
  struct jes_element *it = NULL;
  struct jes_element *array = NULL;
  struct jes_element *key = NULL;
  struct jes_element *value = NULL;
  struct jes_element dummy;

  const char json_str_positive_tests[][500] = {
        "{\"key\": \"value\"}",
        "{\"key\": [\"value\", {}]}",
        "     {    \"key\"     :   \"value\"    }   ",
        "{\"key\": {\"key\": [\"value1\", [{\"key\": \"value\"}, \"value\", [{}]]]}}",
        "{\"\": \"value\"}",
        "{\"\": \"\"}",
        "[{\"\": \"\"}]",
        "[1, null, true, false, \"string\", [], {}]",
        "null",         /* Standalone null -Not Wrapped in an Object- */
        "false",        /* Standalone false -Not Wrapped in an Object- */
        "true",         /* Standalone true -Not Wrapped in an Object- */
        "1",            /* Standalone number -Not Wrapped in an Object- */
        "0",            /* Standalone number -Not Wrapped in an Object- */
        "1.1",          /* Standalone number -Not Wrapped in an Object- */
        "-1.0",         /* Standalone number -Not Wrapped in an Object- */
        "0.6",          /* Standalone number -Not Wrapped in an Object- */
        "-0.6",         /* Standalone number -Not Wrapped in an Object- */
        "435000000000.1234567890", /* Standalone number -Not Wrapped in an Object- */
        "4.35e-10",     /* Standalone number -Not Wrapped in an Object- */
        "\"string\"",   /* Standalone string -Not Wrapped in an Object- */
        "\"unicode: \\u4f60\\u597d\""
        };

  const char json_str_negative_tests[][500] = {
        "{",                                                  /* Single Opening Brace */
        "}",                                                  /* Single Closing Brace */
        "{1}",                                                /*  */
        "{\"key\": }",                                        /* Empty Value */
        "{\"key\": \"val",                                    /* EOF in the middle of string */
        "{\"key\": \"value\"",
        "{\"key: \"value\"",                                  /* Non-terminated key String */
        "{\"key\": \"value}",                                 /* Non-terminated value String */
        "{key\": \"value\"",
        "\"key\": \"value\"",
        "{key: \"value\"}",                                   /* Missing Quotes Around Key */
        "{\"key\": \"value\",}",                              /* Trailing comma */

#if 0
        "{\"key2\": \"val"
        "ue\"}",                                              /* Unescaped Control Character */
#endif
        "{'key1': \"value\"}",                                /* Single Quotes Instead of Double Quotes */
        "{\"key\" \"value\"}",                                /* Missing Colon */
        "{\"key\": {}",                                       /* Missing closing Brace */
        "{\"key\": \"value\"}}",                              /* Extra closing Brace */
        "{\"key\": [\"value\",]}",                            /* Array with trailing Comma */
        "{\"key1\": \"value1\" \"key2\": \"value2\"}",        /* Missing Comma Between Fields */
        "{\"key\": value}",                                   /* Unexpected Token */
        "{\"key1\": \"value1\", \"key1\": \"value2\"}",       /* Duplicate Keys */
        "{\"key\": True}",                                    /* Incorrect Boolean Case */
        "{\"key\": False}",                                   /* Incorrect Boolean Case */
        "{\"key\": Null}",                                    /* Incorrect null Case */
        "{\"key\": []]}",                                     /* Unexpected closing bracket */
        "{\"key\": [[]}",                                     /* Missing closing Bracket */
        "{\"key\": [{,}]}",                                   /* Unexpected Comma in object */
        "{\"key\": [,]}",                                     /* Unexpected Comma in array */
        "{\"key\": ][}",                                      /* Wrong order of Brackets */
        "{\"key\": [[[[[[[[[[]]]]]]]]]]]]]]]]]]]]]]]]]]]]]}", /* Extra closing Brackets */
        "{\"key\": ,[]}",                                     /* Unexpected Comma before value */
        "{\"key\": [][]}",                                    /* Duplicate Array as value */
        "01",                                                 /* Standalone invalid number -Not Wrapped in an Object- */

        };

  printf("\nRunning parser positive tests...");
  for (idx = 0; idx < (sizeof(json_str_positive_tests)/sizeof(json_str_positive_tests[0])); idx++) {
    printf("\nParsing: %s...", json_str_positive_tests[idx]);
    doc = jes_init(work_buffer, sizeof(work_buffer));
    if (!doc) {
      printf("\n Context initiation failed!");
      return -1;
    }

    if (NULL == jes_load(doc, json_str_positive_tests[idx], sizeof(json_str_positive_tests[idx])))
    {
      printf("\n    %s", jes_stringify_status(doc, err_msg, sizeof(err_msg)));
      return -1;
    }
    out_size = jes_render(doc, output, sizeof(output), true);
    if (out_size == 0) {
      printf("\n    %s", jes_stringify_status(doc, err_msg, sizeof(err_msg)));
      return -1;
    }
  }

  printf("\nRunning parser negative tests...");
  for (idx = 0; idx < (sizeof(json_str_negative_tests)/sizeof(json_str_negative_tests[0])); idx++) {
    printf("\nParsing: %s...", json_str_negative_tests[idx]);
    doc = jes_init(work_buffer, sizeof(work_buffer));
    if (!doc) {
      printf("\n Context initiation failed!");
      return -1;
    }

    if (NULL == jes_load(doc, json_str_negative_tests[idx], sizeof(json_str_negative_tests[idx]))) {
      printf("  %s", jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    }
    else {
      printf("\nUnexpected successful parsing of invalid JSON string!");
      return -1;
    }
  }

  /* Testing some specific cases */
    doc = jes_init(work_buffer, sizeof(work_buffer));
    if (!doc) {
      printf("\n Context initiation failed!");
      return -1;
    }
    for (idx = 2; idx <= sizeof("{\"key\":\"value\"}"); idx++) {
      printf("\n Parsing: %.*s ", sizeof("{\"key\":\"value\"}")-idx, "{\"key\":\"value\"}");
      if (NULL == jes_load(doc, "{\"key\":\"value\"}", sizeof("{\"key\":\"value\"}")-idx)) {
        printf("  %s", jes_stringify_status(doc, err_msg, sizeof(err_msg)));
      }
      else {
        printf("\nUnexpected successful parsing of invalid JSON string!");
        return -1;
      }
    }

    printf("\n Passing NULL as the json string parameter");
    if (NULL == jes_load(doc, NULL, sizeof("{\"key\":\"value\"}"))) {
      printf("  %s", jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    }
    else {
      printf("\nUnexpected successful parsing of invalid JSON string!");
      return -1;
    }

  printf("\nTest finished successfully.");
  return 0;


}