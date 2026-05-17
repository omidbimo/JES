/**
 * Demonstrates loading a JSON file and rendering it in both compact
 * and formatted form using the JES tree-based parser/serializer.
 *
 * Build using GCC
 * gcc -o load_render load_render.c ../src/*.c -I ../src
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/jes.h"
#include "../src/jes_logger.h"

#define MAX_JSON_SIZE  (7 * 1024)
#define MAX_NODE_COUNT 1000

static char    file_data[MAX_JSON_SIZE];
static uint8_t workspace[JES_REQUIRED_SIZE(MAX_NODE_COUNT)];
static char    output[MAX_JSON_SIZE];

int main(void)
{
  size_t json_length;
  size_t out_size;

  /* Read JSON file */
  FILE *fp = fopen("demo.json", "rb");
  if (fp == NULL) {
    fprintf(stderr, "Failed to open demo.json\n");
    return -1;
  }

  json_length = fread(file_data, sizeof(char), sizeof(file_data), fp);
  if (ferror(fp) != 0) {
    fprintf(stderr, "Failed to read demo.json\n");
    fclose(fp);
    return -1;
  }
  fclose(fp);

  /* Initialize context */
  struct jes_context *ctx = jes_init(workspace, sizeof(workspace), JES_SEARCH_LINEAR);
  if (!ctx) {
    fprintf(stderr, "Failed to initialize JES context\n");
    return -1;
  }

  /* Parse JSON */
  if (jes_load(ctx, file_data, json_length) != JES_NO_ERROR) {
    char err_msg[128] = {'\0'};
    fprintf(stderr, "Failed to parse JSON: %s\n",
            jes_stringify_status(ctx, err_msg, sizeof(err_msg)));
    return -1;
  }

  printf("There are totally %zu elements in the JSON file.\n\n", jes_get_element_count(ctx));

  /* Render compact */
  out_size = jes_render(ctx, output, sizeof(output), true);
  if (out_size == 0) {
    fprintf(stderr, "Compact render failed: %d\n", jes_get_status(ctx));
    return -1;
  }
  printf("=== Compact (%zu bytes) ===\n%s\n\n", out_size - 1, output);

  /* Render formatted */
  out_size = jes_render(ctx, output, sizeof(output), false);
  if (out_size == 0) {
    fprintf(stderr, "Formatted render failed: %d\n", jes_get_status(ctx));
    return -1;
  }

  printf("=== Formatted (%zu bytes) ===\n%s\n", out_size - 1, output);

  return 0;
}
