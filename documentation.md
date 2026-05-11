## Table of Contents

- [Overview](#overview)
- [Compile-time Configuration](#compile-time-configuration)
- [Core Data Structures](#core-data-structures)
- [Functions](#functions)
  - [Initialization](#initialization)
  - [Loading and Rendering](#loading-and-rendering)
  - [Streaming Serialization](#streaming-serialization)
  - [Navigating the JSON Tree](#navigating-the-json-tree)
  - [Working with Objects and Keys](#working-with-objects-and-keys)
  - [Working with Arrays](#working-with-arrays)
  - [Element Manipulation](#element-manipulation)
  - [Statistics and Diagnostics](#statistics-and-diagnostics)
  - [Error Handling](#error-handling)
  - [Iteration Macros](#iteration-macros)
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

/* Default path separator when searching a key. It can be changed at runtime using the API. */
#define JES_DEFAULT_PATH_SEPARATOR '.'

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

### `jes_search_mode`

Defines the preferred key search method

| Search Mode         | Description                                                                                |
| ------------------- | ------------------------------------------------------------------------------------------ |
| `JES_SEARCH_LINEAR` | Linear search with O(n) performance                                                        |
| `JES_SEARCH_HASHED` | Hash Table search with O(1) performance but has more memory overhead and run-time overhead |

### `jes_status`

Defines the possible status codes for JES operations:

| Status Code                  | Description                                      |
| ---------------------------- | ------------------------------------------------ |
| `JES_NO_ERROR`               | No error occurred.                               |
| `JES_UNEXPECTED_SYMBOL`      | Tokenizer error - unexpected character.          |
| `JES_INVALID_UNICODE`        | Tokenizer error - invalid Unicode sequence.      |
| `JES_INVALID_NUMBER`         | Tokenizer error - malformed number.              |
| `JES_INVALID_ESCAPED_SYMBOL` | Tokenizer error - invalid escaped symbol.        |
| `JES_UNEXPECTED_EOF`         | Unexpected end of JSON data.                     |
| `JES_OUT_OF_MEMORY`          | Insufficient memory for operation.               |
| `JES_UNEXPECTED_TOKEN`       | Parser error - unexpected token.                 |
| `JES_UNEXPECTED_STATE`       | Parser/Serializer error - invalid state.         |
| `JES_UNEXPECTED_ELEMENT`     | Serializer/Tree management error.                |
| `JES_BUFFER_TOO_SMALL`       | Serializer error: output buffer too small.       |
| `JES_INVALID_PARAMETER`      | Invalid parameter passed to function.            |
| `JES_ELEMENT_NOT_FOUND`      | Requested JSON element not found.                |
| `JES_INVALID_CONTEXT`        | Context validation failed.                       |
| `JES_BROKEN_TREE`            | Malformed JSON tree detected.                    |
| `JES_DUPLICATE_KEY`          | Duplicate key detected in object.                |
| `JES_INVALID_OPERATION`      | API error: operation not valid in current state. |
| `JES_PATH_TOO_LONG`          | API error: path exceeds `JES_MAX_PATH_LENGTH`.   |
| `JES_RENDER_FAILED`          | Rendering failed.                                |
| `JES_MAX_DEPTH_EXCEEDED`     | Maximum nesting depth exceeded.                  |

### JSON Token Types

### `jes_token_type`

Defines the types of JSON elements:

| Type                        | Description               |
| --------------------------- | ------------------------- |
| `JES_TOKEN_EOF`             | End Of File               |
| `JES_TOKEN_OPENING_BRACE`   | Opening/Left Brace `{`    |
| `JES_TOKEN_CLOSING_BRACE`   | Closing/right Brace `}`   |
| `JES_TOKEN_OPENING_BRACKET` | Opening/left Bracket `[`  |
| `JES_TOKEN_CLOSING_BRACKET` | Closing/right Bracket `]` |
| `JES_TOKEN_COLON`           | Colon `:`                 |
| `JES_TOKEN_COMMA`           | Comma `,`                 |
| `JES_TOKEN_STRING`          | String `""`               |
| `JES_TOKEN_NUMBER`          | Number                    |
| `JES_TOKEN_TRUE`            | Boolean literal `true`    |
| `JES_TOKEN_FALSE`           | Boolean literal `false`   |
| `JES_TOKEN_NULL`            | Literal `null`            |
| `JES_TOKEN_INVALID`         | Unknown/Invalid           |

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

| Field                    | Type     | Description                                   |
| ------------------------ | -------- | --------------------------------------------- |
| `workspace_size`         | `size_t` | Bytes available as workspace                  |
| `context_size`           | `size_t` | Bytes allocated for JES context data          |
| `node_mng_size`          | `size_t` | Bytes dedicated to node management module     |
| `node_mng_capacity`      | `size_t` | Number of total available nodes               |
| `node_mng_node_count`    | `size_t` | Number of allocated nodes                     |
| `hash_table_size`        | `size_t` | Bytes dedicated to hash table (0 if disabled) |
| `hash_table_capacity`    | `size_t` | Number of total available hash entries        |
| `hash_table_entry_count` | `size_t` | Number of allocated hash entries              |

### `jes_status_block`

JES Status Block:

| Field          | Type                  | Description                                      |
| -------------- | --------------------- | ------------------------------------------------ |
| `status`       | `enum jes_status`     | Status form last operation                       |
| `token_type`   | `enum jes_token_type` | Type of the last processed token                 |
| `element_type` | `enum jes_type`       | Type of the last processed element               |
| `cursor_line`  | `size_t`              | The last processed line of the JSON document     |
| `cursor_pos`   | `size_t`              | The last processed position of the JSON document |

### `jes_streaming_serializer_context`

Context for streaming (tree-less) JSON serialization. Must be initialized with `jes_init_streaming()` before use. The streaming serializer writes JSON directly to an output buffer without building an internal tree.

| Field             | Type                    | Description                               |
| ----------------- | ----------------------- | ----------------------------------------- |
| `out_buffer`      | `char*`                 | Output buffer for the rendered JSON       |
| `out_buffer_size` | `size_t`                | Size of `out_buffer` in bytes             |
| `stack`           | `struct jes_container*` | User-provided memory for element tracking |
| `stack_size`      | `size_t`                | Size of the stack buffer in bytes         |
| `stack_top`       | `int`                   | Current stack depth                       |
| `state`           | `unsigned int`          | Internal serializer state                 |

## Functions

### Initialization

### `jes_init`

Initializes a new JES context.

```c
struct jes_context* jes_init(void* buffer, size_t buffer_size, enum jes_search_mode mode);
```

**Parameters**

- `buffer` : Pre-allocated buffer to hold the context and JSON tree nodes
- `buffer_size` : Size of the provided buffer in bytes. Minimum: `JES_REQUIRED_SIZE(expected_node_count)`.
- `mode` : Key search mode (`JES_SEARCH_LINEAR` or `JES_SEARCH_HASHED`)

**Returns** Pointer to the initialized context, or NULL if the buffer is NULL, too small, or initialization fails.

**Note** Buffer ownership stays with the caller. Keep the buffer alive and unmodified for the entire lifetime of the context.

### `jes_init_streaming`

Initializes a streaming serializer context. The streaming serializer writes JSON directly to an output buffer without building an internal tree.

```c
jes_status jes_init_streaming(struct jes_streaming_serializer_context* ctx,
                              char* output, size_t output_size,
                              uint8_t* stack, size_t stack_size);
```

**Parameters**

- `ctx` : Streaming context to initialize
- `output` : Output buffer to write the JSON string
- `output_size` : Size of the output buffer in bytes
- `stack` : User-provided memory used as an element-tracking stack
- `stack_size` : Size of the stack buffer in bytes

**Returns** `JES_NO_ERROR` on success, or an error code on failure.

**Note** Size the stack buffer as `N * sizeof(struct jes_container)` where N is the maximum nesting depth you expect.

### `jes_context_size`

Returns the size in bytes of the `jes_context` structure for the current build. Use this (or `JES_REQUIRED_SIZE`) to size the workspace buffer.

```c
size_t jes_context_size(void);
```

**Returns** Size in bytes of the JES context structure.

### `jes_node_size`

Returns the size of a single JSON node in bytes. Each node stores one JSON element plus tree-linkage overhead.

```c
size_t jes_node_size(void);
```

**Returns** Size in bytes of a single JES node.

### `jes_reset`

Resets a JES context, clearing its internal JSON tree. The workspace buffer is retained and can be reused immediately.

```c
jes_status jes_reset(struct jes_context* ctx);
```

**Parameters**

- `ctx` : JES context to reset

**Returns** `JES_NO_ERROR` on success.

## Loading and Rendering

### `jes_load`

Parse JSON into Tree

```c
jes_status jes_load(struct jes_context* ctx, const char* json_data, size_t json_length);
```

**Parameters**

- `ctx` : Initialized JES context.
- `json_data` : String of JSON data (not necessarily NUL-terminated)
- `json_length` : Length of the JSON data.

**Returns** Status code (JES_NO_ERROR on success) see `jes_status`

### `jes_render`

Serialize the JSON tree into a caller-provided buffer.

```c
size_t jes_render(struct jes_context* ctx, char* buffer, size_t buffer_length, bool compact);
```

**Parameters**

- `ctx` : JES context containing a JSON tree
- `buffer` : Destination buffer for the rendered JSON string
- `buffer_length` : Size of destination buffer in bytes
- `compact` : If true, generates compact JSON; if false, generates formatted JSON with indentation

**Returns** Number of bytes written (including null terminator), or 0 on failure. Call `jes_get_status()` to retrieve the error code on failure.

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

## Streaming Serialization

The streaming serializer writes JSON directly to an output buffer without building an internal tree. Initialize a `jes_streaming_serializer_context` with `jes_init_streaming()`, then call the `jes_render_*()` functions to emit JSON tokens in sequence.

### `jes_render_object_start` / `jes_render_object_end`

Emit the opening `{` and closing `}` of a JSON object.

```c
jes_status jes_render_object_start(struct jes_streaming_serializer_context* ctx);
jes_status jes_render_object_end(struct jes_streaming_serializer_context* ctx);
```

### `jes_render_array_start` / `jes_render_array_end`

Emit the opening `[` and closing `]` of a JSON array.

```c
jes_status jes_render_array_start(struct jes_streaming_serializer_context* ctx);
jes_status jes_render_array_end(struct jes_streaming_serializer_context* ctx);
```

### `jes_render_key`

Emit a JSON object key (quoted string followed by `:`). Must be followed by a value call within an object block.

```c
jes_status jes_render_key(struct jes_streaming_serializer_context* ctx, const char* key, size_t length);
```

**Parameters**

- `ctx` : Streaming serializer context
- `key` : Key string (need not be null-terminated)
- `length` : Length of key in bytes

### `jes_render_int32` / `jes_render_uint32` / `jes_render_int64` / `jes_render_uint64`

Emit an integer value.

```c
jes_status jes_render_int32 (struct jes_streaming_serializer_context* ctx, int32_t  value);
jes_status jes_render_uint32(struct jes_streaming_serializer_context* ctx, uint32_t value);
jes_status jes_render_int64 (struct jes_streaming_serializer_context* ctx, int64_t  value);
jes_status jes_render_uint64(struct jes_streaming_serializer_context* ctx, uint64_t value);
```

### `jes_render_double`

Emit a double-precision floating-point value.

```c
jes_status jes_render_double(struct jes_streaming_serializer_context* ctx, double value);
```

### `jes_render_string`

Emit a quoted JSON string value.

```c
jes_status jes_render_string(struct jes_streaming_serializer_context* ctx, const char* string, size_t length);
```

**Parameters**

- `ctx` : Streaming serializer context
- `string` : String data (need not be null-terminated)
- `length` : Length of string in bytes

### `jes_render_true` / `jes_render_false` / `jes_render_null`

Emit the JSON literals `true`, `false`, or `null`.

```c
jes_status jes_render_true (struct jes_streaming_serializer_context* ctx);
jes_status jes_render_false(struct jes_streaming_serializer_context* ctx);
jes_status jes_render_null (struct jes_streaming_serializer_context* ctx);
```

**All `jes_render_*` functions return** `JES_NO_ERROR` on success, or an error code (e.g. `JES_BUFFER_TOO_SMALL`) on failure.

**Example — streaming a flat object:**

```c
char out[256];
uint8_t stack[16 * sizeof(struct jes_container)];
struct jes_streaming_serializer_context ss;

jes_init_streaming(&ss, out, sizeof(out), stack, sizeof(stack));

jes_render_object_start(&ss);
  jes_render_key(&ss, "sensor", 6); jes_render_string(&ss, "temp", 4);
  jes_render_key(&ss, "value",  5); jes_render_double(&ss, 23.5);
  jes_render_key(&ss, "active", 6); jes_render_true(&ss);
jes_render_object_end(&ss);

/* out now contains: {"sensor":"temp","value":23.5,"active":true} */
printf("%s\n", out);
```

## Navigating the JSON Tree

### `jes_set_path_separator`

Sets path delimiter when searching for a key using a path of keys.

```c
void jes_set_path_separator(struct jes_context* ctx, char separator);
```

**Parameters**

- `ctx` : Initialized JES context
- `separator` : Character to use as path separator (default is `'.'`)

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

### `jes_get_value`

Get a value by path, starting from a parent element. Equivalent to calling `jes_get_key()` followed by `jes_get_key_value()`.

```c
struct jes_element* jes_get_value(struct jes_context* ctx, struct jes_element* parent, const char* path);
```

**Parameters**

- `ctx`: Initialized JES context
- `parent`: Starting `JES_OBJECT` or `JES_KEY` element
- `path`: Null-terminated, separator-delimited key path

**Returns** Value element, or NULL if the path is not found.

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
size_t jes_get_array_size(struct jes_context* ctx, struct jes_element* array);
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

### `jes_get_workspace_size`

Get the total workspace size in bytes.

```c
size_t jes_get_workspace_size(struct jes_context* ctx);
```

**Parameters**

- `ctx`: Initialized JES context

**Returns** Total workspace size in bytes.

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

### `jes_get_status_block`

```c
struct jes_status_block jes_get_status_block(struct jes_context* ctx);
```

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

## Logging

A debug build of JES produces log outputs for Tokenizer, Allocator and Serializer respecting the debug control MACROs .

```dos
JES.Token: [Ln: 1, Col: 2, Pos: 1, Len: 1] OPENING_BRACE    "{"
    + JES.Node: [0] "{" <OBJECT>,    parent:[-1], right:[-1], first_child:[-1], last_child:[-1]
JES.Token: [Ln: 1, Col: 8, Pos: 3, Len: 4] STRING           "name"
    + JES.Node: [1] "name" <KEY>,    parent:[0], right:[-1], first_child:[-1], last_child:[-1]
JES.Token: [Ln: 1, Col: 9, Pos: 8, Len: 1] COLON            ":"
JES.Token: [Ln: 1, Col: 15, Pos: 10, Len: 4] STRING           "John"
    + JES.Node: [2] "John" <STRING>,    parent:[1], right:[-1], first_child:[-1], last_child:[-1]
JES.Token: [Ln: 1, Col: 16, Pos: 15, Len: 1] COMMA            ","
JES.Token: [Ln: 1, Col: 21, Pos: 17, Len: 3] STRING           "age"
    + JES.Node: [3] "age" <KEY>,    parent:[0], right:[-1], first_child:[-1], last_child:[-1]
JES.Token: [Ln: 1, Col: 22, Pos: 21, Len: 1] COLON            ":"
```
