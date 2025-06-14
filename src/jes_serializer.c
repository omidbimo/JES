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

static inline void jes_serializer_render_opening_brace(struct jes_context* ctx)
{
  ctx->serdes.out_length += sizeof("{") - 1;
  if (ctx->serdes.out_buffer != NULL) { *ctx->serdes.out_buffer++ = '{'; }
}

static inline void jes_serializer_render_opening_bracket(struct jes_context* ctx)
{
  ctx->serdes.out_length += sizeof("[") - 1;
  if (ctx->serdes.out_buffer != NULL) { *ctx->serdes.out_buffer++ = '['; }
}

static inline void jes_serializer_render_string(struct jes_context* ctx)
{
  ctx->serdes.out_length += ctx->serdes.iter->json_tlv.length + sizeof("\"\"") - 1;
  if (ctx->serdes.out_buffer != NULL) {
    *ctx->serdes.out_buffer++ = '"';
    /* The JSON string has already been validated for size and structure,
       so this memcpy can proceed safely without further boundary checks. */
    assert((ctx->serdes.out_buffer + ctx->serdes.iter->json_tlv.length) < ctx->serdes.buffer_end);
    ctx->serdes.out_buffer = (char*)memcpy(ctx->serdes.out_buffer, ctx->serdes.iter->json_tlv.value, ctx->serdes.iter->json_tlv.length) + ctx->serdes.iter->json_tlv.length;
    *ctx->serdes.out_buffer++ = '"';
  }
}

static inline void jes_serializer_render_key(struct jes_context* ctx)
{
  ctx->serdes.out_length += (ctx->serdes.iter->json_tlv.length + sizeof("\"\": ") - 1);
  if (ctx->serdes.out_buffer != NULL) {
    *ctx->serdes.out_buffer++ = '"';
    /* The JSON string has already been validated for size and structure,
       so this memcpy can proceed safely without further boundary checks. */
    assert((ctx->serdes.out_buffer + ctx->serdes.iter->json_tlv.length) < ctx->serdes.buffer_end);
    ctx->serdes.out_buffer = (char*)memcpy(ctx->serdes.out_buffer, ctx->serdes.iter->json_tlv.value, ctx->serdes.iter->json_tlv.length) + ctx->serdes.iter->json_tlv.length;
    *ctx->serdes.out_buffer++ = '"';
    *ctx->serdes.out_buffer++ = ':';
    *ctx->serdes.out_buffer++ = ' ';
  }
}

static inline void jes_serializer_render_number(struct jes_context* ctx)
{
  ctx->serdes.out_length += ctx->serdes.iter->json_tlv.length;
  if(ctx->serdes.out_buffer != NULL) {
    /* The JSON string has already been validated for size and structure,
       so this memcpy can proceed safely without further boundary checks. */
    assert((ctx->serdes.out_buffer + ctx->serdes.iter->json_tlv.length) < ctx->serdes.buffer_end);
    ctx->serdes.out_buffer = (char*)memcpy(ctx->serdes.out_buffer, ctx->serdes.iter->json_tlv.value, ctx->serdes.iter->json_tlv.length) + ctx->serdes.iter->json_tlv.length;
  }
}

static inline void jes_serializer_render_literal(struct jes_context* ctx)
{
  ctx->serdes.out_length += ctx->serdes.iter->json_tlv.length;
  if(ctx->serdes.out_buffer != NULL) {
    /* The JSON string has already been validated for size and structure,
       so this memcpy can proceed safely without further boundary checks. */
    assert((ctx->serdes.out_buffer + ctx->serdes.iter->json_tlv.length) < ctx->serdes.buffer_end);
    ctx->serdes.out_buffer = (char*)memcpy(ctx->serdes.out_buffer, ctx->serdes.iter->json_tlv.value, ctx->serdes.iter->json_tlv.length) + ctx->serdes.iter->json_tlv.length;
  }
}

static inline void jes_serializer_render_comma(struct jes_context* ctx)
{
  ctx->serdes.out_length += sizeof(",") - 1;
  if (ctx->serdes.out_buffer != NULL) { *ctx->serdes.out_buffer++ = ','; }
}

static inline void jes_serializer_render_new_line(struct jes_context* ctx)
{
  if (!ctx->serdes.compact) {
    ctx->serdes.out_length += sizeof("\n") - 1 + ctx->serdes.indention;
    if (ctx->serdes.out_buffer != NULL) {
      *ctx->serdes.out_buffer++ = '\n';
      memset(ctx->serdes.out_buffer, ' ', ctx->serdes.indention);
      ctx->serdes.out_buffer += ctx->serdes.indention;
    }
  }
}

static inline void jes_serializer_render_closing_brace(struct jes_context* ctx)
{
  ctx->serdes.out_length += sizeof("}") - 1;
  if (ctx->serdes.out_buffer != NULL) { *ctx->serdes.out_buffer++ = '}'; }
}

static inline void jes_serializer_render_closing_bracket(struct jes_context* ctx)
{
  ctx->serdes.out_length += sizeof("]") - 1;
  if (ctx->serdes.out_buffer != NULL) { *ctx->serdes.out_buffer++ = ']'; }
}

static inline void jes_serializer_process_start_state(struct jes_context* ctx)
{
  switch (NODE_TYPE(ctx->serdes.iter)) {
    case JES_OBJECT:
      jes_serializer_render_opening_brace(ctx);
      ctx->serdes.indention += ctx->serdes.tab_size;
      ctx->serdes.state = JES_EXPECT_KEY;
      break;
    case JES_KEY:
      jes_serializer_render_key(ctx);
      ctx->serdes.state = JES_EXPECT_KEY_VALUE;
      break;
    case JES_ARRAY:
      jes_serializer_render_opening_bracket(ctx);
      ctx->serdes.indention += ctx->serdes.tab_size;
      ctx->serdes.state = JES_EXPECT_ARRAY_VALUE;
      break;
    case JES_STRING:
      jes_serializer_render_string(ctx);
      if (PARENT_TYPE(ctx->node_mng, ctx->serdes.iter) == JES_KEY) {
        ctx->serdes.state = JES_HAVE_KEY_VALUE;
      }
      else if (PARENT_TYPE(ctx->node_mng, ctx->serdes.iter) == JES_ARRAY) {
        ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      }
      break;
    case JES_NUMBER:
      jes_serializer_render_number(ctx);
      ctx->serdes.state = JES_HAVE_KEY_VALUE;
      if (PARENT_TYPE(ctx->node_mng, ctx->serdes.iter) == JES_KEY) {
        ctx->serdes.state = JES_HAVE_KEY_VALUE;
      }
      else if (PARENT_TYPE(ctx->node_mng, ctx->serdes.iter) == JES_ARRAY) {
        ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      }
      break;
    case JES_TRUE:
    case JES_FALSE:
    case JES_NULL:
      jes_serializer_render_literal(ctx);
      if (PARENT_TYPE(ctx->node_mng, ctx->serdes.iter) == JES_KEY) {
        ctx->serdes.state = JES_HAVE_KEY_VALUE;
      }
      else if (PARENT_TYPE(ctx->node_mng, ctx->serdes.iter) == JES_ARRAY) {
        ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      }
      break;
    default:
      assert(0);
      ctx->status = JES_UNEXPECTED_ELEMENT;
      break;
  }
}

static inline void jes_serializer_process_expect_key_state(struct jes_context* ctx)
{
  switch (NODE_TYPE(ctx->serdes.iter)) {
    case JES_KEY:
      jes_serializer_render_new_line(ctx);
      jes_serializer_render_key(ctx);
      ctx->serdes.state = JES_EXPECT_KEY_VALUE;
      break;
    default:
      ctx->status = JES_UNEXPECTED_ELEMENT;
      break;
  }
}

static inline void jes_serializer_process_expect_key_value_state(struct jes_context* ctx)
{
  switch (NODE_TYPE(ctx->serdes.iter)) {
    case JES_STRING:
      jes_serializer_render_string(ctx);
      ctx->serdes.state = JES_HAVE_KEY_VALUE;
      break;
    case JES_NUMBER:
      jes_serializer_render_number(ctx);
      ctx->serdes.state = JES_HAVE_KEY_VALUE;
      break;
    case JES_TRUE:
    case JES_FALSE:
    case JES_NULL:
      jes_serializer_render_literal(ctx);
      ctx->serdes.state = JES_HAVE_KEY_VALUE;
      break;
    case JES_OBJECT:
      jes_serializer_render_opening_brace(ctx);
      ctx->serdes.indention += ctx->serdes.tab_size;
      ctx->serdes.state = JES_EXPECT_KEY;
      if (!HAS_CHILD(ctx->serdes.iter)) {
        jes_serializer_render_closing_brace(ctx);
        ctx->serdes.indention -= ctx->serdes.tab_size;
        ctx->serdes.state = JES_HAVE_KEY_VALUE;
      }
      break;
    case JES_ARRAY:
      jes_serializer_render_opening_bracket(ctx);
      ctx->serdes.indention += ctx->serdes.tab_size;
      ctx->serdes.state = JES_EXPECT_ARRAY_VALUE;
      if (!HAS_CHILD(ctx->serdes.iter)) {
        jes_serializer_render_closing_bracket(ctx);
        ctx->serdes.indention -= ctx->serdes.tab_size;
        ctx->serdes.state = JES_HAVE_KEY_VALUE;
      }
      break;
    default:
      ctx->status = JES_UNEXPECTED_ELEMENT;
      break;
  }
}

static inline void jes_serializer_process_have_key_value_state(struct jes_context* ctx)
{
  struct jes_node* iter = ctx->serdes.iter;

  while (iter) {
    if (HAS_SIBLING(iter)) {
      jes_serializer_render_comma(ctx);
      if (PARENT_TYPE(ctx->node_mng, iter) == JES_ARRAY) {
        ctx->serdes.state = JES_EXPECT_ARRAY_VALUE;
      }
      else {
        ctx->serdes.state = JES_EXPECT_KEY;
      }
      break;
    }
    else { /* node has no siblings. */
      iter = GET_PARENT(ctx->node_mng, iter);
      if (NODE_TYPE(iter) == JES_ARRAY) {
        ctx->serdes.indention -= ctx->serdes.tab_size;
        jes_serializer_render_new_line(ctx);
        jes_serializer_render_closing_bracket(ctx);
      }
      else if (NODE_TYPE(iter) == JES_KEY) {
        ctx->serdes.state = JES_HAVE_KEY_VALUE;
      }
      else if (NODE_TYPE(iter) == JES_OBJECT) {
        ctx->serdes.indention -= ctx->serdes.tab_size;
        jes_serializer_render_new_line(ctx);
        jes_serializer_render_closing_brace(ctx);
        ctx->serdes.state = JES_HAVE_KEY_VALUE;
      }
    }
  }
}

static inline void jes_serializer_process_expect_array_value_state(struct jes_context* ctx)
{

  jes_serializer_render_new_line(ctx);
  switch (NODE_TYPE(ctx->serdes.iter)) {
    case JES_STRING:
      jes_serializer_render_string(ctx);
      ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_NUMBER:
      jes_serializer_render_number(ctx);
      ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_TRUE:
    case JES_FALSE:
    case JES_NULL:
      jes_serializer_render_literal(ctx);
      ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_OBJECT:
      jes_serializer_render_opening_brace(ctx);
      ctx->serdes.indention += ctx->serdes.tab_size;
      ctx->serdes.state = JES_EXPECT_KEY;
      if (!HAS_CHILD(ctx->serdes.iter)) {
        jes_serializer_render_closing_brace(ctx);
        ctx->serdes.indention -= ctx->serdes.tab_size;
        ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      }
      break;
    case JES_ARRAY:
      jes_serializer_render_opening_bracket(ctx);
      ctx->serdes.indention += ctx->serdes.tab_size;
      ctx->serdes.state = JES_EXPECT_ARRAY_VALUE;
      if (!HAS_CHILD(ctx->serdes.iter)) {
        jes_serializer_render_closing_bracket(ctx);
        ctx->serdes.indention -= ctx->serdes.tab_size;
        ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      }
      break;
    default:
      ctx->status = JES_UNEXPECTED_ELEMENT;
      break;
  }
}

static inline void jes_serializer_process_have_array_value_state(struct jes_context* ctx)
{

  struct jes_node* iter = ctx->serdes.iter;

  while (iter) {
    if (HAS_SIBLING(iter)) {
      jes_serializer_render_comma(ctx);

      if (PARENT_TYPE(ctx->node_mng, iter) == JES_ARRAY) {
        ctx->serdes.state = JES_EXPECT_ARRAY_VALUE;
      }
      else {
        ctx->serdes.state = JES_EXPECT_KEY;
      }
      break;
    }
    else { /* node has no siblings. */
      iter = GET_PARENT(ctx->node_mng, iter);
      if (NODE_TYPE(iter) == JES_ARRAY) {
        ctx->serdes.indention -= ctx->serdes.tab_size;
        jes_serializer_render_new_line(ctx);
        jes_serializer_render_closing_bracket(ctx);
      }
      else if (NODE_TYPE(iter) == JES_KEY) {
        ctx->serdes.state = JES_HAVE_KEY_VALUE;
      }
      else if (NODE_TYPE(iter) == JES_OBJECT) {
        ctx->serdes.indention -= ctx->serdes.tab_size;
        jes_serializer_render_new_line(ctx);
        jes_serializer_render_closing_brace(ctx);
        ctx->serdes.state = JES_HAVE_KEY_VALUE;
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
    assert(ctx->serdes.state == JES_HAVE_ARRAY_VALUE || ctx->serdes.state == JES_HAVE_KEY_VALUE);
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

  ctx->serdes.state = JES_START;
  ctx->serdes.iter = NULL;
  ctx->serdes.out_length = 1; /* 1 byte for NUL termination */
  ctx->serdes.indention = 0;

  jes_serializer_get_node(ctx);

  while ((ctx->serdes.iter != NULL) && (ctx->status == JES_NO_ERROR)) {
#if defined(JES_ENABLE_SERIALIZER_STATE_LOG)
    JES_LOG_STATE("\nJES.Serializer.State: ", ctx->serdes.state, "");
#endif
    switch (ctx->serdes.state) {
      case JES_START:
        jes_serializer_process_start_state(ctx);
        break;
      case JES_EXPECT_KEY:
        jes_serializer_process_expect_key_state(ctx);
        break;
      case JES_EXPECT_KEY_VALUE:
        jes_serializer_process_expect_key_value_state(ctx);
        if (ctx->serdes.state == JES_HAVE_KEY_VALUE) { continue; }
        break;
      case JES_HAVE_KEY_VALUE:
        jes_serializer_process_have_key_value_state(ctx);
        break;
      case JES_EXPECT_ARRAY_VALUE:
        jes_serializer_process_expect_array_value_state(ctx);
        if (ctx->serdes.state == JES_HAVE_ARRAY_VALUE) { continue; }
        break;
      case JES_HAVE_ARRAY_VALUE:
         jes_serializer_process_have_array_value_state(ctx);
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
    jes_serializer_get_node(ctx);
  }

#if defined(JES_ENABLE_SERIALIZER_STATE_LOG)
    JES_LOG_STATE("\nJES.Serializer.State: ", ctx->serdes.state, "");
#endif
#if 0
  if (ctx->status == JES_NO_ERROR && ctx->serdes.state != JES_END) {
    ctx->status = JES_RENDER_FAILED;
  }
#endif
  return ctx->status;
}

uint32_t jes_render(struct jes_context *ctx, char* buffer, size_t buffer_length, bool compact)
{
  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return 0;
  }

  if (ctx->node_mng.root == NULL) {
    return 0;
  }

  ctx->serdes.tab_size = 2;
  ctx->serdes.out_buffer = NULL;
  ctx->serdes.compact = compact;
  ctx->status = jes_serialize(ctx);

  if ((ctx->status == JES_NO_ERROR) && (buffer_length >= ctx->serdes.out_length)) {
    ctx->serdes.out_buffer = buffer;
    ctx->serdes.buffer_end = buffer + buffer_length;
    ctx->serdes.evaluated = true;
    ctx->status = jes_serialize(ctx);

    if (ctx->status == JES_NO_ERROR) {
      buffer[ctx->serdes.out_length] = '\0';
    }
  }

  return ctx->serdes.out_length;
}

uint32_t jes_evaluate(struct jes_context *ctx, bool compact)
{

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return 0;
  }

  if (ctx->node_mng.root == NULL) {
    return 0;
  }

  ctx->serdes.tab_size = 2;
  ctx->serdes.out_buffer = NULL;
  ctx->serdes.compact = compact;
  ctx->status = jes_serialize(ctx);

  return ctx->serdes.out_length;
}
