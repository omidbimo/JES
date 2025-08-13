#include "src\jes.h"
#include "src\jes_logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>


char file_data[7*1024];
uint8_t working_buffer[20*1024];
char output[7*1024];

int main(void)
{

  FILE *fp;
  size_t out_size;

  char err_msg[250] = {'\0'};

  jes_status err;

  struct jes_element *root;
  struct jes_context *doc;


  fp = fopen("demo.json", "rb");

  if (fp != NULL) {
    fread(file_data, sizeof(char), sizeof(file_data), fp);
    if ( ferror(fp) != 0 ) {
      fclose(fp);
      return -1;
    }
    fclose(fp);
  }

  doc = jes_init(working_buffer, sizeof(working_buffer));
  if (!doc) {
    printf("\n Context initiation failed!");
    return -1;
  }

  if ((root = jes_load(doc, file_data, strlen(file_data))) == NULL)
  {
    return -1;
  }

  printf("\nSize of JSON data: %lld bytes", strnlen(file_data, sizeof(file_data)));
  printf("\nElement count: %d", jes_get_element_count(doc));

  out_size = jes_render(doc, output, sizeof(output), true);
  if (out_size == 0) {
    printf("\nRender Error: %d - %s, size: %d", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)), out_size);
  }
  else {
    printf("\nCompact JSON string size: %d", strlen(output));
    printf("\n\n%.*s\n\n", out_size, output);
  }

  out_size = jes_render(doc, output, sizeof(output), false);
  if (out_size == 0) {
    printf("\nRender Error: %d - %s, size: %d", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)), out_size);
  }
  else {
    printf("\n Formatted JSON string size: %d", out_size);
    printf("\n\n%.*s\n\n", out_size, output);
  }

  struct jes_status_block sb = jes_get_status_block(doc);
  printf("\n status: %i", sb.status);
  printf("\n token type: %i", sb.token_type);
  printf("\n element type: %i", sb.element_type);
  printf("\n line: %u", sb.cursor_line);
  printf("\n pos: %u", sb.cursor_pos);


  return 0;

}