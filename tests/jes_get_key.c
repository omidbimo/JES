
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

  const char json_str[] = "{\"key1\": \"value1\"}";

  printf("\nTests for jes_get_key()\n");

  doc = jes_init(work_buffer, sizeof(work_buffer));
  if (!doc) {
    printf("\n Context initiation failed!");
    return -1;
  }

  printf("\nJES - parsing...");
  if (NULL == jes_load(doc, json_str, sizeof(json_str)))
  {
    printf("\n    %s", jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return -1;
  }

  printf("\n    Size of JSON data: %lld bytes", strnlen(json_str, sizeof(json_str)));
  printf("\n    node count: %d", jes_get_element_count(doc));

  key = jes_get_key(dummy_ctx, NULL, "key");
  if (key) {
    printf("\nError! Unexpected KEY value when an un-initialized context is used.");
    return -1;
  }

  key = jes_get_key(NULL, NULL, "key");
  if (key) {
    printf("\nError! Unexpected KEY value when a NULL context is used.");
    return -1;
  }

  key = jes_get_key(doc, NULL, NULL);
  if (key) {
    printf("\nError! Unexpected KEY value when a NULL context is used.");
    return -1;
  }
  if (jes_get_status(doc) != JES_INVALID_PARAMETER) {
    printf("\nError! Unexpected status code: %d when expected JES_INVALID_PARAMETER.", jes_get_status(doc));
    return -1;
  }

  key = jes_get_key(doc, &dummy, "");
  if (key) {
    printf("\nError! Unexpected KEY value when an invalid KEY element is used.");
    return -1;
  }
  if (jes_get_status(doc) != JES_INVALID_PARAMETER) {
    printf("\nError! Unexpected status code: %d when expected JES_INVALID_PARAMETER.", jes_get_status(doc));
    return -1;
  }

  key = jes_get_key(doc, &dummy, "");
  if (key) {
    printf("\nError! Unexpected KEY value when an invalid KEY element is used.");
    return -1;
  }
  if (jes_get_status(doc) != JES_INVALID_PARAMETER) {
    printf("\nError! Unexpected status code: %d when expected JES_INVALID_PARAMETER.", jes_get_status(doc));
    return -1;
  }

  key = jes_get_key(doc, jes_get_root(doc), "key1");
  value = jes_get_key_value(doc, key);
  key = jes_get_key(doc, value, "");
  if (key) {
    printf("\nError! Unexpected KEY element when a parent_key of non KEY type is used.");
    printf("\n    %s", jes_stringify_element(key, err_msg, sizeof(err_msg)));
    return -1;
  }
  if (jes_get_status(doc) != JES_INVALID_PARAMETER) {
    printf("\nError! Unexpected status code: %s when expected JES_INVALID_PARAMETER.", jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return -1;
  }

  key = jes_get_key(doc, jes_get_root(doc), "value");
  if (key) {
    printf("\nError! Unexpected KEY element when a non-existing keyword is used.");
    return -1;
  }
  if (jes_get_status(doc) != JES_ELEMENT_NOT_FOUND) {
    printf("\nError! Unexpected status code: %s when expected JES_ELEMENT_NOT_FOUND.", jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return -1;
  }

  key = jes_get_key(doc, jes_get_root(doc), "");
  if (key) {
    printf("\nError! Unexpected KEY element when an empty keyword is used.");
    return -1;
  }
  if (jes_get_status(doc) != JES_ELEMENT_NOT_FOUND) {
    printf("\nError! Unexpected status code: %s when expected JES_ELEMENT_NOT_FOUND.", jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return -1;
  }

  key = jes_get_key(doc, jes_get_root(doc), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  if (key) {
    printf("\nError! Unexpected KEY element when an invalid keyword is used.");
    return -1;
  }
  if (jes_get_status(doc) != JES_ELEMENT_NOT_FOUND) {
    printf("\nError! Unexpected status code: %s when expected JES_ELEMENT_NOT_FOUND.", jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return -1;
  }

  key = jes_get_key(doc, jes_get_root(doc), "............................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................");
  if (key) {
    printf("\nError! Unexpected KEY element when an invalid keyword is used.");
    return -1;
  }
  if (jes_get_status(doc) != JES_ELEMENT_NOT_FOUND) {
    printf("\nError! Unexpected status code: %d when expected JES_ELEMENT_NOT_FOUND.", jes_get_status(doc));
    return -1;
  }

  key = jes_get_key(doc, jes_get_root(doc), "\n\t\r");
  if (key) {
    printf("\nError! Unexpected KEY element when an invalid keyword is used.");
    return -1;
  }
  if (jes_get_status(doc) != JES_ELEMENT_NOT_FOUND) {
    printf("\nError! Unexpected status code: %d when expected JES_ELEMENT_NOT_FOUND.", jes_get_status(doc));
    return -1;
  }

  key = jes_get_key(doc, jes_get_root(doc), "key1");
  if (key == NULL) {
    printf("\n Error: %d - %s", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return -1;
  }

  if (NULL == jes_update_key_value_to_object(doc, key)) {
    printf("\n    %s", jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return -1;
  }


  for (idx = 0; idx < 10; idx++) {
    snprintf(keywords[idx], sizeof(keywords[idx]), "key%d", idx+2);

    key = jes_add_key(doc, key, keywords[idx]);
    if (key == NULL) {
      printf("\n    %s", jes_stringify_status(doc, err_msg, sizeof(err_msg)));
      //printf("\n Error: %d - %s", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)));
      return -1;
    }
  }

  jes_update_key_value_to_null(doc, key);
  /* Rendering the JSON elements into a string (not NUL-terminated) */
  printf("\nSerilize JSON tree using a compact format...");
  out_size = jes_render(doc, output, sizeof(output), true);
  if (out_size == 0) {
    printf("\n    %s", jes_stringify_status(doc, err_msg, sizeof(err_msg)));
  }
  else {
    printf("\n%.*s", out_size, output);
  }
  printf("\nTest finished successfully.");
  return 0;


}