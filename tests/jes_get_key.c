
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "..\\src\\jes.h"

#define BUFFER_SIZE 0xFFF

int main(void)
{
  uint32_t idx;
  struct jes_context *doc = NULL;
  struct jes_context *dummy_ctx;
  size_t out_size;
  jes_status err;
  char err_msg[250] = {'\0'};
  uint8_t work_buffer[BUFFER_SIZE];
  char output[0xFFFF];

  struct jes_element *it = NULL;
  struct jes_element *array = NULL;
  struct jes_element *key = NULL;
  struct jes_element *value = NULL;
  struct jes_element dummy;

  const char json_str[] = "{\"key\": \"value\"}";

  doc = jes_init(work_buffer, sizeof(work_buffer));
  if (!doc) {
    printf("\n Context initiation failed!");
    return -1;
  }

  printf("\nJES - parsing...");
  if (0 != (err = jes_load(doc, json_str, sizeof(json_str))))
  {
    printf("\n    %s", jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return -1;
  }

  printf("\n    Size of JSON data: %lld bytes", strnlen(json_str, sizeof(json_str)));
  printf("\n    node count: %d", jes_get_node_count(doc));

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

  key = jes_get_key(doc, jes_get_root(doc), "");
  if (key) {
    printf("\nError! Unexpected KEY element when a parent_key of non KEY type is used.");
    printf("\n    %s", jes_stringify_element(key, err_msg, sizeof(err_msg)));
    return -1;
  }
  if (jes_get_status(doc) != JES_INVALID_PARAMETER) {
    printf("\nError! Unexpected status code: %d when expected JES_INVALID_PARAMETER.", jes_get_status(doc));
    return -1;
  }

  key = jes_get_key(doc, NULL, "value");
  if (key) {
    printf("\nError! Unexpected KEY element when a non-existing keyword is used.");
    return -1;
  }
  if (jes_get_status(doc) != JES_ELEMENT_NOT_FOUND) {
    printf("\nError! Unexpected status code: %d when expected JES_ELEMENT_NOT_FOUND.", jes_get_status(doc));
    return -1;
  }

  key = jes_get_key(doc, NULL, "");
  if (key) {
    printf("\nError! Unexpected KEY element when an empty keyword is used.");
    return -1;
  }
  if (jes_get_status(doc) != JES_ELEMENT_NOT_FOUND) {
    printf("\nError! Unexpected status code: %d when expected JES_ELEMENT_NOT_FOUND.", jes_get_status(doc));
    return -1;
  }

  key = jes_get_key(doc, NULL, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  if (key) {
    printf("\nError! Unexpected KEY element when an invalid keyword is used.");
    return -1;
  }
  if (jes_get_status(doc) != JES_ELEMENT_NOT_FOUND) {
    printf("\nError! Unexpected status code: %d when expected JES_ELEMENT_NOT_FOUND.", jes_get_status(doc));
    return -1;
  }

  key = jes_get_key(doc, NULL, "............................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................");
  if (key) {
    printf("\nError! Unexpected KEY element when an invalid keyword is used.");
    return -1;
  }
  if (jes_get_status(doc) != JES_ELEMENT_NOT_FOUND) {
    printf("\nError! Unexpected status code: %d when expected JES_ELEMENT_NOT_FOUND.", jes_get_status(doc));
    return -1;
  }

  key = jes_get_key(doc, NULL, "\n\t\r");
  if (key) {
    printf("\nError! Unexpected KEY element when an invalid keyword is used.");
    return -1;
  }
  if (jes_get_status(doc) != JES_ELEMENT_NOT_FOUND) {
    printf("\nError! Unexpected status code: %d when expected JES_ELEMENT_NOT_FOUND.", jes_get_status(doc));
    return -1;
  }

  key = jes_get_key(doc, NULL, "key");
  if (key == NULL) {
    printf("\n Error: %d - %s", jes_get_status(doc), jes_stringify_status(doc, err_msg, sizeof(err_msg)));
    return -1;
  }


  printf("\nTest Finished");
  return 0;


}