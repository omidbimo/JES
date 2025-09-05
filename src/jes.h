#ifndef JES_H
#define JES_H

#include <stdbool.h>
#include <stdint.h>

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
// #define JES_USE_32BIT_NODE_DESCRIPTOR

/**
 * JES_MAX_PATH_LENGTH
 *
 * Maximum allowed path length (in bytes) when searching a key
 *
 * Default: 512 bytes.
 *
 */
#define JES_MAX_PATH_LENGTH 512

/**
 * JES_ENABLE_KEY_HASHING
 *
 * Enables hash table based key lookup to accelerate access in large JSON objects.
 * Useful for frequent key access in large documents.
 *
 * Note:
 * - Requires more memory for the internal hash table.
 * - In small JSON objects, linear search may be more efficient.
 */
//#define JES_ENABLE_KEY_HASHING

/**
 * JES_ENABLE_FALL_BACK_TO_LINEAR_SEARCH
 *
 * Allows fallback to linear search when node buffer is exhausted, reclaiming hash table memory.
 *
 * Behavior when defined:
 * - On node buffer exhaustion, hash table memory is reclaimed to continue parsing.
 * - After fallback, key lookups become O(n) linear searches.
 * - Hash table entries are discarded and cannot be re-enabled.
 *
 * Behavior when not defined:
 * - Parsing fails when the node buffer is exhausted.
 */
// #define JES_ENABLE_FALL_BACK_TO_LINEAR_SEARCH

/**
 * JES_TAB_SIZE
 *
 * Determines the size of the indention when pretty printing the JSON.
 */
#define JES_TAB_SIZE 2

/* Logging output control in debug mode */
//#define JES_ENABLE_TOKEN_LOG
//#define JES_ENABLE_PARSER_NODE_LOG
//#define JES_ENABLE_PARSER_STATE_LOG
//#define JES_ENABLE_SERIALIZER_NODE_LOG
//#define JES_ENABLE_SERIALIZER_STATE_LOG

typedef enum jes_status {
  JES_NO_ERROR = 0,
  JES_UNEXPECTED_SYMBOL,      /* Tokenizer error */
  JES_INVALID_UNICODE,        /* Tokenizer error */
  JES_INVALID_NUMBER,         /* Tokenizer error */
  JES_INVALID_ESCAPED_SYMBOL, /* Tokenizer error */
  JES_UNEXPECTED_EOF,         /* Tokenizer, parser and serializer error */
  JES_OUT_OF_MEMORY,          /* Parser error */
  JES_UNEXPECTED_TOKEN,       /* Parser error */
  JES_UNEXPECTED_STATE,       /* Parser and Serializer error */
  JES_UNEXPECTED_ELEMENT,     /* Serializer and Tree management error */
  JES_BUFFER_TOO_SMALL,       /* Serializer error */
  JES_INVALID_PARAMETER,      /* API error */
  JES_ELEMENT_NOT_FOUND,      /* API error */
  JES_INVALID_CONTEXT,        /* API error */
  JES_BROKEN_TREE,            /* API and Tree management error */
  JES_DUPLICATE_KEY,          /* Tree management error */
  JES_INVALID_OPERATION,      /* API error */
} jes_status;

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

/**
 * Represents a JSON element with type, length, and value.
 */
struct jes_element {
  uint16_t type;        /* Type of element (see jes_type) */
  uint16_t length;      /* Length of value */
  const char *value;    /* Value pointer (not copied) */
};

/* Forward declaration for internal parser state */
struct jes_context;

struct jes_stat {
  size_t objects; /* Number of OBJECT elements */
  size_t keys;    /* Number of KEY elements */
  size_t arrays;  /* Number of ARRAY elements */
  size_t values;  /* Number of value elements (STRING, NUMBER, TRUE, FALSE, NULL) */
};

/**
 * Memory allocation breakdown for a JES workspace.
 *
 * This structure provides detailed information about how the user-provided
 * workspace buffer is divided among different JES components.
 */
struct jes_workspace_stat {
    size_t workspace_size;      /* Total bytes available as workspace */
    size_t context_size;        /* Bytes allocated for JES context data */
    size_t node_mng_size;       /* Bytes dedicated to the node management module */
    size_t node_mng_capacity;   /* Number of total available nodes. */
    size_t node_mng_node_count; /* Number of allocated nodes. */
    size_t hash_table_size;     /* Bytes dedicated to the hash table module (0 if disabled) */
    size_t hash_table_capacity; /* Number of total available hash entries. */
    size_t hash_table_entry_count; /* Number of allocated hash entries. */
};

struct jes_status_block {
  /* Status form last operation */
  enum jes_status status;
  /* Type of the last processed token */
  enum jes_token_type token_type;
  /* Type of the last processed element */
  enum jes_type element_type;
  /* The last processed line of the JSON document */
  size_t cursor_line;
  /* The last processed position of the JSON document */
  size_t cursor_pos;
};

/**
 * Initializes a JES context within the provided buffer.
 *
 * @param buffer Pointer to workspace memory to hold context and node pool (and optional hash table).
 * @param buffer_size Size of buffer in bytes.
 * @return Pointer to jes_context or NULL on failure.
 *
 * Note: Buffer must be large enough for context and all nodes (and optional hash table).
 */
struct jes_context* jes_init(void* buffer, size_t buffer_size);

/**
 * Returns the minimal size required for a JES context (excluding node pool and hash table).
 */
size_t jes_get_context_size(void);

/**
 * Returns the size of jes node containing JSON element data and tree management overhead.
 */
size_t jes_get_node_size(void);

/**
 * Resets a JES context, clearing its internal state and JSON tree.
 */
void jes_reset(struct jes_context* ctx);

/**
 * Parses JSON text into an internal tree.
 *
 * @param ctx JES context.
 * @param json_data JSON text (not necessarily null-terminated).
 * @param json_length Length of JSON text in bytes.
 * @return Root element, or NULL on failure.
 */
struct jes_element* jes_load(struct jes_context* ctx, const char* json_data, size_t json_length);

/**
 * Serializes JSON tree to a buffer.
 *
 * @param ctx JES context.
 * @param dst Output buffer.
 * @param length Output buffer size in bytes.
 * @param compact If true, generates compact JSON without spaces; if false, pretty-formatted with indentation.
 * @return Number of bytes written (including null terminator space), or 0 on failure.
 */
size_t jes_render(struct jes_context* ctx, char* dst, size_t length, bool compact);

/**
 * Calculates required buffer size to serialize JSON.
 *
 * @param ctx JES context.
 * @param compact Compact or formatted mode.
 * @return Required size including null terminator, or 0 on error.
 */
size_t jes_evaluate(struct jes_context* ctx, bool compact);

/**
 * Returns last operation status.
 *
 * @param ctx JES context.
 * @return Status code.
 */
jes_status jes_get_status(struct jes_context* ctx);

/**
 * Returns the current JES status block, containing detailed diagnostic information
 *         about the parser or serializer state. This function is useful when
 *         jes_get_status() alone is insufficient to understand the cause of an error.
 * Note: The returned jes_status_block contains context-specific fields whose
 *       meaning depends on the status code.
 *
 * @param ctx JES context.
 * @return jes_status_block structure.
 *
 */
struct jes_status_block jes_get_status_block(struct jes_context* ctx);

/**
 * Deletes an element and its children from the JSON tree.
 *
 * @param ctx JES context.
 * @param element Target element.
 * @return Status code.
 */
jes_status jes_delete_element(struct jes_context* ctx, struct jes_element* element);

/**
 * Returns current element count in JSON tree.
 */
size_t jes_get_element_count(struct jes_context* ctx);

/**
 * Returns maximum number of elements that can be allocated in this context.
 */
size_t jes_get_element_capacity(struct jes_context* ctx);

/**
 * Sets path delimiter when searching for a key using a path of keys.
 * The default separator is '.'
 */
void jes_set_path_separator(struct jes_context* ctx, char delimiter);
/**
 * Gets root element.
 *
 * @return Root element or NULL if tree is empty.
 */
struct jes_element* jes_get_root(struct jes_context* ctx);

/* Navigation helpers */
struct jes_element* jes_get_parent(struct jes_context* ctx, struct jes_element* element);
struct jes_element* jes_get_child(struct jes_context* ctx, struct jes_element* element);
struct jes_element* jes_get_sibling(struct jes_context* ctx, struct jes_element* element);

/**
 * Searches for a nested key.
 *
 * @param ctx JES context.
 * @param parent Starting object or key.
 * @param keys Dot-separated key path (e.g., "a.b.c").
 * @return Found KEY element or NULL if not found.
 *
 * Note: Supports caching — searches start from a parent, reducing repeated traversal.
 */
struct jes_element* jes_get_key(struct jes_context* ctx, struct jes_element* parent, const char* keys);

/**
 * Returns value element of a given key.
 */
struct jes_element* jes_get_key_value(struct jes_context* ctx, struct jes_element* key);

/* Key operations */
struct jes_element* jes_add_key(struct jes_context* ctx, struct jes_element* parent, const char* keyword, size_t keyword_length);
struct jes_element* jes_add_key_before(struct jes_context* ctx, struct jes_element* key, const char* keyword, size_t keyword_length);
struct jes_element* jes_add_key_after(struct jes_context* ctx, struct jes_element* key, const char* keyword, size_t keyword_length);
jes_status jes_update_key(struct jes_context* ctx, struct jes_element* key, const char* keyword, size_t keyword_length);
struct jes_element* jes_update_key_value(struct jes_context* ctx, struct jes_element* key, enum jes_type type, const char* value, size_t value_length);

/* Convert key value type helpers */
struct jes_element* jes_update_key_value_to_object(struct jes_context* ctx, struct jes_element* key);
struct jes_element* jes_update_key_value_to_array(struct jes_context* ctx, struct jes_element* key);
struct jes_element* jes_update_key_value_to_true(struct jes_context* ctx, struct jes_element* key);
struct jes_element* jes_update_key_value_to_false(struct jes_context* ctx, struct jes_element* key);
struct jes_element* jes_update_key_value_to_null(struct jes_context* ctx, struct jes_element* key);

/* Array operations */
size_t jes_get_array_size(struct jes_context* ctx, struct jes_element* array);
struct jes_element* jes_get_array_value(struct jes_context* ctx, struct jes_element* array, int32_t index);
struct jes_element* jes_update_array_value(struct jes_context* ctx, struct jes_element* array, int32_t index, enum jes_type type, const char* value, size_t value_length);
struct jes_element* jes_append_array_value(struct jes_context* ctx, struct jes_element* array, enum jes_type type, const char* value, size_t value_length);
struct jes_element* jes_add_array_value(struct jes_context* ctx, struct jes_element* array, int32_t index, enum jes_type type, const char *value, size_t value_length);

/* Generic add helper */
struct jes_element* jes_add_element(struct jes_context* ctx, struct jes_element* parent, enum jes_type type, const char* value, size_t value_length);

/**
 * Provides a summary of the number of elements in the parsed tree,
 * categorized by their types. It can be used for diagnostics, validation, or
 * performance analysis. */
struct jes_stat jes_get_stat(struct jes_context* ctx);

/**
 * Retrieves memory usage statistics for a JES workspace.
 *
 * This function analyzes the current workspace allocation and returns
 * a breakdown of how the user-provided buffer is segmented among the
 * JES context, node management system, and optional hash table.
 */
struct jes_workspace_stat jes_get_workspace_stat(struct jes_context* ctx);

size_t jes_get_workspace_size(struct jes_context *ctx);

/**
 * Resize the workspace buffer of a JES context.
 *
 * This function re-initializes the workspace of the given JES context
 * to use a new memory buffer provided by the caller. The caller is responsible
 * for allocating and managing the lifetime of this buffer. Any previous buffer
 * associated with the context is not freed by this function.
 *
 * @param ctx JES context to update.
 * @param new_buffer new memory buffer to be used as the workspace. Can be the
          current buffer resized in-place (using realloc)
 * @param new_size Size (in bytes) must be larger than the current workspace.
 *
 * @return
 *    Pointer to the updated JES context on success,
 *    or NULL if the context could not be resized.
 *
 * @note
 *    - The function does not allocate or free memory; ownership of buffer
 *      remains with the caller.
 *    - Resizing doesn't invalidate the current context.
 */
struct jes_context* jes_resize_workspace(struct jes_context *ctx, void *new_buffer, size_t new_size);

/* Iteration macros */

/**
 * Iterate over children of a given type.
 *
 * Usage example:
 * JES_FOR_EACH(ctx, elem, JES_OBJECT) { ... }
 */
#define JES_FOR_EACH(ctx_, elem_, type_) for(elem_ = (elem_->type == type_) ? jes_get_child(ctx_, elem_) : NULL; elem_ != NULL; elem_ = jes_get_sibling(ctx_, elem_))

/**
 * Iterate over array elements.
 */
#define JES_ARRAY_FOR_EACH(ctx_, array_, iter_) for(iter_ = (array_->type == JES_ARRAY) ? jes_get_child(ctx_, array_) : NULL; iter_ != NULL; iter_ = jes_get_sibling(ctx_, iter_))

/**
 * Iterate over keys of an object.
 */
#define JES_FOR_EACH_KEY(ctx_, object_, iter_) for(iter_ = (object_->type == JES_OBJECT) ? jes_get_child(ctx_, object_) : NULL; iter_ != NULL && iter_->type == JES_KEY; iter_ = jes_get_sibling(ctx_, iter_))

#endif /* JES_H */
