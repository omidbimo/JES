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
  if ((ctx->serdes.state != JES_EXPECT_OBJECT) &&
      (ctx->serdes.state != JES_EXPECT_KEY_VALUE)  &&
      (ctx->serdes.state != JES_EXPECT_ARRAY_VALUE)) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->serdes.state;
    return;
  }

  /* Append node */
  ctx->serdes.iter = jes_tree_insert_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx, ctx->serdes.iter),
                              JES_OBJECT, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
  ctx->serdes.state = JES_EXPECT_KEY;
}


/**
 * @brief Handles the parsing of a closing brace '}' in a JSON document.
 * It validates the current parser state, determines the proper parent node to return to,
 * and updates the parser state accordingly.
 */
static inline void jes_parser_on_closing_brace(struct jes_context *ctx)
{
  /* Validate current state - closing brace is only valid in specific states */
  if ((ctx->serdes.state != JES_EXPECT_KEY) &&
      (ctx->serdes.state != JES_HAVE_KEY_VALUE)) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->serdes.state;
    return;
  }

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

static inline void jes_parser_on_opening_bracket(struct jes_context *ctx)
{
  if ((ctx->serdes.state != JES_EXPECT_KEY_VALUE) &&
      (ctx->serdes.state != JES_EXPECT_ARRAY_VALUE)) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->serdes.state;
    return;
  }

  ctx->serdes.iter = jes_tree_insert_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx, ctx->serdes.iter),
                              JES_ARRAY, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
  ctx->serdes.state = JES_EXPECT_ARRAY_VALUE;
}

/**
 * @brief Handles the parsing of a closing bracket ']' in a JSON document.
 * It validates the current parser state, determines the proper parent node to return to,
 * and updates the parser state accordingly.
 */
static inline void jes_parser_on_closing_bracket(struct jes_context *ctx)
{
  /* Validate current state - closing bracket is only valid in specific states */
  if ((ctx->serdes.state != JES_EXPECT_ARRAY_VALUE) &&
      (ctx->serdes.state != JES_HAVE_ARRAY_VALUE)) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->serdes.state;
    return;
  }

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
    /* Now that we've found the object being closed, move up one more level
     * to the parent container (either an object or array) to continue parsing */
    ctx->serdes.iter = jes_tree_get_container_parent_node(ctx, ctx->serdes.iter);
  }

  if (ctx->serdes.iter == NULL) {
    /* We've reached the root level, This should never inside an array happen */
    ctx->status = JES_PARSING_FAILED;
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

static inline void jes_parser_on_colon(struct jes_context *ctx)
{
  if (ctx->serdes.state == JES_EXPECT_COLON) {
    ctx->serdes.state = JES_EXPECT_KEY_VALUE;
  }
  else {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->serdes.state;
    return;
  }
}

static inline void jes_parser_on_comma(struct jes_context *ctx)
{
  /* Update parser state based on the current context */
  if (ctx->serdes.state == JES_HAVE_KEY_VALUE) {
    /* Inside an object, after a key-value pair - now expect another key */
    ctx->serdes.state = JES_EXPECT_KEY;
  }
  else if (ctx->serdes.state == JES_HAVE_ARRAY_VALUE) {
    /* Inside an array, after a value - now expect another array value */
    ctx->serdes.state = JES_EXPECT_ARRAY_VALUE;
  }
  else {
    /* Comma is invalid in any other state */
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->serdes.state;
    return;
  }

  /*
   * Handle navigation within the JSON structure after a comma:
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

static inline void jes_parser_on_string(struct jes_context *ctx)
{

  if (ctx->serdes.state == JES_EXPECT_KEY) {

    /* Append the key */
    ctx->serdes.iter = jes_tree_insert_key_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx, ctx->serdes.iter),
                         ctx->tokenizer.token.length, ctx->tokenizer.token.value);
    ctx->serdes.state = JES_EXPECT_COLON;
  }
  else if (ctx->serdes.state == JES_EXPECT_KEY_VALUE) {
    /* Append value node */
    ctx->serdes.iter = jes_tree_insert_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx, ctx->serdes.iter),
             JES_STRING, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
    ctx->serdes.state = JES_HAVE_KEY_VALUE;
  }
  else if (ctx->serdes.state == JES_EXPECT_ARRAY_VALUE) {
    ctx->serdes.iter = jes_tree_insert_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx, ctx->serdes.iter),
             JES_STRING, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
    ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
  }
  else {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->serdes.state;
    return;
  }
}

static inline void jes_parser_on_value(struct jes_context *ctx, enum jes_type value_type)
{
  if (ctx->serdes.state == JES_EXPECT_KEY_VALUE) {
    ctx->serdes.state = JES_HAVE_KEY_VALUE;
  }
  else if (ctx->serdes.state == JES_EXPECT_ARRAY_VALUE) {
    ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
  }
  else {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->serdes.state;
    return;
  }

  ctx->serdes.iter = jes_tree_insert_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx, ctx->serdes.iter),
             value_type, ctx->tokenizer.token.length, ctx->tokenizer.token.value);
}

void jes_parse(struct jes_context *ctx)
{
  assert(ctx != NULL);
  assert(ctx->json_data != NULL);
  assert(ctx->json_size != 0);

  ctx->serdes.state = JES_EXPECT_OBJECT;
  jes_tokenizer_set_cursor(ctx, ctx->json_data);

  do {
    if (jes_tokenizer_get_token(ctx) != JES_NO_ERROR) break;

    switch (ctx->tokenizer.token.type) {

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
  } while ((ctx->status == JES_NO_ERROR) && (ctx->tokenizer.token.type != JES_TOKEN_EOF));

  if ((ctx->status == JES_NO_ERROR) && (ctx->serdes.iter != NULL)) {
      ctx->status = JES_UNEXPECTED_EOF;
  }
}