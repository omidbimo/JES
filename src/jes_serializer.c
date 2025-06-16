#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "jes.h"
#include "jes_private.h"
#include "jes_logger.h"

#ifndef NDEBUG
  #define JES_LOG_NODE  jes_log_node
  #define JES_LOG_STATE jes_log_state
#else
  #define JES_LOG_NODE(...)
  #define JES_LOG_STATE(...)
#endif

static void jes_serializer_calculate_delimiter(struct jes_context* ctx)
{
  ctx->serdes.out_length += sizeof(char);
}

static void jes_serializer_calculate_string(struct jes_context* ctx)
{
  ctx->serdes.out_length += ctx->serdes.iter->json_tlv.length + sizeof("\"\"") - 1;
}

static void jes_serializer_calculate_key(struct jes_context* ctx)
{
  ctx->serdes.out_length += ctx->serdes.iter->json_tlv.length;
  ctx->serdes.out_length += sizeof("\"\": ") - 1;
}

static void jes_serializer_calculate_number(struct jes_context* ctx)
{
  ctx->serdes.out_length += ctx->serdes.iter->json_tlv.length;
}

static void jes_serializer_calculate_literal(struct jes_context* ctx)
{
  ctx->serdes.out_length += ctx->serdes.iter->json_tlv.length;
}

static void jes_serializer_calculate_new_line(struct jes_context* ctx)
{
  ctx->serdes.out_length += ctx->serdes.indention;
  ctx->serdes.out_length += sizeof("\n") - 1;
}

static void jes_serializer_calculate_compact_key(struct jes_context* ctx)
{
  ctx->serdes.out_length += ctx->serdes.iter->json_tlv.length;
  ctx->serdes.out_length += sizeof("\"\":") - 1;
}

static void jes_serializer_calculate_compact_new_line(struct jes_context* ctx)
{
  /* Compact format requires no new line. Do nothing */
}

static void jes_serializer_render_opening_brace(struct jes_context* ctx)
{
  ctx->serdes.out_length += sizeof("{") - 1;
  *ctx->serdes.out_buffer++ = '{';
}

static void jes_serializer_render_closing_brace(struct jes_context* ctx)
{
  ctx->serdes.out_length += sizeof("}") - 1;
  *ctx->serdes.out_buffer++ = '}';
}

static void jes_serializer_render_opening_bracket(struct jes_context* ctx)
{
  ctx->serdes.out_length += sizeof("[") - 1;
  *ctx->serdes.out_buffer++ = '[';
}

static void jes_serializer_render_closing_bracket(struct jes_context* ctx)
{
  ctx->serdes.out_length += sizeof("]") - 1;
  *ctx->serdes.out_buffer++ = ']';
}

static void jes_serializer_render_string(struct jes_context* ctx)
{
  ctx->serdes.out_length += ctx->serdes.iter->json_tlv.length + sizeof("\"\"") - 1;
  *ctx->serdes.out_buffer++ = '"';
  /* The JSON string has already been validated for size and structure,
     so this memcpy can proceed safely without further boundary checks. */
  assert((ctx->serdes.out_buffer + ctx->serdes.iter->json_tlv.length) < ctx->serdes.buffer_end);
  memcpy(ctx->serdes.out_buffer, ctx->serdes.iter->json_tlv.value, ctx->serdes.iter->json_tlv.length);
  ctx->serdes.out_buffer += ctx->serdes.iter->json_tlv.length;
  *ctx->serdes.out_buffer++ = '"';
}

static void jes_serializer_render_key(struct jes_context* ctx)
{
  ctx->serdes.out_length += (ctx->serdes.iter->json_tlv.length + sizeof("\"\": ") - 1);
  *ctx->serdes.out_buffer++ = '"';
  /* The JSON string has already been validated for size and structure,
     so this memcpy can proceed safely without further boundary checks. */
  assert((ctx->serdes.out_buffer + ctx->serdes.iter->json_tlv.length) < ctx->serdes.buffer_end);
  memcpy(ctx->serdes.out_buffer, ctx->serdes.iter->json_tlv.value, ctx->serdes.iter->json_tlv.length);
  ctx->serdes.out_buffer += ctx->serdes.iter->json_tlv.length;
  *ctx->serdes.out_buffer++ = '"';
  *ctx->serdes.out_buffer++ = ':';
  *ctx->serdes.out_buffer++ = ' ';
}

static void jes_serializer_render_number(struct jes_context* ctx)
{
  ctx->serdes.out_length += ctx->serdes.iter->json_tlv.length;
  /* The JSON string has already been validated for size and structure,
     so this memcpy can proceed safely without further boundary checks. */
  assert((ctx->serdes.out_buffer + ctx->serdes.iter->json_tlv.length) < ctx->serdes.buffer_end);
  memcpy(ctx->serdes.out_buffer, ctx->serdes.iter->json_tlv.value, ctx->serdes.iter->json_tlv.length);
  ctx->serdes.out_buffer += ctx->serdes.iter->json_tlv.length;
}

static void jes_serializer_render_literal(struct jes_context* ctx)
{
  ctx->serdes.out_length += ctx->serdes.iter->json_tlv.length;
  /* The JSON string has already been validated for size and structure,
     so this memcpy can proceed safely without further boundary checks. */
  assert((ctx->serdes.out_buffer + ctx->serdes.iter->json_tlv.length) < ctx->serdes.buffer_end);
  memcpy(ctx->serdes.out_buffer, ctx->serdes.iter->json_tlv.value, ctx->serdes.iter->json_tlv.length);
  ctx->serdes.out_buffer += ctx->serdes.iter->json_tlv.length;
}

static void jes_serializer_render_comma(struct jes_context* ctx)
{
  ctx->serdes.out_length += sizeof(",") - 1;
  *ctx->serdes.out_buffer++ = ',';
}

static void jes_serializer_render_new_line(struct jes_context* ctx)
{
  ctx->serdes.out_length += ctx->serdes.indention + sizeof("\n") - 1;
  *ctx->serdes.out_buffer++ = '\n';
  memset(ctx->serdes.out_buffer, ' ', ctx->serdes.indention);
  ctx->serdes.out_buffer += ctx->serdes.indention;
}

static void jes_serializer_render_compact_key(struct jes_context* ctx)
{
  ctx->serdes.out_length += (ctx->serdes.iter->json_tlv.length + sizeof("\"\":") - 1);
  *ctx->serdes.out_buffer++ = '"';
  /* The JSON string has already been validated for size and structure,
     so this memcpy can proceed safely without further boundary checks. */
  assert((ctx->serdes.out_buffer + ctx->serdes.iter->json_tlv.length) < ctx->serdes.buffer_end);
  memcpy(ctx->serdes.out_buffer, ctx->serdes.iter->json_tlv.value, ctx->serdes.iter->json_tlv.length);
  ctx->serdes.out_buffer += ctx->serdes.iter->json_tlv.length;
  *ctx->serdes.out_buffer++ = '"';
  *ctx->serdes.out_buffer++ = ':';
}

static void jes_serializer_render_compact_new_line(struct jes_context* ctx)
{
  /* Compact format requires no new line. Do nothing */
}

static inline void jes_serializer_process_expect_key_state(struct jes_context* ctx)
{
  switch (NODE_TYPE(ctx->serdes.iter)) {
    case JES_KEY:
      ctx->serdes.renderer->key(ctx);
      ctx->serdes.state = JES_EXPECT_VALUE;
      break;
    default:
      ctx->status = JES_UNEXPECTED_ELEMENT;
      break;
  }
}

static inline void jes_serializer_process_expect_value_state(struct jes_context* ctx)
{
  switch (NODE_TYPE(ctx->serdes.iter)) {
    case JES_STRING:
      ctx->serdes.renderer->string(ctx);
      ctx->serdes.state = JES_HAVE_VALUE;
      break;
    case JES_NUMBER:
      ctx->serdes.renderer->number(ctx);
      ctx->serdes.state = JES_HAVE_VALUE;
      break;
    case JES_TRUE:
    case JES_FALSE:
    case JES_NULL:
      ctx->serdes.renderer->literal(ctx);
      ctx->serdes.state = JES_HAVE_VALUE;
      break;
    case JES_OBJECT:
      ctx->serdes.renderer->opening_brace(ctx);
      ctx->serdes.indention += JES_TAB_SIZE;
      ctx->serdes.state = JES_EXPECT_KEY;
      if (HAS_CHILD(ctx->serdes.iter)) {
        ctx->serdes.renderer->new_line(ctx);
      }
      else { /* Empty OBJECT */
        ctx->serdes.renderer->closing_brace(ctx);
        ctx->serdes.indention -= JES_TAB_SIZE;
        ctx->serdes.state = JES_HAVE_VALUE;
      }
      break;
    case JES_ARRAY:
      ctx->serdes.renderer->opening_bracket(ctx);
      ctx->serdes.indention += JES_TAB_SIZE;
      ctx->serdes.state = JES_EXPECT_VALUE;
      if (HAS_CHILD(ctx->serdes.iter)) {
        ctx->serdes.renderer->new_line(ctx);
      }
      else { /* Empty ARRAY */
        ctx->serdes.renderer->closing_bracket(ctx);
        ctx->serdes.indention -= JES_TAB_SIZE;
        ctx->serdes.state = JES_HAVE_VALUE;
      }
      break;
    default:
      ctx->status = JES_UNEXPECTED_ELEMENT;
      break;
  }
}

static inline void jes_serializer_process_have_value_state(struct jes_context* ctx)
{
  struct jes_node* iter = ctx->serdes.iter;

  while (iter) {
    if (HAS_SIBLING(iter)) {
      ctx->serdes.renderer->comma(ctx);
      if (PARENT_TYPE(ctx->node_mng, iter) == JES_ARRAY) {
        ctx->serdes.state = JES_EXPECT_VALUE;
        ctx->serdes.renderer->new_line(ctx);
      }
      else {
        ctx->serdes.renderer->new_line(ctx);
        ctx->serdes.state = JES_EXPECT_KEY;
      }
      break;
    }
    else { /* node has no siblings. move up one level to the parent node and render the required closings. */
      ctx->serdes.state = JES_HAVE_VALUE;
      iter = GET_PARENT(ctx->node_mng, iter);
      if (iter != NULL) {
        if (NODE_TYPE(iter) == JES_ARRAY) {
          ctx->serdes.indention -= JES_TAB_SIZE;
          ctx->serdes.renderer->new_line(ctx);
          ctx->serdes.renderer->closing_bracket(ctx);
        }
        else if (NODE_TYPE(iter) == JES_OBJECT) {
          ctx->serdes.indention -= JES_TAB_SIZE;
          ctx->serdes.renderer->new_line(ctx);
          ctx->serdes.renderer->closing_brace(ctx);
        }
      }
      else { /* We've reached the root level, set the final state. */
        ctx->serdes.state = JES_END;
      }
    }
  }
}

/* Pre-order depth-first traversal
   visit node → visit children → visit siblings → backtrack to parent's siblings. */
static inline struct jes_node* jes_serializer_get_node(struct jes_context* ctx)
{
  assert(ctx != NULL);

  /* Start at root node on first call */
  if (ctx->serdes.iter == NULL) {
    ctx->serdes.iter = ctx->node_mng.root;
  }
  /* If current node has children, get the first child */
  else if (HAS_CHILD(ctx->serdes.iter)) {
    ctx->serdes.iter = GET_FIRST_CHILD(ctx->node_mng, ctx->serdes.iter);
  }
  /* No children available: try to move to next sibling (breadth at current level) */
  else if (HAS_SIBLING(ctx->serdes.iter)) {
    ctx->serdes.iter = GET_SIBLING(ctx->node_mng, ctx->serdes.iter);
  }
  else {
    /* No children or siblings: backtrack up the tree to find next un-visited branch
       Walk up parent chain until we find a parent with an un-visited sibling */
    while ((ctx->serdes.iter = GET_PARENT(ctx->node_mng, ctx->serdes.iter))) {
      /* Found a parent with a sibling - this is our next branch to explore */
      if (HAS_SIBLING(ctx->serdes.iter)) {
        ctx->serdes.iter = GET_SIBLING(ctx->node_mng, ctx->serdes.iter);
        break;
      }
    }
  }

  if (ctx->serdes.iter == NULL) {
    JES_LOG_STATE("\nJES.Serializer.State: ", ctx->serdes.state, "");
    assert(ctx->serdes.state == JES_HAVE_VALUE);
    ctx->serdes.state = JES_END;
  }
  /* Return NULL if traversal is complete */
  return ctx->serdes.iter;
}

static enum jes_status jes_serialize(struct jes_context* ctx)
{
  assert(ctx != NULL);

  if (ctx->serdes.out_buffer != NULL) {
    assert(ctx->serdes.evaluated != false);
  }

  ctx->serdes.state = JES_EXPECT_VALUE;
  ctx->serdes.iter = NULL;
  ctx->serdes.out_length = sizeof('\0');
  ctx->serdes.indention = 0;

  do {
#if defined(JES_ENABLE_SERIALIZER_STATE_LOG)
    JES_LOG_STATE("\nJES.Serializer.State: ", ctx->serdes.state, "");
#endif
    switch (ctx->serdes.state) {
      case JES_EXPECT_KEY:
        jes_serializer_get_node(ctx);
        jes_serializer_process_expect_key_state(ctx);
        break;
      case JES_EXPECT_VALUE:
        jes_serializer_get_node(ctx);
        jes_serializer_process_expect_value_state(ctx);
        break;
      case JES_HAVE_VALUE:
        jes_serializer_process_have_value_state(ctx);
        break;
      default:
        assert(0);
        ctx->status = JES_RENDER_FAILED;
        break;
    }

#if defined(JES_ENABLE_SERIALIZER_NODE_LOG)
    JES_LOG_NODE("\n", JES_NODE_INDEX(ctx->node_mng, ctx->serdes.iter),
                          ctx->serdes.iter->json_tlv.type,
                          ctx->serdes.iter->json_tlv.length,
                          ctx->serdes.iter->json_tlv.value,
                          ctx->serdes.iter->parent,
                          ctx->serdes.iter->sibling,
                          ctx->serdes.iter->first_child,
                          ctx->serdes.iter->last_child,
                          "");
#endif

  } while ((ctx->serdes.state != JES_END) && (ctx->serdes.iter != NULL) && (ctx->status == JES_NO_ERROR));

#if defined(JES_ENABLE_SERIALIZER_STATE_LOG)
    JES_LOG_STATE("\nJES.Serializer.State: ", ctx->serdes.state, "");
#endif

  return ctx->status;
}

/* Dual-pass JSON serializer that renders the JES tree structure into a string buffer.
 *
 * Pass 1 (Calculate): Traverses the tree using calculation functions to determine the exact
 *         buffer size required for the final JSON output, including formatting overhead.
 * Pass 2 (Render): Re-traverses the tree using render functions to generate the actual
 *         JSON string, guaranteed to fit within the pre-calculated buffer bounds.
 */
uint32_t jes_render(struct jes_context *ctx, char* buffer, size_t buffer_length, bool compact)
{
  struct jes_renderer_set calculate_functions = {
    .opening_brace    = jes_serializer_calculate_delimiter,
    .closing_brace    = jes_serializer_calculate_delimiter,
    .opening_bracket  = jes_serializer_calculate_delimiter,
    .closing_bracket  = jes_serializer_calculate_delimiter,
    .new_line         = jes_serializer_calculate_new_line,
    .key              = jes_serializer_calculate_key,
    .literal          = jes_serializer_calculate_literal,
    .string           = jes_serializer_calculate_string,
    .number           = jes_serializer_calculate_number,
    .comma            = jes_serializer_calculate_delimiter,
  };

  struct jes_renderer_set render_functions = {
    .opening_brace    = jes_serializer_render_opening_brace,
    .closing_brace    = jes_serializer_render_closing_brace,
    .opening_bracket  = jes_serializer_render_opening_bracket,
    .closing_bracket  = jes_serializer_render_closing_bracket,
    .new_line         = jes_serializer_render_new_line,
    .key              = jes_serializer_render_key,
    .literal          = jes_serializer_render_literal,
    .string           = jes_serializer_render_string,
    .number           = jes_serializer_render_number,
    .comma            = jes_serializer_render_comma,
  };

  if (compact) {
    calculate_functions.new_line = jes_serializer_calculate_compact_new_line;
    calculate_functions.key      = jes_serializer_calculate_compact_key;
    render_functions.new_line    = jes_serializer_render_compact_new_line;
    render_functions.key         = jes_serializer_render_compact_key;
  }

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return 0;
  }

  if (ctx->node_mng.root == NULL) {
    return 0;
  }

  ctx->serdes.out_buffer = NULL;
  ctx->serdes.renderer = &calculate_functions;
  ctx->status = jes_serialize(ctx);

  if ((ctx->status == JES_NO_ERROR) && (buffer_length >= ctx->serdes.out_length)) {
    ctx->serdes.out_buffer = buffer;
    ctx->serdes.buffer_end = buffer + buffer_length;
    ctx->serdes.evaluated = true;
    ctx->serdes.renderer = &render_functions;
    ctx->status = jes_serialize(ctx);
    if (ctx->status == JES_NO_ERROR) {
      buffer[ctx->serdes.out_length] = '\0';
    }
  }

  return ctx->serdes.out_length;
}

uint32_t jes_evaluate(struct jes_context *ctx, bool compact)
{
  struct jes_renderer_set calculate_functions = {
    .opening_brace    = jes_serializer_calculate_delimiter,
    .closing_brace    = jes_serializer_calculate_delimiter,
    .opening_bracket  = jes_serializer_calculate_delimiter,
    .closing_bracket  = jes_serializer_calculate_delimiter,
    .new_line         = jes_serializer_calculate_new_line,
    .key              = jes_serializer_calculate_key,
    .literal          = jes_serializer_calculate_literal,
    .string           = jes_serializer_calculate_string,
    .number           = jes_serializer_calculate_number,
    .comma            = jes_serializer_calculate_delimiter,
  };

  if (compact) {
    calculate_functions.new_line = jes_serializer_calculate_compact_new_line;
    calculate_functions.key      = jes_serializer_calculate_compact_key;
  }

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return 0;
  }

  if (ctx->node_mng.root == NULL) {
    return 0;
  }

  ctx->serdes.out_buffer = NULL;
  ctx->serdes.renderer = &calculate_functions;
  ctx->status = jes_serialize(ctx);

  return ctx->serdes.out_length;
}
