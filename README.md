# JSON for Embedded Systems (JES)

**JES** is a lightweight JSON library designed specifically for embedded systems with strict resource constraints, such as limited stack size and no dynamic memory allocation. The goal is to make JES fully conform to the [RFC 8259 JSON standard](https://tools.ietf.org/html/rfc8259).

## Overview

JES provides a simple and efficient API to:  

- Parse a JSON string into a tree of JSON elements.  
- Modify the elements in place.  
- Serialize the tree back into a JSON string.  

## Key Features

- ✅ **No dynamic memory allocation** – All objects are stored in a single working buffer, ensuring predictable memory usage.  
- ✅ **Non-recursive parser** – Designed to handle deep JSON structures without relying on recursion, making it suitable for environments with limited stack size.  
- ✅ **Concurrent document handling** – Supports parsing multiple JSON documents simultaneously by using a separate parser context and working buffer for each document.  
- ✅ **High performance** – Operates directly on the input buffer without copying data, ensuring minimal processing overhead.  
- ✅ **No external dependencies** – Fully self-contained, making it easy to integrate into embedded projects.  
- ✅ **Configurable key handling** – Can be configured at compile time to support or overwrite duplicate keys.  
- ✅ **Flexible output formatting** – The output can be generated as a compact string or formatted with indentation for readability.

## 

## API Documentation

### Enums

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

---

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

### Structs

### `jes_context`
An opaque structure that holds the internal state of the parser including JSON tree information, element pool managemnet and process status. 

### `jes_element`

Represents a JSON element in the tree.

| Field         | Type                  | Description                       |
| ------------- | --------------------- | --------------------------------- |
| `type`        | `uint16_t`            | Type of element (see `jes_type`). |
| `length`      | `uint16_t`            | Length of the value.              |
| `value`       | `const char *`        | Pointer to the value.             |
| `parent`      | `jes_node_descriptor` | Index of the parent node.         |
| `sibling`     | `jes_node_descriptor` | Index of the next sibling.        |
| `first_child` | `jes_node_descriptor` | Index of the first child node.    |
| `last_child`  | `jes_node_descriptor` | Index of the last child node.     |

---

### Functions

### `jes_init`

Initializes a new JES context.

```c
struct jes_context* jes_init(void *buffer, uint32_t buffer_size);
```

**Parameters**

- `buffer` – The working buffer to hold the context and JSON tree nodes.

- `buffer_size` – Size of the working buffer.

**Returns**  
Pointer to context or `NULL` in case of failure.

---

### `jes_load`

Loads a JSON string and creates a tree of JSON elements.

```c
uint32_t jes_load(struct jes_context* ctx, const char *json_data, uint32_t json_length);
```

**Parameters**

- `ctx` – Initialized JES context.

- `json_data` – JSON data string.

- `json_length` – Length of the JSON data.

**Returns**  
Status of the parsing process (see `jes_status`)

---

### `jes_render`

Renders a JSON tree into a string.

```c
uint32_t jes_render(struct jes_context *ctx, char *dst, uint32_t length, bool compact);
```

**Parameters**

- `ctx` – Initialized JES context.

- `dst` – Destination buffer.

- `length` – Size of the destination buffer.

- `compact` – If `true`, renders a compact JSON string; otherwise, formatted with indentation.

**Returns**  
Size of the JSON string or `0` if rendering failed. (use jes_get_status for the failure code)

---

### `jes_evaluate`

Evaluates the JSON tree structure and calculates the size of the rendered string.

```c
uint32_t jes_evaluate(struct jes_context *ctx, bool compact);
```

**Parameters**

- `ctx` – Initialized JES context.

- `compact` – If `true`, evaluates size of compact JSON string; otherwise, formatted JSON size.

**Returns**  
Size of JSON string or `0` if evaluation fails. (use jes_get_status for the failure code)

--- 

### `jes_get_status`

Gets the status of the last process.

```c
`jes_status jes_get_status(struct jes_context *ctx);
```

**Parameters**

- `ctx` – Initialized JES context.

**Returns**  
Status of the last process stored in the context.

---

### `jes_get_root`

Returns the root element of the JSON tree.

```c
struct jes_element* jes_get_root(struct jes_context *ctx);
```

Gets the root element of the JSON tree. (This should be the lowest level OBJECT)

---

### `jes_get_key`

Returns a key element addressed by a parent KEY element and a sequence of key names.

```c
struct jes_element* jes_get_key(struct jes_context *ctx, struct jes_element *parent_key, const char *keys);
```

**Parameters

- `ctx` – Initialized JES context.

- `parent_key` – A KEY element to start search from or NULL to start the search from root

- `keys` – NUL-terminated string containing several key names separated by a dot "."



---





### `jes_stringify_status`

Converts a status code to a human-readable string.

```c
char* jes_stringify_status(struct jes_context *ctx, char *msg, size_t msg_len);
```
