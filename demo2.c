#include "src\jes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t buffer[32 * 1024]; /* 32KB buffer */

int main() {

  struct jes_context *ctx = jes_init(buffer, sizeof(buffer));

  /* JSON data to parse */
  const char *json = "{\"person\":{\"name\":\"John\",\"age\":30}}";

  /* Parse JSON */
  if (jes_load(ctx, json, strlen(json)) != JES_NO_ERROR) {
    fprintf(stderr, "Failed to parse JSON: %d\n", jes_get_status(ctx));
    return 1;
  }

  /* Find the person object */
  struct jes_element *person = jes_get_key(ctx, jes_get_root(ctx), "person");

  /* Add email key */
  const char *email_key = "email";
  const char *email_value = "john@example.com";
  struct jes_element *email = jes_add_key(ctx, person, email_key, strlen(email_key));
  jes_update_key_value(ctx, email, JES_STRING, email_value, strlen(email_value));

  /* Update age */
  struct jes_element *age = jes_get_key(ctx, person, "age");
  const char *new_age = "31";
  jes_update_key_value(ctx, age, JES_NUMBER, new_age, strlen(new_age));

  /* Add an address object */
  const char *addr_key = "address";
  struct jes_element *addr = jes_add_key(ctx, person, addr_key, strlen(addr_key));
  jes_update_key_value_to_object(ctx, addr);

  /* Add fields to address */
  const char *city_key = "city";
  const char *city_value = "New York";
  struct jes_element *city = jes_add_key(ctx, addr, city_key, strlen(city_key));
  jes_update_key_value(ctx, city, JES_STRING, city_value, strlen(city_value));

  const char *zip_key = "zip";
  const char *zip_value = "10001";
  struct jes_element *zip = jes_add_key(ctx, addr, zip_key, strlen(zip_key));
  jes_update_key_value(ctx, zip, JES_STRING, zip_value, strlen(zip_value));

  /* Add hobbies array */
  const char *hobbies_key = "hobbies";
  struct jes_element *hobbies = jes_add_key(ctx, person, hobbies_key, strlen(hobbies_key));
  jes_update_key_value_to_array(ctx, hobbies);

  /* Add items to array */
  const char *hobby1 = "reading";
  const char *hobby2 = "running";
  const char *hobby3 = "coding";
  jes_append_array_value(ctx, jes_get_key_value(ctx, hobbies), JES_STRING, hobby1, strlen(hobby1));
  jes_append_array_value(ctx, jes_get_key_value(ctx, hobbies), JES_STRING, hobby2, strlen(hobby2));
  jes_append_array_value(ctx, jes_get_key_value(ctx, hobbies), JES_STRING, hobby3, strlen(hobby3));

  /* Render modified JSON */
  size_t required_size = jes_evaluate(ctx, false);
  char *output = malloc(required_size);
  jes_render(ctx, output, required_size, false);

  printf("\nModified JSON:\n%s\n", output);

  /* Clean up */
  free(output);

  return 0;
}