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
#include "jes_serializer.h"

struct jes_context* jes_init(void* buffer, size_t buffer_size)
{
  struct jes_context* ctx = buffer;

  if ((buffer == NULL) || buffer_size < sizeof(struct jes_context)) {
    return NULL;
  }

  memset(ctx, 0, sizeof(*ctx));

  ctx->status = JES_NO_ERROR;

  ctx->workspace = buffer;
  ctx->workspace_size = buffer_size;
  jes_tree_init(ctx, (struct jes_context*)ctx->workspace + 1, ctx->workspace_size - sizeof(*ctx));

  ctx->cookie = JES_CONTEXT_COOKIE;
  ctx->path_separator = '.';
  return ctx;
}

void jes_reset(struct jes_context* ctx)
{
  if ((ctx != NULL) && JES_IS_INITIATED(ctx)) {
    ctx->status = JES_NO_ERROR;
    ctx->serdes.tokenizer.json_data = NULL;
    ctx->path_separator = '.';
    jes_tree_init(ctx, (struct jes_context*)ctx->workspace + 1, ctx->workspace_size - sizeof(*ctx));
  }
}

struct jes_element* jes_get_root(struct jes_context* ctx)
{
  if ((ctx != NULL) && JES_IS_INITIATED(ctx)) {
    return (struct jes_element*)ctx->node_mng.root;
  }
  return NULL;
}

enum jes_type jes_get_parent_type(struct jes_context* ctx, struct jes_element* element)
{
  enum jes_type parent_type = JES_UNKNOWN;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return JES_UNKNOWN;
  }

  if ((element == NULL) || !jes_validate_node(ctx, (struct jes_node*)element)) {
    ctx->status = JES_INVALID_PARAMETER;
    return JES_UNKNOWN;
  }

  struct jes_element* parent = jes_get_parent(ctx, element);
  if (parent) {
    return parent->type;
  }

  return parent_type;
}

struct jes_element* jes_get_parent(struct jes_context* ctx, struct jes_element* element)
{
  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if ((element == NULL) || !jes_validate_node(ctx, (struct jes_node*)element)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  struct jes_node* parent = jes_tree_get_parent_node(ctx, (struct jes_node*)element);
  if (parent) {
    return (struct jes_element*)parent;
  }

  return NULL;
}

struct jes_element* jes_get_sibling(struct jes_context* ctx, struct jes_element* element)
{
  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if ((element == NULL) || !jes_validate_node(ctx, (struct jes_node*)element)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  struct jes_node* sibling = jes_tree_get_sibling_node(ctx, (struct jes_node*)element);
  if (sibling) {
    return (struct jes_element*)sibling;
  }

  return NULL;
}

struct jes_element* jes_get_child(struct jes_context* ctx, struct jes_element* element)
{
  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if ((element == NULL) || !jes_validate_node(ctx, (struct jes_node*)element)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  struct jes_node* child = jes_tree_get_child_node(ctx, (struct jes_node*)element);
  if (child) {
    return (struct jes_element*)child;
  }

  return NULL;
}

jes_status jes_delete_element(struct jes_context* ctx, struct jes_element* element)
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

struct jes_element* jes_get_key(struct jes_context* ctx, struct jes_element* parent, const char* keys)
{
  struct jes_element* target_key = NULL;
  struct jes_node* iter = (struct jes_node*)parent;
  size_t key_len;
  const char* key;
  char* separator;

  if (!ctx || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if ((parent == NULL) || (!jes_validate_node(ctx, (struct jes_node*)parent)) || (keys == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  key_len = strnlen(keys, JES_MAX_PATH_LENGTH);
  if (key_len == JES_MAX_PATH_LENGTH) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  if ((parent->type != JES_OBJECT) && (parent->type != JES_KEY)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  ctx->status = JES_NO_ERROR;

  /* TODO: Cleanup */
  while (iter != NULL) {
    key = keys;
    separator = strchr(keys, ctx->path_separator);
    if (separator) {
      key_len = separator - keys;
      keys = keys + key_len + sizeof(*separator);
    }
    else {
      /* keys length has already been validated to make sure a buffer over read won't happen. */
      key_len = strlen(keys);
      keys = keys + key_len;
    }

    if (NODE_TYPE(iter) == JES_KEY) {
      iter = GET_FIRST_CHILD(ctx->node_mng, iter);
    }
    iter = ctx->node_mng.find_key_fn(ctx, iter, key, key_len);

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

struct jes_element* jes_get_key_value(struct jes_context* ctx, struct jes_element* key)
{
  struct jes_node* node = NULL;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if ((key == NULL) || (!jes_validate_node(ctx, (struct jes_node*)key)) || (key->type != JES_KEY)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  node = GET_FIRST_CHILD(ctx->node_mng, (struct jes_node*)key);
  return (struct jes_element*)node;
}

uint16_t jes_get_array_size(struct jes_context* ctx, struct jes_element* array)
{
  uint16_t array_size = 0;
  struct jes_node* iter = NULL;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return 0;
  }

  ctx->status = JES_NO_ERROR;

  if ((array == NULL) || (!jes_validate_node(ctx, (struct jes_node*)array)) || (array->type != JES_ARRAY)) {
    ctx->status = JES_INVALID_PARAMETER;
    return 0;
  }

  iter = GET_FIRST_CHILD(ctx->node_mng, (struct jes_node*)array);
  if (iter) {
    for (array_size = 0; iter != NULL; array_size++) {
      iter = GET_SIBLING(ctx->node_mng, iter);
    }
  }

  return array_size;
}

struct jes_element* jes_get_array_value(struct jes_context* ctx, struct jes_element* array, int32_t index)
{
  struct jes_node* iter = NULL;
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

  iter = GET_FIRST_CHILD(ctx->node_mng, (struct jes_node*)array);
  for (; iter && index > 0; index--) {
    iter = GET_SIBLING(ctx->node_mng, iter);
  }

  if (iter) {
    return &iter->json_tlv;
  }

  /* We shouldn't land here. */
  ctx->status = JES_BROKEN_TREE;
  assert(0);
  return NULL;
}

static bool jes_validate_tlv(struct jes_context* ctx, enum jes_type type, size_t length, const char* value)
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

struct jes_element* jes_add_element(struct jes_context* ctx, struct jes_element* parent, enum jes_type type, const char* value, size_t value_length)
{
  struct jes_node* new_node = NULL;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  ctx->status = JES_NO_ERROR;

  if ((parent == NULL) && (ctx->node_mng.root != NULL)) {
    /* JSON is not empty. Invalid request. */
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  if ((parent != NULL) && (!jes_validate_node(ctx, (struct jes_node*)parent))) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  switch (type) {
    case JES_NUMBER:
      if (jes_tokenizer_validate_number(ctx, value, value_length) != JES_NO_ERROR) {
        ctx->status = JES_INVALID_PARAMETER;
        return NULL;
      }
      break;
    case JES_KEY: /* Fall through is intended */
    case JES_STRING:
      if (jes_tokenizer_validate_string(ctx, value, value_length) != JES_NO_ERROR) {
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

  new_node = jes_tree_insert_node(ctx,
                                  (struct jes_node*)parent,
                                  GET_LAST_CHILD(ctx->node_mng,
                                  (struct jes_node*)parent),
                                  type, value_length, value);

  return (struct jes_element*)new_node;
}

struct jes_element* jes_add_key(struct jes_context* ctx, struct jes_element* parent, const char* keyword, size_t keyword_length)
{
  struct jes_node* object = NULL;
  struct jes_node* new_node = NULL;

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

  if (jes_tokenizer_validate_string(ctx, keyword, keyword_length) != JES_NO_ERROR) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  if (parent->type == JES_KEY) {
    /* The key must be added to an existing key and must be embedded in an OBJECT */
    object = GET_FIRST_CHILD(ctx->node_mng, (struct jes_node*)parent);
    if (object == NULL) {
      object = jes_tree_insert_node(ctx, (struct jes_node*)parent, GET_LAST_CHILD(ctx->node_mng, (struct jes_node*)parent), JES_OBJECT, 1, "{");
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
  new_node = jes_tree_insert_key_node(ctx, object, GET_LAST_CHILD(ctx->node_mng, object), keyword_length, keyword);

  return (struct jes_element*)new_node;
}

struct jes_element* jes_add_key_before(struct jes_context* ctx, struct jes_element* key, const char* keyword, size_t keyword_length)
{
  struct jes_node* new_node = NULL;
  struct jes_node* parent = NULL;
  struct jes_node* iter = NULL;
  struct jes_node* before = NULL;
  struct jes_node* key_node = (struct jes_node*)key;


  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  ctx->status = JES_NO_ERROR;

  if ((key == NULL) || !jes_validate_node(ctx, key_node) || (key->type != JES_KEY) || (keyword == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  parent = GET_PARENT(ctx->node_mng, key_node);
  assert(parent != NULL);
  assert(NODE_TYPE(parent) == JES_OBJECT);

  for (iter = GET_FIRST_CHILD(ctx->node_mng, parent); iter != NULL; iter = GET_SIBLING(ctx->node_mng, iter)) {
    assert(NODE_TYPE(iter) == JES_KEY);
    if (iter == key_node) {
      new_node = jes_tree_insert_key_node(ctx, parent, before, keyword_length, keyword);
      break;
    }
    before = iter;
  }

  return (struct jes_element*)new_node;
}

struct jes_element* jes_add_key_after(struct jes_context* ctx, struct jes_element* key, const char* keyword, size_t keyword_length)
{
  struct jes_node* key_node = (struct jes_node*)key;
  struct jes_node* new_node = NULL;
  struct jes_node* parent = NULL;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  ctx->status = JES_NO_ERROR;

  if ((key == NULL) || !jes_validate_node(ctx, key_node) || (key->type != JES_KEY) || (keyword == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  parent = GET_PARENT(ctx->node_mng, key_node);
  assert(parent != NULL);
  assert(NODE_TYPE(parent) == JES_OBJECT);

  new_node = jes_tree_insert_key_node(ctx, parent, (struct jes_node*)key, keyword_length, keyword);

  return (struct jes_element*)new_node;
}

enum jes_status jes_update_key(struct jes_context* ctx, struct jes_element* key, const char* keyword, size_t keyword_length)
{
  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return JES_INVALID_CONTEXT;
  }

  ctx->status = JES_NO_ERROR;

  if (!jes_validate_node(ctx, (struct jes_node*)key) || (key->type != JES_KEY) || (keyword == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return ctx->status;
  }

  if (jes_tokenizer_validate_string(ctx, keyword, keyword_length) != JES_NO_ERROR) {
    ctx->status = JES_INVALID_PARAMETER;
    return ctx->status;
  }

  key->length = keyword_length;
  key->value = keyword;

  return JES_NO_ERROR;
}

struct jes_element* jes_update_key_value(struct jes_context* ctx, struct jes_element* key, enum jes_type type, const char* value, size_t value_length)
{
  struct jes_element* key_value = NULL;

  /* First delete the old value of the key if exists. */
  jes_tree_delete_node(ctx, GET_FIRST_CHILD(ctx->node_mng, (struct jes_node*)key));
  key_value = jes_add_element(ctx, key, type, value, value_length);

  return key_value;
}

struct jes_element* jes_update_key_value_to_object(struct jes_context* ctx, struct jes_element* key)
{
  return jes_update_key_value(ctx, key, JES_OBJECT, "{", sizeof("{") - 1);
}

struct jes_element* jes_update_key_value_to_array(struct jes_context* ctx, struct jes_element* key)
{
  return jes_update_key_value(ctx, key, JES_ARRAY, "[", sizeof("[") - 1);
}

struct jes_element* jes_update_key_value_to_true(struct jes_context* ctx, struct jes_element* key)
{
  return jes_update_key_value(ctx, key, JES_TRUE, "true", sizeof("true") - 1);
}

struct jes_element* jes_update_key_value_to_false(struct jes_context* ctx, struct jes_element* key)
{
  return jes_update_key_value(ctx, key, JES_FALSE, "false", sizeof("false") - 1);
}

struct jes_element* jes_update_key_value_to_null(struct jes_context* ctx, struct jes_element* key)
{
  return jes_update_key_value(ctx, key, JES_NULL, "null", sizeof("null") - 1);
}

struct jes_element* jes_update_array_value(struct jes_context* ctx, struct jes_element* array, int32_t index, enum jes_type type, const char* value, size_t value_length)
{
  struct jes_node* target_node = NULL;
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

  if ((array_size == 0) || (index < 0) || (index > array_size)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  for(target_node = GET_FIRST_CHILD(ctx->node_mng, (struct jes_node*)array); target_node != NULL; target_node = GET_SIBLING(ctx->node_mng, target_node)) {
    if (index-- == 0) {
      break;
    }
  }

  if (target_node) {
    /* We'll not delete the target_node to keep the original array order. Just update its JSON TLV.
     * The rest of the branch however must be removed. */
    jes_tree_delete_node(ctx, GET_FIRST_CHILD(ctx->node_mng, target_node));
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

struct jes_element* jes_append_array_value(struct jes_context* ctx, struct jes_element* array, enum jes_type type, const char* value, size_t value_length)
{
  struct jes_node* anchor_node = NULL;
  struct jes_node* new_node = NULL;

  if (!ctx || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  ctx->status = JES_NO_ERROR;

  if ((array == NULL) || !jes_validate_node(ctx, (struct jes_node*)array) || (array->type != JES_ARRAY) || (value == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  new_node = jes_tree_insert_node(ctx, (struct jes_node*)array, GET_LAST_CHILD(ctx->node_mng, (struct jes_node*)array), type, value_length, value);

  return (struct jes_element*)new_node;
}

struct jes_element* jes_add_array_value(struct jes_context* ctx, struct jes_element* array, int32_t index, enum jes_type type, const char* value, size_t value_length)
{
  struct jes_node* anchor_node = NULL;
  struct jes_node* prev_node = NULL;
  struct jes_node* new_node = NULL;
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
    return jes_append_array_value(ctx, array, type, value, value_length);
  }

  for (anchor_node = GET_FIRST_CHILD(ctx->node_mng, (struct jes_node*)array); anchor_node != NULL; anchor_node = GET_SIBLING(ctx->node_mng, anchor_node)) {
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

size_t jes_get_element_count(struct jes_context* ctx)
{
  if ((ctx != NULL) && JES_IS_INITIATED(ctx)) {
    return ctx->node_mng.node_count;
  }
  return 0;
}

jes_status jes_get_status(struct jes_context* ctx)
{
  if ((ctx != NULL) && JES_IS_INITIATED(ctx)) {
    return ctx->status;
  }
  return JES_INVALID_CONTEXT;
}

size_t jes_get_element_capacity(struct jes_context* ctx)
{
  if ((ctx != NULL) && JES_IS_INITIATED(ctx)) {
    return ctx->node_mng.capacity;
  }
  return 0;
}

struct jes_element* jes_load(struct jes_context* ctx, const char* json_data, size_t json_length)
{
  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if ((json_data == NULL) || (json_length == 0)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  jes_reset(ctx);

  ctx->serdes.tokenizer.json_data = json_data;
  ctx->serdes.tokenizer.json_length = json_length;
  jes_parse(ctx);

  return ctx->status == JES_NO_ERROR ? (struct jes_element*)ctx->node_mng.root : NULL;
}

void jes_set_path_separator(struct jes_context* ctx, char delimiter)
{
  if ((ctx != NULL) && JES_IS_INITIATED(ctx)) {
    ctx->path_separator = delimiter;
  }
}

size_t jes_get_context_size(void)
{
  return sizeof(struct jes_context);
}

size_t jes_get_node_size(void)
{
  return sizeof(struct jes_node);
}

struct jes_stat jes_get_stat(struct jes_context* ctx)
{
  struct jes_stat stat = { 0 };

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return stat;
  }

  ctx->serdes.iter = NULL;

  while (NULL != jes_serializer_get_node(ctx)) {
    switch (ctx->serdes.iter->json_tlv.type) {
      case JES_OBJECT:
        stat.objects++;
        break;
      case JES_KEY:
        stat.keys++;
        break;
      case JES_ARRAY:
        stat.arrays++;
        break;
      case JES_STRING:
      case JES_NUMBER:
      case JES_TRUE:
      case JES_FALSE:
      case JES_NULL:
        stat.values++;
        break;
      default:
        assert(0);
        break;
    }
  }
  return stat;
}

struct jes_workspace_stat jes_get_workspace_stat(struct jes_context* ctx)
{
  struct jes_workspace_stat stat = { 0 };

  if ((ctx != NULL) && JES_IS_INITIATED(ctx)) {
    stat.context = jes_get_context_size();
    stat.node_mng = ctx->node_mng.size;
    stat.node_mng_used = sizeof(struct jes_node_mng_context) + ctx->node_mng.node_count * sizeof(struct jes_node);
    stat.hash_table = ctx->hash_table.size;
    stat.hash_table_used = 0; /* TODO: Not implemented yet. */
  }

  return stat;
}