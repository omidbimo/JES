
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "src\jes.h"
#include "src\jes_logger.h"

#define POOL_SIZE 0xFFFFF

int main(void)
{
  size_t out_size;

  char err_msg[250] = {'\0'};
  uint8_t working_buffer[POOL_SIZE];
  char output[0xFFFF];
  char str_storage_buffer[0xFFFF];
  char *str_storage_ptr = str_storage_buffer;
  char *str_storage_end_ptr = str_storage_buffer + sizeof(str_storage_buffer);
  uint32_t str_int_len;
  jes_status err;

  struct jes_context *doc;
  struct jes_element *array = NULL;
  struct jes_element *key = NULL;

  uint8_t binary_data[2000] = { 0 };
  uint32_t i, j, h;

  for (i = 0; i < sizeof(binary_data); i++) {
    binary_data[i] = 0xFF - (i%10);
  }

  doc = jes_init(working_buffer, sizeof(working_buffer));
  if (!doc) {
    printf("\n Context initiation failed!");
    return -1;
  }

  if (jes_load(doc, "{}", sizeof("\"{}\"")) == NULL)
  {
    printf("\n    %s", jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return -1;
  }

  out_size = jes_render(doc, output, sizeof(output), true);
  if (out_size == 0) {
    printf("\nRender Error: %d - %s, size: %d", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)), out_size);
  }
  else {
    printf("\n%.*s", out_size, output);
  }
  printf("     JSON Element count: %d\n", jes_get_element_count(doc));

  key = jes_add_key(doc, key, "BINARY");
  if (key == NULL) {
    printf("\n Error: %d - %s, size: %d", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)), out_size);
    return 0;
  }

  array = jes_update_key_value_to_array(doc, key);
  if (key == NULL) {
    printf("\n Error: %d - %s, size: %d", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)), out_size);
    return 0;
  }

  for (i = 0; i < sizeof(binary_data); i++) {
    str_int_len = snprintf(str_storage_ptr, str_storage_end_ptr - str_storage_ptr,
                           "%d", binary_data[i]);
    if ((str_int_len > 0) && (str_int_len < (str_storage_end_ptr - str_storage_ptr))) {
      if (jes_append_array_value(doc, array, JES_VALUE_NUMBER, str_storage_ptr) == NULL) {
        printf("\n Error! Add array element failed. %s, size: %d", jes_stringify_status(doc, err_msg, sizeof(err_msg)), out_size);
        return -1;
      }
      str_storage_ptr += str_int_len + 1;
    }
    else {
      printf("Error! Converting number to string failed. %d", str_int_len);
      return -1;
    }
  }

  printf("\nRequired buffer size for %d string numbers: %d bytes", sizeof(binary_data), (uint64_t)(str_storage_ptr - str_storage_buffer));

  printf("\nJSON Element count: %d", jes_get_element_count(doc));

  /* Rendering the JSON elements into a string (not NUL-terminated) */
  printf("\n\nSerilize JSON tree using a compact format...\n");
  out_size = jes_render(doc, output, sizeof(output), true);
  if (out_size == 0) {
    printf("\nRender Error: %d - %s, size: %d", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)), out_size);
  }
  else {
    printf("\n%.*s\n\n", out_size, output);
  }

  return 0;
}