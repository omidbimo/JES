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

static inline void jes_parser_on_opening_brace(struct jes_context *ctx)
{
  if ((ctx->state != JES_EXPECT_OBJECT) &&
      (ctx->state != JES_EXPECT_KEY_VALUE)  &&
      (ctx->state != JES_EXPECT_ARRAY_VALUE)) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }

  /* Append node */
  ctx->iter = jes_tree_insert_node(ctx, ctx->iter, GET_LAST_CHILD(ctx, ctx->iter),
                              JES_OBJECT, ctx->token.length, ctx->token.value);
  ctx->state = JES_EXPECT_KEY;
}


/**
 * @brief Handles the parsing of a closing brace '}' in a JSON document.
 * It validates the current parser state, determines the proper parent node to return to,
 * and updates the parser state accordingly.
 */
static inline void jes_parser_on_closing_brace(struct jes_context *ctx)
{
  /* Validate current state - closing brace is only valid in specific states */
  if ((ctx->state != JES_EXPECT_KEY) &&
      (ctx->state != JES_HAVE_KEY_VALUE)) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }

  /* Handle special case: empty object "{}" */
  if ((NODE_TYPE(ctx->iter) == JES_OBJECT) && (ctx->state == JES_EXPECT_KEY)) {
    /* An object in EXPECT_KEY state, can only be an empty object with no children */
    if (HAS_CHILD(ctx->iter)) {
      ctx->status = JES_UNEXPECTED_TOKEN;
      ctx->ext_status = ctx->state;
      return;
    }
  }

  /* If current node is not an OBJECT type, navigate up to find the parent OBJECT */
  if (NODE_TYPE(ctx->iter) != JES_OBJECT) {
    ctx->iter = jes_tree_get_parent_node_by_type(ctx, ctx->iter, JES_OBJECT);
    assert(ctx->iter != NULL);
  }

  if (ctx->iter != NULL) {
    /* Now that we've found the object being closed, move up one more level
     * to the parent container (either an object or array) to transition based on
     * the parent type. */
    ctx->iter = jes_tree_get_container_parent_node(ctx, ctx->iter);
  }

  if (ctx->iter == NULL) {
    /* We've reached the root level, expect end of file */
    ctx->state = JES_EXPECT_EOF;
    return;
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
}

static inline void jes_parser_on_opening_bracket(struct jes_context *ctx)
{
  if ((ctx->state != JES_EXPECT_KEY_VALUE) &&
      (ctx->state != JES_EXPECT_ARRAY_VALUE)) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }

  ctx->iter = jes_tree_insert_node(ctx, ctx->iter, GET_LAST_CHILD(ctx, ctx->iter),
                              JES_ARRAY, ctx->token.length, ctx->token.value);
  ctx->state = JES_EXPECT_ARRAY_VALUE;
}

/**
 * @brief Handles the parsing of a closing bracket ']' in a JSON document.
 * It validates the current parser state, determines the proper parent node to return to,
 * and updates the parser state accordingly.
 */
static inline void jes_parser_on_closing_bracket(struct jes_context *ctx)
{
  /* Validate current state - closing bracket is only valid in specific states */
  if ((ctx->state != JES_EXPECT_ARRAY_VALUE) &&
      (ctx->state != JES_HAVE_ARRAY_VALUE)) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }

  /* Handle special case: empty array "[]" */
  if ((NODE_TYPE(ctx->iter) == JES_ARRAY) && (ctx->state == JES_EXPECT_ARRAY_VALUE)) {
    /* An array in expecting state, can only be an empty and must have no values */
    if (HAS_CHILD(ctx->iter)) {
      ctx->status = JES_UNEXPECTED_TOKEN;
      ctx->ext_status = ctx->state;
      return;
    }
  }

  /* If current node is not an ARRAY type, navigate up to find the parent ARRAY */
  if (NODE_TYPE(ctx->iter) != JES_ARRAY) {
    ctx->iter = jes_tree_get_parent_node_by_type(ctx, ctx->iter, JES_ARRAY);
    assert(ctx->iter != NULL);
  }

  if (ctx->iter != NULL) {
    /* Now that we've found the object being closed, move up one more level
     * to the parent container (either an object or array) to continue parsing */
    ctx->iter = jes_tree_get_container_parent_node(ctx, ctx->iter);
  }

  if (ctx->iter == NULL) {
    /* We've reached the root level, This should never inside an array happen */
    ctx->status = JES_PARSING_FAILED;
    return;
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
  /* Update parser state based on the current context */
  if (ctx->state == JES_HAVE_KEY_VALUE) {
    /* Inside an object, after a key-value pair - now expect another key */
    ctx->state = JES_EXPECT_KEY;
  }
  else if (ctx->state == JES_HAVE_ARRAY_VALUE) {
    /* Inside an array, after a value - now expect another array value */
    ctx->state = JES_EXPECT_ARRAY_VALUE;
  }
  else {
    /* Comma is invalid in any other state */
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }

  /*
   * Handle navigation within the JSON structure after a comma:
   * - For container nodes (objects/arrays), validate they have children
   * - For value nodes, move back up to the parent container
   */
  if ((NODE_TYPE(ctx->iter) == JES_OBJECT) || (NODE_TYPE(ctx->iter) == JES_ARRAY)) {
    if (!HAS_CHILD(ctx->iter)) {
      ctx->status = JES_UNEXPECTED_TOKEN;
      ctx->ext_status = ctx->state;
    }
  }
  else {
    /* Current node is a value - navigate up to the parent container (object or array) */
    ctx->iter = jes_tree_get_container_parent_node(ctx, ctx->iter);
  }
}

static inline void jes_parser_on_string(struct jes_context *ctx)
{

  if (ctx->state == JES_EXPECT_KEY) {

    /* Append the key */
    ctx->iter = jes_tree_insert_key_node(ctx, ctx->iter, GET_LAST_CHILD(ctx, ctx->iter),
                         ctx->token.length, ctx->token.value);
    ctx->state = JES_EXPECT_COLON;
  }
  else if (ctx->state == JES_EXPECT_KEY_VALUE) {
    /* Append value node */
    ctx->iter = jes_tree_insert_node(ctx, ctx->iter, GET_LAST_CHILD(ctx, ctx->iter),
             JES_STRING, ctx->token.length, ctx->token.value);
    ctx->state = JES_HAVE_KEY_VALUE;
  }
  else if (ctx->state == JES_EXPECT_ARRAY_VALUE) {
    ctx->iter = jes_tree_insert_node(ctx, ctx->iter, GET_LAST_CHILD(ctx, ctx->iter),
             JES_STRING, ctx->token.length, ctx->token.value);
    ctx->state = JES_HAVE_ARRAY_VALUE;
  }
  else {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }
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

  ctx->iter = jes_tree_insert_node(ctx, ctx->iter, GET_LAST_CHILD(ctx, ctx->iter),
             value_type, ctx->token.length, ctx->token.value);
}

void jes_parse(struct jes_context *ctx)
{
  assert(ctx != NULL);
  assert(ctx->json_data != NULL);
  assert(ctx->json_size != 0);

  ctx->state = JES_EXPECT_OBJECT;
  ctx->tokenizer_pos = ctx->json_data;
  do {
    if (jes_tokenizer_get_token(ctx) != JES_NO_ERROR) break;

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

  if ((ctx->status == JES_NO_ERROR) && (ctx->iter != NULL)) {
      ctx->status = JES_UNEXPECTED_EOF;
  }
}