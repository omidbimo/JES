
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
  char a[4] = {0};
  struct jes_context *dummy_ctx = (struct jes_context *)a;
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
        };

  const char json_str_negative_tests[][500] = {
        "{",
        "}",
        "{\"key\": }",
        "{\"key\": \"value\"",
        "{\"key: \"value\"",
        "{key\": \"value\"",
        "\"key\": \"value\"",
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

    if (NULL == jes_load(doc, json_str_negative_tests[idx], sizeof(json_str_negative_tests[idx])))
    {
      printf("  %s", jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    }
    else {
      printf("\nUnexpected successful parsing of invalid JSON string!");
      return -1;
    }
  }

  printf("\nTest finished successfully.");
  return 0;


}