/* Example4.c */
#include "src\jes.h"
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

    struct jes_element *root = jes_load(ctx, json, strlen(json));
    if (!root) {
        fprintf(stderr, "Failed to parse JSON\n");
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
    struct jes_workspace_stat ws_stats = jes_get_workspace_stat(ctx);
    printf("\nMemory Usage:\n");
    printf("Context: %zu bytes\n", ws_stats.context);
    printf("Node management: %zu/%zu bytes (%.1f%% used)\n",
           ws_stats.node_mng_used, ws_stats.node_mng,
           (double)ws_stats.node_mng_used / ws_stats.node_mng * 100.0);

    if (ws_stats.hash_table > 0) {
        printf("Hash table: %zu/%zu bytes (%.1f%% used)\n",
               ws_stats.hash_table_used, ws_stats.hash_table,
               (double)ws_stats.hash_table_used / ws_stats.hash_table * 100.0);
    } else {
        printf("Hash table: disabled\n");
    }

    /* Demonstrate path separator customization */
    jes_set_path_separator(ctx, '/');
    struct jes_element *version_key = jes_get_key(ctx, root, "metadata/version");
    if (version_key) {
        struct jes_element *version_value = jes_get_key_value(ctx, version_key);
        printf("\nmetadata/Version: %.*s\n",
               version_value->length, version_value->value);
    }

    return 0;
}