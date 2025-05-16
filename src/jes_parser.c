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

  jes_add_object_node(ctx);
  ctx->state = JES_EXPECT_KEY;
}

static inline void jes_parser_on_closing_brace(struct jes_context *ctx)
{
  if ((ctx->state != JES_EXPECT_KEY) &&
      (ctx->state != JES_HAVE_KEY_VALUE)) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }

  /* Delimiter tokens can trigger upward iteration in the direction of parent node.
     A '}' indicates the end of a key:value sequence (object). */

  /* {} (empty object)is a special case that needs no iteration back to
   the parent node. */
  if ((ctx->iter->json_tlv.type == JES_OBJECT) && (ctx->state == JES_EXPECT_KEY)) {
    /* An object in EXPECT_KEY state, can only be an empty object and must have no values */
    if (HAS_FIRST_CHILD(ctx->iter)) {
      ctx->status = JES_UNEXPECTED_TOKEN;
      ctx->ext_status = ctx->state;
      return;
    }
  }

  /* The current node isn't an OBJECT? then iterate the parents to find a matching OBJECT. */
  /* TODO: It's probably not a solid way to find the correct OBJECT. */
  if (ctx->iter->json_tlv.type != JES_OBJECT) {
    ctx->iter = jes_get_parent_node_of_type(ctx, ctx->iter, JES_OBJECT);
    assert(ctx->iter != NULL);
  }

  /* Internal iterator now points to the object that is just closed. One more iteration
     is needed to get the parent object or array for further insertions. */
  ctx->iter = jes_get_parent_node_of_type_object_or_array(ctx, ctx->iter);

  if (ctx->iter) {
    if (ctx->iter->json_tlv.type == JES_ARRAY) {
      ctx->state = JES_HAVE_ARRAY_VALUE;
    }
    else if (ctx->iter->json_tlv.type == JES_OBJECT) {
      ctx->state = JES_HAVE_KEY_VALUE;
    }
    else {
      assert(0);
    }
  }
  else {
    ctx->state = JES_EXPECT_EOF;
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

  jes_add_array_node(ctx);
  ctx->state = JES_EXPECT_ARRAY_VALUE;
}

static inline void jes_parser_on_closing_bracket(struct jes_context *ctx)
{
  if ((ctx->state != JES_EXPECT_ARRAY_VALUE) &&
      (ctx->state != JES_HAVE_ARRAY_VALUE)) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }

  /* Delimiter tokens can trigger upward iteration in the direction of parent node.
     A ']' indicates the end of an Array or possibly the end of a key:value sequence.
  */

  /* [] (empty array) is a special case that needs no backward iteration in the
     parent node direction.
  */
  if ((ctx->iter->json_tlv.type == JES_ARRAY) && (ctx->state == JES_EXPECT_ARRAY_VALUE)) {
    /* An array in expecting state, can only be an empty and must have no values */
    if (HAS_FIRST_CHILD(ctx->iter)) {
      ctx->status = JES_UNEXPECTED_TOKEN;
      ctx->ext_status = ctx->state;
      return;
    }
  }

  if (ctx->iter->json_tlv.type != JES_ARRAY) {
    ctx->iter = jes_get_parent_node_of_type(ctx, ctx->iter, JES_ARRAY);
    assert(ctx->iter != NULL);
  }
  /* Iterator now points the array that is just closed. One more upward iteration
     is required to get the parent object or array for new insertions. */
  ctx->iter = jes_get_parent_node_of_type_object_or_array(ctx, ctx->iter);

  if (!ctx->iter) {
    ctx->status = JES_PARSING_FAILED;
    return;
  }

  if (ctx->iter->json_tlv.type == JES_ARRAY) {
    ctx->state = JES_HAVE_ARRAY_VALUE;
  }
  else if (ctx->iter->json_tlv.type == JES_OBJECT) {
    ctx->state = JES_HAVE_KEY_VALUE;
  }
  else {
    assert(0);
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
  if (ctx->state == JES_HAVE_KEY_VALUE) {
    ctx->state = JES_EXPECT_KEY;
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

  if ((ctx->iter->json_tlv.type == JES_OBJECT) || (ctx->iter->json_tlv.type == JES_ARRAY)) {
    if (!HAS_FIRST_CHILD(ctx->iter)) {
      ctx->status = JES_UNEXPECTED_TOKEN;
      ctx->ext_status = ctx->state;
    }
  }
  else {
    ctx->iter = jes_get_parent_node_of_type_object_or_array(ctx, ctx->iter);
  }
}

static inline void jes_parser_on_string(struct jes_context *ctx)
{
  if (ctx->state == JES_EXPECT_KEY) {
    /* Append the key */
    ctx->iter = jes_add_key_node_after(ctx, ctx->iter, GET_LAST_CHILD(ctx, ctx->iter), ctx->token.length, &ctx->json_data[ctx->token.offset]);
    ctx->state = JES_EXPECT_COLON;
  }
  else if (ctx->state == JES_EXPECT_KEY_VALUE) {
    jes_add_value_node(ctx, JES_VALUE_STRING);
    ctx->state = JES_HAVE_KEY_VALUE;

  }
  else if (ctx->state == JES_EXPECT_ARRAY_VALUE) {
    jes_add_value_node(ctx, JES_VALUE_STRING);
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

  jes_add_value_node(ctx, value_type);
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
        jes_parser_on_value(ctx, JES_VALUE_FALSE);
        break;

      case JES_TOKEN_TRUE:
        jes_parser_on_value(ctx, JES_VALUE_TRUE);
        break;

      case JES_TOKEN_NULL:
        jes_parser_on_value(ctx, JES_VALUE_NULL);
        break;

      case JES_TOKEN_NUMBER:
        jes_parser_on_value(ctx, JES_VALUE_NUMBER);
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