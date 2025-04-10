
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "src\jes.h"
#include "src\jes_logger.h"

int main(void)
{

  FILE *fp;
  size_t out_size;

  char err_msg[250] = {'\0'};
  uint8_t working_buffer[0xFFFF];
  char file_data[0xFFFF];
  char output[0xFFFF];

  jes_status err;
  struct jes_element *element;
  struct jes_element *root;
  struct jes_context *doc;
  struct jes_element *it = NULL;
  struct jes_element *array = NULL;
  struct jes_element *key = NULL;
  struct jes_element *value = NULL;

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

  printf("\nJES - parsing demo.json...");
  if ((root = jes_load(doc, file_data, sizeof(file_data))) == NULL)
  {
    printf("\n    %s", jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return -1;
  }

  printf("\n    Size of JSON data: %lld bytes", strnlen(file_data, sizeof(file_data)));
  printf("\n    element count: %d", jes_get_element_count(doc));

  key = jes_get_key(doc, root, "key0.key1.key2.key3.key4");
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

  key = jes_add_key(doc, root, "Trainer");
  key = jes_add_key(doc, key, "Last Name");
  if (NULL == jes_update_key_value(doc, key, JES_VALUE_STRING, "Kiboshi")) {
    printf("\n Error: %d - %s", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return 0;
  }

  key = jes_get_key(doc, root, "Trainer");
  if (key) {
    key = jes_add_key(doc, key, "Age");
  }

  if (NULL == jes_update_key_value(doc, key, JES_VALUE_NUMBER, "46")) {
    printf("\n Error: %d - %s", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return 0;
  }

  key = jes_get_key(doc, root, "Trainer.Last Name");
  key = jes_add_key_before(doc, key, "First Name");
  if (NULL == jes_update_key_value(doc, key, JES_VALUE_STRING, "Steve")) {
    printf("\n Error: %d - %s", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return 0;
  }

  key = jes_get_key(doc, root, "Trainer.Last Name");
  key = jes_add_key_after(doc, key, "Gender");
  if (NULL == jes_update_key_value(doc, key, JES_VALUE_STRING, "Male")) {
    printf("\n Error: %d - %s", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return 0;
  }

  key = jes_get_key(doc, root, "Trainer");
  key = jes_add_key(doc, key, "Profi");
  if (NULL == jes_update_key_value(doc, key, JES_VALUE_TRUE, "true")) {
    printf("\n Error: %d - %s", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return 0;
  }

  jes_update_key_value_to_false(doc, key);

  key = jes_add_key(doc, root, "Team");
  jes_update_key_value_to_object(doc, key);
  key = jes_add_key(doc, key, "name");
  if (NULL == jes_update_key_value(doc, key, JES_VALUE_STRING, "Trantor FC")) {
    printf("\n Error: %d - %s", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return 0;
  }

  key = jes_get_key(doc, root, "Team");
  printf("\nhere1");
  key = jes_add_key(doc, key, "members");
  printf("\nhere2");
  array = jes_update_key_value_to_array(doc, key);
  if (array == NULL) {
    printf("\n Error: %d - %s,", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return 0;
  }

  if (NULL == jes_add_array_value(doc, array, -1, JES_VALUE_STRING, "Alex")) {
    printf("\n Error: %d - %s", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return 0;
  }

    if (NULL == jes_add_array_value(doc, array, 0, JES_VALUE_STRING, "Ben")) {
    printf("\n Error: %d - %s", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return 0;
  }

  if (NULL == jes_add_array_value(doc, array, 1, JES_VALUE_STRING, "Clive")) {
    printf("\n Error: %d - %s", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return 0;
  }

  if (NULL == jes_add_array_value(doc, array, -1, JES_VALUE_STRING, "Edd")) {
    printf("\n Error: %d - %s", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return 0;
  }

  if (NULL == jes_add_array_value(doc, array, -2, JES_VALUE_STRING, "Dave")) {
    printf("\n Error: %d - %s", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return 0;
  }

  if (NULL == jes_append_array_value(doc, array, JES_VALUE_STRING, "Gustav")) {
    printf("\n Error: %d - %s", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return 0;
  }

  if (NULL == jes_add_array_value(doc, array, 1000, JES_VALUE_STRING, "Henry")) {
    printf("\n Error: %d - %s", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return 0;
  }

  if (NULL == jes_add_array_value(doc, array, -1000, JES_VALUE_STRING, "Niels")) {
    printf("\n Error: %d - %s", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return 0;
  }

  jes_update_array_value(doc, array, -4, JES_VALUE_STRING, "Albert");

  /* Rendering the JSON elements into a string (not NUL-terminated) */
  printf("\nSerilize JSON tree using a compact format...\n\n");
  out_size = jes_render(doc, output, sizeof(output), true);
  if (out_size == 0) {
    printf("\n Render Error: %d - %s, size: %d", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)), out_size);
  }
  else {
    printf("\n %.*s\n\n", out_size, output);
  }

  printf("\nSerilize JSON tree with indention...\n\n");
  out_size = jes_render(doc, output, sizeof(output), false);
  if (out_size == 0) {
    printf("\n Render Error: %d - %s, size: %d", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)), out_size);
  }
  else {
    printf("\n%.*s\n\n", out_size, output);
  }

  printf("\n    element count: %d", jes_get_element_count(doc));

  return 0;

}