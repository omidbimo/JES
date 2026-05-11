#ifndef JES_H
#define JES_H

#include <stdbool.h>
#include <stdint.h>

/* =========================================================================
 * Compile-time configuration
 * ========================================================================= */

/**
 * JES_USE_32BIT_NODE_DESCRIPTOR
 *
 * Enables 32-bit node descriptors instead of 16-bit (default).
 *
 * Addressing capacity:
 * - 16-bit: Up to 65,535 nodes (2^16 - 1), reserving 0xFFFF for JES_INVALID_INDEX or NULL.
 * - 32-bit: Up to ~4.29 billion nodes (2^32 - 1), reserving 0xFFFFFFFF for JES_INVALID_INDEX or NULL.
 *
 * Memory impact:
 * - 32-bit descriptors double memory usage for node references (parent, first_child, last_child, sibling).
 */
//#define JES_USE_32BIT_NODE_DESCRIPTOR

/**
 * JES_WORKSPACE_NODE_POOL_PERCENT
 *
 * When using hash table search mode (JES_SEARCH_HASHED), the workspace is
 * partitioned between the node pool and hash table. By default, 75% of the
 * available workspace is allocated to the node pool, with the remaining 25%
 * reserved for the hash table.
 *
 * Increasing the node pool percentage reduces hash table size, which may
 * degrade lookup performance but allows storing more JSON elements.
 * Decreasing it enlarges the hash table for faster lookups but reduces
 * storage capacity.
 *
 * Recommended range: 70-80%
 *
 * This can be overridden at compile time:
 * @code
 * gcc -DJES_WORKSPACE_NODE_POOL_PERCENT=80 ...
 * @endcode
 *
 * Note: When using JES_SEARCH_LINEAR mode, 100% of workspace goes to node pool (and jes_context).
 */
#ifndef JES_WORKSPACE_NODE_POOL_PERCENT
  #define JES_WORKSPACE_NODE_POOL_PERCENT   75
#endif

/**
 * JES_MAX_PATH_LENGTH
 *
 * Maximum allowed path length (in bytes) when searching a key.
 * Default: 512 bytes.
 */
#define JES_MAX_PATH_LENGTH 512

/**
 * JES_DEFAULT_PATH_SEPARATOR
 *
 * Default path separator when searching a key. Can be changed at runtime
 * via jes_set_path_separator().
 */
#define JES_DEFAULT_PATH_SEPARATOR '.'

/**
 * JES_TAB_SIZE
 *
 * Indentation width (in spaces) used when pretty-printing JSON.
 */
#define JES_TAB_SIZE 2

/* Logging output control in debug mode */
#define JES_ENABLE_TOKEN_LOG
#define JES_ENABLE_PARSER_NODE_LOG
//#define JES_ENABLE_PARSER_STATE_LOG
//#define JES_ENABLE_SERIALIZER_NODE_LOG
//#define JES_ENABLE_SERIALIZER_STATE_LOG

#define JES_STREAMING_SERIALIZER_MAX_DEPTH  8

/* =========================================================================
 * Internal size constants (platform-dependent)
 * ========================================================================= */

#if __SIZEOF_POINTER__ == 4
  #define JES_CONTEXT_SIZE  128
  #ifdef JES_USE_32BIT_NODE_DESCRIPTOR
    #define JES_NODE_SIZE     24
  #else
    #define JES_NODE_SIZE     16
  #endif
#else
  #define JES_CONTEXT_SIZE  248
  #ifdef JES_USE_32BIT_NODE_DESCRIPTOR
    #define JES_NODE_SIZE     28
  #else
    #define JES_NODE_SIZE     16
  #endif
#endif

/**
 * JES_REQUIRED_SIZE(node_count)
 *
 * Calculates the minimum workspace buffer size in bytes needed to hold
 * a context and the given number of nodes.
 *
 * @param node_count Expected maximum number of JSON nodes (elements).
 *
 * Example:
 * @code
 * uint8_t workspace[JES_REQUIRED_SIZE(128)];
 * struct jes_context* ctx = jes_init(workspace, sizeof(workspace), JES_SEARCH_LINEAR);
 * @endcode
 */
#define JES_REQUIRED_SIZE(node_count) \
    (JES_CONTEXT_SIZE + (node_count * JES_NODE_SIZE))

/**
 * JES_STREAMING_SERIALIZER_REQUIRED_STACK_SIZE
 *
 * Calculates the size required by the streaming serializer stack to support the
 * JES_STREAMING_SERIALIZER_MAX_DEPTH
 *
 * Example:
 * @code
 * uint8_t out[1024];
 * static uint8_t stack[JES_STREAMING_SERIALIZER_REQUIRED_STACK_SIZE];
 * struct jes_streaming_serializer_context *ss_ctx = jes_init_streaming(
 *                            ss_ctx, out, sizeof(out), stack, sizeof(stack) );
 * @endcode
 */
#define JES_STREAMING_SERIALIZER_REQUIRED_SIZE \
    ((JES_STREAMING_SERIALIZER_MAX_DEPTH * 4) + 24)

/* =========================================================================
 * Enumerations
 * ========================================================================= */

/**
 * Key search mode, selected at jes_init() time.
 */
enum jes_search_mode {
  JES_SEARCH_LINEAR = 0, /* Linear key search, O(n) performance */
  JES_SEARCH_HASHED,     /* Hash table key search, O(1) performance */
};

/**
 * Status codes returned by most JES API functions.
 */
typedef enum jes_status {
  JES_NO_ERROR = 0,
  JES_UNEXPECTED_SYMBOL,      /* Tokenizer error */
  JES_INVALID_UNICODE,        /* Tokenizer error */
  JES_INVALID_NUMBER,         /* Tokenizer error */
  JES_INVALID_ESCAPED_SYMBOL, /* Tokenizer error */
  JES_UNEXPECTED_EOF,         /* Tokenizer, parser and serializer error */
  JES_OUT_OF_MEMORY,          /* Parser error: workspace exhausted */
  JES_UNEXPECTED_TOKEN,       /* Parser error */
  JES_UNEXPECTED_STATE,       /* Parser and Serializer error */
  JES_UNEXPECTED_ELEMENT,     /* Serializer and Tree management error */
  JES_BUFFER_TOO_SMALL,       /* Serializer error: output buffer too small */
  JES_INVALID_PARAMETER,      /* API error: bad argument */
  JES_ELEMENT_NOT_FOUND,      /* API error: key or path not found */
  JES_INVALID_CONTEXT,        /* API error: context not properly initialized */
  JES_BROKEN_TREE,            /* API and Tree management error */
  JES_DUPLICATE_KEY,          /* Tree management error: key already exists */
  JES_INVALID_OPERATION,      /* API error: operation not valid in current state */
  JES_PATH_TOO_LONG,          /* API error: path exceeds JES_MAX_PATH_LENGTH */
  JES_RENDER_FAILED,
  JES_MAX_DEPTH_EXCEEDED,
} jes_status;

/**
 * Token types produced by the tokenizer (used in jes_status_block for diagnostics).
 */
enum jes_token_type {
  JES_TOKEN_EOF = 0,
  JES_TOKEN_OPENING_BRACE,   /* { */
  JES_TOKEN_CLOSING_BRACE,   /* } */
  JES_TOKEN_OPENING_BRACKET, /* [ */
  JES_TOKEN_CLOSING_BRACKET, /* ] */
  JES_TOKEN_COLON,
  JES_TOKEN_COMMA,
  JES_TOKEN_STRING,
  JES_TOKEN_NUMBER,
  JES_TOKEN_TRUE,
  JES_TOKEN_FALSE,
  JES_TOKEN_NULL,
  JES_TOKEN_INVALID,
};

/**
 * JSON element types.
 */
enum jes_type {
  JES_UNKNOWN = 0,
  JES_OBJECT,
  JES_KEY,
  JES_ARRAY,
  JES_STRING,
  JES_NUMBER,
  JES_TRUE,
  JES_FALSE,
  JES_NULL,
};

/* =========================================================================
 * Data structures
 * ========================================================================= */

/* Forward declaration — jes_context is an opaque type */
struct jes_context;

/* Forward declaration — jes_streaming_serializer_context is an opaque type
 * Context for streaming (tree-less) JSON serialization.
 */
struct jes_streaming_serializer_context;

/**
 * Represents a single JSON element.
 *
 * The value pointer references the original JSON buffer directly — it is
 * NOT null-terminated and NOT copied. The source buffer must remain valid
 * for the lifetime of the context.
 */
struct jes_element {
  uint16_t    type;    /* Element type (see jes_type) */
  uint16_t    length;  /* Length of value in bytes */
  const char* value;   /* Pointer into the original JSON buffer */
};

/**
 * Element count summary, broken down by type.
 * Returned by jes_get_stat().
 */
struct jes_stat {
  size_t objects; /* Number of OBJECT elements */
  size_t keys;    /* Number of KEY elements */
  size_t arrays;  /* Number of ARRAY elements */
  size_t values;  /* Number of value elements (STRING, NUMBER, TRUE, FALSE, NULL) */
};

/**
 * Memory allocation breakdown for a JES workspace.
 * Returned by jes_get_workspace_stat().
 */
struct jes_workspace_stat {
    size_t workspace_size;        /* Total bytes provided as workspace */
    size_t context_size;          /* Bytes used by the JES context */
    size_t node_mng_size;         /* Bytes used by the node management module */
    size_t node_mng_capacity;     /* Maximum number of nodes */
    size_t node_mng_node_count;   /* Currently allocated nodes */
    size_t hash_table_size;       /* Bytes used by the hash table (0 if disabled) */
    size_t hash_table_capacity;   /* Maximum number of hash entries */
    size_t hash_table_entry_count;/* Currently allocated hash entries */
};

/**
 * Detailed diagnostic snapshot from the last parser or serializer operation.
 * Returned by jes_get_status_block(). Useful when jes_get_status() alone
 * is insufficient to diagnose an error.
 */
struct jes_status_block {
  enum jes_status    status;       /* Status of the last operation */
  enum jes_token_type token_type;  /* Last processed token type */
  enum jes_type      element_type; /* Last processed element type */
  size_t             cursor_line;  /* Line number in the JSON document */
  size_t             cursor_pos;   /* Column position in the JSON document */
};

/* =========================================================================
 * Context setup
 * ========================================================================= */

/**
 * Initializes a JES context within the provided workspace buffer.
 *
 * The buffer is partitioned to hold the context, node pool, and optionally
 * a hash table depending on the search mode.
 *
 * @param buffer      Pointer to caller-owned workspace memory. Must be properly aligned.
 * @param buffer_size Size of the buffer in bytes.
 *                    Minimum: JES_REQUIRED_SIZE(expected_node_count).
 * @param mode        Key search strategy:
 *                    - JES_SEARCH_LINEAR: entire buffer used for node pool; O(n) lookups.
 *                      Best for objects with fewer than ~50 keys or memory-constrained targets.
 *                    - JES_SEARCH_HASHED: buffer split between nodes and hash table; O(1) lookups.
 * @return Initialized context pointer on success, NULL if the buffer is NULL,
 *         too small, or initialization fails.
 *
 * @note Buffer ownership stays with the caller. Keep the buffer alive and
 *       unmodified for the entire lifetime of the context.
 */
struct jes_context* jes_init(void* buffer, size_t buffer_size, enum jes_search_mode mode);

/**
 * Initializes a streaming serializer context.
 *
 * The streaming serializer writes JSON directly to an output buffer without
 * building an internal tree. Use the jes_render_*() family of functions after
 * initialization to emit JSON elements.
 *
 * @param workspace User-provided memory used as an internal workspace. Use
 *        JES_STREAMING_SERIALIZER_REQUIRED_SIZE to allocate the buffer.
 * @param workspace_size  Size of the workspace buffer in bytes.
 * @param output      Output buffer to write the JSON string.
 * @param output_size Size of the output buffer in bytes.
 *
 * @return Initialized Streaming context pointer or NULL on failure.
 */
struct jes_streaming_serializer_context* jes_init_streaming(
                                      void* workspace, size_t workspace_size,
                                      char* output, size_t output_size);

/**
 * Resets the context, clearing the JSON tree and internal state.
 * The workspace buffer is retained and can be reused immediately.
 *
 * @param ctx JES context.
 * @return JES_NO_ERROR on success.
 */
jes_status jes_reset(struct jes_context* ctx);

/* =========================================================================
 * Size queries
 * ========================================================================= */

/**
 * Returns the size in bytes of the jes_context structure for the current build.
 * Use this (or JES_REQUIRED_SIZE) to size the workspace buffer.
 */
size_t jes_context_size(void);

/**
 * Returns the size of a single JSON node in bytes.
 * Each node stores one JSON element plus tree-linkage overhead.
 */
size_t jes_node_size(void);

/**
 * JES_REQUIRED_BUFFER_SIZE(nodes_count)
 *
 * Runtime equivalent of JES_REQUIRED_SIZE using the actual sizes returned by
 * jes_context_size() and jes_node_size(). Prefer this when buffer sizing
 * must account for run-time build configuration.
 */
#define JES_REQUIRED_BUFFER_SIZE(nodes_count) \
    (jes_context_size() + \
    (nodes_count) * jes_node_size() )

/* =========================================================================
 * Parse & serialize
 * ========================================================================= */

/**
 * Parses JSON text into an internal element tree.
 *
 * The input buffer does not need to be null-terminated.
 * The context references the json_data directly — the data must be valid and
 * unchanged for the lifetime of the generated tree.
 *
 * @param ctx         JES context.
 * @param json_data   Pointer to JSON text.(does not need to be null-terminated.)
 * @param json_length Length of JSON text in bytes.
 * @return JES_NO_ERROR on success, or a tokenizer/parser status code on failure.
 *         Call jes_get_status_block() for line and column details on error.
 */
jes_status jes_load(struct jes_context* ctx, const char* json_data, size_t json_length);

/**
 * Evaluates the JSON tree then calculates the buffer size required to serialize
 * the current JSON tree.
 *
 * Call this optionally to allocate an appropriately sized output buffer.
 *
 * @param ctx     JES context.
 * @param compact If true, calculates size for compact JSON; false for pretty-printed.
 * @return Required byte count including the null terminator, or 0 on error.
 */
size_t jes_evaluate(struct jes_context *ctx, bool compact);

/**
 * Serializes the JSON tree into a caller-provided buffer.
 *
 * @param ctx           JES context.
 * @param buffer        Output buffer.
 * @param buffer_length Size of the output buffer in bytes.
 * @param compact       If true, writes compact JSON; false writes pretty-printed JSON
 *                      indented by JES_TAB_SIZE spaces.
 * @return Number of bytes written (including null terminator), or 0 on failure.
 *         Call jes_get_status() to retrieve the error code on failure..
 */
size_t jes_render(struct jes_context* ctx, char* buffer, size_t buffer_length, bool compact);

/* =========================================================================
 * Error handling
 * ========================================================================= */

/**
 * Returns the status code of the last API operation.
 *
 * @param ctx JES context.
 * @return Last status code.
 */
jes_status jes_get_status(struct jes_context* ctx);

/**
 * Returns a detailed diagnostic snapshot of the last parser or serializer operation.
 *
 * Includes the error code, last token type, element type, and the line/column
 * position in the JSON document where processing stopped. Use this when
 * jes_get_status() alone is not enough to locate an error.
 *
 * @param ctx JES context.
 * @return jes_status_block with diagnostic details.
 */
struct jes_status_block jes_get_status_block(struct jes_context* ctx);

/* =========================================================================
 * Capacity & diagnostics
 * ========================================================================= */

/**
 * Returns the number of elements currently allocated in the JSON tree.
 *
 * @param ctx JES context.
 */
size_t jes_get_element_count(struct jes_context* ctx);

/**
 * Returns the maximum number of elements the workspace can hold.
 *
 * @param ctx JES context.
 */
size_t jes_get_element_capacity(struct jes_context* ctx);

/**
 * Returns the total workspace size in bytes.
 *
 * @param ctx JES context.
 */
size_t jes_get_workspace_size(struct jes_context *ctx);

/**
 * Returns an element count summary categorized by type.
 * Useful for diagnostics and validation.
 *
 * @param ctx JES context.
 * @return jes_stat with counts per element type.
 */
struct jes_stat jes_get_stat(struct jes_context* ctx);

/**
 * Returns a memory breakdown of how the workspace is partitioned.
 *
 * Shows bytes and capacity for the context, node pool, and hash table.
 *
 * @param ctx JES context.
 * @return jes_workspace_stat with allocation details.
 */
struct jes_workspace_stat jes_get_workspace_stat(struct jes_context* ctx);

/* =========================================================================
 * Tree navigation
 * ========================================================================= */

/**
 * Returns the root element of the JSON tree.
 *
 * @param ctx JES context.
 * @return Root element, or NULL if the tree is empty.
 */
struct jes_element* jes_get_root(struct jes_context* ctx);

/**
 * Returns the parent element of the given element.
 *
 * @param ctx     JES context.
 * @param element Target element.
 * @return Parent element, or NULL if element is the root.
 */
struct jes_element* jes_get_parent(struct jes_context* ctx, struct jes_element* element);

/**
 * Returns the first child of the given element.
 *
 * @param ctx     JES context.
 * @param element Target element (JES_OBJECT, JES_ARRAY, or JES_KEY).
 * @return First child element, or NULL if none.
 */
struct jes_element* jes_get_child(struct jes_context* ctx, struct jes_element* element);

/**
 * Returns the right sibling of the given element.
 *
 * @param ctx     JES context.
 * @param element Target element.
 * @return Next sibling element, or NULL if there is none.
 */
struct jes_element* jes_get_sibling(struct jes_context* ctx, struct jes_element* element);

/* =========================================================================
 * Key lookup
 * ========================================================================= */

/**
 * Sets the path separator used by jes_get_key() and jes_get_value().
 * Defaults to JES_DEFAULT_PATH_SEPARATOR ('.').
 *
 * @param ctx       JES context.
 * @param separator New separator character.
 */
void jes_set_path_separator(struct jes_context* ctx, char separator);

/**
 * Searches for a nested key by [dot]-separated path.
 *
 * The path must be null-terminated. The separator can be changed with
 * jes_set_path_separator(). Searches start from parent, which may be
 * the root object or any intermediate key.
 *
 * @param ctx    JES context.
 * @param parent Starting JES_OBJECT or JES_KEY element.
 * @param path   Null-terminated, separator-delimited key path (e.g. "a.b.c").
 * @return Found JES_KEY element, or NULL if not found.
 */
struct jes_element* jes_get_key(struct jes_context* ctx, struct jes_element* parent, const char* path);

/**
 * Returns the value element associated with a JES_KEY element.
 *
 * @param ctx JES context.
 * @param key A JES_KEY element.
 * @return Value element (JES_OBJECT, JES_ARRAY, JES_STRING, etc.), or NULL.
 */
struct jes_element* jes_get_key_value(struct jes_context* ctx, struct jes_element* key);

/**
 * Returns the value element at the given path, starting from parent.
 * Equivalent to calling jes_get_key() followed by jes_get_key_value().
 *
 * @param ctx    JES context.
 * @param parent Starting JES_OBJECT or JES_KEY element.
 * @param path   Null-terminated, separator-delimited key path.
 * @return Value element, or NULL if the path is not found.
 */
struct jes_element* jes_get_value(struct jes_context* ctx, struct jes_element* parent, const char* path);

/* =========================================================================
 * Key operations
 * ========================================================================= */

/**
 * Appends a new key to a JES_OBJECT.
 *
 * @param ctx            JES context.
 * @param parent         Target JES_OBJECT element.
 * @param keyword        Key name (need not be null-terminated).
 * @param keyword_length Length of keyword in bytes.
 * @return New JES_KEY element, or NULL on failure (e.g. JES_OUT_OF_MEMORY, JES_DUPLICATE_KEY).
 */
struct jes_element* jes_add_key(struct jes_context* ctx, struct jes_element* parent, const char* keyword, size_t keyword_length);

/**
 * Inserts a new key immediately before an existing key.
 *
 * @param ctx            JES context.
 * @param key            Reference JES_KEY element; new key is inserted before this one.
 * @param keyword        Key name.
 * @param keyword_length Length of keyword in bytes.
 * @return New JES_KEY element, or NULL on failure.
 */
struct jes_element* jes_add_key_before(struct jes_context* ctx, struct jes_element* key, const char* keyword, size_t keyword_length);

/**
 * Inserts a new key immediately after an existing key.
 *
 * @param ctx            JES context.
 * @param key            Reference JES_KEY element; new key is inserted after this one.
 * @param keyword        Key name.
 * @param keyword_length Length of keyword in bytes.
 * @return New JES_KEY element, or NULL on failure.
 */
struct jes_element* jes_add_key_after(struct jes_context* ctx, struct jes_element* key, const char* keyword, size_t keyword_length);

/**
 * Sets or replaces the value of a key.
 *
 * @param ctx          JES context.
 * @param key          Target JES_KEY element.
 * @param type         New value type (e.g. JES_STRING, JES_NUMBER, JES_OBJECT, ...).
 * @param value        Value data (need not be null-terminated). Pass NULL for JES_OBJECT/JES_ARRAY/JES_TRUE/JES_FALSE/JES_NULL.
 * @param value_length Length of value in bytes.
 * @return Updated value element, or NULL on failure.
 */
struct jes_element* jes_update_key_value(struct jes_context* ctx, struct jes_element* key, enum jes_type type, const char* value, size_t value_length);

/**
 * Converts a key's value to an empty object {}.
 * Any existing value or children are replaced.
 *
 * @param ctx JES context.
 * @param key Target JES_KEY element.
 * @return New JES_OBJECT element, or NULL on failure.
 */
struct jes_element* jes_update_key_value_to_object(struct jes_context* ctx, struct jes_element* key);

/**
 * Converts a key's value to an empty array [].
 * Any existing value or children are replaced.
 *
 * @param ctx JES context.
 * @param key Target JES_KEY element.
 * @return New JES_ARRAY element, or NULL on failure.
 */
struct jes_element* jes_update_key_value_to_array(struct jes_context* ctx, struct jes_element* key);

/**
 * Sets a key's value to true.
 * Any existing value or children are replaced.
 *
 * @param ctx JES context.
 * @param key Target JES_KEY element.
 * @return Updated element, or NULL on failure.
 */
struct jes_element* jes_update_key_value_to_true(struct jes_context* ctx, struct jes_element* key);

/**
 * Sets a key's value to false.
 * Any existing value or children are replaced.
 *
 * @param ctx JES context.
 * @param key Target JES_KEY element.
 * @return Updated element, or NULL on failure.
 */
struct jes_element* jes_update_key_value_to_false(struct jes_context* ctx, struct jes_element* key);

/**
 * Sets a key's value to null.
 * Any existing value or children are replaced.
 *
 * @param ctx JES context.
 * @param key Target JES_KEY element.
 * @return Updated element, or NULL on failure.
 */
struct jes_element* jes_update_key_value_to_null(struct jes_context* ctx, struct jes_element* key);

/* =========================================================================
 * Array operations
 * ========================================================================= */

/**
 * Returns the number of elements in an array.
 * The function iterates all array elements to count them and has a o(n) performance.
 *
 * @param ctx   JES context.
 * @param array A JES_ARRAY element.
 * @return Element count, or 0 if the array is empty or invalid.
 */
size_t jes_get_array_size(struct jes_context* ctx, struct jes_element* array);

/**
 * Returns the element at the given index in an array (0-based).
 *
 * @param ctx   JES context.
 * @param array A JES_ARRAY element.
 * @param index Zero-based index.
 * @return Element at index, or NULL if out of range.
 */
struct jes_element* jes_get_array_value(struct jes_context* ctx, struct jes_element* array, int32_t index);

/**
 * Appends a new value at the end of an array.
 *
 * @param ctx          JES context.
 * @param array        Target JES_ARRAY element.
 * @param type         Value type.
 * @param value        Value data. Pass NULL for JES_OBJECT/JES_ARRAY/JES_TRUE/JES_FALSE/JES_NULL.
 * @param value_length Length of value in bytes.
 * @return New element, or NULL on failure.
 */
struct jes_element* jes_append_array_value(struct jes_context* ctx, struct jes_element* array, enum jes_type type, const char* value, size_t value_length);

/**
 * Inserts a new value at the given index in an array, shifting subsequent elements.
 *
 * @param ctx          JES context.
 * @param array        Target JES_ARRAY element.
 * @param index        Insertion index (0-based).
 * @param type         Value type.
 * @param value        Value data.
 * @param value_length Length of value in bytes.
 * @return New element, or NULL on failure.
 */
struct jes_element* jes_add_array_value(struct jes_context* ctx, struct jes_element* array, int32_t index, enum jes_type type, const char *value, size_t value_length);

/**
 * Replaces the value at the given index in an array.
 *
 * @param ctx          JES context.
 * @param array        Target JES_ARRAY element.
 * @param index        Zero-based index of the element to update.
 * @param type         New value type.
 * @param value        New value data.
 * @param value_length Length of value in bytes.
 * @return Updated element, or NULL on failure.
 */
struct jes_element* jes_update_array_value(struct jes_context* ctx, struct jes_element* array, int32_t index, enum jes_type type, const char* value, size_t value_length);

/* =========================================================================
 * Generic element operations
 * ========================================================================= */

/**
 * Appends a child element to a parent (JES_OBJECT, JES_ARRAY, or JES_KEY).
 *
 * Lower-level than the key/array helpers. Prefer jes_add_key() or
 * jes_append_array_value() for typed operations.
 *
 * @param ctx          JES context.
 * @param parent       Parent element.
 * @param type         Child element type.
 * @param value        Value data. Pass NULL for container/boolean/null types.
 * @param value_length Length of value in bytes.
 * @return New element, or NULL on failure.
 */
struct jes_element* jes_add_element(struct jes_context* ctx, struct jes_element* parent, enum jes_type type, const char* value, size_t value_length);

/**
 * Deletes an element and all of its children from the tree.
 *
 * @param ctx     JES context.
 * @param element Element to delete.
 * @return JES_NO_ERROR on success.
 */
jes_status jes_delete_element(struct jes_context* ctx, struct jes_element* element);

/* =========================================================================
 * Streaming serialization
 * ========================================================================= */

/**
 * Begins a JSON object block '{'.
 * Must be paired with jes_render_object_end().
 *
 * @param ctx Streaming serializer context.
 * @return JES_NO_ERROR on success.
 */
jes_status jes_render_object_start(struct jes_streaming_serializer_context* ctx);

/**
 * Closes a JSON object block '}'.
 *
 * @param ctx Streaming serializer context.
 * @return JES_NO_ERROR on success.
 */
jes_status jes_render_object_end(struct jes_streaming_serializer_context* ctx);

/**
 * Begins a JSON array block '['.
 * Must be paired with jes_render_array_end().
 *
 * @param ctx Streaming serializer context.
 * @return JES_NO_ERROR on success.
 */
jes_status jes_render_array_start(struct jes_streaming_serializer_context* ctx);

/**
 * Closes a JSON array block ']'.
 *
 * @param ctx Streaming serializer context.
 * @return JES_NO_ERROR on success.
 */
jes_status jes_render_array_end(struct jes_streaming_serializer_context* ctx);

/**
 * Writes a JSON object key (quoted string followed by ':').
 * Must be followed by a value call within an object block.
 *
 * @param ctx    Streaming serializer context.
 * @param key    Key string (need not be null-terminated).
 * @param length Length of key in bytes.
 * @return JES_NO_ERROR on success.
 */
jes_status jes_render_key(struct jes_streaming_serializer_context* ctx, const char* key, size_t length);

/**
 * Writes a signed 32-bit integer value.
 *
 * @param ctx   Streaming serializer context.
 * @param value Value to write.
 * @return JES_NO_ERROR on success.
 */
jes_status jes_render_int32(struct jes_streaming_serializer_context* ctx, int32_t value);

/**
 * Writes a signed 64-bit integer value.
 *
 * @param ctx   Streaming serializer context.
 * @param value Value to write.
 * @return JES_NO_ERROR on success.
 */
jes_status jes_render_int64(struct jes_streaming_serializer_context* ctx, int64_t value);

/**
 * Writes an unsigned 32-bit integer value.
 *
 * @param ctx   Streaming serializer context.
 * @param value Value to write.
 * @return JES_NO_ERROR on success.
 */
jes_status jes_render_uint32(struct jes_streaming_serializer_context* ctx, uint32_t value);

/**
 * Writes an unsigned 64-bit integer value.
 *
 * @param ctx   Streaming serializer context.
 * @param value Value to write.
 * @return JES_NO_ERROR on success.
 */
jes_status jes_render_uint64(struct jes_streaming_serializer_context* ctx, uint64_t value);

/**
 * Writes a double-precision floating-point value.
 *
 * @param ctx   Streaming serializer context.
 * @param value Value to write.
 * @return JES_NO_ERROR on success.
 */
jes_status jes_render_double(struct jes_streaming_serializer_context* ctx, double value);

/**
 * Writes a quoted JSON string value.
 *
 * @param ctx    Streaming serializer context.
 * @param string String data (need not be null-terminated).
 * @param length Length of string in bytes.
 * @return JES_NO_ERROR on success.
 */
jes_status jes_render_string(struct jes_streaming_serializer_context* ctx, const char* string, size_t length);

/**
 * Writes the JSON literal null.
 *
 * @param ctx Streaming serializer context.
 * @return JES_NO_ERROR on success.
 */
jes_status jes_render_null(struct jes_streaming_serializer_context* ctx);

/**
 * Writes the JSON literal true.
 *
 * @param ctx Streaming serializer context.
 * @return JES_NO_ERROR on success.
 */
jes_status jes_render_true(struct jes_streaming_serializer_context* ctx);

/**
 * Writes the JSON literal false.
 *
 * @param ctx Streaming serializer context.
 * @return JES_NO_ERROR on success.
 */
jes_status jes_render_false(struct jes_streaming_serializer_context* ctx);

/* =========================================================================
 * Iteration macros
 * ========================================================================= */

/**
 * JES_FOR_EACH(ctx, elem, type)
 *
 * Iterates over the children of elem that match the given type.
 * elem must already point to the parent element of the desired type.
 *
 * Example — iterate keys of an object:
 * @code
 * struct jes_element* obj = jes_get_root(ctx);
 * struct jes_element* elem = jes_get_child(ctx, obj);
 * JES_FOR_EACH(ctx, elem, JES_OBJECT) {
 *     // elem is each child
 * }
 * @endcode
 */
#define JES_FOR_EACH(ctx_, elem_, type_) \
    for (elem_ = (elem_->type == type_) ? jes_get_child(ctx_, elem_) : NULL; \
         elem_ != NULL; \
         elem_ = jes_get_sibling(ctx_, elem_))

/**
 * JES_ARRAY_FOR_EACH(ctx, array, iter)
 *
 * Iterates over all elements in a JES_ARRAY.
 *
 * Example:
 * @code
 * struct jes_element* iter;
 * JES_ARRAY_FOR_EACH(ctx, array_elem, iter) {
 *     // iter is each array element
 * }
 * @endcode
 */
#define JES_ARRAY_FOR_EACH(ctx_, array_, iter_) \
    for (iter_ = (array_->type == JES_ARRAY) ? jes_get_child(ctx_, array_) : NULL; \
         iter_ != NULL; \
         iter_ = jes_get_sibling(ctx_, iter_))

/**
 * JES_FOR_EACH_KEY(ctx, object, iter)
 *
 * Iterates over all JES_KEY children of a JES_OBJECT.
 *
 * Example:
 * @code
 * struct jes_element* iter;
 * JES_FOR_EACH_KEY(ctx, root, iter) {
 *     // iter is each key element
 * }
 * @endcode
 */
#define JES_FOR_EACH_KEY(ctx_, object_, iter_) \
    for (iter_ = (object_->type == JES_OBJECT) ? jes_get_child(ctx_, object_) : NULL; \
         iter_ != NULL && iter_->type == JES_KEY; \
         iter_ = jes_get_sibling(ctx_, iter_))

#endif /* JES_H */
