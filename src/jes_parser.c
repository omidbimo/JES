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

#ifndef NDEBUG
  #define JES_LOG_NODE  jes_log_node
#else
  #define JES_LOG_NODE(...)
#endif

static struct jes_node* jes_transform_object_to_key(struct jes_context* ctx, struct jes_node* object,
                                    const char* keyword, uint16_t keyword_length)
{
  struct jes_node* key = NULL;

  assert(object->json_tlv.type == JES_EMPTY_OBJECT);

  /* No duplicate keys in the same object are allowed. */
  if (ctx->find_key_fn(ctx, GET_PARENT(ctx, object), keyword, keyword_length) != NULL) {
    ctx->status = JES_DUPLICATE_KEY;
  }
  else
  {
    /* Transform the OBJECT into a KEY. */
    key = object;
    key->json_tlv.type = JES_KEY;
    key->json_tlv.length = keyword_length;
    key->json_tlv.value = keyword;

#ifdef JES_ENABLE_FAST_KEY_SEARCH
    jes_hash_table_add(ctx, GET_PARENT(ctx, key), key);
#endif

    JES_LOG_NODE("\n    > ", JES_NODE_INDEX(ctx, key), key->json_tlv.type,
                  key->json_tlv.length, key->json_tlv.value,
                  key->parent, key->sibling, key->first_child, key->last_child, "");
  }

  return key;
}

static inline void jes_parser_on_opening_brace(struct jes_context *ctx)
{
  if ((ctx->state != JES_EXPECT_OBJECT) &&
      (ctx->state != JES_EXPECT_KEY_VALUE)  &&
      (ctx->state != JES_EXPECT_ARRAY_VALUE)) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }

  if (ctx->root == NULL) {
    ctx->iter = jes_insert_node(ctx, ctx->iter, GET_LAST_CHILD(ctx, ctx->iter),
                            JES_ENTRY_OBJECT, ctx->token.length, &ctx->json_data[ctx->token.offset]);
  }
  /* Append an empty OBJECT node. */
  ctx->iter = jes_insert_node(ctx, ctx->iter, GET_LAST_CHILD(ctx, ctx->iter),
                              JES_EMPTY_OBJECT, ctx->token.length, &ctx->json_data[ctx->token.offset]);
  ctx->state = JES_EXPECT_KEY;
}

static inline void jes_parser_on_closing_brace(struct jes_context *ctx)
{
#ifdef JES_PARSER_LOG
          printf("\n                         state: %d", ctx->state);
    JES_LOG_NODE("\n                          -->> ", JES_NODE_INDEX(ctx, ctx->iter), ctx->iter->json_tlv.type,
                  ctx->iter->json_tlv.length, ctx->iter->json_tlv.value,
                  ctx->iter->parent, ctx->iter->sibling, ctx->iter->first_child, ctx->iter->last_child, "");
#endif
  if ((ctx->state != JES_EXPECT_KEY) &&
      (ctx->state != JES_HAVE_KEY_VALUE)) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }

  /* '}' indicates the end of a key:value sequence (object).
      An iteration in the direction of object's parent is required.
   */

  /* {} is valid and indicates an empty OBJECT. */
  if ((NODE_TYPE(ctx->iter) == JES_EMPTY_OBJECT) && (ctx->state == JES_EXPECT_KEY)) {
    if (HAS_CHILD(ctx->iter)) {
    /* A closing OBJECT in the EXPECT_KEY state, can only be an empty object and must have no children */
      ctx->status = JES_UNEXPECTED_TOKEN;
      ctx->ext_status = ctx->state;
      return;
    }
  }
  else {
    /* Getting the key node of the key:value sequence that is just closed. */
    ctx->iter = GET_PARENT(ctx, ctx->iter);
    assert(ctx->iter != NULL);
  }

  if (ctx->iter) {
    if (PARENT_TYPE(ctx, ctx->iter) == JES_ARRAY) {
      ctx->state = JES_HAVE_ARRAY_VALUE;
    }
    else if (PARENT_TYPE(ctx, ctx->iter) == JES_KEY) {
      ctx->state = JES_HAVE_KEY_VALUE;
    }
    else if (PARENT_TYPE(ctx, ctx->iter) == JES_ENTRY_OBJECT) {
      ctx->state = JES_EXPECT_EOF;
      ctx->iter = GET_PARENT(ctx, ctx->iter);
    }
    else {
      assert(0);
    }
  }
  else {
    ctx->state = JES_EXPECT_EOF;
  }
#ifdef JES_PARSER_LOG
      JES_LOG_NODE("\n                          <<-- ", JES_NODE_INDEX(ctx, ctx->iter), ctx->iter->json_tlv.type,
                  ctx->iter->json_tlv.length, ctx->iter->json_tlv.value,
                  ctx->iter->parent, ctx->iter->sibling, ctx->iter->first_child, ctx->iter->last_child, "");
#endif
}

static inline void jes_parser_on_opening_bracket(struct jes_context *ctx)
{
  if ((ctx->state != JES_EXPECT_KEY_VALUE) &&
      (ctx->state != JES_EXPECT_ARRAY_VALUE)) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }

  ctx->iter = jes_insert_node(ctx, ctx->iter, GET_LAST_CHILD(ctx, ctx->iter),
                              JES_ARRAY, ctx->token.length, &ctx->json_data[ctx->token.offset]);
  ctx->state = JES_EXPECT_ARRAY_VALUE;
}

static inline void jes_parser_on_closing_bracket(struct jes_context *ctx)
{
#ifdef JES_PARSER_LOG
    printf("\n                          state: %d", ctx->state);
    JES_LOG_NODE("\n                          -->> ", JES_NODE_INDEX(ctx, ctx->iter), ctx->iter->json_tlv.type,
                  ctx->iter->json_tlv.length, ctx->iter->json_tlv.value,
                  ctx->iter->parent, ctx->iter->sibling, ctx->iter->first_child, ctx->iter->last_child, "");
#endif
  if ((ctx->state != JES_EXPECT_ARRAY_VALUE) &&
      (ctx->state != JES_HAVE_ARRAY_VALUE)) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }

  /* ']' indicates the end of an Array.
      An iteration in the direction of object's parent is required.
  */

  /* [] is valid and indicates an empty ARRAY */
  if ((NODE_TYPE(ctx->iter) == JES_ARRAY) && (ctx->state == JES_EXPECT_ARRAY_VALUE)) {
    if (HAS_CHILD(ctx->iter)) {
    /* An array in expecting value state, can only be an empty and must have no values */
      ctx->status = JES_UNEXPECTED_TOKEN;
      ctx->ext_status = ctx->state;
      return;
    }
  }
  else {
    ctx->iter = GET_PARENT(ctx, ctx->iter);
    assert(ctx->iter != NULL);
  }

  if (!ctx->iter) {
    ctx->status = JES_PARSING_FAILED;
    return;
  }

  if (PARENT_TYPE(ctx, ctx->iter) == JES_ARRAY) {
    ctx->state = JES_HAVE_ARRAY_VALUE;
  }
  else if (PARENT_TYPE(ctx, ctx->iter) == JES_KEY) {
    ctx->state = JES_HAVE_KEY_VALUE;
  }
  else {
    assert(0);
  }
#ifdef JES_PARSER_LOG
    JES_LOG_NODE("\n                           <<-- ", JES_NODE_INDEX(ctx, ctx->iter), ctx->iter->json_tlv.type,
                  ctx->iter->json_tlv.length, ctx->iter->json_tlv.value,
                  ctx->iter->parent, ctx->iter->sibling, ctx->iter->first_child, ctx->iter->last_child, "");
#endif
}

static inline void jes_parser_on_colon(struct jes_context *ctx)
{
  if (ctx->state == JES_EXPECT_COLON) {
    ctx->state = JES_EXPECT_KEY_VALUE;
  }
  else {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }
}

static inline void jes_parser_on_comma(struct jes_context *ctx)
{
#ifdef JES_PARSER_LOG
    printf("\n                          state: %d", ctx->state);
    JES_LOG_NODE("\n                          -->> ", JES_NODE_INDEX(ctx, ctx->iter), ctx->iter->json_tlv.type,
                  ctx->iter->json_tlv.length, ctx->iter->json_tlv.value,
                  ctx->iter->parent, ctx->iter->sibling, ctx->iter->first_child, ctx->iter->last_child, "");
#endif
  if (ctx->state == JES_HAVE_KEY_VALUE) {
    ctx->state = JES_EXPECT_KEY;
    /* { "key1": { "key2": "value",... because we don't store OBJECT nodes,
     * a new key3 shall be added as a child of key1. We need two iterations from value to key1.
     * First iteration is the next. */
    ctx->iter = GET_PARENT(ctx, ctx->iter);
  }
  else if (ctx->state == JES_HAVE_ARRAY_VALUE) {
    ctx->state = JES_EXPECT_ARRAY_VALUE;
  }
  else {
    ctx->status = JES_UNEXPECTED_TOKEN;
        ctx->ext_status = ctx->state;
    return;
  }

  /* Delimiter tokens can trigger upward iteration in the direction of parent node.
     A ',' indicates the end of a value.
       - If the value is inside an array, iterate back to the parent array node.
       - Otherwise, iterate back to the parent object.
  */

    ctx->iter = GET_PARENT(ctx, ctx->iter);
#ifdef JES_PARSER_LOG
      JES_LOG_NODE("\n                          <<-- ", JES_NODE_INDEX(ctx, ctx->iter), ctx->iter->json_tlv.type,
                  ctx->iter->json_tlv.length, ctx->iter->json_tlv.value,
                  ctx->iter->parent, ctx->iter->sibling, ctx->iter->first_child, ctx->iter->last_child, "");
#endif
}

static inline void jes_parser_on_string(struct jes_context *ctx)
{
#ifdef JES_PARSER_LOG
      printf("\n                          state: %d", ctx->state);
        JES_LOG_NODE("\n                          -->> ", JES_NODE_INDEX(ctx, ctx->iter), ctx->iter->json_tlv.type,
                  ctx->iter->json_tlv.length, ctx->iter->json_tlv.value,
                  ctx->iter->parent, ctx->iter->sibling, ctx->iter->first_child, ctx->iter->last_child, "");
#endif
  if (ctx->state == JES_EXPECT_KEY) {
    if (ctx->iter->json_tlv.type == JES_EMPTY_OBJECT) {
      ctx->iter = jes_transform_object_to_key(ctx, ctx->iter, &ctx->json_data[ctx->token.offset], ctx->token.length);
    }
    else {
      /* Append the key */
      ctx->iter = jes_insert_key_node(ctx, ctx->iter, GET_LAST_CHILD(ctx, ctx->iter), ctx->token.length, &ctx->json_data[ctx->token.offset]);
    }

    ctx->state = JES_EXPECT_COLON;
  }
  else if (ctx->state == JES_EXPECT_KEY_VALUE) {
    /* Append value node */
    ctx->iter = jes_insert_node(ctx, ctx->iter, GET_LAST_CHILD(ctx, ctx->iter), JES_STRING, ctx->token.length, &ctx->json_data[ctx->token.offset]);
    ctx->state = JES_HAVE_KEY_VALUE;
  }
  else if (ctx->state == JES_EXPECT_ARRAY_VALUE) {
    ctx->iter = jes_insert_node(ctx, ctx->iter, GET_LAST_CHILD(ctx, ctx->iter), JES_STRING, ctx->token.length, &ctx->json_data[ctx->token.offset]);
    ctx->state = JES_HAVE_ARRAY_VALUE;
  }
  else {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }
#ifdef JES_PARSER_LOG
          JES_LOG_NODE("\n                          <<-- ", JES_NODE_INDEX(ctx, ctx->iter), ctx->iter->json_tlv.type,
                  ctx->iter->json_tlv.length, ctx->iter->json_tlv.value,
                  ctx->iter->parent, ctx->iter->sibling, ctx->iter->first_child, ctx->iter->last_child, "");
#endif
}

static inline void jes_parser_on_value(struct jes_context *ctx, enum jes_type value_type)
{
  if (ctx->state == JES_EXPECT_KEY_VALUE) {
    ctx->state = JES_HAVE_KEY_VALUE;
  }
  else if (ctx->state == JES_EXPECT_ARRAY_VALUE) {
    ctx->state = JES_HAVE_ARRAY_VALUE;
  }
  else {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }

  ctx->iter = jes_insert_node(ctx, ctx->iter, GET_LAST_CHILD(ctx, ctx->iter), value_type, ctx->token.length, &ctx->json_data[ctx->token.offset]);
}

void jes_parse(struct jes_context *ctx)
{
  assert(ctx != NULL);
  assert(ctx->json_data != NULL);
  assert(ctx->json_size != 0);

  ctx->state = JES_EXPECT_OBJECT;

  do {
    if (jes_get_token(ctx) != JES_NO_ERROR) break;

    switch (ctx->token.type) {

      case JES_TOKEN_EOF:
        break;

      case JES_TOKEN_OPENING_BRACE:
        jes_parser_on_opening_brace(ctx);
        break;

      case JES_TOKEN_CLOSING_BRACE:
        jes_parser_on_closing_brace(ctx);
        break;

      case JES_TOKEN_OPENING_BRACKET:
        jes_parser_on_opening_bracket(ctx);
        break;

      case JES_TOKEN_CLOSING_BRACKET:
        jes_parser_on_closing_bracket(ctx);
        break;

      case JES_TOKEN_COLON:
        jes_parser_on_colon(ctx);
        break;

      case JES_TOKEN_COMMA:
        jes_parser_on_comma(ctx);
        break;

      case JES_TOKEN_STRING:
        jes_parser_on_string(ctx);
        break;

      case JES_TOKEN_FALSE:
        jes_parser_on_value(ctx, JES_FALSE);
        break;

      case JES_TOKEN_TRUE:
        jes_parser_on_value(ctx, JES_TRUE);
        break;

      case JES_TOKEN_NULL:
        jes_parser_on_value(ctx, JES_NULL);
        break;

      case JES_TOKEN_NUMBER:
        jes_parser_on_value(ctx, JES_NUMBER);
        break;

      case JES_TOKEN_INVALID:
        /* Do not overwrite errors from Tokenizer */
        if (ctx->status == JES_NO_ERROR) {
          ctx->status = JES_PARSING_FAILED;
        }
        break;

      default:
        assert(0);
        ctx->status = JES_PARSING_FAILED;
        break;
    }
  } while ((ctx->status == JES_NO_ERROR) && (ctx->token.type != JES_TOKEN_EOF));

  if (ctx->status == JES_NO_ERROR) {
    if ((ctx->iter == NULL) || (NODE_TYPE(ctx->iter) != JES_ENTRY_OBJECT)) {
      ctx->status = JES_PARSING_FAILED;
    }
    else {
      ctx->root = ctx->iter;
    }
  }
}