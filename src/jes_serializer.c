#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "jes.h"
#include "jes_private.h"
#include "jes_logger.h"
#include "jes_serializer.h"

#ifndef NDEBUG
  #define JES_LOG_NODE  jes_log_node
  #define JES_LOG_STATE jes_log_state
#else
  #define JES_LOG_NODE(...)
  #define JES_LOG_STATE(...)
#endif

struct jes_serializer;

struct jes_renderer_set {
  void (*opening_brace)   (struct jes_serializer*);
  void (*closing_brace)   (struct jes_serializer*);
  void (*opening_bracket) (struct jes_serializer*);
  void (*closing_bracket) (struct jes_serializer*);
  void (*new_line)        (struct jes_serializer*);
  void (*comma)           (struct jes_serializer*);
  void (*key)             (struct jes_serializer*, struct jes_element*);
  void (*literal)         (struct jes_serializer*, struct jes_element*);
  void (*string)          (struct jes_serializer*, struct jes_element*);
  void (*number)          (struct jes_serializer*, struct jes_element*);
};

struct jes_serializer {
  struct jes_renderer_set renderer;
  size_t evaluated_length;
  size_t indention;
  char* out_buffer;
  char* buffer_end;
  bool evaluated;
};

static void jes_serializer_calculate_delimiter(struct jes_serializer* serializer)
{
  serializer->evaluated_length += sizeof(char);
}

static void jes_serializer_calculate_string(struct jes_serializer* serializer, struct jes_element* element)
{
  serializer->evaluated_length += element->length + sizeof("\"\"") - 1;
}

static void jes_serializer_calculate_key(struct jes_serializer* serializer, struct jes_element* element)
{
  serializer->evaluated_length += element->length;
  serializer->evaluated_length += sizeof("\"\": ") - 1;
}

static void jes_serializer_calculate_number(struct jes_serializer* serializer, struct jes_element* element)
{
  serializer->evaluated_length += element->length;
}

static void jes_serializer_calculate_literal(struct jes_serializer* serializer, struct jes_element* element)
{
  serializer->evaluated_length += element->length;
}

static void jes_serializer_calculate_new_line(struct jes_serializer* serializer)
{
  serializer->evaluated_length += serializer->indention;
  serializer->evaluated_length += sizeof("\n") - 1;
}

static void jes_serializer_calculate_compact_key(struct jes_serializer* serializer, struct jes_element* element)
{
  serializer->evaluated_length += element->length;
  serializer->evaluated_length += sizeof("\"\":") - 1;
}

static void jes_serializer_calculate_compact_new_line(struct jes_serializer* serializer)
{
  /* Compact format requires no new line. Do nothing */
  (void)serializer;
}

static void jes_serializer_render_opening_brace(struct jes_serializer* serializer)
{
  assert(serializer->evaluated);
  assert(serializer->out_buffer + sizeof(char) < serializer->buffer_end);
  *serializer->out_buffer++ = '{';
}

static void jes_serializer_render_closing_brace(struct jes_serializer* serializer)
{
  assert(serializer->evaluated);
  assert(serializer->out_buffer + sizeof(char) < serializer->buffer_end);
  *serializer->out_buffer++ = '}';
}

static void jes_serializer_render_opening_bracket(struct jes_serializer* serializer)
{
  assert(serializer->evaluated);
  assert(serializer->out_buffer + sizeof(char) < serializer->buffer_end);
  *serializer->out_buffer++ = '[';
}

static void jes_serializer_render_closing_bracket(struct jes_serializer* serializer)
{
  assert(serializer->evaluated);
  assert(serializer->out_buffer + sizeof(char) < serializer->buffer_end);
  *serializer->out_buffer++ = ']';
}

static void jes_serializer_render_string(struct jes_serializer* serializer, struct jes_element* element)
{
  assert(serializer->evaluated);
  assert((serializer->out_buffer + element->length + 2*sizeof(char)) < serializer->buffer_end);
  *serializer->out_buffer++ = '"';
  /* The JSON string has already been validated for size and structure,
     so this memcpy can proceed safely without further boundary checks. */
  memcpy(serializer->out_buffer, element->value, element->length);
  serializer->out_buffer += element->length;
  *serializer->out_buffer++ = '"';

}

static void jes_serializer_render_key(struct jes_serializer* serializer, struct jes_element* element)
{
  assert(serializer->evaluated);
  assert((serializer->out_buffer + element->length + 4*sizeof(char)) < serializer->buffer_end);
  *serializer->out_buffer++ = '"';
  /* The JSON string has already been validated for size and structure,
     so this memcpy can proceed safely without further boundary checks. */
  memcpy(serializer->out_buffer, element->value, element->length);
  serializer->out_buffer += element->length;
  *serializer->out_buffer++ = '"';
  *serializer->out_buffer++ = ':';
  *serializer->out_buffer++ = ' ';
}

static void jes_serializer_render_number(struct jes_serializer* serializer, struct jes_element* element)
{
  assert(serializer->evaluated);
  assert((serializer->out_buffer + element->length) < serializer->buffer_end);
  /* The JSON string has already been validated for size and structure,
     so this memcpy can proceed safely without further boundary checks. */
  memcpy(serializer->out_buffer, element->value, element->length);
  serializer->out_buffer += element->length;
}

static void jes_serializer_render_literal(struct jes_serializer* serializer, struct jes_element* element)
{
  assert(serializer->evaluated);
  assert((serializer->out_buffer + element->length) < serializer->buffer_end);
  /* The JSON string has already been validated for size and structure,
     so this memcpy can proceed safely without further boundary checks. */
  memcpy(serializer->out_buffer, element->value, element->length);
  serializer->out_buffer += element->length;
}

static void jes_serializer_render_comma(struct jes_serializer* serializer)
{
  assert(serializer->evaluated);
  assert((serializer->out_buffer + sizeof(char)) < serializer->buffer_end);
  *serializer->out_buffer++ = ',';
}

static void jes_serializer_render_new_line(struct jes_serializer* serializer)
{
  assert(serializer->evaluated);
  assert((serializer->out_buffer + serializer->indention + sizeof(char)) < serializer->buffer_end);
  *serializer->out_buffer++ = '\n';
  memset(serializer->out_buffer, ' ', serializer->indention);
  serializer->out_buffer += serializer->indention;
}

static void jes_serializer_render_compact_key(struct jes_serializer* serializer, struct jes_element* element)
{
  assert(serializer->evaluated);
  assert((serializer->out_buffer + element->length + 3*sizeof(char)) < serializer->buffer_end);
  *serializer->out_buffer++ = '"';
  /* The JSON string has already been validated for size and structure,
     so this memcpy can proceed safely without further boundary checks. */
  memcpy(serializer->out_buffer, element->value, element->length);
  serializer->out_buffer += element->length;
  *serializer->out_buffer++ = '"';
  *serializer->out_buffer++ = ':';
}

static void jes_serializer_render_compact_new_line(struct jes_serializer* serializer)
{
  assert(serializer->evaluated);
  /* Compact format requires no new line. Do nothing */
  (void)serializer;
}

static inline void jes_serializer_process_expect_key_state(struct jes_context* ctx, struct jes_serializer* serializer)
{
  switch (NODE_TYPE(ctx->serdes.iter)) {
    case JES_KEY:
      serializer->renderer.key(serializer, &ctx->serdes.iter->json_tlv);
      ctx->serdes.state = JES_EXPECT_VALUE;
      break;
    default:
      ctx->status = JES_UNEXPECTED_ELEMENT;
      break;
  }
}

static inline void jes_serializer_process_expect_value_state(struct jes_context* ctx, struct jes_serializer* serializer)
{
  switch (NODE_TYPE(ctx->serdes.iter)) {
    case JES_STRING:
      serializer->renderer.string(serializer, &ctx->serdes.iter->json_tlv);
      ctx->serdes.state = JES_HAVE_VALUE;
      break;
    case JES_NUMBER:
      serializer->renderer.number(serializer, &ctx->serdes.iter->json_tlv);
      ctx->serdes.state = JES_HAVE_VALUE;
      break;
    case JES_TRUE:
    case JES_FALSE:
    case JES_NULL:
      serializer->renderer.literal(serializer, &ctx->serdes.iter->json_tlv);
      ctx->serdes.state = JES_HAVE_VALUE;
      break;
    case JES_OBJECT:
      serializer->renderer.opening_brace(serializer);
      serializer->indention += JES_TAB_SIZE;
      ctx->serdes.state = JES_EXPECT_KEY;
      if (HAS_CHILD(ctx->serdes.iter)) {
        serializer->renderer.new_line(serializer);
      }
      else { /* Empty OBJECT */
        serializer->renderer.closing_brace(serializer);
        serializer->indention -= JES_TAB_SIZE;
        ctx->serdes.state = JES_HAVE_VALUE;
      }
      break;
    case JES_ARRAY:
      serializer->renderer.opening_bracket(serializer);
      serializer->indention += JES_TAB_SIZE;
      ctx->serdes.state = JES_EXPECT_VALUE;
      if (HAS_CHILD(ctx->serdes.iter)) {
        serializer->renderer.new_line(serializer);
      }
      else { /* Empty ARRAY */
        serializer->renderer.closing_bracket(serializer);
        serializer->indention -= JES_TAB_SIZE;
        ctx->serdes.state = JES_HAVE_VALUE;
      }
      break;
    default:
      ctx->status = JES_UNEXPECTED_ELEMENT;
      break;
  }
}

static inline void jes_serializer_process_have_value_state(struct jes_context* ctx, struct jes_serializer* serializer)
{
  struct jes_node* iter = ctx->serdes.iter;

  while (iter) {
    if (HAS_SIBLING(iter)) {
      serializer->renderer.comma(serializer);
      if (PARENT_TYPE(ctx->node_mng, iter) == JES_ARRAY) {
        ctx->serdes.state = JES_EXPECT_VALUE;
        serializer->renderer.new_line(serializer);
      }
      else {
        serializer->renderer.new_line(serializer);
        ctx->serdes.state = JES_EXPECT_KEY;
      }
      break;
    }
    else { /* node has no siblings. move up one level to the parent node and render the required closings. */
      ctx->serdes.state = JES_HAVE_VALUE;
      iter = GET_PARENT(ctx->node_mng, iter);
      if (iter != NULL) {
        if (NODE_TYPE(iter) == JES_ARRAY) {
          serializer->indention -= JES_TAB_SIZE;
          serializer->renderer.new_line(serializer);
          serializer->renderer.closing_bracket(serializer);
        }
        else if (NODE_TYPE(iter) == JES_OBJECT) {
          serializer->indention -= JES_TAB_SIZE;
          serializer->renderer.new_line(serializer);
          serializer->renderer.closing_brace(serializer);
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
struct jes_node* jes_serializer_get_node(struct jes_context* ctx)
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
    ctx->serdes.state = JES_END;
  }
  /* Returns NULL if traversal is complete */
  return ctx->serdes.iter;
}

static void jes_serializer_state_machine(struct jes_context* ctx, struct jes_serializer* serializer)
{
  assert(ctx != NULL);

  if (serializer->out_buffer != NULL) {
    assert(serializer->evaluated);
  }

  ctx->serdes.state = JES_EXPECT_VALUE;
  ctx->serdes.iter = NULL;
  serializer->indention = 0;

  do {
#if defined(JES_ENABLE_SERIALIZER_STATE_LOG)
    JES_LOG_STATE("\nJES.Serializer.State: ", ctx->serdes.state, "");
#endif
    switch (ctx->serdes.state) {
      case JES_EXPECT_KEY:
        jes_serializer_get_node(ctx);
        jes_serializer_process_expect_key_state(ctx, serializer);
        break;
      case JES_EXPECT_VALUE:
        jes_serializer_get_node(ctx);
        jes_serializer_process_expect_value_state(ctx, serializer);
        break;
      case JES_HAVE_VALUE:
        jes_serializer_process_have_value_state(ctx, serializer);
        break;
      default:
        assert(0);
        ctx->status = JES_UNEXPECTED_STATE;
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
}

/* Dual-pass JSON serializer that renders the JES tree structure into a string buffer.
 *
 * Pass 1 (Calculate): Traverses the tree using calculation functions to determine the exact
 *         buffer size required for the final JSON output, including formatting overhead.
 * Pass 2 (Render): Re-traverses the tree using render functions to generate the actual
 *         JSON string, guaranteed to fit within the pre-calculated buffer bounds.
 */
size_t jes_render(struct jes_context *ctx, char* buffer, size_t buffer_length, bool compact)
{
  struct jes_serializer serializer = { 0 };

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return 0;
  }

  if (ctx->node_mng.root == NULL) {
    return 0;
  }

  /* First pass: just evaluate the JSON structure and calculate the required output buffer length. */
  serializer.renderer.opening_brace = jes_serializer_calculate_delimiter;
  serializer.renderer.closing_brace = jes_serializer_calculate_delimiter;
  serializer.renderer.opening_bracket = jes_serializer_calculate_delimiter;
  serializer.renderer.closing_bracket = jes_serializer_calculate_delimiter;
  serializer.renderer.literal = jes_serializer_calculate_literal;
  serializer.renderer.string = jes_serializer_calculate_string;
  serializer.renderer.number = jes_serializer_calculate_number;
  serializer.renderer.comma = jes_serializer_calculate_delimiter;
  serializer.renderer.key = compact
                          ? jes_serializer_calculate_compact_key
                          : jes_serializer_calculate_key;
  serializer.renderer.new_line = compact
                               ? jes_serializer_calculate_compact_new_line
                               : jes_serializer_calculate_new_line;
  serializer.out_buffer = NULL;
  serializer.evaluated = false;

  jes_serializer_state_machine(ctx, &serializer);
  serializer.evaluated_length += sizeof(char); /* NUL termination */

  if (buffer_length < serializer.evaluated_length) {
    ctx->status = JES_BUFFER_TOO_SMALL;
  }

  if (ctx->status == JES_NO_ERROR) {
    /* Second pass: Render the JSON tree into the string buffer. */
    serializer.renderer.opening_brace = jes_serializer_render_opening_brace;
    serializer.renderer.closing_brace = jes_serializer_render_closing_brace;
    serializer.renderer.opening_bracket = jes_serializer_render_opening_bracket;
    serializer.renderer.closing_bracket = jes_serializer_render_closing_bracket;
    serializer.renderer.literal = jes_serializer_render_literal;
    serializer.renderer.string = jes_serializer_render_string;
    serializer.renderer.number = jes_serializer_render_number;
    serializer.renderer.comma = jes_serializer_render_comma;
    serializer.renderer.key = compact
                            ? jes_serializer_render_compact_key
                            : jes_serializer_render_key;
    serializer.renderer.new_line = compact
                                 ? jes_serializer_render_compact_new_line
                                 : jes_serializer_render_new_line;
    serializer.out_buffer = buffer;
    serializer.buffer_end = buffer + buffer_length;
    serializer.evaluated = true;

    jes_serializer_state_machine(ctx, &serializer);

    if (ctx->status == JES_NO_ERROR) {
      assert(serializer.out_buffer + sizeof(char) == buffer + serializer.evaluated_length);
      *serializer.out_buffer++ = '\0';
    }
  }

  return (ctx->status == JES_NO_ERROR) ? serializer.evaluated_length : 0;
}

size_t jes_evaluate(struct jes_context *ctx, bool compact)
{
  struct jes_serializer serializer = { 0 };

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return 0;
  }

  if (ctx->node_mng.root == NULL) {
    return 0;
  }

  serializer.renderer.opening_brace = jes_serializer_calculate_delimiter;
  serializer.renderer.closing_brace = jes_serializer_calculate_delimiter;
  serializer.renderer.opening_bracket = jes_serializer_calculate_delimiter;
  serializer.renderer.closing_bracket = jes_serializer_calculate_delimiter;
  serializer.renderer.literal = jes_serializer_calculate_literal;
  serializer.renderer.string = jes_serializer_calculate_string;
  serializer.renderer.number = jes_serializer_calculate_number;
  serializer.renderer.comma = jes_serializer_calculate_delimiter;
  serializer.renderer.key = compact
                          ? jes_serializer_calculate_compact_key
                          : jes_serializer_calculate_key;
  serializer.renderer.new_line = compact
                               ? jes_serializer_calculate_compact_new_line
                               : jes_serializer_calculate_new_line;

  jes_serializer_state_machine(ctx, &serializer);
  serializer.evaluated_length += sizeof(char); /* NUL termination */

  return (ctx->status == JES_NO_ERROR) ? serializer.evaluated_length : 0;
}


/* Streaming serialization */
static inline bool jes_streaming_serializer_stack_is_empty(struct jes_streaming_serializer_context* ctx)
{
  return ctx->stack_top == -1;
}

static inline bool jes_streaming_serializer_stack_is_full(struct jes_streaming_serializer_context* ctx)
{
  return ctx->stack_top == (int)(ctx->stack_size - 1);
}

static inline jes_status jes_streaming_serializer_stack_push(struct jes_streaming_serializer_context* ctx, uint16_t type)
{
  assert((type == JES_OBJECT) || (type == JES_ARRAY));

  if (!jes_streaming_serializer_stack_is_full(ctx)) {
    ctx->stack_top++;
    ctx->stack[ctx->stack_top].member_count = 0;
    ctx->stack[ctx->stack_top].type = type;
    return JES_NO_ERROR;
  }

  return JES_INVALID_OPERATION;
}

static inline struct jes_container jes_streaming_serializer_stack_pop(struct jes_streaming_serializer_context* ctx)
{
  if (!jes_streaming_serializer_stack_is_empty(ctx)) {
    return ctx->stack[ctx->stack_top--];
  }

  return (struct jes_container){0, 0};
}

static inline struct jes_container jes_streaming_serializer_stack_get(struct jes_streaming_serializer_context* ctx)
{
  if (!jes_streaming_serializer_stack_is_empty(ctx)) {
    return ctx->stack[ctx->stack_top];
  }

  return (struct jes_container){0, 0};
}

static inline enum jes_type jes_streaming_serializer_stack_get_container_type(struct jes_streaming_serializer_context* ctx)
{
  if (!jes_streaming_serializer_stack_is_empty(ctx)) {
    return ctx->stack[ctx->stack_top].type;
  }

  return JES_UNKNOWN;
}

static inline uint16_t jes_streaming_serializer_stack_get_member_count(struct jes_streaming_serializer_context* ctx)
{
  if (!jes_streaming_serializer_stack_is_empty(ctx)) {
    return ctx->stack[ctx->stack_top].member_count;
  }

  return 0;
}

static inline jes_status jes_streaming_serializer_stack_add_member(struct jes_streaming_serializer_context* ctx)
{
  if (!jes_streaming_serializer_stack_is_empty(ctx)) {
    ctx->stack[ctx->stack_top].member_count++;
    return JES_NO_ERROR;
  }

  return JES_INVALID_OPERATION;
}

static inline jes_status jes_streaming_serializer_validate_state_object_start(struct jes_streaming_serializer_context* ctx)
{
  jes_status result = JES_NO_ERROR;

  switch (ctx->state) {
    case JES_START:             /* Fall through is intended */
    case JES_EXPECT_KEY:
    case JES_EXPECT_ARRAY_VALUE:
      break;
    default:
      result = JES_UNEXPECTED_STATE;
      break;
  }

  return result;
}

static inline jes_status jes_streaming_serializer_validate_state_object_end(struct jes_streaming_serializer_context* ctx)
{
  jes_status result = JES_NO_ERROR;

  switch (ctx->state) {
    case JES_EXPECT_KEY:
      break;
    default:
      result = JES_UNEXPECTED_STATE;
      break;
  }

  return result;
}

static inline jes_status jes_streaming_serializer_validate_state_array_start(struct jes_streaming_serializer_context* ctx)
{
  jes_status result = JES_NO_ERROR;

  switch (ctx->state) {
    case JES_START:  /* Fall through is intended */
    case JES_EXPECT_VALUE:
    case JES_EXPECT_ARRAY_VALUE:
      break;

    default:
      result = JES_UNEXPECTED_STATE;
      break;
  }

  return result;
}

static inline jes_status jes_streaming_serializer_validate_state_array_end(struct jes_streaming_serializer_context* ctx)
{
  jes_status result = JES_NO_ERROR;

  switch (ctx->state) {
    case JES_EXPECT_ARRAY_VALUE:
      break;
    default:
      result = JES_UNEXPECTED_STATE;
      break;
  }

  return result;
}

static inline jes_status jes_streaming_serializer_validate_state_key(struct jes_streaming_serializer_context* ctx)
{
  jes_status result = JES_NO_ERROR;

  switch (ctx->state) {
    case JES_EXPECT_KEY:
      break;
    default:
      result = JES_UNEXPECTED_STATE;
      break;
  }

  return result;
}

static inline jes_status jes_streaming_serializer_validate_state_value(struct jes_streaming_serializer_context* ctx)
{
  jes_status result = JES_NO_ERROR;

  assert(ctx);

  switch (ctx->state) {
    case JES_EXPECT_VALUE:
    case JES_EXPECT_ARRAY_VALUE:
      break;
    default:
      result = JES_UNEXPECTED_STATE;
      break;
  }

  return result;
}

static inline jes_status jes_streaming_serializer_render_object_start(struct jes_streaming_serializer_context* ctx)
{
  int render_size;

  assert(ctx);

  render_size = snprintf(ctx->out_buffer, ctx->out_buffer_size, "{");
  if ((render_size == 1) && (render_size < ctx->out_buffer_size)) {
    ctx->out_buffer += render_size;
    ctx->out_buffer_size -= render_size;
    return JES_NO_ERROR;
  }

  return JES_RENDER_FAILED;
}

static inline jes_status jes_streaming_serializer_render_object_end(struct jes_streaming_serializer_context* ctx)
{
  int render_size;

  assert(ctx);

  render_size = snprintf(ctx->out_buffer, ctx->out_buffer_size, "}");
  if ((render_size == 1) && (render_size < ctx->out_buffer_size)) {
    ctx->out_buffer += render_size;
    ctx->out_buffer_size -= render_size;
    return JES_NO_ERROR;
  }

  return JES_RENDER_FAILED;
}

static inline jes_status jes_streaming_serializer_render_array_start(struct jes_streaming_serializer_context* ctx)
{
  int render_size;

  assert(ctx);

  render_size = snprintf(ctx->out_buffer, ctx->out_buffer_size, "[");
  if ((render_size == 1) && (render_size < ctx->out_buffer_size)){
    ctx->out_buffer += render_size;
    ctx->out_buffer_size -= render_size;
    return JES_NO_ERROR;
  }

  return JES_RENDER_FAILED;
}

static inline jes_status jes_streaming_serializer_render_array_end(struct jes_streaming_serializer_context* ctx)
{
  int render_size;

  assert(ctx);

  render_size = snprintf(ctx->out_buffer, ctx->out_buffer_size, "]");
  if ((render_size == 1) && (render_size < ctx->out_buffer_size)) {
    ctx->out_buffer += render_size;
    ctx->out_buffer_size -= render_size;
    return JES_NO_ERROR;
  }

  return JES_RENDER_FAILED;
}

static inline jes_status jes_streaming_serializer_render_key(struct jes_streaming_serializer_context* ctx, const char* key, size_t key_length)
{
  int render_size;

  assert(ctx);
  assert(key);

  render_size = snprintf(ctx->out_buffer, ctx->out_buffer_size, "\"%.*s\":", key_length, key);
  if ((render_size == (key_length + 3)) && (render_size < ctx->out_buffer_size)) {
    ctx->out_buffer += render_size;
    ctx->out_buffer_size -= render_size;
    return JES_NO_ERROR;
  }

  return JES_RENDER_FAILED;
}

static inline jes_status jes_streaming_serializer_render_comma(struct jes_streaming_serializer_context* ctx)
{
  int render_size;

  assert(ctx);

  render_size = snprintf(ctx->out_buffer, ctx->out_buffer_size, ",");
  if ((render_size == 1) && (render_size < ctx->out_buffer_size)) {
    ctx->out_buffer += render_size;
    ctx->out_buffer_size -= render_size;
    return JES_NO_ERROR;
  }

  return JES_RENDER_FAILED;
}

static inline jes_status jes_streaming_serializer_render_literal(struct jes_streaming_serializer_context* ctx, const char* literal)
{
  int render_size;

  assert(ctx);
  assert(literal);

  render_size = snprintf(ctx->out_buffer, ctx->out_buffer_size, "%s", literal);
  if (render_size > 0) {
    ctx->out_buffer += render_size;
    ctx->out_buffer_size -= render_size;
    return JES_NO_ERROR;
  }

  return JES_RENDER_FAILED;
}

static inline jes_status jes_streaming_serializer_render_string(struct jes_streaming_serializer_context* ctx, const char* string, size_t length)
{
  int render_size;

  assert(ctx);
  assert(string);

  render_size = snprintf(ctx->out_buffer, ctx->out_buffer_size, "\"%.*s\"", length, string);
  if ((render_size == (length + 2)) && (render_size < ctx->out_buffer_size)) {
    ctx->out_buffer += render_size;
    ctx->out_buffer_size -= render_size;
    return JES_NO_ERROR;
  }

  return JES_RENDER_FAILED;
}

static inline jes_status jes_streaming_serializer_render_int32(struct jes_streaming_serializer_context* ctx, int32_t value)
{
  int render_size;

  assert(ctx);

  render_size = snprintf(ctx->out_buffer, ctx->out_buffer_size, "%d", value);
  if ((render_size != 0) && (render_size < ctx->out_buffer_size)) {
    ctx->out_buffer += render_size;
    ctx->out_buffer_size -= render_size;
    return JES_NO_ERROR;
  }

  return JES_RENDER_FAILED;
}

static inline jes_status jes_streaming_serializer_render_int64(struct jes_streaming_serializer_context* ctx, int64_t value)
{
  int render_size;

  assert(ctx);

  render_size = snprintf(ctx->out_buffer, ctx->out_buffer_size, "%lld", value);
  if ((render_size != 0) && (render_size < ctx->out_buffer_size)) {
    ctx->out_buffer += render_size;
    ctx->out_buffer_size -= render_size;
    return JES_NO_ERROR;
  }

  return JES_RENDER_FAILED;
}

static inline jes_status jes_streaming_serializer_render_uint32(struct jes_streaming_serializer_context* ctx, uint32_t value)
{
  int render_size;

  assert(ctx);

  render_size = snprintf(ctx->out_buffer, ctx->out_buffer_size, "%u", value);
  if ((render_size != 0) && (render_size < ctx->out_buffer_size)) {
    ctx->out_buffer += render_size;
    ctx->out_buffer_size -= render_size;
    return JES_NO_ERROR;
  }

  return JES_RENDER_FAILED;
}

static inline jes_status jes_streaming_serializer_render_uint64(struct jes_streaming_serializer_context* ctx, uint64_t value)
{
  int render_size;

  assert(ctx);

  render_size = snprintf(ctx->out_buffer, ctx->out_buffer_size, "%llu", value);
  if ((render_size != 0) && (render_size < ctx->out_buffer_size)) {
    ctx->out_buffer += render_size;
    ctx->out_buffer_size -= render_size;
    return JES_NO_ERROR;
  }

  return JES_RENDER_FAILED;
}

static inline jes_status jes_streaming_serializer_render_double(struct jes_streaming_serializer_context* ctx, double value)
{
  int render_size;

  assert(ctx);

  render_size = snprintf(ctx->out_buffer, ctx->out_buffer_size, "%f", value);
  if ((render_size != 0) && (render_size < ctx->out_buffer_size)) {
    ctx->out_buffer += render_size;
    ctx->out_buffer_size -= render_size;
    return JES_NO_ERROR;
  }

  return JES_RENDER_FAILED;
}

jes_status jes_init_streaming(struct jes_streaming_serializer_context* ctx,
                              char* output, size_t output_size,
                              uint8_t* stack, size_t stack_size)
{
  if ((ctx == NULL) || (output == NULL) || (stack == NULL) ||
      (stack_size < sizeof(struct jes_container))) {
    return JES_INVALID_PARAMETER;
  }

  ctx->out_buffer = output;
  ctx->out_buffer_size = output_size;
  ctx->stack_size = stack_size;
  ctx->stack = (struct jes_container*)stack;
  ctx->stack_top = -1;
  ctx->state = JES_START;
  return JES_NO_ERROR;
}


jes_status jes_render_object_start(struct jes_streaming_serializer_context* ctx)
{
  jes_status result = JES_NO_ERROR;

  if (ctx == NULL) {
    return JES_INVALID_CONTEXT;
  }

  result = jes_streaming_serializer_validate_state_object_start(ctx);
  if (result == JES_NO_ERROR) {
    result = jes_streaming_serializer_stack_push(ctx, JES_OBJECT);
    if (result == JES_NO_ERROR) {
      result = jes_streaming_serializer_render_object_start(ctx);
    }
  }

  if (result == JES_NO_ERROR) {
    ctx->state = JES_EXPECT_KEY;
  }

  return result;
}

jes_status jes_render_object_end(struct jes_streaming_serializer_context* ctx)
{
  jes_status result = JES_NO_ERROR;
  enum jes_type container_type;

  if (ctx == NULL) {
    return JES_INVALID_CONTEXT;
  }

  result = jes_streaming_serializer_validate_state_object_end(ctx);
  if (result == JES_NO_ERROR) {
    (void)jes_streaming_serializer_stack_pop(ctx);
    result = jes_streaming_serializer_render_object_end(ctx);
  }

  if (result == JES_NO_ERROR) {
    container_type = jes_streaming_serializer_stack_get_container_type(ctx);
    switch (container_type) {
      case JES_OBJECT:
        ctx->state = JES_EXPECT_KEY;
        break;
      case JES_ARRAY:
        ctx->state = JES_EXPECT_ARRAY_VALUE;
        break;
      default:
        assert(container_type == JES_UNKNOWN);
        ctx->state = JES_END;
        break;
    }
  }

  return result;
}

jes_status jes_render_array_start(struct jes_streaming_serializer_context* ctx)
{
  jes_status result = JES_NO_ERROR;

  if (ctx == NULL) {
    return JES_INVALID_CONTEXT;
  }

  result = jes_streaming_serializer_validate_state_array_start(ctx);
  if (result == JES_NO_ERROR) {
    result = jes_streaming_serializer_stack_push(ctx, JES_ARRAY);
    if (result == JES_NO_ERROR) {
      result = jes_streaming_serializer_render_array_start(ctx);
    }
  }

  if (result == JES_NO_ERROR) {
    ctx->state = JES_EXPECT_ARRAY_VALUE;
  }

  return result;
}

jes_status jes_render_array_end(struct jes_streaming_serializer_context* ctx)
{
  jes_status result = JES_NO_ERROR;
  enum jes_type container_type;

  if (ctx == NULL) {
    return JES_INVALID_CONTEXT;
  }

  result = jes_streaming_serializer_validate_state_array_end(ctx);
  if (result == JES_NO_ERROR) {
    (void)jes_streaming_serializer_stack_pop(ctx);
    result = jes_streaming_serializer_render_array_end(ctx);
  }

  if (result == JES_NO_ERROR) {
    container_type = jes_streaming_serializer_stack_get_container_type(ctx);
    switch (container_type) {
      case JES_OBJECT:
        ctx->state = JES_EXPECT_KEY;
        break;
      case JES_ARRAY:
        ctx->state = JES_EXPECT_ARRAY_VALUE;
        break;
      default:
        assert(container_type == JES_UNKNOWN);
        ctx->state = JES_END;
        break;
    }
  }

  return result;
}

jes_status jes_render_key(struct jes_streaming_serializer_context* ctx, const char* key, size_t length)
{
  jes_status result = JES_NO_ERROR;

  if (ctx == NULL) {
    return JES_INVALID_CONTEXT;
  }

  if (key == NULL) {
    return JES_INVALID_PARAMETER;
  }

  result = jes_streaming_serializer_validate_state_key(ctx);
  if (result == JES_NO_ERROR) {
    if (jes_streaming_serializer_stack_get_member_count(ctx) > 0) {
      result = jes_streaming_serializer_render_comma(ctx);
    }

    if (result == JES_NO_ERROR) {
      result = jes_streaming_serializer_stack_add_member(ctx);
    }

    if (result == JES_NO_ERROR) {
      result = jes_streaming_serializer_render_key(ctx, key, length);
    }
  }

  if (result == JES_NO_ERROR) {
    ctx->state = JES_EXPECT_VALUE;
  }

  return result;
}

jes_status jes_render_int32(struct jes_streaming_serializer_context* ctx, int32_t value)
{
  jes_status result = JES_NO_ERROR;
  enum jes_type container_type;

  if (ctx == NULL) {
    return JES_INVALID_CONTEXT;
  }

  result = jes_streaming_serializer_validate_state_value(ctx);

  if (result == JES_NO_ERROR) {
    container_type = jes_streaming_serializer_stack_get_container_type(ctx);
    if (container_type == JES_ARRAY) {
      if (jes_streaming_serializer_stack_get_member_count(ctx) > 0) {
        result = jes_streaming_serializer_render_comma(ctx);
      }
      if (result == JES_NO_ERROR) {
        result = jes_streaming_serializer_stack_add_member(ctx);
      }
    }

    if (result == JES_NO_ERROR) {
      result = jes_streaming_serializer_render_int32(ctx, value);
    }

    if (result == JES_NO_ERROR) {
      switch (container_type) {
        case JES_OBJECT:
          ctx->state = JES_EXPECT_KEY;
          break;
        case JES_ARRAY:
          ctx->state = JES_EXPECT_ARRAY_VALUE;
          break;
        default:
          assert(0);
          break;
      }
    }
  }

  return result;
}

jes_status jes_render_int64(struct jes_streaming_serializer_context* ctx, int64_t value)
{
  jes_status result = JES_NO_ERROR;
  enum jes_type container_type;

  if (ctx == NULL) {
    return JES_INVALID_CONTEXT;
  }

  result = jes_streaming_serializer_validate_state_value(ctx);

  if (result == JES_NO_ERROR) {
    container_type = jes_streaming_serializer_stack_get_container_type(ctx);
    if (container_type == JES_ARRAY) {
      if (jes_streaming_serializer_stack_get_member_count(ctx) > 0) {
        result = jes_streaming_serializer_render_comma(ctx);
      }
      if (result == JES_NO_ERROR) {
        result = jes_streaming_serializer_stack_add_member(ctx);
      }
    }

    if (result == JES_NO_ERROR) {
      result = jes_streaming_serializer_render_int64(ctx, value);
    }

    if (result == JES_NO_ERROR) {
      switch (container_type) {
        case JES_OBJECT:
          ctx->state = JES_EXPECT_KEY;
          break;
        case JES_ARRAY:
          ctx->state = JES_EXPECT_ARRAY_VALUE;
          break;
        default:
          assert(0);
          break;
      }
    }
  }

  return result;
}

jes_status jes_render_uint32(struct jes_streaming_serializer_context* ctx, uint32_t value)
{
  jes_status result = JES_NO_ERROR;
  enum jes_type container_type;

  if (ctx == NULL) {
    return JES_INVALID_CONTEXT;
  }

  result = jes_streaming_serializer_validate_state_value(ctx);

  if (result == JES_NO_ERROR) {
    container_type = jes_streaming_serializer_stack_get_container_type(ctx);
    if (container_type == JES_ARRAY) {
      if (jes_streaming_serializer_stack_get_member_count(ctx) > 0) {
        result = jes_streaming_serializer_render_comma(ctx);
      }
      if (result == JES_NO_ERROR) {
        result = jes_streaming_serializer_stack_add_member(ctx);
      }
    }

    if (result == JES_NO_ERROR) {
      result = jes_streaming_serializer_render_uint32(ctx, value);
    }

    if (result == JES_NO_ERROR) {
      switch (container_type) {
        case JES_OBJECT:
          ctx->state = JES_EXPECT_KEY;
          break;
        case JES_ARRAY:
          ctx->state = JES_EXPECT_ARRAY_VALUE;
          break;
        default:
          assert(0);
          break;
      }
    }
  }

  return result;
}

jes_status jes_render_uint64(struct jes_streaming_serializer_context* ctx, uint64_t value)
{
  jes_status result = JES_NO_ERROR;
  enum jes_type container_type;

  if (ctx == NULL) {
    return JES_INVALID_CONTEXT;
  }

  result = jes_streaming_serializer_validate_state_value(ctx);

  if (result == JES_NO_ERROR) {
    container_type = jes_streaming_serializer_stack_get_container_type(ctx);
    if (container_type == JES_ARRAY) {
      if (jes_streaming_serializer_stack_get_member_count(ctx) > 0) {
        result = jes_streaming_serializer_render_comma(ctx);
      }
      if (result == JES_NO_ERROR) {
        result = jes_streaming_serializer_stack_add_member(ctx);
      }
    }

    if (result == JES_NO_ERROR) {
      result = jes_streaming_serializer_render_uint64(ctx, value);
    }

    if (result == JES_NO_ERROR) {
      switch (container_type) {
        case JES_OBJECT:
          ctx->state = JES_EXPECT_KEY;
          break;
        case JES_ARRAY:
          ctx->state = JES_EXPECT_ARRAY_VALUE;
          break;
        default:
          assert(0);
          break;
      }
    }
  }

  return result;
}

jes_status jes_render_double(struct jes_streaming_serializer_context* ctx, double value)
{
  jes_status result = JES_NO_ERROR;
  enum jes_type container_type;

  if (ctx == NULL) {
    return JES_INVALID_CONTEXT;
  }

  result = jes_streaming_serializer_validate_state_value(ctx);

  if (result == JES_NO_ERROR) {
    container_type = jes_streaming_serializer_stack_get_container_type(ctx);
    if (container_type == JES_ARRAY) {
      if (jes_streaming_serializer_stack_get_member_count(ctx) > 0) {
        result = jes_streaming_serializer_render_comma(ctx);
      }
      if (result == JES_NO_ERROR) {
        result = jes_streaming_serializer_stack_add_member(ctx);
      }
    }

    if (result == JES_NO_ERROR) {
      result = jes_streaming_serializer_render_double(ctx, value);
    }

    if (result == JES_NO_ERROR) {
      switch (container_type) {
        case JES_OBJECT:
          ctx->state = JES_EXPECT_KEY;
          break;
        case JES_ARRAY:
          ctx->state = JES_EXPECT_ARRAY_VALUE;
          break;
        default:
          assert(0);
          break;
      }
    }
  }

  return result;
}

jes_status jes_render_string(struct jes_streaming_serializer_context* ctx, const char* string, size_t length)
{
  jes_status result = JES_NO_ERROR;
  enum jes_type container_type;

  if (ctx == NULL) {
    return JES_INVALID_CONTEXT;
  }

  result = jes_streaming_serializer_validate_state_value(ctx);

  if (result == JES_NO_ERROR) {
    container_type = jes_streaming_serializer_stack_get_container_type(ctx);
    if (container_type == JES_ARRAY) {
      if (jes_streaming_serializer_stack_get_member_count(ctx) > 0) {
        result = jes_streaming_serializer_render_comma(ctx);
      }
      if (result == JES_NO_ERROR) {
        result = jes_streaming_serializer_stack_add_member(ctx);
      }
    }

    if (result == JES_NO_ERROR) {
      result = jes_streaming_serializer_render_string(ctx, string, length);
    }

    if (result == JES_NO_ERROR) {
      switch (container_type) {
        case JES_OBJECT:
          ctx->state = JES_EXPECT_KEY;
          break;
        case JES_ARRAY:
          ctx->state = JES_EXPECT_ARRAY_VALUE;
          break;
        default:
          assert(0);
          break;
      }
    }
  }

  return result;
}

jes_status jes_render_null(struct jes_streaming_serializer_context* ctx)
{
  jes_status result = JES_NO_ERROR;
  enum jes_type container_type;

  if (ctx == NULL) {
    return JES_INVALID_CONTEXT;
  }

  result = jes_streaming_serializer_validate_state_value(ctx);

  if (result == JES_NO_ERROR) {
    container_type = jes_streaming_serializer_stack_get_container_type(ctx);
    if (container_type == JES_ARRAY) {
      if (jes_streaming_serializer_stack_get_member_count(ctx) > 0) {
        result = jes_streaming_serializer_render_comma(ctx);
      }
      if (result == JES_NO_ERROR) {
        result = jes_streaming_serializer_stack_add_member(ctx);
      }
    }

    if (result == JES_NO_ERROR) {
      result = jes_streaming_serializer_render_literal(ctx, "null");
    }

    if (result == JES_NO_ERROR) {
      switch (container_type) {
        case JES_OBJECT:
          ctx->state = JES_EXPECT_KEY;
          break;
        case JES_ARRAY:
          ctx->state = JES_EXPECT_ARRAY_VALUE;
          break;
        default:
          assert(0);
          break;
      }
    }
  }

  return result;
}

jes_status jes_render_true(struct jes_streaming_serializer_context* ctx)
{
  jes_status result = JES_NO_ERROR;
  enum jes_type container_type;

  if (ctx == NULL) {
    return JES_INVALID_CONTEXT;
  }

  result = jes_streaming_serializer_validate_state_value(ctx);

  if (result == JES_NO_ERROR) {
    container_type = jes_streaming_serializer_stack_get_container_type(ctx);
    if (container_type == JES_ARRAY) {
      if (jes_streaming_serializer_stack_get_member_count(ctx) > 0) {
        result = jes_streaming_serializer_render_comma(ctx);
      }
      if (result == JES_NO_ERROR) {
        result = jes_streaming_serializer_stack_add_member(ctx);
      }
    }

    if (result == JES_NO_ERROR) {
      result = jes_streaming_serializer_render_literal(ctx, "true");
    }

    if (result == JES_NO_ERROR) {
      switch (container_type) {
        case JES_OBJECT:
          ctx->state = JES_EXPECT_KEY;
          break;
        case JES_ARRAY:
          ctx->state = JES_EXPECT_ARRAY_VALUE;
          break;
        default:
          assert(0);
          break;
      }
    }
  }

  return result;
}

jes_status jes_render_false(struct jes_streaming_serializer_context* ctx)
{
  jes_status result = JES_NO_ERROR;
  enum jes_type container_type;

  if (ctx == NULL) {
    return JES_INVALID_CONTEXT;
  }

  result = jes_streaming_serializer_validate_state_value(ctx);

  if (result == JES_NO_ERROR) {
    container_type = jes_streaming_serializer_stack_get_container_type(ctx);
    if (container_type == JES_ARRAY) {
      if (jes_streaming_serializer_stack_get_member_count(ctx) > 0) {
        result = jes_streaming_serializer_render_comma(ctx);
      }
      if (result == JES_NO_ERROR) {
        result = jes_streaming_serializer_stack_add_member(ctx);
      }
    }

    if (result == JES_NO_ERROR) {
      result = jes_streaming_serializer_render_literal(ctx, "false");
    }

    if (result == JES_NO_ERROR) {
      switch (container_type) {
        case JES_OBJECT:
          ctx->state = JES_EXPECT_KEY;
          break;
        case JES_ARRAY:
          ctx->state = JES_EXPECT_ARRAY_VALUE;
          break;
        default:
          assert(0);
          break;
      }
    }
  }

  return result;
}