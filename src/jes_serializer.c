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

struct jes_serializer_context {
  /* State of the parser state machine or the serializer state machine */
  enum jes_state state;
  /* Internal node iterator */
  struct jes_node* iter;

  size_t out_length;
  size_t indention;
  size_t tab_size;
  bool compact;
  char* out_buffer;
  char* buffer_end;
  bool evaluated;
  struct jes_context* jes_ctx;
};

static inline void jes_serializer_render_opening_brace(struct jes_serializer_context* ctx)
{
  ctx->out_length += sizeof("{") - 1;
  if (ctx->out_buffer != NULL) { *ctx->out_buffer++ = '{'; }
}

static inline void jes_serializer_render_opening_bracket(struct jes_serializer_context* ctx)
{
  ctx->out_length += sizeof("[") - 1;
  if (ctx->out_buffer != NULL) { *ctx->out_buffer++ = '['; }
}

static inline void jes_serializer_render_string(struct jes_serializer_context* ctx)
{
  ctx->out_length += ctx->iter->json_tlv.length + sizeof("\"\"") - 1;
  if (ctx->out_buffer != NULL) {
    *ctx->out_buffer++ = '"';
    /* The JSON string has already been validated for size and structure,
       so this memcpy can proceed safely without further boundary checks. */
    assert((ctx->out_buffer + ctx->iter->json_tlv.length) < ctx->buffer_end);
    ctx->out_buffer = (char*)memcpy(ctx->out_buffer, ctx->iter->json_tlv.value, ctx->iter->json_tlv.length) + ctx->iter->json_tlv.length;
    *ctx->out_buffer++ = '"';
  }
}

static inline void jes_serializer_render_key(struct jes_serializer_context* ctx)
{
  ctx->out_length += (ctx->iter->json_tlv.length + sizeof("\"\": ") - 1);
  if (ctx->out_buffer != NULL) {
    *ctx->out_buffer++ = '"';
    /* The JSON string has already been validated for size and structure,
       so this memcpy can proceed safely without further boundary checks. */
    assert((ctx->out_buffer + ctx->iter->json_tlv.length) < ctx->buffer_end);
    ctx->out_buffer = (char*)memcpy(ctx->out_buffer, ctx->iter->json_tlv.value, ctx->iter->json_tlv.length) + ctx->iter->json_tlv.length;
    *ctx->out_buffer++ = '"';
    *ctx->out_buffer++ = ':';
    *ctx->out_buffer++ = ' ';
  }
}

static inline void jes_serializer_render_number(struct jes_serializer_context* ctx)
{
  ctx->out_length += ctx->iter->json_tlv.length;
  if(ctx->out_buffer != NULL) {
    /* The JSON string has already been validated for size and structure,
       so this memcpy can proceed safely without further boundary checks. */
    assert((ctx->out_buffer + ctx->iter->json_tlv.length) < ctx->buffer_end);
    ctx->out_buffer = (char*)memcpy(ctx->out_buffer, ctx->iter->json_tlv.value, ctx->iter->json_tlv.length) + ctx->iter->json_tlv.length;
  }
}

static inline void jes_serializer_render_literal(struct jes_serializer_context* ctx)
{
  ctx->out_length += ctx->iter->json_tlv.length;
  if(ctx->out_buffer != NULL) {
    /* The JSON string has already been validated for size and structure,
       so this memcpy can proceed safely without further boundary checks. */
    assert((ctx->out_buffer + ctx->iter->json_tlv.length) < ctx->buffer_end);
    ctx->out_buffer = (char*)memcpy(ctx->out_buffer, ctx->iter->json_tlv.value, ctx->iter->json_tlv.length) + ctx->iter->json_tlv.length;
  }
}

static inline void jes_serializer_render_comma(struct jes_serializer_context* ctx)
{
  ctx->out_length += sizeof(",") - 1;
  if (ctx->out_buffer != NULL) { *ctx->out_buffer++ = ','; }
}

static inline void jes_serializer_render_new_line(struct jes_serializer_context* ctx)
{
  if (!ctx->compact) {
    ctx->out_length += sizeof("\n") - 1 + ctx->indention;
    if (ctx->out_buffer != NULL) {
      *ctx->out_buffer++ = '\n';
      memset(ctx->out_buffer, ' ', ctx->indention);
      ctx->out_buffer += ctx->indention;
    }
  }
}

static inline void jes_serializer_render_closing_brace(struct jes_serializer_context* ctx)
{
  ctx->out_length += sizeof("}") - 1;
  if (ctx->out_buffer != NULL) { *ctx->out_buffer++ = '}'; }
}

static inline void jes_serializer_render_closing_bracket(struct jes_serializer_context* ctx)
{
  ctx->out_length += sizeof("]") - 1;
  if (ctx->out_buffer != NULL) { *ctx->out_buffer++ = ']'; }
}

static inline enum jes_status jes_serializer_process_start_state(struct jes_serializer_context* ctx)
{
  enum jes_status status = JES_NO_ERROR;
  switch (NODE_TYPE(ctx->iter)) {
    case JES_OBJECT:
      jes_serializer_render_opening_brace(ctx);
      ctx->indention += ctx->tab_size;
      ctx->state = JES_EXPECT_KEY;
      break;
    case JES_KEY:
      jes_serializer_render_key(ctx);
      ctx->state = JES_EXPECT_KEY_VALUE;
      break;
    case JES_ARRAY:
      jes_serializer_render_opening_bracket(ctx);
      ctx->indention += ctx->tab_size;
      ctx->state = JES_EXPECT_ARRAY_VALUE;
      break;
    case JES_STRING:
      jes_serializer_render_string(ctx);
      if (PARENT_TYPE(ctx->jes_ctx->node_mng, ctx->iter) == JES_KEY) {
        ctx->state = JES_HAVE_KEY_VALUE;
      }
      else if (PARENT_TYPE(ctx->jes_ctx->node_mng, ctx->iter) == JES_ARRAY) {
        ctx->state = JES_HAVE_ARRAY_VALUE;
      }
      break;
    case JES_NUMBER:
      jes_serializer_render_number(ctx);
      if (PARENT_TYPE(ctx->jes_ctx->node_mng, ctx->iter) == JES_KEY) {
        ctx->state = JES_HAVE_KEY_VALUE;
      }
      else if (PARENT_TYPE(ctx->jes_ctx->node_mng, ctx->iter) == JES_ARRAY) {
        ctx->state = JES_HAVE_ARRAY_VALUE;
      }
      break;
    case JES_TRUE:
    case JES_FALSE:
    case JES_NULL:
      jes_serializer_render_literal(ctx);
      if (PARENT_TYPE(ctx->jes_ctx->node_mng, ctx->iter) == JES_KEY) {
        ctx->state = JES_HAVE_KEY_VALUE;
      }
      else if (PARENT_TYPE(ctx->jes_ctx->node_mng, ctx->iter) == JES_ARRAY) {
        ctx->state = JES_HAVE_ARRAY_VALUE;
      }
      break;
    default:
      assert(0);
      status = JES_UNEXPECTED_ELEMENT;
      break;
  }
  return status;
}

static inline enum jes_status jes_serializer_process_expect_key_state(struct jes_serializer_context* ctx)
{
  enum jes_status status = JES_NO_ERROR;

  switch (NODE_TYPE(ctx->iter)) {
    case JES_KEY:
      jes_serializer_render_new_line(ctx);
      jes_serializer_render_key(ctx);
      ctx->state = JES_EXPECT_KEY_VALUE;
      break;
    default:
      status = JES_UNEXPECTED_ELEMENT;
      break;
  }

  return status;
}

static inline enum jes_status jes_serializer_process_expect_key_value_state(struct jes_serializer_context* ctx)
{
  enum jes_status status = JES_NO_ERROR;

  switch (NODE_TYPE(ctx->iter)) {
    case JES_STRING:
      jes_serializer_render_string(ctx);
      ctx->state = JES_HAVE_KEY_VALUE;
      break;
    case JES_NUMBER:
      jes_serializer_render_number(ctx);
      ctx->state = JES_HAVE_KEY_VALUE;
      break;
    case JES_TRUE:
    case JES_FALSE:
    case JES_NULL:
      jes_serializer_render_literal(ctx);
      ctx->state = JES_HAVE_KEY_VALUE;
      break;
    case JES_OBJECT:
      jes_serializer_render_opening_brace(ctx);
      ctx->indention += ctx->tab_size;
      ctx->state = JES_EXPECT_KEY;
      if (!HAS_CHILD(ctx->iter)) {
        jes_serializer_render_closing_brace(ctx);
        ctx->indention -= ctx->tab_size;
        ctx->state = JES_HAVE_KEY_VALUE;
      }
      break;
    case JES_ARRAY:
      jes_serializer_render_opening_bracket(ctx);
      ctx->indention += ctx->tab_size;
      ctx->state = JES_EXPECT_ARRAY_VALUE;
      if (!HAS_CHILD(ctx->iter)) {
        jes_serializer_render_closing_brace(ctx);
        ctx->indention -= ctx->tab_size;
        ctx->state = JES_HAVE_KEY_VALUE;
      }
      break;
    default:
      status = JES_UNEXPECTED_ELEMENT;
      break;
  }

  return status;
}

static inline enum jes_status jes_serializer_process_have_key_value_state(struct jes_serializer_context* ctx)
{
  struct jes_node* iter = ctx->iter;
  enum jes_status status = JES_NO_ERROR;

  while (iter) {
    if (HAS_SIBLING(iter)) {
      jes_serializer_render_comma(ctx);
      if (PARENT_TYPE(ctx->jes_ctx->node_mng, iter) == JES_ARRAY) {
        ctx->state = JES_EXPECT_ARRAY_VALUE;
      }
      else {
        ctx->state = JES_EXPECT_KEY;
      }
      break;
    }
    else { /* node has no siblings. */
      iter = GET_PARENT(ctx->jes_ctx->node_mng, iter);
      if (NODE_TYPE(iter) == JES_ARRAY) {
        ctx->indention -= ctx->tab_size;
        jes_serializer_render_new_line(ctx);
        jes_serializer_render_closing_bracket(ctx);
      }
      else if (NODE_TYPE(iter) == JES_KEY) {
        ctx->state = JES_HAVE_KEY_VALUE;
      }
      else if (NODE_TYPE(iter) == JES_OBJECT) {
        ctx->indention -= ctx->tab_size;
        jes_serializer_render_new_line(ctx);
        jes_serializer_render_closing_brace(ctx);
        ctx->state = JES_HAVE_KEY_VALUE;
      }
    }
  }

  return status;
}

static inline enum jes_status jes_serializer_process_expect_array_value_state(struct jes_serializer_context* ctx)
{
  enum jes_status status = JES_NO_ERROR;
  jes_serializer_render_new_line(ctx);
  switch (NODE_TYPE(ctx->iter)) {
    case JES_STRING:
      jes_serializer_render_string(ctx);
      ctx->state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_NUMBER:
      jes_serializer_render_number(ctx);
      ctx->state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_TRUE:
    case JES_FALSE:
    case JES_NULL:
      jes_serializer_render_literal(ctx);
      ctx->state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_OBJECT:
      jes_serializer_render_opening_brace(ctx);
      ctx->indention += ctx->tab_size;
      ctx->state = JES_EXPECT_KEY;
      if (!HAS_CHILD(ctx->iter)) {
        jes_serializer_render_closing_brace(ctx);
        ctx->indention -= ctx->tab_size;
        ctx->state = JES_HAVE_ARRAY_VALUE;
      }
      break;
    case JES_ARRAY:
      jes_serializer_render_opening_bracket(ctx);
      ctx->indention += ctx->tab_size;
      ctx->state = JES_EXPECT_ARRAY_VALUE;
      if (!HAS_CHILD(ctx->iter)) {
        jes_serializer_render_closing_bracket(ctx);
        ctx->indention -= ctx->tab_size;
        ctx->state = JES_HAVE_ARRAY_VALUE;
      }
      break;
    default:
      status = JES_UNEXPECTED_ELEMENT;
      break;
  }

  return status;
}

static inline enum jes_status jes_serializer_process_have_array_value_state(struct jes_serializer_context* ctx)
{
  enum jes_status status = JES_NO_ERROR;
  struct jes_node* iter = ctx->iter;

  while (iter) {
    if (HAS_SIBLING(iter)) {
      jes_serializer_render_comma(ctx);

      if (PARENT_TYPE(ctx->jes_ctx->node_mng, iter) == JES_ARRAY) {
        ctx->state = JES_EXPECT_ARRAY_VALUE;
      }
      else {
        ctx->state = JES_EXPECT_KEY;
      }
      break;
    }
    else { /* node has no siblings. */
      iter = GET_PARENT(ctx->jes_ctx->node_mng, iter);
      if (NODE_TYPE(iter) == JES_ARRAY) {
        ctx->indention -= ctx->tab_size;
        jes_serializer_render_new_line(ctx);
        jes_serializer_render_closing_bracket(ctx);
      }
      else if (NODE_TYPE(iter) == JES_KEY) {
        ctx->state = JES_HAVE_KEY_VALUE;
      }
      else if (NODE_TYPE(iter) == JES_OBJECT) {
        ctx->indention -= ctx->tab_size;
        jes_serializer_render_new_line(ctx);
        jes_serializer_render_closing_brace(ctx);
        ctx->state = JES_HAVE_KEY_VALUE;
      }
    }
  }

  return status;
}

/* Pre-order depth-first traversal
   visit node → visit children → visit siblings → backtrack to parent's siblings. */
static inline struct jes_node* jes_serializer_get_node(struct jes_serializer_context* ctx)
{
  assert(ctx != NULL);

  /* Start at root node on first call */
  if (ctx->iter == NULL) {
    ctx->iter = ctx->jes_ctx->node_mng.root;
  }
  /* If current node has children, get the first child */
  else if (HAS_CHILD(ctx->iter)) {
    ctx->iter = GET_FIRST_CHILD(ctx->jes_ctx->node_mng, ctx->iter);
  }
  /* No children available: try to move to next sibling (breadth at current level) */
  else if (HAS_SIBLING(ctx->iter)) {
    ctx->iter = GET_SIBLING(ctx->jes_ctx->node_mng, ctx->iter);
  }
  else {
    /* No children or siblings: backtrack up the tree to find next un-visited branch
       Walk up parent chain until we find a parent with an un-visited sibling */
    while ((ctx->iter = GET_PARENT(ctx->jes_ctx->node_mng, ctx->iter))) {
      /* Found a parent with a sibling - this is our next branch to explore */
      if (HAS_SIBLING(ctx->iter)) {
        ctx->iter = GET_SIBLING(ctx->jes_ctx->node_mng, ctx->iter);
        break;
      }
    }
  }

  if (ctx->iter == NULL) {
    assert(ctx->state == JES_HAVE_ARRAY_VALUE || ctx->state == JES_HAVE_KEY_VALUE);
    ctx->state = JES_END;
  }
  /* Return NULL if traversal is complete */
  return ctx->iter;
}

static enum jes_status jes_serialize(struct jes_serializer_context* ctx)
{
  enum jes_status status = JES_NO_ERROR;

  assert(ctx != NULL);
  if (ctx->out_buffer != NULL) {
    assert(ctx->evaluated != false);
  }

  ctx->state = JES_START;
  ctx->iter = NULL;
  ctx->out_length = 1; /* NUL termination */
  ctx->indention = 0;

  jes_serializer_get_node(ctx);

  while ((ctx->iter != NULL) && (status == JES_NO_ERROR)) {
#if defined(JES_ENABLE_SERIALIZER_STATE_LOG)
    JES_LOG_STATE("\nJES.Serializer.State: ", ctx->state, "");
#endif
    switch (ctx->state) {
      case JES_START:
        status = jes_serializer_process_start_state(ctx);
        break;
      case JES_EXPECT_KEY:
        status = jes_serializer_process_expect_key_state(ctx);
        break;
      case JES_EXPECT_KEY_VALUE:
        status = jes_serializer_process_expect_key_value_state(ctx);
        if (ctx->state == JES_HAVE_KEY_VALUE) { continue; }
        break;
      case JES_HAVE_KEY_VALUE:
        status = jes_serializer_process_have_key_value_state(ctx);
        break;
      case JES_EXPECT_ARRAY_VALUE:
        status = jes_serializer_process_expect_array_value_state(ctx);
        if (ctx->state == JES_HAVE_ARRAY_VALUE) { continue; }
        break;
      case JES_HAVE_ARRAY_VALUE:
        status = jes_serializer_process_have_array_value_state(ctx);
        break;
      default:
        assert(0);
        status = JES_RENDER_FAILED;
        break;
    }

#if defined(JES_ENABLE_SERIALIZER_NODE_LOG)
    JES_LOG_NODE("\n", JES_NODE_INDEX(ctx->jes_ctx->node_mng, ctx->iter),
                          ctx->iter->json_tlv.type,
                          ctx->iter->json_tlv.length,
                          ctx->iter->json_tlv.value,
                          ctx->iter->parent,
                          ctx->iter->sibling,
                          ctx->iter->first_child,
                          ctx->iter->last_child,
                          "");
#endif
    jes_serializer_get_node(ctx);
  }

#if defined(JES_ENABLE_SERIALIZER_STATE_LOG)
    JES_LOG_STATE("\nJES.Serializer.State: ", ctx->state, "");
#endif
#if 0
  if (status == JES_NO_ERROR && ctx->state != JES_END) {
    status = JES_RENDER_FAILED;
  }
#endif
  return status;
}

uint32_t jes_render(struct jes_context *ctx, char* buffer, size_t buffer_length, bool compact)
{
  struct jes_serializer_context serializer = { 0 };

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return 0;
  }

  if (ctx->node_mng.root == NULL) {
    return 0;
  }

  serializer.tab_size = 4;
  serializer.out_buffer = NULL;
  serializer.compact = compact;
  serializer.jes_ctx = ctx;
  ctx->status = JES_NO_ERROR;

  ctx->status = jes_serialize(&serializer);

  if ((ctx->status == JES_NO_ERROR) && (buffer_length >= serializer.out_length)) {
    serializer.out_buffer = buffer;
    serializer.buffer_end = buffer + buffer_length;
    serializer.evaluated = true;
    ctx->status = jes_serialize(&serializer);

    if (ctx->status == JES_NO_ERROR) {
      *serializer.out_buffer = '\0';
    }
  }

  return serializer.out_length;
}

uint32_t jes_evaluate(struct jes_context *ctx, bool compact)
{
  struct jes_serializer_context serializer = { 0 };

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return 0;
  }

  if (ctx->node_mng.root == NULL) {
    return 0;
  }

  serializer.tab_size = 4;
  serializer.out_buffer = NULL;
  serializer.compact = compact;
  serializer.jes_ctx = ctx;
  ctx->status = JES_NO_ERROR;

  ctx->status = jes_serialize(&serializer);

  return serializer.out_length;
}
