#ifndef JES_H
#define JES_H

#include <stdbool.h>
#include <stdint.h>

/**
 * JES_USE_32BIT_NODE_DESCRIPTOR
 *
 * Configuration option to select between 16-bit or 32-bit node descriptors.
 *
 * When defined, the parser will use 32-bit integers for node references instead of
 * the default 16-bit integers. This affects all node reference fields: parent,
 * first_child, last_child and sibling.
 *
 * Addressing Capacity:
 * - With 16-bit descriptors (default): Parser can address up to 65,535 nodes
 *   (2^16 - 1, reserving 0xFFFF for JES_INVALID_INDEX and potentially NULL)
 * - With 32-bit descriptors: Parser can address up to 4,294,967,295 nodes
 *   (2^32 - 1, similarly reserving special values)
 *
 * Memory impact:
 * - Enabling 32-bit descriptors doubles the memory consumption for node reference
 *   fields (parent, first_child, last_child, and sibling)
 */
//#define JES_USE_32BIT_NODE_DESCRIPTOR

/**
 * JES_MAX_VALUE_LENGTH
 *
 * Maximum allowed length for string values and keys in JSON documents.
 *
 * This configuration option sets the maximum number of characters (bytes) allowed
 * for individual string values and object keys during JSON parsing. The parser will
 * reject any string that exceeds this limit, providing protection against memory
 * exhaustion attacks and parsing extremely large or malformed JSON documents.
 *
 * Default value: 4096 bytes
 *
 * Customization guidelines:
 * - Increase the limit for applications that legitimately handle large strings
 *   (e.g., embedded Base64 data, large text fields)
 * - Decrease the limit in memory-constrained environments or when processing
 *   known small JSON documents
 * - Consider the maximum expected string size in your JSON documents
 *
 */
#define JES_MAX_VALUE_LENGTH 4096

/**
 * JES_ENABLE_FAST_KEY_SEARCH
 * Enables hash table–based key lookup for faster access to keys in JSON objects.
 *
 * When defined, JES uses a hash table to accelerate key lookups within JSON objects.
 * This improves performance in larger JSON documents with many keys, particularly
 * when key access is frequent.
 *
 * For small or simple JSON structures (typically a few kilobytes), linear search is often
 * more efficient and faster due to minimal overhead and lower memory usage. In such cases,
 * it is recommended to keep fast key search disabled.
 *
 * Note: When fast key search is enabled, the user must provide a **larger memory buffer**
 * for the JES context to accommodate the internal hash table structure.
 * Insufficient memory may lead to parsing failures.
 */
#define JES_ENABLE_FAST_KEY_SEARCH

/**
 * JES_ENABLE_FALL_BACK_TO_LINEAR_SEARCH
 *
 * Enables automatic fallback to linear search with hash table buffer reclamation
 * when the node buffer becomes full. This mechanism sacrifices hash table performance
 * to free up workspace memory dedicated to hash table for continued JSON parsing.
 *
 * BEHAVIOR WHEN DEFINED:
 * - When the node buffer pool becomes exhausted during parsing, and fast
 *   key searching feature is enabled, the hash table buffer is reclaimed and
 *   added back to the available workspace memory
 * - Hash table operations are disabled and all key searches will fall back from
 *   O(1) to O(n) - linear search through object properties
 * - The hash table cannot be re-enabled after fallback occurs and all the
 *   Hash table entries will be lost
 *
 * BEHAVIOR WHEN NOT DEFINED:
 * - Node buffer exhaustion results in parsing failure
 */
//#define JES_ENABLE_FALL_BACK_TO_LINEAR_SEARCH

#define JES_TAB_SIZE 2

#define JES_ENABLE_TOKEN_LOG
#define JES_ENABLE_PARSER_NODE_LOG
#define JES_ENABLE_PARSER_STATE_LOG
#define JES_ENABLE_SERIALIZER_NODE_LOG
#define JES_ENABLE_SERIALIZER_STATE_LOG

typedef enum jes_status {
  JES_NO_ERROR = 0,
  JES_PARSING_FAILED,
  JES_RENDER_FAILED,
  JES_OUT_OF_MEMORY,
  JES_UNEXPECTED_SYMBOL,
  JES_UNEXPECTED_TOKEN,
  JES_UNEXPECTED_ELEMENT,
  JES_UNEXPECTED_EOF,
  JES_INVALID_PARAMETER,
  JES_ELEMENT_NOT_FOUND,
  JES_INVALID_CONTEXT,
  JES_BROKEN_TREE,
  JES_DUPLICATE_KEY,
  JES_INVALID_UNICODE,
  JES_INVALID_NUMBER,
} jes_status;

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

/* JES element contains JSON data in the form of Type/Length/Value */
struct jes_element {
  /* Type of element. See jes_type */
  uint16_t type;
  /* Length of value */
  uint16_t length;
  /* Value of element */
  const char *value;
};

/* jes_context holds the internal state of the parser including JSON tree, pool management and process status. */
struct jes_context;

struct jes_stat {
  size_t objects; /* Number of OBJECT elements in the JSON tree */
  size_t keys;    /* Number of KEY elements in the JSON tree */
  size_t arrays;  /* Number of ARRAY elements in the JSON tree */
  size_t values;  /* Number of value(STRING, NUMBER, TRUE, FALSE, NULL) elements in the JSON tree */
};

/* Initialize a new JES context. The context will be placed at the start of the provided
 * working buffer.
 *
 * param [in] buffer will hold the context and JSON tree nodes
 * param [in] buffer_size: size of the provided buffer.
 *
 * Note: The buffer must have enough space to hold the parser context and the required json tree nodes.
 *
 * return pointer to context or NULL in case of a failure.
 */
struct jes_context* jes_init(void* buffer, size_t buffer_size);

/* */
size_t jes_get_context_size(void);

void jes_reset(struct jes_context* ctx);

/* Loads a string JSON and parse it into a tree of JSON nodes.
 * param [in] ctx is an initialized context
 * param [in] json_data is a string of ascii characters. (no need to be NUL terminated)
 * param [in] json_length is the size of json to be parsed.
 *
 * return root of the tree which is always an OBJECT or NULL in case of a failure. (see also jes_get_status)
 */
struct jes_element* jes_load(struct jes_context* ctx, const char* json_data, size_t json_length);

/* Render a tree of JSON elements into the destination buffer as a string of ascii characters. (not NUL terminated)
 * param [in] ctx the jes context containing a JSON tree.
 * param [in] dst the destination buffer to hold the JSON string.
 * param [in] length is the size of destination buffer in bytes.
 * param [in] compact if true, the serializer will generate a compact version of JSON string
 *            without formatting or any spaces. If set to false, a formatted JSON string with two
 *            spaces indention will be generated.
 *
 * return the size of JSON string including the space required for NUL termination.
 *        If zero, there where probably a failure. Check the ctx->status
 *
 * note: The output JSON is totally compact without any space characters.
 * note: It's possible to get the size of JSON string by calling the evaluate function.
 */
size_t jes_render(struct jes_context* ctx, char* dst, size_t length, bool compact);

/* Evaluates a tree of JSON elements to check if the structure is valid. It also
 * additionally calculates the size of the rendered JSON.
 * param [in] ctx: an Initialized jes context containing a JSON tree.
 * param [in] compact: if true, the function will calculate the length of a compact JSON string
 *            If false, the length of formatted JSON with two spaces indention will be returned.
 *
 * return the required buffer size to render the JSON into string including the NUL termination.
 *        If zero, there might be failures in the tree. use jes_get_status or jes_stringify_status.
 */
size_t jes_evaluate(struct jes_context* ctx, bool compact);

/* Get the status of latest process
 * param [in] ctx: an Initialized jes context
 */
jes_status jes_get_status(struct jes_context* ctx);

/* Delete an element and its whole branch.
  return: Status code
 */
jes_status jes_delete_element(struct jes_context* ctx, struct jes_element* element);

/* Number of JSON tree element count */
size_t jes_get_element_count(struct jes_context* ctx);

/* Get maximum number of JSON elements that can be allocated */
size_t jes_get_element_capacity(struct jes_context* ctx);

/* Delivers the root element of the JSON tree (which is always an OBJECT)
 * A NULL represents an empty tree.
 */
struct jes_element* jes_get_root(struct jes_context* ctx);

/* Delivers the parent element of given JSON element */
struct jes_element* jes_get_parent(struct jes_context* ctx, struct jes_element* element);

/* Delivers the first child of given JSON element */
struct jes_element* jes_get_child(struct jes_context* ctx, struct jes_element* element);

/* Delivers the sibling of given JSON element
 * Note: There is a singly link between siblings which means each element knows
 *       only the sibling on its right: element -> element -> element .
 */
struct jes_element* jes_get_sibling(struct jes_context* ctx, struct jes_element* element);

/* Returns a Key element inside the given object.
 * param [in] ctx
 * param [in] parent is a JSON element of type JES_OBJECT or JES_KEY
 * param [in] keys is a NUL-terminated string containing potentially several key names separated by a dot "."
 *            The target key is the last key in keys.
 * Note: The search for the target key starts from the parent which can be an OBJECT or a KEY.
 *       as an example: given the keys equal to key1.key2.key3, the key1 will be searched among the
 *       parent values. The the key2 will be searched among the key2 values. and finally key3
 *       will be searched among the key2 values and will be delivered as the result of the search.
 *       This helps to speedup complicated searches:
 *        - The interface provides parent caching so the search shouldn't always start from the root
 *        - The interface provides multiple keying so a deep search can be performed in just one call.
 *
 * return an element of type JES_KEY or NULL if the key is not found or a failure has been occurred.
 */
struct jes_element* jes_get_key(struct jes_context* ctx, struct jes_element* parent, const char* keys);

/* Returns value element of a given key. returns NULL if element has no value yet or in case of a failure */
struct jes_element* jes_get_key_value(struct jes_context* ctx, struct jes_element* key);

/* Append a key to the value of an existing key.
 * param [in] ctx
 * param [in] parent can be a KEY or an OBJECT element.
              If parent is a KEY, then an OBJECT will be created and assigned as
              its value and the new KEY will be added to this OBJECT element.
 * param [in] keyword: The name of the new key
 * param [in] keyword_length: The length of the new key name
 * return: The new KEY element or NULL in case of a failure
 *
 * Note: If the parent is a KEY, its value should be of type OBJECT or be empty.
 */
struct jes_element* jes_add_key(struct jes_context* ctx, struct jes_element* parent, const char* keyword, size_t keyword_length);

/* Insert a key before an existing key */
struct jes_element* jes_add_key_before(struct jes_context* ctx, struct jes_element* key, const char* keyword, size_t keyword_length);

/* Insert a key after an existing key */
struct jes_element* jes_add_key_after(struct jes_context* ctx, struct jes_element* key, const char* keyword, size_t keyword_length);

/* Update the name of a key element.
 * note: The keyword will not be copied and must be non-retentive for the life cycle of jes_context.
 * return a status code of type enum jes_status */
enum jes_status jes_update_key(struct jes_context* ctx, struct jes_element* key, const char* keyword, size_t keyword_length);

/* Update a key value
 * note: The new value will not be copied and must be non-retentive for the life cycle of jes_context.
 * note: When updating a key value, the existing value elements associated with the key
 *       will be deleted first. If adding the new value for any reason fails, the key will end up without a value.
 *
 * return the new key value element or NULL in case of a failure
 */
struct jes_element* jes_update_key_value(struct jes_context* ctx, struct jes_element* key, enum jes_type type, const char* value, size_t value_length);

/* Convert the key value to a JES_OBJECT element. All the previous value elements will be lost.
 * return the new key value element or NULL in case of a failure
 */
struct jes_element* jes_update_key_value_to_object(struct jes_context* ctx, struct jes_element* key);

/* Convert the key value to a JES_ARRAY element. All the previous value elements will be lost.
 * return the new key value element or NULL in case of a failure
 */
struct jes_element* jes_update_key_value_to_array(struct jes_context* ctx, struct jes_element* key);

/* Convert the key value to a JES_TRUE element. All the previous value elements will be lost.
 * return the new key value element or NULL in case of a failure
 */
struct jes_element* jes_update_key_value_to_true(struct jes_context* ctx, struct jes_element* key);

/* Convert the key value to a JES_FALSE element. All the previous value elements will be lost.
 * return the new key value element or NULL in case of a failure
 */
struct jes_element* jes_update_key_value_to_false(struct jes_context* ctx, struct jes_element* key);

/* Convert the key value to a JES_NULL element. All the previous value elements will be lost.
 * return the new key value element or NULL in case of a failure
 */
struct jes_element* jes_update_key_value_to_null(struct jes_context* ctx, struct jes_element* key);

/* Get the number of elements of a JES_ARRAY */
uint16_t jes_get_array_size(struct jes_context* ctx, struct jes_element* array);

/* Get the value element of a JES_ARRAY.
 * param [in] ctx
 * param [in] array is an element of type JES_ARRAY to get its value
 * param [in] index is the index of target value to read.
 * note: negative indexes are also valid and help to get values from the end of array.
 * note: out of the bound indexes will be rejected
 *
 * return: value element of a JES_ARRAY or NULL if array is empty or in case of a failure.
 */
struct jes_element* jes_get_array_value(struct jes_context* ctx, struct jes_element* array, int32_t index);

/* Update an array value element.
 * param [in] ctx
 * param [in] array is an element of type JES_ARRAY to update its value element
 * param [in] index is the index of target value to be updated.
 * param [in] type determines the jes_type of the value element
 * param [in] Value is a string
 * param [in] value_length
 *
 * note: The new value will not be copied and must be non-retentive for the life time of jes_context.
 * note: Negative indexes are also valid and help to get values from the end of array.
 * note: Out of the bound indexes will be rejected
 *
 * return the modified value element or NULL in case of a failure */
struct jes_element* jes_update_array_value(struct jes_context* ctx, struct jes_element* array, int32_t index, enum jes_type type, const char* value, size_t value_length);

/* Append an element to a JES_ARRAY
 * param [in] ctx
 * param [in] array is an element of type JES_ARRAY to get the new value
 * param [in] type determines the jes_type of the value element
 * param [in] value is a NUL-terminated string
 *
 * note: The new value will not be copied and must be non-retentive for the life time of jes_context.
 *
 * return the modified value element or NULL in case of a failure */
struct jes_element* jes_append_array_value(struct jes_context* ctx, struct jes_element* array, enum jes_type type, const char* value, size_t value_length);

/* Insert an element into a JES_ARRAY
 * param [in] ctx
 * param [in] array is an element of type JES_ARRAY to get the new value
 * param [in] index is the index that the new value will be inserted.
 * param [in] type determines the jes_type of the value element
 * param [in] value is a NUL-terminated string
 *
 * note: The new value will not be copied and must be non-retentive for the life time of jes_context.
 * note: Negative indexes are also valid and help to insert values from the end of array.
 * note: Out of the bound indexes won't be rejected. A positive out the bound index will be handle as an append.
 *                                                   A negative out the bound index will be handled a prepend.
 * note: Inserting an element to a target index, will shift the previous element to the right
 *
 * return the modified value element or NULL in case of a failure */
struct jes_element* jes_add_array_value(struct jes_context* ctx, struct jes_element* array, int32_t index, enum jes_type type, const char *value, size_t value_length);

/* Add an element to another element. */
struct jes_element* jes_add_element(struct jes_context* ctx, struct jes_element* parent, enum jes_type type, const char* value, size_t value_length);

struct jes_stat jes_get_stat(struct jes_context* ctx);

#define JES_FOR_EACH(ctx_, elem_, type_) for(elem_ = (elem_->type == type_) ? jes_get_child(ctx_, elem_) : NULL; elem_ != NULL; elem_ = jes_get_sibling(ctx_, elem_))
/* For loop to iterate over array elements.
 * array_ and iter_ must be pointers of type jes_element.
 * iter_ must be initially NULL then in each iteration it will deliver an array member. */
#define JES_ARRAY_FOR_EACH(ctx_, array_, iter_) for(iter_ = (array_->type == JES_ARRAY) ? jes_get_child(ctx_, array_) : NULL; iter_ != NULL; iter_ = jes_get_sibling(ctx_, iter_))
/* For loop to to iterate over keys of an object.
 * object_ is a pointer to an object element to iterate its KEYs.
 * iter_ must be initially NULL then in each iteration it will deliver a KEY member. */
#define JES_FOR_EACH_KEY(ctx_, object_, iter_) for(iter_ = (object_->type == JES_OBJECT) ? jes_get_child(ctx_, object_) : NULL; iter_ != NULL && iter_->type == JES_KEY; iter_ = jes_get_sibling(ctx_, iter_))

#endif