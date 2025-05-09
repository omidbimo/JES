## Table of Contents

- [Overview](#overview)
- [Compile-time Configuration](#Compile-time-Configuration)
- [Core Data Structures](#Core-Data-Structures)
- [Functions](#Functions)
  - [Initialization](#Initialization)
  - [Loading and Rendering](#Loading-and-Rendering)
  - [Navigating the JSON Tree](#Navigating-the-JSON-Tree)
  - [Working with Objects and Keys](#Working-with-Objects-and-Keys)
  - [Working with Arrays](#Working-with-Arrays)
  - [Element Manipulation](#Element-Manipulation)
  - [Error Handling](#Error-Handling)
  - [Iteration Macros](#Iteration-Macros)
- [Usage Examples](#Usage-Examples)
  - [Basic Parse and Render](#Basic-Parse-and-Render)
  - [Modifying JSON*](#Modifying-JSON)
  - [Iterating Through Elements](#Iterating-Through-Elements)
- [Logging](#Logging)


## Overview

JES represents JSON data as a tree of typed elements, with each element having a specific type, length, and value pointer. The library is designed to be memory-efficient, using a pre-allocated buffer for context and JSON nodes, and provides a comprehensive API for navigating, modifying, and rendering JSON structures.

## Compile-time Configuration

The library behavior can be configured by defining the following macros:

```c
/* Allow duplicate keys (default: disabled)
 * When enabled, multiple keys with the same name can exist in the same object
 *      Enable this feature with caution. It improves the parser performance.
 *      But the parser always delivers the first key it founds.
 * When disabled, JES will generate error for duplicate keys.
 * Note: Disabling this reduces parser performance due to duplicate key detection
 */
#define JES_ALLOW_DUPLICATE_KEYS

/* Use 32-bit node descriptors (default: 16-bit)
 * Enable this to parse very large JSON files (up to 4,294,967,294 nodes)
 * With 16-bit descriptors, the parser can address up to 65534 nodes
 */
#define JES_USE_32BIT_NODE_DESCRIPTOR
```

### Core Data Structures

### `jes_status`

Defines the possible status codes for JES operations:

| Status Code             | Description                                     |
| ----------------------- | ----------------------------------------------- |
| `JES_NO_ERROR`          | No error occurred.                              |
| `JES_PARSING_FAILED`    | Parsing of JSON data failed.                    |
| `JES_RENDER_FAILED`     | Rendering of JSON tree failed.                  |
| `JES_OUT_OF_MEMORY`     | Insufficient memory for operation.              |
| `JES_UNEXPECTED_TOKEN`  | Encountered an unexpected token during parsing. |
| `JES_UNEXPECTED_NODE`   | Encountered an unexpected node in the tree.     |
| `JES_UNEXPECTED_EOF`    | Unexpected end of JSON data.                    |
| `JES_INVALID_PARAMETER` | Invalid parameter passed to function.           |
| `JES_ELEMENT_NOT_FOUND` | Requested JSON element not found.               |
| `JES_INVALID_CONTEXT`   | Context Cookie doesn't match the expected value |
| `JES_BROKEN_TREE`       | A malformed JSON tree is detected               |
| `JES_DUPLICATE_KEY`     | Duplicate Key is detected                       |

---

### JSON Element Types

### `jes_type`

Defines the types of JSON elements:

| Type               | Description            |
| ------------------ | ---------------------- |
| `JES_UNKNOWN`      | Unknown element type.  |
| `JES_OBJECT`       | JSON object.           |
| `JES_KEY`          | JSON key.              |
| `JES_ARRAY`        | JSON array.            |
| `JES_VALUE_STRING` | String value.          |
| `JES_VALUE_NUMBER` | Numeric value.         |
| `JES_VALUE_TRUE`   | Boolean `true` value.  |
| `JES_VALUE_FALSE`  | Boolean `false` value. |
| `JES_VALUE_NULL`   | Null value.            |

---

### `jes_element`

Represents a JSON element in the tree.

| Field    | Type           | Description                       |
| -------- | -------------- | --------------------------------- |
| `type`   | `uint16_t`     | Type of element (see `jes_type`). |
| `length` | `uint16_t`     | Length of the value.              |
| `value`  | `const char *` | Pointer to the value.             |

---

### `jes_context`

An opaque structure that holds the internal state of the parser including JSON tree information, element pool managemnet and process status.

### Functions

#### Initialization

### `jes_init`

Initializes a new JES context.

```c
struct jes_context* jes_init(void *buffer, uint32_t buffer_size);
```

**Parameters**

  - `buffer` : Pre-allocated buffer to hold the context and JSON tree nodes
  - `buffer_size` : Size of the provided buffer

**Returns**  Pointer to the initialized context or NULL on failure
**Note** The buffer must have enough space to hold the context and required JSON tree nodes (The context structure requires TBD bytes on a 32-bit system)

---

## Loading and Rendering

### `jes_load`

Parse JSON into Tree

```c
struct jes_element* jes_load(struct jes_context* ctx, const char* json_data, uint32_t json_length);
```

**Parameters**
  - `ctx` : Initialized JES context.
  - `json_data` : String of JSON data (not necessarily NUL-terminated)
  - `json_length` : Length of the JSON data.

**Returns**  Root element of the tree (always an OBJECT) or NULL on failure

---

### `jes_render`

Render JSON Tree to String

```c
uint32_t jes_render(struct jes_context *ctx, char *dst, uint32_t length, bool compact);
```

**Parameters**
  - `ctx` : JES context containing a JSON tree
  - `dst` : Destination buffer for the rendered JSON string
  - `length` : Size of destination buffer in bytes
  - `compact` : If true, generates compact JSON; if false, generates formatted JSON with 2-space indentation

**Returns**  Size of the generated JSON string, or 0 on failure

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

---

### `jes_evaluate`

Evaluate the JSON tree and calculate required Buffer size for rendering

```c
uint32_t jes_evaluate(struct jes_context *ctx, bool compact);
```

**Parameters**
  - `ctx` : Initialized JES context containing a JSON tree
  - `compact` – If true, calculates size for compact JSON; if false, for formatted JSON

**Returns** Required buffer size to render the JSON, or 0 on failure

## Navigating the JSON Tree

## `jes_get_root`

Get Root Element

```c
struct jes_element* jes_get_root(struct jes_context *ctx);
```

**Parameters**
  - `ctx` – Initialized JES context.

**Returns**  Root element of the JSON tree (always a `JES_OBJECT`) or NULL if tree is empty

## `jes_get_parent`

Get parent Element

```c
struct jes_element* jes_get_parent(struct jes_context *ctx, struct jes_element *element);
```

**Parameters**
  - `ctx` : Initialized JES context.
  - `element` : Element to find the parent of

**Returns** Parent element or NULL if no parent exists

## `jes_get_child`

Get First Child Element

```c
struct jes_element* jes_get_child(struct jes_context *ctx, struct jes_element *element);
```

**Parameters**
  - `ctx`: Initialized JES context
  - `element`: Element to get the first child of

**Returns** First child element or NULL if no children exist

## `jes_get_sibling`

Get Next Sibling Element

```c
struct jes_element* jes_get_sibling(struct jes_context *ctx, struct jes_element *element);
```

**Parameters**
  - `ctx`: Initialized JES context
  - `element`: Element to get the next sibling of

**Returns** Next sibling element or NULL if no more siblings exist

---

## `jes_get_parent_type`

Get Parent Type

```c
enum jes_type jes_get_parent_type(struct jes_context *ctx, struct jes_element *element);
```

**Parameters**
- `ctx`: Initialized JES context
- `element`: Element to get the parent type of

**Returns** Type of parent element

---

## Working with Objects and Keys

## `jes_get_key`

Find a Key by Path

```c
struct jes_element* jes_get_key(struct jes_context *ctx, struct jes_element *parent, const char *keys);
```

**Parameters**
  - `ctx`: Initialized JES context
  - `parent`: Starting element (`JES_OBJECT` or `JES_KEY`) to search from
  - `keys`: Path of keys separated by dots (e.g., "user.address.city")

**Returns** Key element or NULL if not found
**Note** Supports hierarchical navigation through dot notation

---

## `jes_get_key_value`

Get Value of a Key

```c
struct jes_element* jes_get_key_value(struct jes_context *ctx, struct jes_element *key);
```

**Parameters**
  - `ctx`: Initialized JES context
  - `key`: Key element to get the value of

**Returns** Value element or NULL if key has no value

---

## `jes_add_key`

Add a Key to an Object

```c
struct jes_element* jes_add_key(struct jes_context *ctx, struct jes_element *parent, const char *keyword);
```

**Parameters**
  - `ctx`: Initialized JES context
  - `parent`: Parent element (`JES_OBJECT` or `JES_KEY`)
  - `keyword`: Name of the new key

**Returns** New key element or NULL on failure
**Note** If parent is a KEY, its value will become an OBJECT if not already

---

## `jes_add_key_before`
## `jes_add_key_after`

Insert a Key Before or After

```c
struct jes_element* jes_add_key_before(struct jes_context *ctx, struct jes_element *key, const char *keyword);
struct jes_element* jes_add_key_after(struct jes_context *ctx, struct jes_element *key, const char *keyword);
```

**Parameters**
  - `ctx`: Initialized JES context
  - `key`: Reference key element
  - `keyword`: Name of the new key

**Returns** New key element or NULL on failure

---

## `jes_update_key`

Update a Key Name

```c
uint32_t jes_update_key(struct jes_context *ctx, struct jes_element *key, const char *keyword);
```

**Parameters**
  - `ctx`: Initialized JES context
  - `key`: Key element to update
  - `keyword`: New name for the key

**Returns** Status code
**Note** The keyword string must persist for the lifecycle of the context

---

## `jes_update_key_value`

Update a Key Value

```c
struct jes_element* jes_update_key_value(struct jes_context *ctx, struct jes_element *key, enum jes_type type, const char *value);
```

**Parameters**
  - `ctx`: Initialized JES context
  - `key`: Key element to update its value
  - `type`: Type for the new value
  - `value`: String representation of the new value

**Returns** New value element or NULL on failure
**Note** Existing value elements will be deleted first

---

## `jes_update_key_value_to_object`
## `jes_update_key_value_to_array`
## `jes_update_key_value_to_true`
## `jes_update_key_value_to_false`
## `jes_update_key_value_to_null`

Convert Key Value to Specific Types

```c
struct jes_element* jes_update_key_value_to_object(struct jes_context *ctx, struct jes_element *key);
struct jes_element* jes_update_key_value_to_array(struct jes_context *ctx, struct jes_element *key);
struct jes_element* jes_update_key_value_to_true(struct jes_context *ctx, struct jes_element *key);
struct jes_element* jes_update_key_value_to_false(struct jes_context *ctx, struct jes_element *key);
struct jes_element* jes_update_key_value_to_null(struct jes_context *ctx, struct jes_element *key);
```

**Parameters**
  - `ctx`: Initialized JES context
  - `key`: Key element to update the value of

**Returns** New value element or NULL on failure
**Note** All previous value elements will be lost

---

## Working with Arrays

## `jes_get_array_size`

Get Array Size

```c
uint16_t jes_get_array_size(struct jes_context *ctx, struct jes_element *array);
```

**Parameters**
  - `ctx`: Initialized JES context
  - `array`: Array element

**Returns** Number of elements in the array

---

## `jes_get_array_value`

Get Array Value by Index

```c
struct jes_element* jes_get_array_value(struct jes_context *ctx, struct jes_element *array, int32_t index);
```

**Parameters**
  - `ctx`: Initialized JES context
  - `array`: Array element
  - `index`: Index of the value to retrieve (negative indices count from the end)

**Returns** Value element or NULL if index is out of bounds

---

## `jes_update_array_value`

Update Array Value

```c
struct jes_element* jes_update_array_value(struct jes_context *ctx, struct jes_element *array, int32_t index, enum jes_type type, const char *value);
```

**Parameters**
  - `ctx`: Initialized JES context
  - `array`: Array element
  - `index`: Index of the value to update
  - `type`: Type for the new value
  - `value`: String representation of the new value

**Returns** Updated value element or NULL on failure

---

## `jes_append_array_value`

Append Value to Array

```c
struct jes_element* jes_append_array_value(struct jes_context *ctx, struct jes_element *array, enum jes_type type, const char *value);
```

**Parameters**
  - `ctx`: Initialized JES context
  - `array`: Array element
  - `type`: Type for the new value
  - `value`: String representation of the new value

**Returns** New value element or NULL on failure

---

## `jes_add_array_value`

Insert Value into Array

```c
struct jes_element* jes_add_array_value(struct jes_context *ctx, struct jes_element *array, int32_t index, enum jes_type type, const char *value);
```

**Parameters**
  - `ctx`: Initialized JES context
  - `array`: Array element
  - `index`: Position to insert the new value (negative indices count from the end)
  - `type`: Type for the new value
  - `value`: String representation of the new value

**Returns** New value element or NULL on failure
**Note** Out-of-bounds positive indices are handled as append, negative as prepend

---

## Element Manipulation

## `jes_add_element`

Generic way to add an Element

```c
struct jes_element* jes_add_element(struct jes_context *ctx, struct jes_element *parent, enum jes_type type, const char *value);
```

**Parameters**
  - `ctx`: Initialized JES context
  - `parent`: Parent element to add the new element to
  - `type`: Type for the new element
  - `value`: String representation of the element value

**Returns** New element or NULL on failure

---

## `jes_delete_element`

Delete an Element

```c
jes_status jes_delete_element(struct jes_context *ctx, struct jes_element *element);
```

**Parameters**
  - `ctx`: Initialized JES context
  - `element`: Element to delete along with its branch

**Returns** Status code

---

## `jes_get_element_count`

Count Elements

```c
size_t jes_get_element_count(struct jes_context *ctx);
```

**Parameters**
  - `ctx`: Initialized JES context

**Returns** Number of elements in the JSON tree

---

## Error Handling

## `jes_get_status`

```c
jes_status jes_get_status(struct jes_context *ctx);
```

**Parameters**
  - `ctx`: Initialized JES context

**Returns** Status code of the latest operation

---

## `jes_stringify_status`

Convert status codes into human-readable string

```c
char* jes_stringify_status(struct jes_context *ctx, char *msg, size_t msg_len);
```
**Parameters**
  - `ctx`: Initialized JES context
  - `msg`: Buffer to hold the string
  - `msg_len`: Size of the buffer in bytes

**Returns** Pointer to the buffer for convenient access (e.g., calling the function inside printf())

---

## Iteration Macros

**Iterate Over Elements by Type**

```c
#define JES_FOR_EACH(ctx_, elem_, type_)
```

**Iterate Over Array Elements**

```c
#define JES_ARRAY_FOR_EACH(ctx_, array_, iter_)
```

**Iterate Over Object Keys**

```c
#define JES_FOR_EACH_KEY(ctx_, object_, iter_)
```

---

## Usage Examples

**Basic Parse and Render**

```c
#include "jes.h"
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

  /* Calculate required buffer for rendering if you are using malloc */
  uint32_t required_size = jes_evaluate(ctx, false); /* Formatted output */

  /* Allocate buffer for rendered JSON */
  char *output = malloc(required_size + 1);  // +1 for null terminator
  if (!output) {
    fprintf(stderr, "Failed to allocate output buffer\n");
    return 1;
  }

  /* Render JSON */
  uint32_t rendered_size = jes_render(ctx, output, required_size, false);
  if (rendered_size == 0) {
    fprintf(stderr, "Failed to render JSON: %d\n", jes_get_status(ctx));
    free(output);
    return 1;
  }

  /* Null-terminate and print */
  output[rendered_size] = '\0';
  printf("Formatted JSON:\n%s\n", output);

  /* Clean up */
  free(output);
  return 0;
}
```

**Modifying JSON**

```c
#include "jes.h"
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
  struct jes_element *email = jes_add_key(ctx, person, "email");
  jes_update_key_value(ctx, email, JES_VALUE_STRING, "john@example.com");

  /* Update age */
  struct jes_element *age = jes_get_key(ctx, person, "age");
  jes_update_key_value(ctx, age, JES_VALUE_NUMBER, "31");

  /* Add an address object */
  struct jes_element *addr = jes_add_key(ctx, person, "address");
  jes_update_key_value_to_object(ctx, addr);

  /* Add fields to address */
  struct jes_element *city = jes_add_key(ctx, addr, "city");
  jes_update_key_value(ctx, city, JES_VALUE_STRING, "New York");

  struct jes_element *zip = jes_add_key(ctx, addr, "zip");
  jes_update_key_value(ctx, zip, JES_VALUE_STRING, "10001");

  /* Add hobbies array */
  struct jes_element *hobbies = jes_add_key(ctx, person, "hobbies");
  jes_update_key_value_to_array(ctx, hobbies);

  /* Add items to array */
  jes_append_array_value(ctx, jes_get_key_value(ctx, hobbies), JES_VALUE_STRING, "reading");
  jes_append_array_value(ctx, jes_get_key_value(ctx, hobbies), JES_VALUE_STRING, "running");
  jes_append_array_value(ctx, jes_get_key_value(ctx, hobbies), JES_VALUE_STRING, "coding");

  /* Render modified JSON */
  uint32_t required_size = jes_evaluate(ctx, false);
  char *output = malloc(required_size + 1);
  uint32_t rendered_size = jes_render(ctx, output, required_size, false);
  output[rendered_size] = '\0';

  printf("Modified JSON:\n%s\n", output);

  /* Clean up */
  free(output);

  return 0;
}
```

**Iterating Through Elements**

```c
#include "jes.h"
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
          case JES_VALUE_STRING:
            printf("\"%.*s\"\n", value->length, value->value);
            break;
          case JES_VALUE_NUMBER:
            printf("%.*s\n", value->length, value->value);
            break;
          case JES_VALUE_TRUE:
            printf("true\n");
            break;
          case JES_VALUE_FALSE:
            printf("false\n");
            break;
          case JES_VALUE_NULL:
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
              case JES_VALUE_STRING:
                printf("\"%.*s\"\n", item->length, item->value);
                break;
              case JES_VALUE_NUMBER:
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
    /* Allocate buffer for JES context */
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

    printf("JSON Structure:\n");
    print_object(ctx, root, 0);
    return 0;
}
```

## Logging

A debug build of JES produces log outputs for Tokenizer, Allocator and Serializer.

```dos
JES - parsing demo.json...
 JES.Token: [Pos:     0, Len:   1] OPENING_BRACE    "{"
    + JES.Node: [0] "{" <OBJECT>,    parent:[-1], right:[-1], child:[-1]
 JES.Token: [Pos:     8, Len:   0] STRING           ""
    + JES.Node: [1] "" <KEY>,    parent:[0], right:[-1], child:[-1]
 JES.Token: [Pos:     9, Len:   1] COLON            ":"
 JES.Token: [Pos:    11, Len:   4] NULL             "null"
    + JES.Node: [2] "null" <NULL_VALUE>,    parent:[1], right:[-1], child:[-1]
 JES.Token: [Pos:    15, Len:   1] COMMA            ","
 JES.Token: [Pos:    23, Len:   2] STRING           "ID"
    + JES.Node: [3] "ID" <KEY>,    parent:[0], right:[-1], child:[-1]
```
