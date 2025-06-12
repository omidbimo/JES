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

static inline void jes_parser_process_opening_brace(struct jes_deserializer_context* ctx)
{
  /* Append node */
  ctx->iter = jes_tree_insert_node(ctx->jes_ctx, ctx->iter, GET_LAST_CHILD(ctx->jes_ctx->node_mng, ctx->iter),
                              JES_OBJECT, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
  ctx->state = JES_EXPECT_KEY;
}


/**
 * @brief Handles the parsing of a closing brace '}' in a JSON document.
 */
static inline enum jes_status jes_parser_process_closing_brace(struct jes_deserializer_context* ctx)
{
  /* Handle special case: empty object "{}" */
  if ((NODE_TYPE(ctx->iter) == JES_OBJECT) && (ctx->state == JES_EXPECT_KEY)) {
    /* An object in EXPECT_KEY state, can only be an empty object with no children */
    if (HAS_CHILD(ctx->iter)) {
      return JES_UNEXPECTED_TOKEN;
    }
  }

  /* If current node is not an OBJECT type, navigate up to find the parent OBJECT */
  if (NODE_TYPE(ctx->iter) != JES_OBJECT) {
    ctx->iter = jes_tree_get_parent_node_by_type(ctx->jes_ctx, ctx->iter, JES_OBJECT);
    assert(ctx->iter != NULL);
  }

  if (ctx->iter != NULL) {
    /* Now that we've found the object being closed, move up one more level
     * to the parent container (either an object or array) to transition based on
     * the parent type. */
    ctx->iter = jes_tree_get_container_parent_node(ctx->jes_ctx, ctx->iter);
  }

  if (ctx->iter == NULL) {
    /* We've reached the root level, expect end of file */
    ctx->state = JES_EXPECT_EOF;
    return JES_NO_ERROR;
  }

  /* Update parser state based on the type of parent container we've moved to */
  switch (NODE_TYPE(ctx->iter)) {
    case JES_ARRAY:
      /* We're now inside an array, ready for the next value or ARRAY closing */
      ctx->state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_OBJECT:
      /* We're now inside an object, just finished a key-value pair */
      ctx->state = JES_HAVE_KEY_VALUE;
      break;
    default:
      /* This should never happen - parent should always be object or array */
      assert(0);
      break;
  }

  return JES_NO_ERROR;
}

static inline void jes_parser_process_opening_bracket(struct jes_deserializer_context* ctx)
{
  ctx->iter = jes_tree_insert_node(ctx->jes_ctx, ctx->iter, GET_LAST_CHILD(ctx->jes_ctx->node_mng, ctx->iter),
                              JES_ARRAY, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
  ctx->state = JES_EXPECT_ARRAY_VALUE;
}

/**
 * @brief Handles the parsing of a closing bracket ']' in a JSON document.
 */
static inline enum jes_status jes_parser_process_closing_bracket(struct jes_deserializer_context* ctx)
{
  /* Handle special case: empty array "[]" */
  if ((NODE_TYPE(ctx->iter) == JES_ARRAY) && (ctx->state == JES_EXPECT_ARRAY_VALUE)) {
    /* An array in expecting state, can only be an empty and must have no values */
    if (HAS_CHILD(ctx->iter)) {
      return JES_UNEXPECTED_TOKEN;
    }
  }

  /* If current node is not an ARRAY type, navigate up to find the parent ARRAY */
  if (NODE_TYPE(ctx->iter) != JES_ARRAY) {
    ctx->iter = jes_tree_get_parent_node_by_type(ctx->jes_ctx, ctx->iter, JES_ARRAY);
    assert(ctx->iter != NULL);
  }

  if (ctx->iter != NULL) {
    /* Now that we've found the array being closed, move up one more level
     * to the parent container (either an object or array) to continue parsing */
    ctx->iter = jes_tree_get_container_parent_node(ctx->jes_ctx, ctx->iter);
  }

  if (ctx->iter == NULL) {
#if defined(JES_ALLOW_TOPLEVEL_ARRAY)
    /* We've reached the root level, expect end of file */
    ctx->state = JES_EXPECT_EOF;
#else
    /* We've reached the root level, This should never inside an array happen */
    return JES_PARSING_FAILED;
#endif
    return JES_NO_ERROR;
  }

  /* Update parser state based on the type of parent container we've moved to */
  switch (NODE_TYPE(ctx->iter)) {
    case JES_ARRAY:
      /* We're now inside an array, ready for the next value or ARRAY closing */
      ctx->state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_OBJECT:
      /* We're now inside an object, just finished a key-value pair */
      ctx->state = JES_HAVE_KEY_VALUE;
      break;
    default:
      /* This should never happen - parent should always be object or array */
      assert(0);
      break;
  }

  return JES_NO_ERROR;
}

static inline enum jes_status jes_parser_process_comma(struct jes_deserializer_context* ctx)
{
  enum jes_status status = JES_NO_ERROR;
  /* Handle navigation within the JSON structure after a comma:
   * - For container nodes (objects/arrays), validate they have children
   * - For value nodes, move back up to the parent container
   */
  if ((NODE_TYPE(ctx->iter) == JES_OBJECT) || (NODE_TYPE(ctx->iter) == JES_ARRAY)) {
    if (!HAS_CHILD(ctx->iter)) {
      status = JES_UNEXPECTED_TOKEN;
    }
  }
  else {
    /* Current node is a value - navigate up to the parent container (object or array) */
    ctx->iter = jes_tree_get_container_parent_node(ctx->jes_ctx, ctx->iter);
  }

  return status;
}

static inline enum jes_status jes_parser_process_start_state(struct jes_deserializer_context* ctx)
{
  enum jes_status status = JES_NO_ERROR;

  switch (ctx->tokenizer.token.type) {
    case JES_TOKEN_OPENING_BRACE:
      jes_parser_process_opening_brace(ctx);
      break;
    case JES_TOKEN_OPENING_BRACKET:
#if defined(JES_ALLOW_TOPLEVEL_ARRAY) || defined(JES_ALLOW_TOPLEVEL_ANY)
      jes_parser_process_opening_bracket(ctx);
#else
      ctx->state = JES_UNEXPECTED_TOKEN;
#endif
      break;
    default:
#if defined(JES_ALLOW_TOPLEVEL_ANY)
      ctx->state = JES_EXPECT_EOF;
#else
      status = JES_UNEXPECTED_TOKEN;
#endif
      break;
  }

  return status;
}

static inline enum jes_status jes_parser_process_expect_key_state(struct jes_deserializer_context* ctx)
{
  enum jes_status status = JES_NO_ERROR;

  switch (ctx->tokenizer.token.type) {
    case JES_TOKEN_STRING:
      /* Append the key */
      ctx->iter = jes_tree_insert_key_node(ctx->jes_ctx, ctx->iter, GET_LAST_CHILD(ctx->jes_ctx->node_mng, ctx->iter),
                           ctx->tokenizer.token.length, ctx->tokenizer.token.value);
      ctx->state = JES_EXPECT_COLON;
    break;
  case JES_TOKEN_CLOSING_BRACE:
    status = jes_parser_process_closing_brace(ctx);
    break;
  default:
    status = JES_UNEXPECTED_TOKEN;
    break;
  }

  return status;
}

static inline enum jes_status jes_parser_process_expect_key_value_state(struct jes_deserializer_context* ctx)
{
  enum jes_status status = JES_NO_ERROR;
  enum jes_type value_type;

  switch (ctx->tokenizer.token.type) {
    case JES_TOKEN_STRING:
      value_type = JES_STRING;
      ctx->state = JES_HAVE_KEY_VALUE;
      break;
    case JES_TOKEN_NUMBER:
      value_type = JES_NUMBER;
      ctx->state = JES_HAVE_KEY_VALUE;
      break;
    case JES_TOKEN_TRUE:
      value_type = JES_TRUE;
      ctx->state = JES_HAVE_KEY_VALUE;
      break;
    case JES_TOKEN_FALSE:
      value_type = JES_FALSE;
      ctx->state = JES_HAVE_KEY_VALUE;
      break;
    case JES_TOKEN_NULL:
      value_type = JES_NULL;
      ctx->state = JES_HAVE_KEY_VALUE;
      break;
    case JES_TOKEN_OPENING_BRACKET:
      value_type = JES_ARRAY;
      ctx->state = JES_EXPECT_ARRAY_VALUE;
      break;
    case JES_TOKEN_OPENING_BRACE:
      value_type = JES_OBJECT;
      ctx->state = JES_EXPECT_KEY;
      break;
    default:
      return JES_UNEXPECTED_TOKEN;
  }

  ctx->iter = jes_tree_insert_node(ctx->jes_ctx, ctx->iter, GET_LAST_CHILD(ctx->jes_ctx->node_mng, ctx->iter),
           value_type, ctx->tokenizer.token.length, ctx->tokenizer.token.value);

  return status;
}

static inline enum jes_status jes_parser_process_have_key_value_state(struct jes_deserializer_context* ctx)
{
  enum jes_status status = JES_NO_ERROR;

  switch (ctx->tokenizer.token.type) {
    case JES_TOKEN_CLOSING_BRACE:
      status = jes_parser_process_closing_brace(ctx);
      break;
    case JES_TOKEN_COMMA:
      jes_parser_process_comma(ctx);
      assert(NODE_TYPE(ctx->iter) == JES_OBJECT);
      ctx->state = JES_EXPECT_KEY;
      break;
    default:
      status = JES_UNEXPECTED_TOKEN;
      break;
  }

  return status;
}

static inline enum jes_status jes_parser_process_expect_array_value_state(struct jes_deserializer_context* ctx)
{
  enum jes_status status = JES_NO_ERROR;

  switch (ctx->tokenizer.token.type) {
    case JES_TOKEN_STRING:
      ctx->iter = jes_tree_insert_node(ctx->jes_ctx, ctx->iter, GET_LAST_CHILD(ctx->jes_ctx->node_mng, ctx->iter),
             JES_STRING, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
            ctx->state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_TOKEN_NUMBER:
      ctx->iter = jes_tree_insert_node(ctx->jes_ctx, ctx->iter, GET_LAST_CHILD(ctx->jes_ctx->node_mng, ctx->iter),
             JES_NUMBER, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
            ctx->state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_TOKEN_TRUE:
      ctx->iter = jes_tree_insert_node(ctx->jes_ctx, ctx->iter, GET_LAST_CHILD(ctx->jes_ctx->node_mng, ctx->iter),
             JES_TRUE, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
            ctx->state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_TOKEN_FALSE:
      ctx->iter = jes_tree_insert_node(ctx->jes_ctx, ctx->iter, GET_LAST_CHILD(ctx->jes_ctx->node_mng, ctx->iter),
             JES_FALSE, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
            ctx->state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_TOKEN_NULL:
      ctx->iter = jes_tree_insert_node(ctx->jes_ctx, ctx->iter, GET_LAST_CHILD(ctx->jes_ctx->node_mng, ctx->iter),
             JES_NULL, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
            ctx->state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_TOKEN_OPENING_BRACKET:
      ctx->iter = jes_tree_insert_node(ctx->jes_ctx, ctx->iter, GET_LAST_CHILD(ctx->jes_ctx->node_mng, ctx->iter),
             JES_ARRAY, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
      ctx->state = JES_EXPECT_ARRAY_VALUE;
      break;
    case JES_TOKEN_CLOSING_BRACKET:
      status = jes_parser_process_closing_bracket(ctx);
      break;
    case JES_TOKEN_OPENING_BRACE:
      jes_parser_process_opening_brace(ctx);
      break;
    default:
      status = JES_UNEXPECTED_TOKEN;
      break;
  }

  return status;
}

static inline enum jes_status jes_parser_process_have_array_value_state(struct jes_deserializer_context* ctx)
{
  enum jes_status status = JES_NO_ERROR;

  switch (ctx->tokenizer.token.type) {
    case JES_TOKEN_CLOSING_BRACKET:
      assert(ctx->iter != NULL);
      status = jes_parser_process_closing_bracket(ctx);
      break;
    case JES_TOKEN_COMMA:
      jes_parser_process_comma(ctx);
      assert(NODE_TYPE(ctx->iter) == JES_ARRAY);
      ctx->state = JES_EXPECT_ARRAY_VALUE;
      break;
    default:
      status = JES_UNEXPECTED_TOKEN;
      break;
  }

  return status;
}

static inline enum jes_status jes_parser_process_expect_colon_state(struct jes_deserializer_context* ctx)
{
  enum jes_status status = JES_NO_ERROR;

  if (ctx->tokenizer.token.type != JES_TOKEN_COLON) {
    status = JES_UNEXPECTED_TOKEN;
  }
  else {
    ctx->state = JES_EXPECT_KEY_VALUE;
  }

  return status;
}

void jes_parse(struct jes_context *ctx)
{
  struct jes_deserializer_context parser = { 0 };

  assert(ctx != NULL);
  assert(ctx->json_data != NULL);
  assert(ctx->json_size != 0);

  parser.state = JES_START;
  parser.tokenizer.json_data = ctx->json_data;
  parser.tokenizer.json_length = ctx->json_size;
  parser.jes_ctx = ctx;

  jes_tokenizer_set_cursor(&parser.tokenizer, ctx->json_data);

  while ((ctx->status == JES_NO_ERROR) && (parser.state != JES_END) && (jes_tokenizer_get_token(&parser.tokenizer) == JES_NO_ERROR)) {
    //printf("\n1... %d, state: %d", ctx->status, parser.state);
    switch (parser.state) {
      case JES_START:
        ctx->status = jes_parser_process_start_state(&parser);
        break;
      case JES_EXPECT_KEY:
        ctx->status = jes_parser_process_expect_key_state(&parser);
        break;
      case JES_EXPECT_COLON:
        ctx->status = jes_parser_process_expect_colon_state(&parser);
        break;
      case JES_EXPECT_KEY_VALUE:
        ctx->status = jes_parser_process_expect_key_value_state(&parser);
        break;
      case JES_HAVE_KEY_VALUE:
        ctx->status = jes_parser_process_have_key_value_state(&parser);
        break;
      case JES_EXPECT_ARRAY_VALUE:
        ctx->status = jes_parser_process_expect_array_value_state(&parser);
        break;
      case JES_HAVE_ARRAY_VALUE:
        ctx->status = jes_parser_process_have_array_value_state(&parser);
        break;
      case JES_EXPECT_EOF:
        if (parser.tokenizer.token.type == JES_TOKEN_EOF) {
          parser.state = JES_END;
          break;
        }
        ctx->status = JES_UNEXPECTED_TOKEN;
        break;
      default:
        assert(0);
        ctx->status = JES_PARSING_FAILED;
        break;
    }
  }
  //printf("\n2... %d, state: %d", ctx->status, parser.state);
  if ((ctx->status == JES_NO_ERROR) && (parser.iter != NULL)) {
      ctx->status = JES_UNEXPECTED_EOF;
  }
  return;

}