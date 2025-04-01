
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "src\\jes.h"

#define POOL_SIZE 0xFFFF



#define JES_ARRAY_LEN(arr) (sizeof(arr)/sizeof(arr[0]))
int main(void)
{
  struct jes_context *doc;
  FILE *fp;
  size_t out_size;
  jes_status err;
  struct jes_element *element;
  struct jes_element *root;
  char err_msg[250] = {'\0'};
  uint8_t working_buffer[POOL_SIZE];
  char file_data[0xFFFF];
  char output[0xFFFF];

  fp = fopen("demo.json", "rb");

  if (fp != NULL) {
    fread(file_data, sizeof(char), sizeof(file_data), fp);
    if ( ferror( fp ) != 0 ) {
      fclose(fp);
      return 0;
    }
    fclose(fp);
  }

  doc = jes_init(working_buffer, sizeof(working_buffer));
  if (!doc) {
    printf("\n Context initiation failed!");
    return -1;
  }

  printf("\nJES - parsing demo.json...");
  if (0 != (err = jes_load(doc, file_data, sizeof(file_data))))
  {
    printf("\n    %s", jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return -1;
  }

  printf("\n    Size of JSON data: %lld bytes", strnlen(file_data, sizeof(file_data)));
  printf("\n    node count: %d", jes_get_node_count(doc));

  struct jes_element *it = NULL;
  struct jes_element *array = NULL;
  struct jes_element *key = NULL;
  struct jes_element *value = NULL;

  key = jes_get_key(doc, NULL, "key0.key1.key2.key3.key4");
  if (key) { printf("\nFound Key: name: %.*s", key->length, key->value);}

  key = jes_get_key(doc, key, "key5.key6");
  if (key) { printf("\nFound Key: name: %.*s", key->length, key->value);}

  array = jes_get_key_value(doc, key);
  if (array && array->type == JES_ARRAY) {
      printf("\nIterating over %.*s array elements", key->length, key->value);
      JES_ARRAY_FOR_EACH(doc, array, it) {
        printf("\n    %s", jes_stringify_element(it, err_msg, sizeof(err_msg)));
      }
  }

  key = jes_add_key(doc, NULL, "Trainer");
  key = jes_add_key(doc, key, "Last Name");
  if (0 != jes_update_key_value(doc, key, JES_VALUE_STRING, "Kiboshi")) {
    printf("\n Error: %d - %s, size: %d", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)), out_size);
    return 0;
  }

  key = jes_get_key(doc, NULL, "Trainer");
  if (key) {
    key = jes_add_key(doc, key, "Age");
  }

  if (0 != jes_update_key_value(doc, key, JES_VALUE_NUMBER, "46")) {
    printf("\n Error: %d - %s, size: %d", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)), out_size);
    return 0;
  }

  key = jes_get_key(doc, NULL, "Trainer.Last Name");
  key = jes_add_key_before(doc, key, "First Name");
  if (0 != jes_update_key_value(doc, key, JES_VALUE_STRING, "Steve")) {
    printf("\n Error: %d - %s, size: %d", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)), out_size);
    return 0;
  }

  key = jes_get_key(doc, NULL, "Trainer.Last Name");
  key = jes_add_key_after(doc, key, "Gender");
  if (0 != jes_update_key_value(doc, key, JES_VALUE_STRING, "Male")) {
    printf("\n Error: %d - %s, size: %d", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)), out_size);
    return 0;
  }

  key = jes_get_key(doc, NULL, "Trainer");
  key = jes_add_key(doc, key, "Profi");
  if (0 != jes_update_key_value(doc, key, JES_VALUE_TRUE, "true")) {
    printf("\n Error: %d - %s, size: %d", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)), out_size);
    return 0;
  }

  jes_update_key_value_to_false(doc, key);

  key = jes_add_key(doc, NULL, "Team");
  jes_update_key_value_to_object(doc, key);
  key = jes_add_key(doc, key, "name");
  if (0 != jes_update_key_value(doc, key, JES_VALUE_STRING, "Trantor FC")) {
    printf("\n Error: %d - %s, size: %d", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)), out_size);
    return 0;
  }

  key = jes_get_key(doc, NULL, "Team");
  key = jes_add_key(doc, key, "members");
  jes_update_key_value_to_array(doc, key);
  array = jes_get_key_value(doc, key);
  printf("\n%s", jes_stringify_element(array, err_msg, sizeof(err_msg)));
  if (0 != jes_add_array_value(doc, array, -1, JES_VALUE_STRING, "ALEX")){
    printf("\n Error: %d - %s, size: %d", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)), out_size);
    return 0;
  }

  jes_add_array_value(doc, array, -1, JES_VALUE_STRING, "BEN");
  jes_add_array_value(doc, array, -1, JES_VALUE_STRING, "Clive");
  jes_add_array_value(doc, array, -1, JES_VALUE_STRING, "Edd");
  jes_add_array_value(doc, array, -2, JES_VALUE_STRING, "Dave");
  jes_update_array_value(doc, array, -2, JES_VALUE_STRING, "David");

  /* Rendering the JSON elements into a string (not NUL-terminated) */
  printf("\nSerilize JSON tree using a compact format...");
  out_size = jes_render(doc, output, sizeof(output), true);
  if (out_size == 0) {
    printf("\n Render Error: %d - %s, size: %d", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)), out_size);
  }
  else {
    printf("\n%.*s", out_size, output);
  }

#if 0
  printf("\nSerilize JSON tree with indention...");
  out_size = jes_render(doc, output, sizeof(output), false);
  if (out_size == 0) {
    printf("\n Render Error: %d - %s, size: %d", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)), out_size);
  }
  else {
    printf("\n%.*s", out_size, output);
  }
#endif
  return 0;

}