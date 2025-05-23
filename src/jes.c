#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "jes.h"
#include "jes_private.h"
#include "jes_logger.h"
#include "jes_tokenizer.h"
#include "jes_tree.h"
#include "jes_parser.h"

struct jes_context* jes_init(void *buffer, uint32_t buffer_size)
{
  struct jes_context *ctx = buffer;

  if (buffer_size < sizeof(struct jes_context)) {
    return NULL;
  }

  memset(ctx, 0, sizeof(*ctx));

  ctx->status = JES_NO_ERROR;

  ctx->json_data = NULL;
  ctx->json_size = 0;
  jes_tree_init(ctx);
#ifndef JES_ENABLE_FAST_KEY_SEARCH
  ctx->pool_size = buffer_size - (uint32_t)(sizeof(struct jes_context));
  ctx->capacity = (ctx->pool_size / sizeof(struct jes_node)) < JES_INVALID_INDEX
                 ? (jes_node_descriptor)(ctx->pool_size / sizeof(struct jes_node))
                 : JES_INVALID_INDEX -1;
  ctx->hash_table = NULL;
  ctx->find_key_fn = jes_tree_find_key;
#else
  {
    size_t usable_size = buffer_size - sizeof(struct jes_context) - sizeof(struct jes_hash_table);
    size_t node_count = usable_size / (sizeof(struct jes_node) + sizeof(struct jes_hash_entry));

    ctx->capacity = node_count < JES_INVALID_INDEX
                   ? (jes_node_descriptor)node_count
                   : JES_INVALID_INDEX -1;
    ctx->pool_size = ctx->capacity * sizeof(struct jes_node);
    ctx->hash_table = jes_init_hash_table(ctx, (uint8_t*)ctx->node_pool + ctx->pool_size, buffer_size - sizeof(struct jes_context) - ctx->pool_size);
  }
#endif
  jes_tokenizer_init(ctx);
  ctx->iter = NULL;
  ctx->root = NULL;

  ctx->cookie = JES_CONTEXT_COOKIE;
  return ctx;
}

void jes_reset(struct jes_context *ctx)
{
  if (JES_IS_INITIATED(ctx)) {
    ctx->status = JES_NO_ERROR;
    ctx->json_data = NULL;
    jes_tree_init(ctx);
    jes_tokenizer_init(ctx);
    ctx->iter = NULL;
    ctx->root = NULL;

  }
}

struct jes_element* jes_get_root(struct jes_context *ctx)
{
  if ((ctx != NULL) && JES_IS_INITIATED(ctx)) {
    return &ctx->root->json_tlv;
  }
  return NULL;
}

enum jes_type jes_get_parent_type(struct jes_context *ctx, struct jes_element *element)
{
  /* TODO: add missing parameter checks */
  struct jes_element *parent = jes_get_parent(ctx, element);
  if (parent) {
    return parent->type;
  }

  return JES_UNKNOWN;
}

struct jes_element* jes_get_parent(struct jes_context *ctx, struct jes_element *element)
{
  if (ctx && element && jes_validate_node(ctx, (struct jes_node*)element)) {
    struct jes_node *node = (struct jes_node*)element;
    node = GET_PARENT(ctx, node);
    if (node) {
      return &node->json_tlv;
    }
  }

  return NULL;
}

struct jes_element* jes_get_sibling(struct jes_context *ctx, struct jes_element *element)
{
  if (ctx && element && jes_validate_node(ctx, (struct jes_node*)element)) {
    struct jes_node *node = (struct jes_node*)element;
    node = GET_SIBLING(ctx, node);
    if (node) {
      return &node->json_tlv;
    }
  }

  return NULL;
}

struct jes_element* jes_get_child(struct jes_context *ctx, struct jes_element *element)
{
  if (ctx && element && jes_validate_node(ctx, (struct jes_node*)element)) {
    struct jes_node *node = (struct jes_node*)element;
    node = GET_FIRST_CHILD(ctx, node);
    if (node) {
      return &node->json_tlv;
    }
  }

  return NULL;
}

jes_status jes_delete_element(struct jes_context *ctx, struct jes_element *element)
{
  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return JES_INVALID_CONTEXT;
  }

  ctx->status = JES_NO_ERROR;

  if (element == NULL) {
    return JES_NO_ERROR;
  }

  if (!jes_validate_node(ctx, (struct jes_node*)element)) {
    ctx->status = JES_INVALID_PARAMETER;
    return ctx->status;
  }

  ctx->status = JES_NO_ERROR;
  jes_tree_delete_node(ctx, (struct jes_node*)element);
  return ctx->status;
}

struct jes_element* jes_get_key(struct jes_context *ctx, struct jes_element *parent, const char *keys)
{
  struct jes_element *target_key = NULL;
  struct jes_node *iter = (struct jes_node*)parent;
  uint32_t key_len;
  const char *key;
  char *dot;

  if (!ctx || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if ((parent == NULL) || (!jes_validate_node(ctx, (struct jes_node*)parent)) || (keys == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  key_len = strnlen(keys, JES_MAX_VALUE_LENGTH);
  if (key_len == JES_MAX_VALUE_LENGTH) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  if ((parent->type != JES_OBJECT) && (parent->type != JES_KEY)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }
  /* TODO: Cleanup */
  while (iter != NULL) {
    key = keys;
    dot = strchr(keys, '.');
    if (dot) {
      key_len = dot - keys;
      keys = keys + key_len + sizeof(*dot);
    }
    else {
      /* keys length has already been validated to make sure a buffer over read won't happen. */
      key_len = strlen(keys);
      keys = keys + key_len;
    }

    if (NODE_TYPE(iter) == JES_KEY) {
      iter = GET_FIRST_CHILD(ctx, iter);
    }
    iter = ctx->find_key_fn(ctx, iter, key, key_len);

    if ((iter != NULL) && (*keys == '\0')) {
      target_key = (struct jes_element*)iter;
      break;
    }
  }

  if ((target_key == NULL) && (ctx->status == JES_NO_ERROR)) {
    ctx->status = JES_ELEMENT_NOT_FOUND;
  }

  return target_key;
}

struct jes_element* jes_get_key_value(struct jes_context *ctx, struct jes_element *key)
{
  struct jes_node *node = NULL;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if ((key == NULL) || (!jes_validate_node(ctx, (struct jes_node*)key)) || (key->type != JES_KEY)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  node = GET_FIRST_CHILD(ctx, (struct jes_node*)key);
  return (struct jes_element*)node;
}

uint16_t jes_get_array_size(struct jes_context *ctx, struct jes_element *array)
{
  uint16_t array_size = 0;
  struct jes_node *iter = NULL;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return 0;
  }

  ctx->status = JES_NO_ERROR;

  if ((array == NULL) || (!jes_validate_node(ctx, (struct jes_node*)array)) || (array->type != JES_ARRAY)) {
    ctx->status = JES_INVALID_PARAMETER;
    return 0;
  }

  iter = GET_FIRST_CHILD(ctx, (struct jes_node*)array);
  if (iter) {
    for (array_size = 0; iter != NULL; array_size++) {
      iter = GET_SIBLING(ctx, iter);
    }
  }

  return array_size;
}

struct jes_element* jes_get_array_value(struct jes_context *ctx, struct jes_element *array, int32_t index)
{
  struct jes_node *iter = NULL;
  /* Skip checking ctx and array only when calling jes_get_array_size */
  uint16_t array_size = jes_get_array_size(ctx, array);

  if (ctx->status != JES_NO_ERROR) {
    return NULL;
  }

  if (index < 0) { /* converting negative index to an index from the end of array. */
    if (-index <= array_size) {
      index = array_size + index;
    }
  }

  if ((index < 0) || (index > array_size)) {
    ctx->status = JES_ELEMENT_NOT_FOUND;
    return NULL;
  }

  iter = GET_FIRST_CHILD(ctx, (struct jes_node*)array);
  for (; iter && index > 0; index--) {
    iter = GET_SIBLING(ctx, iter);
  }

  if (iter) {
    return &iter->json_tlv;
  }

  /* We shouldn't land here. */
  ctx->status = JES_BROKEN_TREE;
  assert(0);
  return NULL;
}

static bool jes_validate_tlv(struct jes_context *ctx, enum jes_type type, size_t length, const char *value)
{
  bool is_valid = false;
  switch (type) {
    case JES_KEY:
    case JES_STRING:
      break;
    case JES_NUMBER:
      break;
    case JES_OBJECT:
    case JES_ARRAY:
    case JES_TRUE:
    case JES_FALSE:
    case JES_NULL:
      is_valid = true;
      break;
    default:
      break;
  }

  return is_valid;
}

struct jes_element* jes_add_element(struct jes_context *ctx, struct jes_element *parent, enum jes_type type, const char *value)
{
  struct jes_node *new_node = NULL;
  uint32_t value_length = 0;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  ctx->status = JES_NO_ERROR;

  if ((parent == NULL) && (ctx->root != NULL)) {
    /* JSON is not empty. Invalid request. */
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  if ((parent != NULL) && (!jes_validate_node(ctx, (struct jes_node*)parent))) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }
  /* TODO: review */
  if (value) {
    value_length = strnlen(value, JES_MAX_VALUE_LENGTH);
    if (value_length == JES_MAX_VALUE_LENGTH) {
      ctx->status = JES_INVALID_PARAMETER;
      return NULL;
    }
  }

  switch (type) {
    case JES_NUMBER:
      if (!jes_tokenizer_validate_number(ctx, value, value_length)) {
        ctx->status = JES_INVALID_PARAMETER;
        return NULL;
      }
      break;
    case JES_KEY: /* Fall through is intended */
    case JES_STRING:
      if (!jes_tokenizer_validate_string(ctx, value, value_length)) {
        ctx->status = JES_INVALID_PARAMETER;
        return NULL;
      }
      break;

    case JES_OBJECT:
    case JES_ARRAY:
    case JES_TRUE:
    case JES_FALSE:
    case JES_NULL:
      break;

    default:
      ctx->status = JES_INVALID_PARAMETER;
      return NULL;
  }

  new_node = jes_tree_insert_node(ctx, (struct jes_node*)parent, GET_LAST_CHILD(ctx, (struct jes_node*)parent),
                             type, value_length, value);

  return (struct jes_element*)new_node;
}

struct jes_element* jes_add_key(struct jes_context *ctx, struct jes_element *parent, const char *keyword)
{
  struct jes_node *object = NULL;
  struct jes_node *new_node = NULL;
  uint16_t keyword_length = 0;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  ctx->status = JES_NO_ERROR;

  if ((parent == NULL) || (!jes_validate_node(ctx, (struct jes_node*)parent)) || (keyword == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  if ((parent->type != JES_OBJECT) && (parent->type != JES_KEY)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  keyword_length = strnlen(keyword, JES_MAX_VALUE_LENGTH);
  if (keyword_length == JES_MAX_VALUE_LENGTH) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }



  if (!jes_tokenizer_validate_string(ctx, keyword, keyword_length)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  if (parent->type == JES_KEY) {
    /* The key must be added to an existing key and must be embedded in an OBJECT */
    object = GET_FIRST_CHILD(ctx, (struct jes_node*)parent);
    if (object == NULL) {
      object = jes_tree_insert_node(ctx, (struct jes_node*)parent, GET_LAST_CHILD(ctx, (struct jes_node*)parent), JES_OBJECT, 1, "{");
    }
    else if (NODE_TYPE(object) != JES_OBJECT) {
      /* We should not land here */
      ctx->status = JES_UNEXPECTED_ELEMENT;
      return NULL;
    }
  }
  else { /* parent is an OBJECT */
    object = (struct jes_node*)parent;
  }
  /* Append the key */
  new_node = jes_tree_insert_key_node(ctx, object, GET_LAST_CHILD(ctx, object), keyword_length, keyword);

  return (struct jes_element*)new_node;
}

struct jes_element* jes_add_key_before(struct jes_context *ctx, struct jes_element *key, const char *keyword)
{
  struct jes_node *new_node = NULL;
  struct jes_node *parent = NULL;
  struct jes_node *iter = NULL;
  struct jes_node *before = NULL;
  struct jes_node *key_node = (struct jes_node*)key;
  uint32_t keyword_length = 0;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  ctx->status = JES_NO_ERROR;

  if ((key == NULL) || !jes_validate_node(ctx, key_node) || (key->type != JES_KEY) || (keyword == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  keyword_length = strnlen(keyword, JES_MAX_VALUE_LENGTH);
  if (keyword_length == JES_MAX_VALUE_LENGTH) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  parent = GET_PARENT(ctx, key_node);
  assert(parent != NULL);
  assert(NODE_TYPE(parent) == JES_OBJECT);


  for (iter = GET_FIRST_CHILD(ctx, parent); iter != NULL; iter = GET_SIBLING(ctx, iter)) {
    assert(NODE_TYPE(iter) == JES_KEY);
    if (iter == key_node) {
      new_node = jes_tree_insert_key_node(ctx, parent, before, keyword_length, keyword);
      break;
    }
    before = iter;
  }

  return (struct jes_element*)new_node;
}

struct jes_element* jes_add_key_after(struct jes_context *ctx, struct jes_element *key, const char *keyword)
{
  struct jes_node *key_node = (struct jes_node*)key;
  struct jes_node *new_node = NULL;
  struct jes_node *parent = NULL;
  uint32_t keyword_length = 0;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  ctx->status = JES_NO_ERROR;

  if ((key == NULL) || !jes_validate_node(ctx, key_node) || (key->type != JES_KEY) || (keyword == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  parent = GET_PARENT(ctx, key_node);
  assert(parent != NULL);
  assert(NODE_TYPE(parent) == JES_OBJECT);

  keyword_length = strnlen(keyword, JES_MAX_VALUE_LENGTH);
  if (keyword_length == JES_MAX_VALUE_LENGTH) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  new_node = jes_tree_insert_key_node(ctx, parent, (struct jes_node*)key, keyword_length, keyword);

  return (struct jes_element*)new_node;
}

enum jes_status jes_update_key(struct jes_context *ctx, struct jes_element *key, const char *keyword)
{
  uint32_t keyword_length = 0;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return JES_INVALID_CONTEXT;
  }

  ctx->status = JES_NO_ERROR;

  if (!jes_validate_node(ctx, (struct jes_node*)key) || (key->type != JES_KEY) || (keyword == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return ctx->status;
  }

  keyword_length = strnlen(keyword, JES_MAX_VALUE_LENGTH);
  if (keyword_length == JES_MAX_VALUE_LENGTH) {
    ctx->status = JES_INVALID_PARAMETER;
    return ctx->status;
  }

  if (!jes_tokenizer_validate_string(ctx, keyword, keyword_length)) {
    ctx->status = JES_INVALID_PARAMETER;
    return ctx->status;
  }

  key->length = keyword_length;
  key->value = keyword;

  return JES_NO_ERROR;
}

struct jes_element* jes_update_key_value(struct jes_context *ctx, struct jes_element *key, enum jes_type type, const char *value)
{
  struct jes_element *key_value = NULL;

  /* First delete the old value of the key if exists. */
  jes_tree_delete_node(ctx, GET_FIRST_CHILD(ctx, (struct jes_node*)key));
  key_value = jes_add_element(ctx, key, type, value);

  return key_value;
}

struct jes_element* jes_update_key_value_to_object(struct jes_context *ctx, struct jes_element *key)
{
  return jes_update_key_value(ctx, key, JES_OBJECT, "");
}

struct jes_element* jes_update_key_value_to_array(struct jes_context *ctx, struct jes_element *key)
{
  return jes_update_key_value(ctx, key, JES_ARRAY, "");
}

struct jes_element* jes_update_key_value_to_true(struct jes_context *ctx, struct jes_element *key)
{
  return jes_update_key_value(ctx, key, JES_TRUE, "true");
}

struct jes_element* jes_update_key_value_to_false(struct jes_context *ctx, struct jes_element *key)
{
  return jes_update_key_value(ctx, key, JES_FALSE, "false");
}

struct jes_element* jes_update_key_value_to_null(struct jes_context *ctx, struct jes_element *key)
{
  return jes_update_key_value(ctx, key, JES_NULL, "null");
}

struct jes_element* jes_update_array_value(struct jes_context *ctx, struct jes_element *array, int32_t index, enum jes_type type, const char *value)
{
  struct jes_node *target_node = NULL;
  uint32_t value_length = 0;
  int32_t array_size;

  if (!ctx || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  ctx->status = JES_NO_ERROR;

  if ((array == NULL) || !jes_validate_node(ctx, (struct jes_node*)array) || (array->type != JES_ARRAY) || (value == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  value_length = strnlen(value, JES_MAX_VALUE_LENGTH);
  if (value_length == JES_MAX_VALUE_LENGTH) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  array_size = jes_get_array_size(ctx, array);
  if (index < 0) { /* converting negative index to an index from the end of array. */
    index = array_size + index;
  }

  if ((array_size == 0) || (index < 0) || (index > array_size)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  for(target_node = GET_FIRST_CHILD(ctx, (struct jes_node*)array); target_node != NULL; target_node = GET_SIBLING(ctx, target_node)) {
    if (index-- == 0) {
      break;
    }
  }

  if (target_node) {
    /* We'll not delete the target_node to keep the original array order. Just update its JSON TLV.
     * The rest of the branch however must be removed. */
    jes_tree_delete_node(ctx, GET_FIRST_CHILD(ctx, target_node));
    target_node->json_tlv.type = type;
    target_node->json_tlv.length = value_length;
    target_node->json_tlv.value = value;
  }
  else {
    ctx->status = JES_BROKEN_TREE;
    assert(0);
  }

  return (struct jes_element*)target_node;
}

struct jes_element* jes_append_array_value(struct jes_context *ctx, struct jes_element *array, enum jes_type type, const char *value)
{
  struct jes_node *anchor_node = NULL;
  struct jes_node *new_node = NULL;
  uint32_t value_length = 0;

  if (!ctx || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  ctx->status = JES_NO_ERROR;

  if ((array == NULL) || !jes_validate_node(ctx, (struct jes_node*)array) || (array->type != JES_ARRAY) || (value == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  value_length = strnlen(value, JES_MAX_VALUE_LENGTH);
  if (value_length == JES_MAX_VALUE_LENGTH) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  new_node = jes_tree_insert_node(ctx, (struct jes_node*)array, GET_LAST_CHILD(ctx, (struct jes_node*)array), type, value_length, value);

  return (struct jes_element*)new_node;
}

struct jes_element* jes_add_array_value(struct jes_context *ctx, struct jes_element *array, int32_t index, enum jes_type type, const char *value)
{
  struct jes_node *anchor_node = NULL;
  struct jes_node *prev_node = NULL;
  struct jes_node *new_node = NULL;
  uint32_t value_length = 0;
  int32_t array_size;

  if (!ctx || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  ctx->status = JES_NO_ERROR;

  if ((array == NULL) || !jes_validate_node(ctx, (struct jes_node*)array) || (array->type != JES_ARRAY) || (value == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  array_size = jes_get_array_size(ctx, array);
  if (index < 0) { /* converting negative index to an index from the end of array. */
    index = array_size + index;
  }

  /* Handling out of the bound indices as prepend or append */
  if (index < 0) { index = 0; }
  if (index >= array_size) {
    return jes_append_array_value(ctx, array, type, value);
  }

  value_length = strnlen(value, JES_MAX_VALUE_LENGTH);
  if (value_length == JES_MAX_VALUE_LENGTH) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  for (anchor_node = GET_FIRST_CHILD(ctx, (struct jes_node*)array); anchor_node != NULL; anchor_node = GET_SIBLING(ctx, anchor_node)) {
    if (index == 0) {
      break;
    }
    prev_node = anchor_node;
    index--;
  }

  if ((anchor_node == NULL) && (array_size != 0)) {
    ctx->status = JES_BROKEN_TREE;
    assert(0);
    return NULL;
  }

  new_node = jes_tree_insert_node(ctx, (struct jes_node*)array, anchor_node, type, value_length, value);

  return (struct jes_element*)new_node;
}

size_t jes_get_element_count(struct jes_context *ctx)
{
  return ctx->node_count;
}

jes_status jes_get_status(struct jes_context *ctx)
{
  return ctx->status;
}

struct jes_element* jes_load(struct jes_context *ctx, const char *json_data, uint32_t json_length)
{
  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if ((json_data == NULL) || (json_length == 0)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  jes_reset(ctx);

  ctx->json_data = json_data;
  ctx->json_size = json_length;
  jes_parse(ctx);

  return ctx->status == JES_NO_ERROR ? (struct jes_element*)ctx->root : NULL;
}