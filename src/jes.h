#ifndef JES_H
#define JES_H

#include <stdbool.h>
#include <stdint.h>

/* Use 32-bit node descriptors (default: 16-bit)
 * Enable this to parse very large JSON files (up to 4,294,967,294 nodes)
 * With 16-bit descriptors, the parser can address up to 65,534 nodes
 */
#define JES_USE_32BIT_NODE_DESCRIPTOR

#define JES_MAX_VALUE_LENGTH 0xFFFF
/*  */
#define JES_ENABLE_FAST_KEY_SEARCH

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

/* jes_context holds the internal state of the parser including JSON tree, pool management and process status. */
struct jes_context;

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
struct jes_context* jes_init(void *buffer, uint32_t buffer_size);

void jes_reset(struct jes_context *ctx);

/* Loads a string JSON and parse it into a tree of JSON nodes.
 * param [in] ctx is an initialized context
 * param [in] json_data is a string of ascii characters. (no need to be NUL terminated)
 * param [in] json_length is the size of json to be parsed.
 *
 * return root of the tree which is always an OBJECT or NULL in case of a failure. (see also jes_get_status)
 */
struct jes_element* jes_load(struct jes_context* ctx, const char* json_data, uint32_t json_length);

/* Render a tree of JSON elements into the destination buffer as a string of ascii characters. (not NUL terminated)
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

/* Delete an element and its whole branch.
  return: Status code
 */
jes_status jes_delete_element(struct jes_context *ctx, struct jes_element *element);

/* Number of JSON tree element count */
size_t jes_get_element_count(struct jes_context *ctx);

/* Delivers the root element of the JSON tree (which is always an OBJECT)
 * A NULL represents an empty tree.
 */
struct jes_element* jes_get_root(struct jes_context *ctx);

/* Delivers the parent element of given JSON element */
struct jes_element* jes_get_parent(struct jes_context *ctx, struct jes_element *element);

/* Delivers the first child of given JSON element */
struct jes_element* jes_get_child(struct jes_context *ctx, struct jes_element *element);

/* Delivers the sibling of given JSON element
 * Note: There is a singly link between siblings which means each element knows
 *       only the sibling on its right: element -> element -> element .
 */
struct jes_element* jes_get_sibling(struct jes_context *ctx, struct jes_element *element);

/* Delivers the type of element's parent. */
enum jes_type jes_get_parent_type(struct jes_context *ctx, struct jes_element *element);

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
struct jes_element* jes_get_key(struct jes_context *ctx, struct jes_element *parent, const char *keys);

/* Returns value element of a given key. returns NULL if element has no value yet or in case of a failure */
struct jes_element* jes_get_key_value(struct jes_context *ctx, struct jes_element *key);

/* Append a key to the value of an existing key.
 * param [in] ctx
 * param [in] parent can be a KEY or an OBJECT element.
              If parent is a KEY, then an OBJECT will be created and assigned as
              its value and the new KEY will be added to this OBJECT element.
 * param [in] keyword: The name of the new key
 * return: The new KEY element or NULL in case of a failure
 *
 * Note: If the parent is a KEY, its value should be of type OBJECT or be empty.
 */
struct jes_element* jes_add_key(struct jes_context *ctx, struct jes_element *parent, const char *keyword);

/* Insert a key before an existing key */
struct jes_element* jes_add_key_before(struct jes_context *ctx, struct jes_element *key, const char *keyword);

/* Insert a key after an existing key */
struct jes_element* jes_add_key_after(struct jes_context *ctx, struct jes_element *key, const char *keyword);

/* Update the name of a key element.
 * note: The keyword will not be copied and must be non-retentive for the life cycle of jes_context.
 * return a status code of type enum jes_status */
uint32_t jes_update_key(struct jes_context *ctx, struct jes_element *key, const char *keyword);

/* Update a key value
 * note: The new value will not be copied and must be non-retentive for the life cycle of jes_context.
 * note: When updating a key value, the existing value elements associated with the key
 *       will be deleted first. If adding the new value for any reason fails, the key will end up without a value.
 *
 * return the new key value element or NULL in case of a failure
 */
struct jes_element* jes_update_key_value(struct jes_context *ctx, struct jes_element *key, enum jes_type type, const char *value);

/* Convert the key value to a JES_OBJECT element. All the previous value elements will be lost.
 * return the new key value element or NULL in case of a failure
 */
struct jes_element* jes_update_key_value_to_object(struct jes_context *ctx, struct jes_element *key);

/* Convert the key value to a JES_ARRAY element. All the previous value elements will be lost.
 * return the new key value element or NULL in case of a failure
 */
struct jes_element* jes_update_key_value_to_array(struct jes_context *ctx, struct jes_element *key);

/* Convert the key value to a JES_TRUE element. All the previous value elements will be lost.
 * return the new key value element or NULL in case of a failure
 */
struct jes_element* jes_update_key_value_to_true(struct jes_context *ctx, struct jes_element *key);

/* Convert the key value to a JES_FALSE element. All the previous value elements will be lost.
 * return the new key value element or NULL in case of a failure
 */
struct jes_element* jes_update_key_value_to_false(struct jes_context *ctx, struct jes_element *key);

/* Convert the key value to a JES_NULL element. All the previous value elements will be lost.
 * return the new key value element or NULL in case of a failure
 */
struct jes_element* jes_update_key_value_to_null(struct jes_context *ctx, struct jes_element *key);

/* Get the number of elements of a JES_ARRAY */
uint16_t jes_get_array_size(struct jes_context *ctx, struct jes_element *array);

/* Get the value element of a JES_ARRAY.
 * param [in] ctx
 * param [in] array is an element of type JES_ARRAY to get its value
 * param [in] index is the index of target value to read.
 * note: negative indexes are also valid and help to get values from the end of array.
 * note: out of the bound indexes will be rejected
 *
 * return: value element of a JES_ARRAY or NULL if array is empty or in case of a failure.
 */
struct jes_element* jes_get_array_value(struct jes_context *ctx, struct jes_element *array, int32_t index);

/* Update an array value element.
 * param [in] ctx
 * param [in] array is an element of type JES_ARRAY to update its value element
 * param [in] index is the index of target value to be updated.
 * param [in] type determines the jes_type of the value element
 * param [in] value is a NUL-terminated string
 *
 * note: The new value will not be copied and must be non-retentive for the life time of jes_context.
 * note: Negative indexes are also valid and help to get values from the end of array.
 * note: Out of the bound indexes will be rejected
 *
 * return the modified value element or NULL in case of a failure */
struct jes_element* jes_update_array_value(struct jes_context *ctx, struct jes_element *array, int32_t index, enum jes_type type, const char *value);

/* Append an element to a JES_ARRAY
 * param [in] ctx
 * param [in] array is an element of type JES_ARRAY to get the new value
 * param [in] type determines the jes_type of the value element
 * param [in] value is a NUL-terminated string
 *
 * note: The new value will not be copied and must be non-retentive for the life time of jes_context.
 *
 * return the modified value element or NULL in case of a failure */
struct jes_element* jes_append_array_value(struct jes_context *ctx, struct jes_element *array, enum jes_type type, const char *value);

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
struct jes_element* jes_add_array_value(struct jes_context *ctx, struct jes_element *array, int32_t index, enum jes_type type, const char *value);

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