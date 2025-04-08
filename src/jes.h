#ifndef JES_H
#define JES_H

#include <stdbool.h>
#include <stdint.h>

/* Comment or undef to enable searching for duplicate keys and overwriting
 * their values.
 * Leaving JES_ALLOW_DUPLICATE_KEYS enabled, has a positive impact on the
 * parsing performance when processing large documents. If key duplication is
 * not a big deal in your implementation, then relax the parser.
 */
//#define JES_ALLOW_DUPLICATE_KEYS

/* Un-comment to enable 32-bit node descriptors to be able to parse very large JSON files.
 * For the standard use-cases with 16-bit node descriptors, the parser can address up to 32767 nodes. */
//#define JES_USE_32BIT_NODE_DESCRIPTOR

#define JES_MAX_VALUE_LENGTH 0xFF

typedef enum jes_status {
  JES_NO_ERROR = 0,
  JES_PARSING_FAILED,
  JES_RENDER_FAILED,
  JES_OUT_OF_MEMORY,
  JES_UNEXPECTED_TOKEN,
  JES_UNEXPECTED_ELEMENT,
  JES_UNEXPECTED_EOF,
  JES_INVALID_PARAMETER,
  JES_ELEMENT_NOT_FOUND,
  JES_INVALID_CONTEXT,
  JES_BROKEN_TREE,
} jes_status;

enum jes_type {
  JES_UNKNOWN = 0,
  JES_OBJECT,
  JES_KEY,
  JES_ARRAY,
  JES_VALUE_STRING,
  JES_VALUE_NUMBER,
  JES_VALUE_TRUE,
  JES_VALUE_FALSE,
  JES_VALUE_NULL,
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


struct jes_context;

/* Initialize a new JES context. The context will be allocated in the provided
 * working buffer.
 *
 * param [in] buffer a buffer to hold the context and JSON tree nodes
 * param [in] buffer_size size of the mem_pool must be at least the size of context
 *
 * return pointer to context or NULL in case of a failure.
 */
struct jes_context* jes_init(void *buffer, uint32_t buffer_size);

/* Loads a string JSON and generates a tree of JSON elements.
 * param [in] ctx is an initialized context
 * param [in] json_data in form of string no need to be NUL terminated.
 * param [in] json_length is the size of json to be parsed.
 *
 * return status of the parsing process see: enum jes_status
 *
 * note: the return value is also available in ctx->status
 */
uint32_t jes_load(struct jes_context* ctx, const char *json_data, uint32_t json_length);

/* Render a tree of JSON elements into the destination buffer as a non-NUL terminated string.
 * param [in] ctx the jes context containing a JSON tree.
 * param [in] dst the destination buffer to hold the JSON string.
 * param [in] length is the size of destination buffer in bytes.
 * param [in] compact if true, the serializer will generate a compact version of JSON string
 *            without formatting or any spaces. If set to false, a formatted JSON string with two
 *            spaces indention will be generated.
 *
 * return the size of JSON string. If zero, there where probably a failure. Check the ctx->status
 *
 * note: The output JSON is totally compact without any space characters.
 * note: It's possible to get the size of JSON string by calling the evaluate function.
 */
uint32_t jes_render(struct jes_context *ctx, char *dst, uint32_t length, bool compact);

/* Evaluates a tree of JSON elements to check if the structure is valid. It also
 * additionally calculates the size of the rendered JSON.
 * param [in] ctx: an Initialized jes context containing a JSON tree.
 * param [in] compact: if true, the function will calculate the length of a compact JSON string
 *            If false, the length of formatted JSON with two spaces indention will be returned.
 *
 * return the required buffer size to render the JSON into string. If zero,
          there might be failures in the tree. use jes_get_status or jes_stringify_status.
 */
uint32_t jes_evaluate(struct jes_context *ctx, bool compact);
/* Get the status of latest process
 * param [in] ctx: an Initialized jes context
 */
jes_status jes_get_status(struct jes_context *ctx);

/* Deletes an element, containing all of its sub-elements. */
uint32_t jes_delete_element(struct jes_context *ctx, struct jes_element *element);

size_t jes_get_node_count(struct jes_context *ctx);
/* Delivers the root element of the JSOn tree.
 * Returning a NULL is meaning that the tree is empty. */
struct jes_element* jes_get_root(struct jes_context *ctx);
/* Delivers the parent element of given JSON element */
struct jes_element* jes_get_parent(struct jes_context *ctx, struct jes_element *element);
/* Delivers the first child of given JSON element */
struct jes_element* jes_get_child(struct jes_context *ctx, struct jes_element *element);
/* Delivers the sibling of given JSON element */
struct jes_element* jes_get_sibling(struct jes_context *ctx, struct jes_element *element);

enum jes_type jes_get_parent_type(struct jes_context *ctx, struct jes_element *element);

/* Returns a Key element inside the given object.
 * param [in] ctx
 * param [in] object is a JSON element of type JES_OBJECT
 * param [in] keys is a NUL-terminated string containing several key names separated by a dot "."
 *
 * return an element of type JES_KEY or NULL if the key is not found.
 */
struct jes_element* jes_get_key(struct jes_context *ctx, struct jes_element *parent_key, const char *keys);
/* Returns value element of a given key name. NULL if element has no value yet.  */
struct jes_element* jes_get_key_value(struct jes_context *ctx, struct jes_element *key);
/* append a key to an existing key. */
struct jes_element* jes_add_key(struct jes_context *ctx, struct jes_element *parent_key, const char *keyword);
/* Add a key to an existing key */
struct jes_element* jes_add_key_before(struct jes_context *ctx, struct jes_element *key, const char *keyword);
/* Add a key to an existing key */
struct jes_element* jes_add_key_after(struct jes_context *ctx, struct jes_element *key, const char *keyword);
/* Update the value(name) of a key element.
 * note: The new key name will not be copied and must be non-retentive for the life cycle of jes_context.
 * return a status code of type enum jes_status */
uint32_t jes_update_key(struct jes_context *ctx, struct jes_element *key, const char *keyword);
/* Update key value giving its name or name a series of keys separated with a dot
 * note: The new value will not be copied and must be non-retentive for the life cycle of jes_context.
   note: When updating a key value, the existing value elements associated with the key
 *       will be deleted first. If adding the new value fails, the key may end up without a value.
 *       In such a scenario, the JSON tree may become inconsistent, so the user should either retry
 *       assigning a value to the key or stop using the context.
 * return a status code of type enum jes_status */
struct jes_element* jes_update_key_value(struct jes_context *ctx, struct jes_element *key, enum jes_type type, const char *value);
/* Update the key value to a JES_OBJECT element */
struct jes_element* jes_update_key_value_to_object(struct jes_context *ctx, struct jes_element *key);
/* Update the key value to a JES_ARRAY element */
struct jes_element* jes_update_key_value_to_array(struct jes_context *ctx, struct jes_element *key);
/* Update the key value to a JES_TRUE element */
struct jes_element* jes_update_key_value_to_true(struct jes_context *ctx, struct jes_element *key);
/* Update the key value to a JES_FALSE element */
struct jes_element* jes_update_key_value_to_false(struct jes_context *ctx, struct jes_element *key);
/* Update the key value to a JES_NULL element */
struct jes_element* jes_update_key_value_to_null(struct jes_context *ctx, struct jes_element *key);

/* Get the number elements within an JES_ARRAY element */
uint16_t jes_get_array_size(struct jes_context *ctx, struct jes_element *array);
/* Returns value element of a given array element. NULL if element has no value yet. */
struct jes_element* jes_get_array_value(struct jes_context *ctx, struct jes_element *array, int32_t index);
/* Update array value giving its array element and an index.
 * note: The new value will not be copied and must be non-retentive for the life time of jes_context.
 * return a status code of type enum jes_status */
struct jes_element* jes_update_array_value(struct jes_context *ctx, struct jes_element *array, int32_t index, enum jes_type type, const char *value);
struct jes_element* jes_add_array_value(struct jes_context *ctx, struct jes_element *array, int32_t index, enum jes_type type, const char *value);
struct jes_element* jes_append_array_value(struct jes_context *ctx, struct jes_element *array, enum jes_type type, const char *value);

/* Add an element to another element. */
struct jes_element* jes_add_element(struct jes_context *ctx, struct jes_element *parent, enum jes_type type, const char *value);

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