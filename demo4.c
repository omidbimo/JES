/* Example4.c */
#include "src\jes.h"
#include "src\jes_logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t buffer[64 * 1024]; /* 64KB buffer */

int main() {
    /* Initialize JES context */
    struct jes_context *ctx = jes_init(buffer, sizeof(buffer));
    if (!ctx) {
        fprintf(stderr, "Failed to initialize JES context\n");
        return 1;
    }

    /* Get sizing information */
    printf("Context size: %zu bytes\n", jes_get_context_size());
    printf("Node size: %zu bytes\n", jes_get_node_size());
    printf("Element capacity: %zu\n", jes_get_element_capacity(ctx));

    /* Parse some JSON */
    const char *json = "{"
        "\"users\": ["
            "{\"id\": 1, \"name\": \"Alice\", \"active\": true},"
            "{\"id\": 2, \"name\": \"Bob\", \"active\": false},"
            "{\"id\": 3, \"name\": \"Charlie\", \"active\": null}"
        "],"
        "\"metadata\": {"
            "\"version\": \"1.0\","
            "\"created\": \"1990-01-01\""
        "}"
    "}";

  /* Parse JSON */
  if (jes_load(ctx, json, strlen(json)) != JES_NO_ERROR) {
    fprintf(stderr, "Failed to parse JSON: %d\n", jes_get_status(ctx));
    return 1;
  }

    /* Print element statistics */
    struct jes_stat stats = jes_get_stat(ctx);
    printf("\nElement Statistics:\n");
    printf("Total elements: %zu\n", jes_get_element_count(ctx));
    printf("Objects: %zu\n", stats.objects);
    printf("Keys: %zu\n", stats.keys);
    printf("Arrays: %zu\n", stats.arrays);
    printf("Values: %zu\n", stats.values);

    /* Print memory usage */
    jes_print_workspace_stat(jes_get_workspace_stat(ctx));

    /* Demonstrate path separator customization */
    jes_set_path_separator(ctx, '/');
    struct jes_element *version_key = jes_get_key(ctx, jes_get_root(ctx), "metadata/version");
    if (version_key) {
        struct jes_element *version_value = jes_get_key_value(ctx, version_key);
        printf("\nmetadata/Version: %.*s\n",
               version_value->length, version_value->value);
    }

    return 0;
}