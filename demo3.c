/* Example3.c */
#include "src\jes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t buffer[32 * 1024]; /* 32KB buffer */

/* Print key-value pairs in an object */
void print_object(struct jes_context *ctx, struct jes_element *obj, int indent) {
    struct jes_element *key = NULL;
    char spaces[64] = {0};

    /* Create indentation */
    memset(spaces, ' ', indent * 2);
    spaces[indent * 2] = '\0';

    JES_FOR_EACH_KEY(ctx, obj, key) {
      struct jes_element *value = jes_get_key_value(ctx, key);
      printf("%s- %.*s: ", spaces, key->length, key->value);

      if (!value) {
        printf("null\n");
        continue;
      }

        switch (value->type) {
          case JES_STRING:
            printf("\"%.*s\"\n", value->length, value->value);
            break;
          case JES_NUMBER:
            printf("%.*s\n", value->length, value->value);
            break;
          case JES_TRUE:
            printf("true\n");
            break;
          case JES_FALSE:
            printf("false\n");
            break;
          case JES_NULL:
            printf("null\n");
            break;
          case JES_OBJECT:
            printf("{\n");
            print_object(ctx, value, indent + 1);
            printf("%s}\n", spaces);
            break;
          case JES_ARRAY:
            printf("[\n");
            struct jes_element *item = NULL;
            JES_ARRAY_FOR_EACH(ctx, value, item) {
            printf("%s  ", spaces);
            switch (item->type) {
              case JES_STRING:
                printf("\"%.*s\"\n", item->length, item->value);
                break;
              case JES_NUMBER:
                printf("%.*s\n", item->length, item->value);
                break;
              default:
                printf("(other type)\n");
                break;
            }
          }
          printf("%s]\n", spaces);
          break;
      default:
        printf("(unknown type)\n");
        break;
    }
  }
}

int main() {
    /* Initialize JES context */
    struct jes_context *ctx = jes_init(buffer, sizeof(buffer));

    /* JSON data to parse */
    const char *json = "{"
        "\"name\":\"John\","
        "\"details\":{"
            "\"age\":30,"
            "\"married\":true,"
            "\"children\":null"
        "},"
        "\"hobbies\":[\"reading\",\"swimming\",\"hiking\"]"
    "}";

    /* Parse JSON */
    struct jes_element *root = jes_load(ctx, json, strlen(json));

    printf("\nJSON Structure:\n");
    print_object(ctx, root, 0);

    /* Print statistics */
    struct jes_stat stats = jes_get_stat(ctx);
    printf("\nStatistics:\n");
    printf("Objects: %zu\n", stats.objects);
    printf("Keys: %zu\n", stats.keys);
    printf("Arrays: %zu\n", stats.arrays);
    printf("Values: %zu\n", stats.values);

    /* Print workspace statistics */
    struct jes_workspace_stat ws_stats = jes_get_workspace_stat(ctx);
    printf("\nWorkspace Usage:\n");
    printf("Context: %zu bytes\n", ws_stats.context);
    printf("Node Management: %zu/%zu bytes\n", ws_stats.node_mng_used, ws_stats.node_mng);
    printf("Hash Table: %zu/%zu bytes\n", ws_stats.hash_table_used, ws_stats.hash_table);

    return 0;
}