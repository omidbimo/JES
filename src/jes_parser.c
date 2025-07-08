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
  #define JES_LOG_STATE jes_log_state
#else
  #define JES_LOG_STATE(...)
#endif

static inline void jes_parser_process_opening_brace(struct jes_context* ctx)
{
  /* Append node */
  ctx->serdes.iter = jes_tree_insert_node(ctx,
                                          ctx->serdes.iter,
                                          GET_LAST_CHILD(ctx->node_mng, ctx->serdes.iter),
                                          JES_OBJECT,
                                          ctx->serdes.tokenizer.token.length,
                                          ctx->serdes.tokenizer.token.value);
  ctx->serdes.state = JES_EXPECT_KEY;
}


/**
 * @brief Handles the parsing of a closing brace '}' in a JSON document.
 */
static inline void jes_parser_process_closing_brace(struct jes_context* ctx)
{
  /* Handle special case: empty object "{}" */
  if ((NODE_TYPE(ctx->serdes.iter) == JES_OBJECT) && (ctx->serdes.state == JES_EXPECT_KEY)) {
    /* An object in EXPECT_KEY state, can only be an empty object with no children */
    if (HAS_CHILD(ctx->serdes.iter)) {
      ctx->status = JES_UNEXPECTED_TOKEN;
      return;
    }
  }

  if ((NODE_TYPE(ctx->serdes.iter) == JES_KEY) || (NODE_TYPE(ctx->serdes.iter) == JES_ARRAY)) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    return;
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

  assert(NODE_TYPE(ctx->serdes.iter) == JES_ARRAY || NODE_TYPE(ctx->serdes.iter) == JES_OBJECT);
  ctx->serdes.state = JES_HAVE_VALUE;
}

static inline void jes_parser_process_opening_bracket(struct jes_context* ctx)
{
  ctx->serdes.iter = jes_tree_insert_node(ctx,
                                          ctx->serdes.iter,
                                          GET_LAST_CHILD(ctx->node_mng, ctx->serdes.iter),
                                          JES_ARRAY,
                                          ctx->serdes.tokenizer.token.length,
                                          ctx->serdes.tokenizer.token.value);
  ctx->serdes.state = JES_EXPECT_VALUE;
}

/**
 * @brief Handles the parsing of a closing bracket ']' in a JSON document.
 */
static inline void jes_parser_process_closing_bracket(struct jes_context* ctx)
{
  /* Handle special case: empty array "[]" */
  if ((NODE_TYPE(ctx->serdes.iter) == JES_ARRAY) && (ctx->serdes.state == JES_EXPECT_VALUE)) {
    /* An array in expecting state, can only be an empty and must have no values */
    if (HAS_CHILD(ctx->serdes.iter)) {
      ctx->status = JES_UNEXPECTED_TOKEN;
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
    /* We've reached the root level, expect end of file */
    ctx->serdes.state = JES_EXPECT_EOF;
    return;
  }

  assert(NODE_TYPE(ctx->serdes.iter) == JES_ARRAY || NODE_TYPE(ctx->serdes.iter) == JES_OBJECT);
  ctx->serdes.state = JES_HAVE_VALUE;
}

static inline void jes_parser_process_comma(struct jes_context* ctx)
{
  /* Handle navigation within the JSON structure after a comma:
   * - For container nodes (objects/arrays), validate they have children
   * - For value nodes, move back up to the parent container
   */
  if ((NODE_TYPE(ctx->serdes.iter) == JES_OBJECT) || (NODE_TYPE(ctx->serdes.iter) == JES_ARRAY)) {
    if (!HAS_CHILD(ctx->serdes.iter)) {
      ctx->status = JES_UNEXPECTED_TOKEN;
    }
  }
  else {
    /* Current node is a value - navigate up to the parent container (object or array) */
    ctx->serdes.iter = jes_tree_get_container_parent_node(ctx, ctx->serdes.iter);
  }
}

static inline void jes_parser_process_start_state(struct jes_context* ctx)
{
        printf("\nhere");
  switch (ctx->serdes.tokenizer.token.type) {
    case JES_TOKEN_OPENING_BRACE:
      jes_parser_process_opening_brace(ctx);
      break;
    case JES_TOKEN_OPENING_BRACKET:
      jes_parser_process_opening_bracket(ctx);
      break;
    case JES_TOKEN_STRING:

      ctx->serdes.iter = jes_tree_insert_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx->node_mng, ctx->serdes.iter),
                              JES_STRING, ctx->serdes.tokenizer.token.length, ctx->serdes.tokenizer.token.value);
      ctx->serdes.state = JES_EXPECT_EOF;
      break;
    case JES_TOKEN_NUMBER:
      ctx->serdes.iter = jes_tree_insert_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx->node_mng, ctx->serdes.iter),
                              JES_NUMBER, ctx->serdes.tokenizer.token.length, ctx->serdes.tokenizer.token.value);
      ctx->serdes.state = JES_EXPECT_EOF;
      break;
    case JES_TOKEN_TRUE:
      ctx->serdes.iter = jes_tree_insert_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx->node_mng, ctx->serdes.iter),
                              JES_TRUE, ctx->serdes.tokenizer.token.length, ctx->serdes.tokenizer.token.value);
      ctx->serdes.state = JES_EXPECT_EOF;
      break;
    case JES_TOKEN_FALSE:
      ctx->serdes.iter = jes_tree_insert_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx->node_mng, ctx->serdes.iter),
                              JES_FALSE, ctx->serdes.tokenizer.token.length, ctx->serdes.tokenizer.token.value);
      ctx->serdes.state = JES_EXPECT_EOF;
      break;
    case JES_TOKEN_NULL:
      ctx->serdes.iter = jes_tree_insert_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx->node_mng, ctx->serdes.iter),
                              JES_NULL, ctx->serdes.tokenizer.token.length, ctx->serdes.tokenizer.token.value);
      ctx->serdes.state = JES_EXPECT_EOF;
      break;
    default:
      ctx->status = JES_UNEXPECTED_TOKEN;
      break;
  }
}

static inline void jes_parser_process_expect_key_state(struct jes_context* ctx)
{
  switch (ctx->serdes.tokenizer.token.type) {
    case JES_TOKEN_STRING:
      /* Append the key */
      ctx->serdes.iter = jes_tree_insert_key_node(ctx, ctx->serdes.iter, GET_LAST_CHILD(ctx->node_mng, ctx->serdes.iter),
                           ctx->serdes.tokenizer.token.length, ctx->serdes.tokenizer.token.value);
      if (ctx->serdes.iter == NULL) { /* Something went wrong. exit the process */
        break;
      }
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

static inline void jes_parser_process_expect_value_state(struct jes_context* ctx)
{
  enum jes_type element_type;

  switch (ctx->serdes.tokenizer.token.type) {
    case JES_TOKEN_STRING:
      element_type = JES_STRING;
      ctx->serdes.state = JES_HAVE_VALUE;
      break;
    case JES_TOKEN_NUMBER:
      element_type = JES_NUMBER;
      ctx->serdes.state = JES_HAVE_VALUE;
      break;
    case JES_TOKEN_TRUE:
      element_type = JES_TRUE;
      ctx->serdes.state = JES_HAVE_VALUE;
      break;
    case JES_TOKEN_FALSE:
      element_type = JES_FALSE;
      ctx->serdes.state = JES_HAVE_VALUE;
      break;
    case JES_TOKEN_NULL:
      element_type = JES_NULL;
      ctx->serdes.state = JES_HAVE_VALUE;
      break;
    case JES_TOKEN_OPENING_BRACKET:
      element_type = JES_ARRAY;
      ctx->serdes.state = JES_EXPECT_VALUE;
      break;
    case JES_TOKEN_OPENING_BRACE:
      element_type = JES_OBJECT;
      ctx->serdes.state = JES_EXPECT_KEY;
      break;
    case JES_TOKEN_CLOSING_BRACKET:
      jes_parser_process_closing_bracket(ctx);
      return;
    case JES_TOKEN_CLOSING_BRACE:
      jes_parser_process_closing_brace(ctx);
      return;
    default:
      ctx->status = JES_UNEXPECTED_TOKEN;
      return;
  }

  ctx->serdes.iter = jes_tree_insert_node(ctx,
                                          ctx->serdes.iter,
                                          GET_LAST_CHILD(ctx->node_mng, ctx->serdes.iter),
                                          element_type,
                                          ctx->serdes.tokenizer.token.length,
                                          ctx->serdes.tokenizer.token.value);
}

static inline void jes_parser_process_have_value_state(struct jes_context* ctx)
{
  switch (ctx->serdes.tokenizer.token.type) {
    case JES_TOKEN_CLOSING_BRACE:
      jes_parser_process_closing_brace(ctx);
      break;
    case JES_TOKEN_CLOSING_BRACKET:
      assert(ctx->serdes.iter != NULL);
      jes_parser_process_closing_bracket(ctx);
      break;
    case JES_TOKEN_COMMA:
      jes_parser_process_comma(ctx);
      assert(ctx->serdes.iter);
      switch (NODE_TYPE(ctx->serdes.iter)) {
        case JES_ARRAY:
          ctx->serdes.state = JES_EXPECT_VALUE;
          break;
        case JES_OBJECT:
          ctx->serdes.state = JES_EXPECT_KEY;
          break;
        default:
          ctx->status = JES_UNEXPECTED_TOKEN;
          break;
      }
      break;
    case JES_TOKEN_EOF:
      if (!HAS_PARENT(ctx->serdes.iter) && !HAS_CHILD(ctx->serdes.iter)) {
        ctx->serdes.state = JES_END;
      }
      else {
        ctx->status = JES_UNEXPECTED_TOKEN;
      }
      break;
    default:
      ctx->status = JES_UNEXPECTED_TOKEN;
      break;
  }
}

static inline void jes_parser_process_expect_colon_state(struct jes_context* ctx)
{
  if (ctx->serdes.tokenizer.token.type != JES_TOKEN_COLON) {
    ctx->status = JES_UNEXPECTED_TOKEN;
  }
  else {
    ctx->serdes.state = JES_EXPECT_VALUE;
  }
}

void jes_parse(struct jes_context *ctx)
{
  assert(ctx != NULL);
  assert(ctx->serdes.tokenizer.json_data != NULL);
  assert(ctx->serdes.tokenizer.json_length != 0);

  ctx->serdes.state = JES_EXPECT_VALUE;

  jes_tokenizer_reset_cursor(&ctx->serdes.tokenizer);

#if defined(JES_ENABLE_PARSER_STATE_LOG)
    JES_LOG_STATE("\nJES.Parser.State: ", ctx->serdes.state, "");
#endif

  while ((ctx->status == JES_NO_ERROR) && (ctx->serdes.state != JES_END) && (jes_tokenizer_get_token(&ctx->serdes.tokenizer) == JES_NO_ERROR)) {

    switch (ctx->serdes.state) {
      case JES_EXPECT_KEY:
        jes_parser_process_expect_key_state(ctx);
        break;
      case JES_EXPECT_COLON:
        jes_parser_process_expect_colon_state(ctx);
        break;
      case JES_EXPECT_VALUE:
        jes_parser_process_expect_value_state(ctx);
        break;
      case JES_HAVE_VALUE:
        jes_parser_process_have_value_state(ctx);
        break;
      case JES_EXPECT_EOF:
        if (ctx->serdes.tokenizer.token.type == JES_TOKEN_EOF) {
          ctx->serdes.state = JES_END;
          break;
        }
        ctx->status = JES_UNEXPECTED_TOKEN;
        break;
      default:
        ctx->status = JES_PARSING_FAILED;
        break;
    }
#if defined(JES_ENABLE_PARSER_STATE_LOG)
    JES_LOG_STATE("\nJES.Parser.State: ", ctx->serdes.state, "");
#endif
  }

  if ((ctx->status == JES_NO_ERROR) && (ctx->serdes.state != JES_END) && (ctx->serdes.iter != NULL)) {
      ctx->status = JES_UNEXPECTED_EOF;
  }
}