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
  size_t out_length;
  size_t indention;
  char* out_buffer;
  char* buffer_end;
  bool evaluated;
};

static void jes_serializer_calculate_delimiter(struct jes_serializer* serializer)
{
  serializer->out_length += sizeof(char);
}

static void jes_serializer_calculate_string(struct jes_serializer* serializer, struct jes_element* element)
{
  serializer->out_length += element->length + sizeof("\"\"") - 1;
}

static void jes_serializer_calculate_key(struct jes_serializer* serializer, struct jes_element* element)
{
  serializer->out_length += element->length;
  serializer->out_length += sizeof("\"\": ") - 1;
}

static void jes_serializer_calculate_number(struct jes_serializer* serializer, struct jes_element* element)
{
  serializer->out_length += element->length;
}

static void jes_serializer_calculate_literal(struct jes_serializer* serializer, struct jes_element* element)
{
  serializer->out_length += element->length;
}

static void jes_serializer_calculate_new_line(struct jes_serializer* serializer)
{
  serializer->out_length += serializer->indention;
  serializer->out_length += sizeof("\n") - 1;
}

static void jes_serializer_calculate_compact_key(struct jes_serializer* serializer, struct jes_element* element)
{
  serializer->out_length += element->length;
  serializer->out_length += sizeof("\"\":") - 1;
}

static void jes_serializer_calculate_compact_new_line(struct jes_serializer* serializer)
{
  /* Compact format requires no new line. Do nothing */
}

static void jes_serializer_render_opening_brace(struct jes_serializer* serializer)
{
  serializer->out_length += sizeof("{") - 1;
  *serializer->out_buffer++ = '{';
}

static void jes_serializer_render_closing_brace(struct jes_serializer* serializer)
{
  serializer->out_length += sizeof("}") - 1;
  *serializer->out_buffer++ = '}';
}

static void jes_serializer_render_opening_bracket(struct jes_serializer* serializer)
{
  serializer->out_length += sizeof("[") - 1;
  *serializer->out_buffer++ = '[';
}

static void jes_serializer_render_closing_bracket(struct jes_serializer* serializer)
{
  serializer->out_length += sizeof("]") - 1;
  *serializer->out_buffer++ = ']';
}

static void jes_serializer_render_string(struct jes_serializer* serializer, struct jes_element* element)
{
  serializer->out_length += element->length + sizeof("\"\"") - 1;
  *serializer->out_buffer++ = '"';
  /* The JSON string has already been validated for size and structure,
     so this memcpy can proceed safely without further boundary checks. */
  assert((serializer->out_buffer + element->length) < serializer->buffer_end);
  memcpy(serializer->out_buffer, element->value, element->length);
  serializer->out_buffer += element->length;
  *serializer->out_buffer++ = '"';
}

static void jes_serializer_render_key(struct jes_serializer* serializer, struct jes_element* element)
{
  serializer->out_length += (element->length + sizeof("\"\": ") - 1);
  *serializer->out_buffer++ = '"';
  /* The JSON string has already been validated for size and structure,
     so this memcpy can proceed safely without further boundary checks. */
  assert((serializer->out_buffer + element->length) < serializer->buffer_end);
  memcpy(serializer->out_buffer, element->value, element->length);
  serializer->out_buffer += element->length;
  *serializer->out_buffer++ = '"';
  *serializer->out_buffer++ = ':';
  *serializer->out_buffer++ = ' ';
}

static void jes_serializer_render_number(struct jes_serializer* serializer, struct jes_element* element)
{
  serializer->out_length += element->length;
  /* The JSON string has already been validated for size and structure,
     so this memcpy can proceed safely without further boundary checks. */
  assert((serializer->out_buffer + element->length) < serializer->buffer_end);
  memcpy(serializer->out_buffer, element->value, element->length);
  serializer->out_buffer += element->length;
}

static void jes_serializer_render_literal(struct jes_serializer* serializer, struct jes_element* element)
{
  serializer->out_length += element->length;
  /* The JSON string has already been validated for size and structure,
     so this memcpy can proceed safely without further boundary checks. */
  assert((serializer->out_buffer + element->length) < serializer->buffer_end);
  memcpy(serializer->out_buffer, element->value, element->length);
  serializer->out_buffer += element->length;
}

static void jes_serializer_render_comma(struct jes_serializer* serializer)
{
  serializer->out_length += sizeof(",") - 1;
  *serializer->out_buffer++ = ',';
}

static void jes_serializer_render_new_line(struct jes_serializer* serializer)
{
  serializer->out_length += serializer->indention + sizeof("\n") - 1;
  *serializer->out_buffer++ = '\n';
  memset(serializer->out_buffer, ' ', serializer->indention);
  serializer->out_buffer += serializer->indention;
}

static void jes_serializer_render_compact_key(struct jes_serializer* serializer, struct jes_element* element)
{
  serializer->out_length += (element->length + sizeof("\"\":") - 1);
  *serializer->out_buffer++ = '"';
  /* The JSON string has already been validated for size and structure,
     so this memcpy can proceed safely without further boundary checks. */
  assert((serializer->out_buffer + element->length) < serializer->buffer_end);
  memcpy(serializer->out_buffer, element->value, element->length);
  serializer->out_buffer += element->length;
  *serializer->out_buffer++ = '"';
  *serializer->out_buffer++ = ':';
}

static void jes_serializer_render_compact_new_line(struct jes_serializer* serializer)
{
  /* Compact format requires no new line. Do nothing */
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

static void jes_serializer_state_machine(struct jes_context* ctx, struct jes_serializer* serializer)
{
  assert(ctx != NULL);

  if (serializer->out_buffer != NULL) {
    assert(serializer->evaluated != false);
  }

  ctx->serdes.state = JES_EXPECT_VALUE;
  ctx->serdes.iter = NULL;
  serializer->out_length = sizeof(char); /* NUL termination */
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
  struct jes_serializer serializer;

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
  serializer.renderer.key = (compact != false)
                          ? jes_serializer_calculate_compact_key
                          : jes_serializer_calculate_key;
  serializer.renderer.new_line = (compact != false)
                               ? jes_serializer_calculate_compact_new_line
                               : jes_serializer_calculate_new_line;
  serializer.out_buffer = NULL;
  serializer.evaluated = false;

  jes_serializer_state_machine(ctx, &serializer);

  if ((ctx->status == JES_NO_ERROR) && (buffer_length >= serializer.out_length)) {

    serializer.renderer.opening_brace = jes_serializer_render_opening_brace;
    serializer.renderer.closing_brace = jes_serializer_render_closing_brace;
    serializer.renderer.opening_bracket = jes_serializer_render_opening_bracket;
    serializer.renderer.closing_bracket = jes_serializer_render_closing_bracket;
    serializer.renderer.literal = jes_serializer_render_literal;
    serializer.renderer.string = jes_serializer_render_string;
    serializer.renderer.number = jes_serializer_render_number;
    serializer.renderer.comma = jes_serializer_render_comma;
    serializer.renderer.key = (compact != false)
                            ? jes_serializer_render_compact_key
                            : jes_serializer_render_key;
    serializer.renderer.new_line = (compact != false)
                                 ? jes_serializer_render_compact_new_line
                                 : jes_serializer_render_new_line;
    serializer.out_buffer = buffer;
    serializer.buffer_end = buffer + buffer_length;
    serializer.evaluated = true;

    jes_serializer_state_machine(ctx, &serializer);

    if (ctx->status == JES_NO_ERROR) {
      buffer[serializer.out_length] = '\0';
    }
  }

  return (ctx->status == JES_NO_ERROR) ? serializer.out_length : 0;
}

uint32_t jes_evaluate(struct jes_context *ctx, bool compact)
{
  struct jes_serializer serializer;

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
  serializer.renderer.key = (compact != false)
                          ? jes_serializer_calculate_compact_key
                          : jes_serializer_calculate_key;
  serializer.renderer.new_line = (compact != false)
                               ? jes_serializer_calculate_compact_new_line
                               : jes_serializer_calculate_new_line;
  serializer.out_buffer = NULL;
  serializer.evaluated = false;

  jes_serializer_state_machine(ctx, &serializer);

  return (ctx->status == JES_NO_ERROR) ? serializer.out_length : 0;
}
