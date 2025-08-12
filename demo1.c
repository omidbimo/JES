#include "src\jes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t buffer[32 * 1024]; /* 32KB buffer */

int main() {
  /* Initialize JES context */
  struct jes_context *ctx = jes_init(buffer, sizeof(buffer));
  if (!ctx) {
    fprintf(stderr, "Failed to initialize JES context\n");
    return 1;
  }

  /* JSON data to parse */
  const char *json = "{\"name\":\"John\",\"age\":30,\"city\":\"New York\"}";

  /* Parse JSON */
  struct jes_element *root = jes_load(ctx, json, strlen(json));
  if (!root) {
    fprintf(stderr, "Failed to parse JSON: %d\n", jes_get_status(ctx));
    return 1;
  }

  /* Calculate required buffer for rendering */
  size_t required_size = jes_evaluate(ctx, false); /* Formatted output */

  /* Allocate buffer for rendered JSON */
  char *output = malloc(required_size);
  if (!output) {
    fprintf(stderr, "Failed to allocate output buffer\n");
    return 1;
  }

  /* Render JSON */
  size_t rendered_size = jes_render(ctx, output, required_size, false);

  if (rendered_size == 0) {
    fprintf(stderr, "Failed to render JSON: %d\n", jes_get_status(ctx));
    free(output);
    return 1;
  }

  printf("\nFormatted JSON:\n%s\n", output);

  /* Clean up */
  free(output);
  return 0;
}