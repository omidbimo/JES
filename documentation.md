## Table of Contents

- [Overview](#overview)
- [Compile-time Configuration](#compile-time-configuration)
- [Core Data Structures](#core-data-structures)
- [Functions](#functions)
  - [Initialization](#initialization)
  - [Loading and Rendering](#loading-and-rendering)
  - [Navigating the JSON Tree](#navigating-the-json-tree)
  - [Working with Objects and Keys](#working-with-objects-and-keys)
  - [Working with Arrays](#working-with-arrays)
  - [Element Manipulation](#element-manipulation)
  - [Statistics and Diagnostics](#statistics-and-diagnostics)
  - [Error Handling](#error-handling)
  - [Iteration Macros](#iteration-macros)
- [Usage Examples](#usage-examples)
  - [Basic Parse and Render](#basic-parse-and-render)
  - [Modifying JSON](#modifying-json)
  - [Iterating Through Elements](#iterating-through-elements)
- [Logging](#logging)

## Overview

JES represents JSON data as a tree of typed elements, with each element having a specific type, length, and value pointer. The library is designed to be memory-efficient, using a pre-allocated buffer for context and JSON nodes, and provides a comprehensive API for navigating, modifying, and rendering JSON structures.

## Compile-time Configuration

The library behavior can be configured by defining the following macros:

```c
/* Use 32-bit node descriptors (default: 16-bit)
 * Enable this to parse very large JSON files (up to ~4.29 billion nodes)
 * With 16-bit descriptors, the parser can address up to 65,535 nodes
 * Memory impact: 32-bit descriptors double memory usage for node references
 */
#define JES_USE_32BIT_NODE_DESCRIPTOR

/* Maximum allowed path length when searching a key (default: 512 bytes) */
#define JES_MAX_PATH_LENGTH 512

/* Enable hash table based key lookup to accelerate access in large JSON objects
 * Useful for frequent key access in large documents
 * Note: Requires more memory for the internal hash table
 * In small JSON objects, linear search may be more efficient
 */
#define JES_ENABLE_FAST_KEY_SEARCH

/* Allow fallback to linear search when node buffer is exhausted
 * On node buffer exhaustion, hash table memory is reclaimed to continue parsing
 * After fallback, key lookups become O(n) linear searches
 * Hash table entries are discarded and cannot be re-enabled
 */
#define JES_ENABLE_FALL_BACK_TO_LINEAR_SEARCH

/* Tab size for pretty printing JSON (default: 2) */
#define JES_TAB_SIZE 2

/* Debug logging controls */
#define JES_ENABLE_TOKEN_LOG
#define JES_ENABLE_PARSER_NODE_LOG
#define JES_ENABLE_PARSER_STATE_LOG
#define JES_ENABLE_SERIALIZER_NODE_LOG
#define JES_ENABLE_SERIALIZER_STATE_LOG
```

## Core Data Structures

### `jes_status`

Defines the possible status codes for JES operations:

| Status Code              | Description                                 |
| ------------------------ | ------------------------------------------- |
| `JES_NO_ERROR`           | No error occurred.                          |
| `JES_UNEXPECTED_SYMBOL`  | Tokenizer error - unexpected character.     |
| `JES_INVALID_UNICODE`    | Tokenizer error - invalid Unicode sequence. |
| `JES_INVALID_NUMBER`     | Tokenizer error - malformed number.         |
| `JES_UNEXPECTED_EOF`     | Unexpected end of JSON data.                |
| `JES_OUT_OF_MEMORY`      | Insufficient memory for operation.          |
| `JES_UNEXPECTED_TOKEN`   | Parser error - unexpected token.            |
| `JES_UNEXPECTED_STATE`   | Parser/Serializer error - invalid state.    |
| `JES_UNEXPECTED_ELEMENT` | Serializer/Tree management error.           |
| `JES_INVALID_PARAMETER`  | Invalid parameter passed to function.       |
| `JES_ELEMENT_NOT_FOUND`  | Requested JSON element not found.           |
| `JES_INVALID_CONTEXT`    | Context validation failed.                  |
| `JES_BROKEN_TREE`        | Malformed JSON tree detected.               |
| `JES_DUPLICATE_KEY`      | Duplicate key detected in object.           |

### JSON Element Types

### `jes_type`

Defines the types of JSON elements:

| Type          | Description            |
| ------------- | ---------------------- |
| `JES_UNKNOWN` | Unknown element type.  |
| `JES_OBJECT`  | JSON object.           |
| `JES_KEY`     | JSON key.              |
| `JES_ARRAY`   | JSON array.            |
| `JES_STRING`  | String value.          |
| `JES_NUMBER`  | Numeric value.         |
| `JES_TRUE`    | Boolean `true` value.  |
| `JES_FALSE`   | Boolean `false` value. |
| `JES_NULL`    | Null value.            |

### `jes_element`

Represents a JSON element in the tree.

| Field    | Type           | Description                       |
| -------- | -------------- | --------------------------------- |
| `type`   | `uint16_t`     | Type of element (see `jes_type`). |
| `length` | `uint16_t`     | Length of the value.              |
| `value`  | `const char *` | Pointer to the value.             |

### `jes_context`

An opaque structure that holds the internal state of the parser including JSON tree information, element pool management and process status.

### `jes_stat`

Statistics about parsed JSON elements:

| Field     | Type     | Description                                                  |
| --------- | -------- | ------------------------------------------------------------ |
| `objects` | `size_t` | Number of OBJECT elements                                    |
| `keys`    | `size_t` | Number of KEY elements                                       |
| `arrays`  | `size_t` | Number of ARRAY elements                                     |
| `values`  | `size_t` | Number of value elements (STRING, NUMBER, TRUE, FALSE, NULL) |

### `jes_workspace_stat`

Memory allocation breakdown for a JES workspace:

| Field             | Type     | Description                                   |
| ----------------- | -------- | --------------------------------------------- |
| `context`         | `size_t` | Bytes allocated for JES context data          |
| `node_mng`        | `size_t` | Bytes dedicated to node management module     |
| `node_mng_used`   | `size_t` | Bytes currently allocated in node manager     |
| `hash_table`      | `size_t` | Bytes dedicated to hash table (0 if disabled) |
| `hash_table_used` | `size_t` | Bytes currently allocated in hash table       |

## Functions

### Initialization

### `jes_init`

Initializes a new JES context.

```c
struct jes_context* jes_init(void* buffer, size_t buffer_size);
```

**Parameters**

- `buffer` : Pre-allocated buffer to hold the context and JSON tree nodes
- `buffer_size` : Size of the provided buffer

**Returns**  Pointer to the initialized context or NULL on failure
**Note** The buffer must have enough space to hold the context and required JSON tree nodes

### `jes_get_context_size`

Returns the minimal size required for a JES context (excluding node pool and hash table).

```c
size_t jes_get_context_size(void);
```

**Returns** Size in bytes required for the JES context structure

### `jes_get_node_size`

Returns the size of a JES node containing JSON element data and tree management overhead.

```c
size_t jes_get_node_size(void);
```

**Returns** Size in bytes of a single JES node

### `jes_reset`

Resets a JES context, clearing its internal state and JSON tree.

```c
void jes_reset(struct jes_context* ctx);
```

**Parameters**

- `ctx` : JES context to reset

## Loading and Rendering

### `jes_load`

Parse JSON into Tree

```c
struct jes_element* jes_load(struct jes_context* ctx, const char* json_data, size_t json_length);
```

**Parameters**

- `ctx` : Initialized JES context.
- `json_data` : String of JSON data (not necessarily NUL-terminated)
- `json_length` : Length of the JSON data.

**Returns**  Root element of the tree (always an OBJECT) or NULL on failure

### `jes_render`

Render JSON Tree to String

```c
size_t jes_render(struct jes_context* ctx, char* dst, size_t length, bool compact);
```

**Parameters**

- `ctx` : JES context containing a JSON tree
- `dst` : Destination buffer for the rendered JSON string
- `length` : Size of destination buffer in bytes
- `compact` : If true, generates compact JSON; if false, generates formatted JSON with indentation

**Returns**  Number of bytes written (including null terminator space), or 0 on failure

Compact JSON example:

```json
{"key1":["value1","value2",null]}
```

Formatted JSON example:

```json
{
  "key1": [
    "value1",
    "value2",
    null
  ]
}
```

### `jes_evaluate`

Evaluate the JSON tree and calculate required buffer size for rendering

```c
size_t jes_evaluate(struct jes_context* ctx, bool compact);
```

**Parameters**

- `ctx` : Initialized JES context containing a JSON tree
- `compact` – If true, calculates size for compact JSON; if false, for formatted JSON

**Returns** Required buffer size including null terminator, or 0 on error

## Navigating the JSON Tree

### `jes_set_path_separator`

Sets path delimiter when searching for a key using a path of keys.

```c
void jes_set_path_separator(struct jes_context* ctx, char delimiter);
```

**Parameters**

- `ctx` : Initialized JES context
- `delimiter` : Character to use as path separator (default is '.')

### `jes_get_root`

Get Root Element

```c
struct jes_element* jes_get_root(struct jes_context* ctx);
```

**Parameters**

- `ctx` – Initialized JES context.

**Returns**  Root element of the JSON tree (always a `JES_OBJECT`) or NULL if tree is empty

### `jes_get_parent`

Get parent Element

```c
struct jes_element* jes_get_parent(struct jes_context* ctx, struct jes_element* element);
```

**Parameters**

- `ctx` : Initialized JES context.
- `element` : Element to find the parent of

**Returns** Parent element or NULL if no parent exists

### `jes_get_child`

Get First Child Element

```c
struct jes_element* jes_get_child(struct jes_context* ctx, struct jes_element* element);
```

**Parameters**

- `ctx`: Initialized JES context
- `element`: Element to get the first child of

**Returns** First child element or NULL if no children exist

### `jes_get_sibling`

Get Next Sibling Element

```c
struct jes_element* jes_get_sibling(struct jes_context* ctx, struct jes_element* element);
```

**Parameters**

- `ctx`: Initialized JES context
- `element`: Element to get the next sibling of

**Returns** Next sibling element or NULL if no more siblings exist

## Working with Objects and Keys

### `jes_get_key`

Find a Key by Path

```c
struct jes_element* jes_get_key(struct jes_context* ctx, struct jes_element* parent, const char* keys);
```

**Parameters**

- `ctx`: Initialized JES context
- `parent`: Starting element (`JES_OBJECT` or `JES_KEY`) to search from
- `keys`: Path of keys separated by delimiter (e.g., "user.address.city")

**Returns** Key element or NULL if not found
**Note** Supports hierarchical navigation through path notation. Supports caching — searches start from a parent, reducing repeated traversal.

### `jes_get_key_value`

Get Value of a Key

```c
struct jes_element* jes_get_key_value(struct jes_context* ctx, struct jes_element* key);
```

**Parameters**

- `ctx`: Initialized JES context
- `key`: Key element to get the value of

**Returns** Value element or NULL if key has no value

### `jes_add_key`

Add a Key to an Object

```c
struct jes_element* jes_add_key(struct jes_context* ctx, struct jes_element* parent, const char* keyword, size_t keyword_length);
```

**Parameters**

- `ctx`: Initialized JES context
- `parent`: Parent element (`JES_OBJECT` or `JES_KEY`)
- `keyword`: Name of the new key
- `keyword_length`: Length of the keyword

**Returns** New key element or NULL on failure
**Note** If parent is a KEY, its value will become an OBJECT if not already

### `jes_add_key_before`

### `jes_add_key_after`

Insert a Key Before or After

```c
struct jes_element* jes_add_key_before(struct jes_context* ctx, struct jes_element* key, const char* keyword, size_t keyword_length);
struct jes_element* jes_add_key_after(struct jes_context* ctx, struct jes_element* key, const char* keyword, size_t keyword_length);
```

**Parameters**

- `ctx`: Initialized JES context
- `key`: Reference key element
- `keyword`: Name of the new key
- `keyword_length`: Length of the keyword

**Returns** New key element or NULL on failure

### `jes_update_key`

Update a Key Name

```c
jes_status jes_update_key(struct jes_context* ctx, struct jes_element* key, const char* keyword, size_t keyword_length);
```

**Parameters**

- `ctx`: Initialized JES context
- `key`: Key element to update
- `keyword`: New name for the key
- `keyword_length`: Length of the keyword

**Returns** Status code
**Note** The keyword string must persist for the lifecycle of the context

### `jes_update_key_value`

Update a Key Value

```c
struct jes_element* jes_update_key_value(struct jes_context* ctx, struct jes_element* key, enum jes_type type, const char* value, size_t value_length);
```

**Parameters**

- `ctx`: Initialized JES context
- `key`: Key element to update its value
- `type`: Type for the new value
- `value`: String representation of the new value
- `value_length`: Length of the value

**Returns** New value element or NULL on failure
**Note** Existing value elements will be deleted first

### `jes_update_key_value_to_object`

### `jes_update_key_value_to_array`

### `jes_update_key_value_to_true`

### `jes_update_key_value_to_false`

### `jes_update_key_value_to_null`

Convert Key Value to Specific Types

```c
struct jes_element* jes_update_key_value_to_object(struct jes_context* ctx, struct jes_element* key);
struct jes_element* jes_update_key_value_to_array(struct jes_context* ctx, struct jes_element* key);
struct jes_element* jes_update_key_value_to_true(struct jes_context* ctx, struct jes_element* key);
struct jes_element* jes_update_key_value_to_false(struct jes_context* ctx, struct jes_element* key);
struct jes_element* jes_update_key_value_to_null(struct jes_context* ctx, struct jes_element* key);
```

**Parameters**

- `ctx`: Initialized JES context
- `key`: Key element to update the value of

**Returns** New value element or NULL on failure
**Note** All previous value elements will be lost

## Working with Arrays

### `jes_get_array_size`

Get Array Size

```c
uint16_t jes_get_array_size(struct jes_context* ctx, struct jes_element* array);
```

**Parameters**

- `ctx`: Initialized JES context
- `array`: Array element

**Returns** Number of elements in the array

### `jes_get_array_value`

Get Array Value by Index

```c
struct jes_element* jes_get_array_value(struct jes_context* ctx, struct jes_element* array, int32_t index);
```

**Parameters**

- `ctx`: Initialized JES context
- `array`: Array element
- `index`: Index of the value to retrieve (negative indices count from the end)

**Returns** Value element or NULL if index is out of bounds

### `jes_update_array_value`

Update Array Value

```c
struct jes_element* jes_update_array_value(struct jes_context* ctx, struct jes_element* array, int32_t index, enum jes_type type, const char* value, size_t value_length);
```

**Parameters**

- `ctx`: Initialized JES context
- `array`: Array element
- `index`: Index of the value to update
- `type`: Type for the new value
- `value`: String representation of the new value
- `value_length`: Length of the value

**Returns** Updated value element or NULL on failure

### `jes_append_array_value`

Append Value to Array

```c
struct jes_element* jes_append_array_value(struct jes_context* ctx, struct jes_element* array, enum jes_type type, const char* value, size_t value_length);
```

**Parameters**

- `ctx`: Initialized JES context
- `array`: Array element
- `type`: Type for the new value
- `value`: String representation of the new value
- `value_length`: Length of the value

**Returns** New value element or NULL on failure

### `jes_add_array_value`

Insert Value into Array

```c
struct jes_element* jes_add_array_value(struct jes_context* ctx, struct jes_element* array, int32_t index, enum jes_type type, const char* value, size_t value_length);
```

**Parameters**

- `ctx`: Initialized JES context
- `array`: Array element
- `index`: Position to insert the new value (negative indices count from the end)
- `type`: Type for the new value
- `value`: String representation of the new value
- `value_length`: Length of the value

**Returns** New value element or NULL on failure
**Note** Out-of-bounds positive indices are handled as append, negative as prepend

## Element Manipulation

### `jes_add_element`

Generic way to add an Element

```c
struct jes_element* jes_add_element(struct jes_context* ctx, struct jes_element* parent, enum jes_type type, const char* value, size_t value_length);
```

**Parameters**

- `ctx`: Initialized JES context
- `parent`: Parent element to add the new element to
- `type`: Type for the new element
- `value`: String representation of the element value
- `value_length`: Length of the value

**Returns** New element or NULL on failure

### `jes_delete_element`

Delete an Element

```c
jes_status jes_delete_element(struct jes_context* ctx, struct jes_element* element);
```

**Parameters**

- `ctx`: Initialized JES context
- `element`: Element to delete along with its branch

**Returns** Status code

### `jes_get_element_count`

Count Elements

```c
size_t jes_get_element_count(struct jes_context* ctx);
```

**Parameters**

- `ctx`: Initialized JES context

**Returns** Number of elements in the JSON tree

### `jes_get_element_capacity`

Get Maximum Element Capacity

```c
size_t jes_get_element_capacity(struct jes_context* ctx);
```

**Parameters**

- `ctx`: Initialized JES context

**Returns** Maximum number of elements that can be allocated in this context

## Statistics and Diagnostics

### `jes_get_stat`

Get Element Statistics

```c
struct jes_stat jes_get_stat(struct jes_context* ctx);
```

**Parameters**

- `ctx`: Initialized JES context

**Returns** Statistics structure with count of different element types
**Note** Provides a summary of the number of elements in the parsed tree, categorized by their types. Can be used for diagnostics, validation, or performance analysis.

### `jes_get_workspace_stat`

Get Memory Usage Statistics

```c
struct jes_workspace_stat jes_get_workspace_stat(struct jes_context* ctx);
```

**Parameters**

- `ctx`: Initialized JES context

**Returns** Workspace statistics structure with memory allocation breakdown
**Note** Analyzes the current workspace allocation and returns a breakdown of how the user-provided buffer is segmented among the JES context, node management system, and optional hash table.

## Error Handling

### `jes_get_status`

```c
jes_status jes_get_status(struct jes_context* ctx);
```

**Parameters**

- `ctx`: Initialized JES context

**Returns** Status code of the latest operation

## Iteration Macros

**Iterate Over Elements by Type**

```c
#define JES_FOR_EACH(ctx_, elem_, type_)
```

Iterates over children of a given element if they match the specified type.

Usage example:

```c
JES_FOR_EACH(ctx, elem, JES_OBJECT) {
    /* Process each object child */
}
```

**Iterate Over Array Elements**

```c
#define JES_ARRAY_FOR_EACH(ctx_, array_, iter_)
```

Iterates over all elements in an array.

Usage example:

```c
JES_ARRAY_FOR_EACH(ctx, array_element, item) {
    /* Process each array item */
}
```

**Iterate Over Object Keys**

```c
#define JES_FOR_EACH_KEY(ctx_, object_, iter_)
```

Iterates over all keys in an object.

Usage example:

```c
JES_FOR_EACH_KEY(ctx, object_element, key) {
    /* Process each key in the object */
}
```

## Usage Examples

**Basic Parse and Render**

```c
/* Example1.c */
#include "src\jes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t buffer[32 * 1024]; /* 32KB buffer */

int main() {
  /* Initialize JES context */
  struct jes_context *ctx = jes_init(buffer, sizeof(buffer));
  if (!ctx) {
    fprintf(stderr, "Failed to initialize JES context\n");
    return 1;
  }

  /* JSON data to parse */
  const char *json = "{\"name\":\"John\",\"age\":30,\"city\":\"New York\"}";

  /* Parse JSON */
  struct jes_element *root = jes_load(ctx, json, strlen(json));
  if (!root) {
    fprintf(stderr, "Failed to parse JSON: %d\n", jes_get_status(ctx));
    return 1;
  }

  /* Calculate required buffer for rendering */
  size_t required_size = jes_evaluate(ctx, false); /* Formatted output */

  /* Allocate buffer for rendered JSON */
  char *output = malloc(required_size);
  if (!output) {
    fprintf(stderr, "Failed to allocate output buffer\n");
    return 1;
  }

  /* Render JSON */
  size_t rendered_size = jes_render(ctx, output, required_size, false);
  if (rendered_size == 0) {
    fprintf(stderr, "Failed to render JSON: %d\n", jes_get_status(ctx));
    free(output);
    return 1;
  }

  printf("\nFormatted JSON:\n%s\n", output);

  /* Clean up */
  free(output);
  return 0;
}
```

To build Example1 in debug mode using gcc:

```dos
gcc Example1.c src\jes.c src\jes_tokenizer.c src\jes_parser.c src\jes_serializer.c src\jes_tree.c src\jes_hash_table.c src\jes_logger.c -g -UDNDEBUG
```

**Modifying JSON**

```c
/* Example2.c */
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
  struct jes_element *root = jes_load(ctx, json, strlen(json));

  /* Find the person object */
  struct jes_element *person = jes_get_key(ctx, root, "person");

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

  printf("Modified JSON:\n%s\n", output);

  /* Clean up */
  free(output);

  return 0;
}
```

**Iterating Through Elements**

```c
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
```

**Working with Statistics and Diagnostics**

```c
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
            "\"created\": \"2024-01-01\""
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
        printf("\nmetadat/Version: %.*s\n",
               version_value->length, version_value->value);
    }

    return 0;
}
```

## Logging

A debug build of JES produces log outputs for Tokenizer, Allocator and Serializer.

```dos
JES.Parser.State: <EXPECT_VALUE>(2)
JES.Token: [Ln: 0, Col: 1, Pos: 1, Len: 1] OPENING_BRACE    "{"
    + JES.Node: [0] "{" <OBJECT>,    parent:[-1], right:[-1], first_child:[-1], last_child:[-1]
JES.Parser.State: <EXPECT_KEY>(0)
JES.Token: [Ln: 1, Col: 7, Pos: 9, Len: 0] STRING           ""
    + JES.Node: [1] "" <KEY>,    parent:[0], right:[-1], first_child:[-1], last_child:[-1]
JES.Parser.State: <EXPECT_COLON>(1)
JES.Token: [Ln: 1, Col: 8, Pos: 10, Len: 1] COLON            ":"
JES.Parser.State: <EXPECT_VALUE>(2)
JES.Token: [Ln: 1, Col: 10, Pos: 12, Len: 4] NULL             "null"
    + JES.Node: [2] "null" <NULL>,    parent:[1], right:[-1], first_child:[-1], last_child:[-1]
JES.Parser.State: <HAVE_VALUE>(3)
```
