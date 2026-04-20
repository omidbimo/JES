#include "src\jes.h"
#include "src\jes_logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>


char file_data[7*1024];
uint8_t working_buffer[JES_REQUIRED_SIZE(1000)];
char output[7*1024];

int main(void)
{

  FILE *fp;
  size_t out_size;

  char err_msg[250] = {'\0'};

  jes_status err;

  struct jes_element *root;
  struct jes_context *ctx;

  printf("\n Context size: %d bytes, pointer size: %zu, node_size: %zu bytes, required size@1000nodes: %zu bytes", jes_context_size(), sizeof(void*), jes_node_size(), JES_REQUIRED_SIZE(1000));

  fp = fopen("demo.json", "rb");
  if (fp == NULL) {
    return -1;
  }

  fread(file_data, sizeof(char), sizeof(file_data), fp);
  if ( ferror(fp) != 0 ) {
    fclose(fp);
    return -1;
  }
  fclose(fp);

  ctx = jes_init(working_buffer, sizeof(working_buffer), JES_SEARCH_LINEAR);
  if (!ctx) {
    printf("\n Context initiation failed!");
    return -1;
  }

  /* Parse JSON */
  if (jes_load(ctx, file_data, strlen(file_data)) != JES_NO_ERROR) {
    fprintf(stderr, "Failed to parse JSON: %d\n", jes_get_status(ctx));
    return -1;
  }

  printf("\nElement count: %d", jes_get_element_count(ctx));

  out_size = jes_render(ctx, output, sizeof(output), true);
  if (out_size == 0) {
    printf("\nRender Error: %d - %s, size: %d", jes_get_status(ctx), jes_stringify_status(ctx, err_msg, sizeof(err_msg)), out_size);
  }
  else {
    printf("\nCompact JSON string size: %d", strlen(output));
    printf("\n\n%.*s\n\n", out_size, output);
  }

  out_size = jes_render(ctx, output, sizeof(output), false);
  if (out_size == 0) {
    printf("\nRender Error: %d - %s, size: %d", jes_get_status(ctx), jes_stringify_status(ctx, err_msg, sizeof(err_msg)), out_size);
  }
  else {
    printf("\n Formatted JSON string size: %d", out_size);
    printf("\n\n%.*s\n\n", out_size, output);
  }

  struct jes_status_block sb = jes_get_status_block(ctx);
  printf("\n status: %i", sb.status);
  printf("\n token type: %i", sb.token_type);
  printf("\n element type: %i", sb.element_type);
  printf("\n line: %u", sb.cursor_line);
  printf("\n pos: %u", sb.cursor_pos);

  return 0;

}