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

static inline void jes_parser_process_opening_brace(struct jes_context *ctx)
{
  /* Append node */
  ctx->serdes.iter = jes_tree_insert_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx, ctx->serdes.iter),
                              JES_OBJECT, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
  ctx->serdes.state = JES_EXPECT_KEY;
}


/**
 * @brief Handles the parsing of a closing brace '}' in a JSON document.
 */
static inline void jes_parser_process_closing_brace(struct jes_context *ctx)
{
  /* Handle special case: empty object "{}" */
  if ((NODE_TYPE(ctx->serdes.iter) == JES_OBJECT) && (ctx->serdes.state == JES_EXPECT_KEY)) {
    /* An object in EXPECT_KEY state, can only be an empty object with no children */
    if (HAS_CHILD(ctx->serdes.iter)) {
      ctx->status = JES_UNEXPECTED_TOKEN;
      ctx->ext_status = ctx->serdes.state;
      return;
    }
  }

  /* If current node is not an OBJECT type, navigate up to find the parent OBJECT */
  if (NODE_TYPE(ctx->serdes.iter) != JES_OBJECT) {
    ctx->serdes.iter = jes_tree_get_parent_node_by_type(ctx, ctx->serdes.iter, JES_OBJECT);
    assert(ctx->serdes.iter != NULL);
  }

  if (ctx->serdes.iter != NULL) {
    /* Now that we've found the object being closed, move up one more level
     * to the parent container (either an object or array) to transition based on
     * the parent type. */
    ctx->serdes.iter = jes_tree_get_container_parent_node(ctx, ctx->serdes.iter);
  }

  if (ctx->serdes.iter == NULL) {
    /* We've reached the root level, expect end of file */
    ctx->serdes.state = JES_EXPECT_EOF;
    return;
  }

  /* Update parser state based on the type of parent container we've moved to */
  switch (NODE_TYPE(ctx->serdes.iter)) {
    case JES_ARRAY:
      /* We're now inside an array, ready for the next value or ARRAY closing */
      ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_OBJECT:
      /* We're now inside an object, just finished a key-value pair */
      ctx->serdes.state = JES_HAVE_KEY_VALUE;
      break;
    default:
      /* This should never happen - parent should always be object or array */
      assert(0);
      break;
  }
}

static inline void jes_parser_process_opening_bracket(struct jes_context *ctx)
{
  ctx->serdes.iter = jes_tree_insert_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx, ctx->serdes.iter),
                              JES_ARRAY, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
  ctx->serdes.state = JES_EXPECT_ARRAY_VALUE;
}

/**
 * @brief Handles the parsing of a closing bracket ']' in a JSON document.
 */
static inline void jes_parser_process_closing_bracket(struct jes_context *ctx)
{
  /* Handle special case: empty array "[]" */
  if ((NODE_TYPE(ctx->serdes.iter) == JES_ARRAY) && (ctx->serdes.state == JES_EXPECT_ARRAY_VALUE)) {
    /* An array in expecting state, can only be an empty and must have no values */
    if (HAS_CHILD(ctx->serdes.iter)) {
      ctx->status = JES_UNEXPECTED_TOKEN;
      ctx->ext_status = ctx->serdes.state;
      return;
    }
  }

  /* If current node is not an ARRAY type, navigate up to find the parent ARRAY */
  if (NODE_TYPE(ctx->serdes.iter) != JES_ARRAY) {
    ctx->serdes.iter = jes_tree_get_parent_node_by_type(ctx, ctx->serdes.iter, JES_ARRAY);
    assert(ctx->serdes.iter != NULL);
  }

  if (ctx->serdes.iter != NULL) {
    /* Now that we've found the array being closed, move up one more level
     * to the parent container (either an object or array) to continue parsing */
    ctx->serdes.iter = jes_tree_get_container_parent_node(ctx, ctx->serdes.iter);
  }

  if (ctx->serdes.iter == NULL) {
#if defined(JES_ALLOW_TOPLEVEL_ARRAY)
    /* We've reached the root level, expect end of file */
    ctx->serdes.state = JES_EXPECT_EOF;
#else
    /* We've reached the root level, This should never inside an array happen */
    ctx->status = JES_PARSING_FAILED;
#endif
    return;
  }

  /* Update parser state based on the type of parent container we've moved to */
  switch (NODE_TYPE(ctx->serdes.iter)) {
    case JES_ARRAY:
      /* We're now inside an array, ready for the next value or ARRAY closing */
      ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_OBJECT:
      /* We're now inside an object, just finished a key-value pair */
      ctx->serdes.state = JES_HAVE_KEY_VALUE;
      break;
    default:
      /* This should never happen - parent should always be object or array */
      assert(0);
      break;
  }
}

static inline void jes_parser_process_comma(struct jes_context *ctx)
{
  /* Handle navigation within the JSON structure after a comma:
   * - For container nodes (objects/arrays), validate they have children
   * - For value nodes, move back up to the parent container
   */
  if ((NODE_TYPE(ctx->serdes.iter) == JES_OBJECT) || (NODE_TYPE(ctx->serdes.iter) == JES_ARRAY)) {
    if (!HAS_CHILD(ctx->serdes.iter)) {
      ctx->status = JES_UNEXPECTED_TOKEN;
      ctx->ext_status = ctx->serdes.state;
    }
  }
  else {
    /* Current node is a value - navigate up to the parent container (object or array) */
    ctx->serdes.iter = jes_tree_get_container_parent_node(ctx, ctx->serdes.iter);
  }
}

static inline void jes_parser_process_empty_state(struct jes_context* ctx)
{
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
      ctx->serdes.state = JES_EXPECT_EOF;
#else
      ctx->status = JES_UNEXPECTED_TOKEN;
#endif
      break;
  }
}

static inline void jes_parser_process_expect_key_state(struct jes_context* ctx)
{
  switch (ctx->tokenizer.token.type) {
    case JES_TOKEN_STRING:
      /* Append the key */
      ctx->serdes.iter = jes_tree_insert_key_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx, ctx->serdes.iter),
                           ctx->tokenizer.token.length, ctx->tokenizer.token.value);
      ctx->serdes.state = JES_EXPECT_COLON;
    break;
  case JES_TOKEN_CLOSING_BRACE:
    jes_parser_process_closing_brace(ctx);
    break;
  default:
    ctx->status = JES_UNEXPECTED_TOKEN;
    break;
  }
}

static inline void jes_parser_process_expect_key_value_state(struct jes_context* ctx)
{
  enum jes_type value_type;
  switch (ctx->tokenizer.token.type) {
    case JES_TOKEN_STRING:
      value_type = JES_STRING;
      ctx->serdes.state = JES_HAVE_KEY_VALUE;
      break;
    case JES_TOKEN_NUMBER:
      value_type = JES_NUMBER;
      ctx->serdes.state = JES_HAVE_KEY_VALUE;
      break;
    case JES_TOKEN_TRUE:
      value_type = JES_TRUE;
      ctx->serdes.state = JES_HAVE_KEY_VALUE;
      break;
    case JES_TOKEN_FALSE:
      value_type = JES_FALSE;
      ctx->serdes.state = JES_HAVE_KEY_VALUE;
      break;
    case JES_TOKEN_NULL:
      value_type = JES_NULL;
      ctx->serdes.state = JES_HAVE_KEY_VALUE;
      break;
    case JES_TOKEN_OPENING_BRACKET:
      value_type = JES_ARRAY;
      ctx->serdes.state = JES_EXPECT_ARRAY_VALUE;
      break;
    case JES_TOKEN_OPENING_BRACE:
      value_type = JES_OBJECT;
      ctx->serdes.state = JES_EXPECT_KEY;
      break;
    default:
      ctx->status = JES_UNEXPECTED_TOKEN;
      return;
  }

  ctx->serdes.iter = jes_tree_insert_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx, ctx->serdes.iter),
           value_type, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
}

static inline void jes_parser_process_have_key_value_state(struct jes_context* ctx)
{
  switch (ctx->tokenizer.token.type) {
    case JES_TOKEN_CLOSING_BRACE:
      jes_parser_process_closing_brace(ctx);
      break;
    case JES_TOKEN_COMMA:
      jes_parser_process_comma(ctx);
      assert(NODE_TYPE(ctx->serdes.iter) == JES_OBJECT);
      ctx->serdes.state = JES_EXPECT_KEY;
      break;
    default:
      ctx->status = JES_UNEXPECTED_TOKEN;
      break;
  }
}

static inline void jes_parser_process_expect_array_value_state(struct jes_context* ctx)
{
  switch (ctx->tokenizer.token.type) {
    case JES_TOKEN_STRING:
      ctx->serdes.iter = jes_tree_insert_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx, ctx->serdes.iter),
             JES_STRING, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
            ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_TOKEN_NUMBER:
      ctx->serdes.iter = jes_tree_insert_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx, ctx->serdes.iter),
             JES_NUMBER, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
            ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_TOKEN_TRUE:
      ctx->serdes.iter = jes_tree_insert_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx, ctx->serdes.iter),
             JES_TRUE, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
            ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_TOKEN_FALSE:
      ctx->serdes.iter = jes_tree_insert_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx, ctx->serdes.iter),
             JES_FALSE, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
            ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_TOKEN_NULL:
      ctx->serdes.iter = jes_tree_insert_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx, ctx->serdes.iter),
             JES_NULL, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
            ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_TOKEN_OPENING_BRACKET:
      ctx->serdes.iter = jes_tree_insert_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx, ctx->serdes.iter),
             JES_ARRAY, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
      ctx->serdes.state = JES_EXPECT_ARRAY_VALUE;
      break;
    case JES_TOKEN_CLOSING_BRACKET:
      jes_parser_process_closing_bracket(ctx);
      break;
    case JES_TOKEN_OPENING_BRACE:
      jes_parser_process_opening_brace(ctx);
      break;
    default:
      ctx->status = JES_UNEXPECTED_TOKEN;
      break;
  }
}

static inline void jes_parser_process_have_array_value_state(struct jes_context* ctx)
{
  switch (ctx->tokenizer.token.type) {
    case JES_TOKEN_CLOSING_BRACKET:
      assert(ctx->serdes.iter != NULL);
      jes_parser_process_closing_bracket(ctx);
      break;
    case JES_TOKEN_COMMA:
      jes_parser_process_comma(ctx);
      assert(NODE_TYPE(ctx->serdes.iter) == JES_ARRAY);
      ctx->serdes.state = JES_EXPECT_ARRAY_VALUE;
      break;
    default:
      ctx->status = JES_UNEXPECTED_TOKEN;
      break;
  }
}

static inline void jes_parser_process_expect_colon_state(struct jes_context* ctx)
{
  if (ctx->tokenizer.token.type != JES_TOKEN_COLON) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->serdes.state;
  }
  else {
    ctx->serdes.state = JES_EXPECT_KEY_VALUE;
  }
}

void jes_parse(struct jes_context *ctx)
{
  assert(ctx != NULL);
  assert(ctx->json_data != NULL);
  assert(ctx->json_size != 0);

  ctx->serdes.state = JES_EMPTY;

  jes_tokenizer_set_cursor(ctx, ctx->json_data);

  while ((ctx->status == JES_NO_ERROR) && (ctx->serdes.state != JES_END)) {
    if (jes_tokenizer_get_token(ctx) != JES_NO_ERROR) return;

    switch (ctx->serdes.state) {
      case JES_EMPTY:
        jes_parser_process_empty_state(ctx);
        break;
      case JES_EXPECT_KEY:
        jes_parser_process_expect_key_state(ctx);
        break;
      case JES_EXPECT_COLON:
        jes_parser_process_expect_colon_state(ctx);
        break;
      case JES_EXPECT_KEY_VALUE:
        jes_parser_process_expect_key_value_state(ctx);
        break;
      case JES_HAVE_KEY_VALUE:
        jes_parser_process_have_key_value_state(ctx);
        break;
      case JES_EXPECT_ARRAY_VALUE:
        jes_parser_process_expect_array_value_state(ctx);
        break;
      case JES_HAVE_ARRAY_VALUE:
        jes_parser_process_have_array_value_state(ctx);
        break;
      case JES_EXPECT_EOF:
        if (ctx->tokenizer.token.type == JES_TOKEN_EOF) {
          ctx->serdes.state = JES_END;
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

  if ((ctx->status == JES_NO_ERROR) && (ctx->serdes.iter != NULL)) {
      ctx->status = JES_UNEXPECTED_EOF;
  }
  return;

}