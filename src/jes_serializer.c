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
#else
  #define JES_LOG_NODE(...)
#endif



uint32_t jes_evaluate(struct jes_context *ctx, bool compact)
{
  uint32_t json_len = 0;
  uint32_t indention = 0;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return 0;
  }

  if (ctx->node_mng.root == NULL) {
    return 0;
  }

  ctx->serdes.iter = ctx->node_mng.root;
  ctx->serdes.state = JES_START;
  ctx->status = JES_NO_ERROR;

  do {
    JES_LOG_NODE("\n   ", ctx->serdes.iter - ctx->node_mng.pool, ctx->serdes.iter->json_tlv.type,
                  ctx->serdes.iter->json_tlv.length, ctx->serdes.iter->json_tlv.value,
                  ctx->serdes.iter->parent, ctx->serdes.iter->sibling, ctx->serdes.iter->first_child, ctx->serdes.iter->last_child, "");

    switch (ctx->serdes.iter->json_tlv.type) {

      case JES_OBJECT:
        if ((ctx->serdes.state == JES_START) ||
            (ctx->serdes.state == JES_EXPECT_KEY_VALUE)  ||
            (ctx->serdes.state == JES_EXPECT_ARRAY_VALUE)) {
          ctx->serdes.state = JES_EXPECT_KEY;
        }
        else {
          ctx->status = JES_UNEXPECTED_ELEMENT;
          return 0;
        }

        json_len += sizeof("{") - 1;
        if (!compact) {
          if (PARENT_TYPE(ctx, ctx->serdes.iter) == JES_ARRAY) {
            json_len += sizeof("\n") - 1;
            json_len += indention;
          }
          indention += 2;
        }
        break;

      case JES_KEY:
        if (ctx->serdes.state == JES_EXPECT_KEY) {
          ctx->serdes.state = JES_EXPECT_KEY_VALUE;
        }
        else {
          ctx->status = JES_UNEXPECTED_ELEMENT;
          return 0;
        }

        json_len += (ctx->serdes.iter->json_tlv.length + sizeof("\"\":") - 1);
        if (!compact) {
            json_len += sizeof("\n ") - 1;
            json_len += indention;
        }
        break;

      case JES_ARRAY:
        if ((ctx->serdes.state == JES_EXPECT_KEY_VALUE) || (ctx->serdes.state == JES_EXPECT_ARRAY_VALUE)) {
          ctx->serdes.state = JES_EXPECT_ARRAY_VALUE;
        }
        else {
          ctx->status = JES_UNEXPECTED_ELEMENT;
          return 0;
        }

        json_len += sizeof("[") - 1;
        if (!compact) {
          if (PARENT_TYPE(ctx, ctx->serdes.iter) == JES_ARRAY) {
            json_len += sizeof("\n") - 1;
            json_len += indention;
          }
          indention += 2;
        }
        break;

      case JES_STRING:
        if (ctx->serdes.state == JES_EXPECT_KEY_VALUE) {
          ctx->serdes.state = JES_HAVE_KEY_VALUE;
        }
        else if (ctx->serdes.state == JES_EXPECT_ARRAY_VALUE) {
          ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
        }
        else {
          ctx->status = JES_UNEXPECTED_ELEMENT;
          return 0;
        }

        json_len += (ctx->serdes.iter->json_tlv.length + sizeof("\"\"") - 1);
        if (!compact) {
          if (PARENT_TYPE(ctx, ctx->serdes.iter) != JES_KEY) {
            json_len += sizeof("\n") - 1;
            json_len += indention;
          }
        }
        break;

      case JES_NUMBER:
      case JES_TRUE:
      case JES_FALSE:
      case JES_NULL:
        if (ctx->serdes.state == JES_EXPECT_KEY_VALUE) {
          ctx->serdes.state = JES_HAVE_KEY_VALUE;
        }
        else if (ctx->serdes.state == JES_EXPECT_ARRAY_VALUE) {
          ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
        }
        else {
          ctx->status = JES_UNEXPECTED_ELEMENT;
           return 0;
        }

        json_len += ctx->serdes.iter->json_tlv.length;
        if (!compact) {
          if (PARENT_TYPE(ctx, ctx->serdes.iter) != JES_KEY) {
            json_len += sizeof("\n") - 1;
            json_len += indention;
          }
        }
        break;

      default:
        assert(0);
        break;
    }

    /* Iterate this branch down to the last child */
    if (HAS_CHILD(ctx->serdes.iter)) {
      ctx->serdes.iter = GET_FIRST_CHILD(ctx, ctx->serdes.iter);
      continue;
    }

    /* Node has no child. if it's an object or array, forge a closing delimiter. */
    if (ctx->serdes.iter->json_tlv.type == JES_OBJECT) {
      /* This covers empty objects */
      json_len += sizeof("}") - 1;
      if (!compact) {
        indention -= 2;
      }
    }
    else if (ctx->serdes.iter->json_tlv.type == JES_ARRAY) {
      /* This covers empty array */
      json_len += sizeof("]") - 1;
      if (!compact) {
        indention -= 2;
      }
    }

    /* If the last child has a sibling then we've an array. Get the sibling and iterate the branch.
       Siblings must always be separated using a comma. */
    if (HAS_SIBLING(ctx->serdes.iter)) {
      if ((ctx->serdes.iter->json_tlv.type == JES_KEY) &&
          ((ctx->serdes.state != JES_EXPECT_ARRAY_VALUE) || (ctx->serdes.state != JES_HAVE_ARRAY_VALUE))) {
        ctx->status = JES_UNEXPECTED_ELEMENT;
        return 0;
      }
      json_len += sizeof(",") - 1;
      ctx->serdes.iter = GET_SIBLING(ctx, ctx->serdes.iter);
      ctx->serdes.state = JES_EXPECT_ARRAY_VALUE;
      continue;
    }

    /* Node doesn't have any children or siblings. Iterate backward to the parent. */
    if (ctx->serdes.iter->json_tlv.type == JES_KEY) {
      break;
    }

    while ((ctx->serdes.iter = GET_PARENT(ctx, ctx->serdes.iter))) {
      if (ctx->serdes.iter->json_tlv.type == JES_KEY) {
        ctx->serdes.state = JES_HAVE_KEY_VALUE;
      }
      /* If the parent is an object or array, forge a closing delimiter. */
      else if (ctx->serdes.iter->json_tlv.type == JES_OBJECT) {
        if (!compact) {
          indention -= 2;
          json_len += indention + sizeof("\n") - 1;
        }
        json_len += sizeof("}") - 1;
      }
      else if (ctx->serdes.iter->json_tlv.type == JES_ARRAY) {
        if (!compact) {
          indention -= 2;
          json_len += indention + sizeof("\n") - 1;
        }
        json_len += sizeof("]") - 1;
      }
      else if ((ctx->serdes.iter->json_tlv.type == JES_KEY) && (ctx->serdes.state != JES_HAVE_KEY_VALUE)) {
        ctx->status = JES_UNEXPECTED_ELEMENT;
        return 0;
      }

      /* If the parent has a sibling, take it and iterate the branch down.
         Siblings must always be separated using a comma. */
      if (HAS_SIBLING(ctx->serdes.iter)) {
        ctx->serdes.iter = GET_SIBLING(ctx, ctx->serdes.iter);
        json_len += sizeof(",") - 1;

        if (PARENT_TYPE(ctx, ctx->serdes.iter) == JES_OBJECT) {
          ctx->serdes.state = JES_EXPECT_KEY;
        }
        else if (PARENT_TYPE(ctx, ctx->serdes.iter) == JES_ARRAY) {
          ctx->serdes.state = JES_EXPECT_ARRAY_VALUE;
        }
        else {
          ctx->status = JES_UNEXPECTED_ELEMENT;
          return 0;
        }
        break;
      }
    }

  } while (ctx->serdes.iter && (ctx->serdes.iter != ctx->node_mng.root));

  if (ctx->serdes.state == JES_EXPECT_KEY_VALUE) {
    ctx->status = JES_RENDER_FAILED;
    json_len = 0;
  }
  else {
    ctx->serdes.iter = ctx->node_mng.root;
  }
  return json_len;
}

uint32_t jes_render(struct jes_context *ctx, char *buffer, uint32_t length, bool compact)
{
  char *dst = buffer;
  struct jes_node *iter = ctx->node_mng.root;
  uint32_t required_buffer = 0;
  uint32_t indention = 0;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return 0;
  }

  if (buffer == NULL) {
    ctx->status = JES_INVALID_PARAMETER;
    return 0;
  }

  required_buffer = jes_evaluate(ctx, compact);

  if (length < required_buffer) {
    ctx->status = JES_OUT_OF_MEMORY;
    return 0;
  }

  if (required_buffer == 0) {
    return 0;
  }

  buffer[0] = '\0';

  while (iter) {

    if (iter->json_tlv.type == JES_OBJECT) {

      if (!compact) {
        if (PARENT_TYPE(ctx, iter) == JES_ARRAY) {
          *dst++ = '\n';
          memset(dst, ' ', indention*sizeof(char));
          dst += indention;
        }
        indention += 2;
      }
      *dst++ = '{';
    }
    else if (iter->json_tlv.type == JES_KEY) {

      if (!compact) {
        *dst++ = '\n';
        memset(dst, ' ', indention*sizeof(char));
        dst += indention;
      }

      *dst++ = '"';
      /* The JSON string has already been validated for size and structure,
         so this memcpy can proceed safely without further boundary checks. */
      assert((dst + iter->json_tlv.length) <= (buffer + length));
      dst = (char*)memcpy(dst, iter->json_tlv.value, iter->json_tlv.length) + iter->json_tlv.length;
      *dst++ = '"';
      *dst++ = ':';

      if (!compact) {
        *dst++ = ' ';
      }
    }
    else if (iter->json_tlv.type == JES_STRING) {

      if (!compact) {
        if (PARENT_TYPE(ctx, iter) != JES_KEY) {
          *dst++ = '\n';
          memset(dst, ' ', indention*sizeof(char));
          dst += indention;
        }
      }

      *dst++ = '"';
      /* The JSON string has already been validated for size and structure,
         so this memcpy can proceed safely without further boundary checks. */
      assert((dst + iter->json_tlv.length) <= (buffer + length));
      dst = (char*)memcpy(dst, iter->json_tlv.value, iter->json_tlv.length) + iter->json_tlv.length;
      *dst++ = '"';
    }
    else if ((iter->json_tlv.type == JES_NUMBER)  ||
             (iter->json_tlv.type == JES_TRUE)    ||
             (iter->json_tlv.type == JES_FALSE)   ||
             (iter->json_tlv.type == JES_NULL)) {

      if (!compact) {
        if (PARENT_TYPE(ctx, iter) != JES_KEY) {
          *dst++ = '\n';
          memset(dst, ' ', indention*sizeof(char));
          dst += indention;
        }
      }
      /* The JSON string has already been validated for size and structure,
         so this memcpy can proceed safely without further boundary checks. */
      assert((dst + iter->json_tlv.length) <= (buffer + length));
      dst = (char*)memcpy(dst, iter->json_tlv.value, iter->json_tlv.length) + iter->json_tlv.length;
    }
    else if (iter->json_tlv.type == JES_ARRAY) {

      if (!compact) {
        if (PARENT_TYPE(ctx, iter) == JES_ARRAY) {
          *dst++ = '\n';
          memset(dst, ' ', indention*sizeof(char));
          dst += indention;
        }
        indention += 2;
      }

      *dst++ = '[';
    }
    else {
      assert(0);
      ctx->status = JES_UNEXPECTED_ELEMENT;
      break;
    }

    /* Iterate this branch down to the last child */
    if (HAS_CHILD(iter)) {
      iter = GET_FIRST_CHILD(ctx, iter);
      continue;
    }

    /* Node has no child. if it's an object or array, forge a closing delimiter. */
    if (iter->json_tlv.type == JES_OBJECT) {
      /* This covers empty objects */
      if (!compact) {
        indention -= 2;
      }
      *dst++ = '}';
    }
    else if (iter->json_tlv.type == JES_ARRAY) {
      /* This covers empty array */
      if (!compact) {
        indention -= 2;
      }
      *dst++ = ']';
    }

    /* If Node has a sibling then Iterate the branch down.
       Siblings must always be separated using a comma. */
    if (HAS_SIBLING(iter)) {
      iter = GET_SIBLING(ctx, iter);
      *dst++ = ',';
      continue;
    }

    /* Node doesn't have any children or siblings. Iterate backward to the parent. */
    while ((iter = GET_PARENT(ctx, iter))) {
      /* If the parent is an object or array, forge a closing delimiter. */
      if (iter->json_tlv.type == JES_OBJECT) {
        if (!compact) {
          *dst++ = '\n';
          indention -= 2;
          memset(dst, ' ', indention*sizeof(char));
          dst += indention;
        }
        *dst++ = '}';
      }
      else if (iter->json_tlv.type == JES_ARRAY) {
        if (!compact) {
          *dst++ = '\n';
          indention -= 2;
          memset(dst, ' ', indention*sizeof(char));
          dst += indention;
        }
        *dst++ = ']';
      }
      /* If the parent has a sibling, take it and iterate the new branch down.
         Siblings must always be separated using a comma. */
      if (HAS_SIBLING(iter)) {
        iter = GET_SIBLING(ctx, iter);
        *dst++ = ',';
        break;
      }
    }
  }

  ctx->serdes.iter = ctx->node_mng.root;
  return dst - buffer;
}

static inline char* jes_serializer_render_opening_brace(struct jes_context* ctx, bool compact, size_t* length, size_t* indention, char* buffer)
{
  *length += sizeof("{") - 1;

  if (!compact) {
    if (PARENT_TYPE(ctx, ctx->serdes.iter) == JES_ARRAY) {
      if (buffer != NULL) {
        *buffer++ = '\n';
        memset(buffer, ' ', *indention * sizeof(char));
      }
      *length += sizeof("\n") - 1;
      *length += *indention;
    }
    *indention += 2;
  }

  if (buffer != NULL) { *buffer++ = '{'; }
  return buffer;
}

static inline char* jes_serializer_render_opening_bracket(struct jes_context* ctx, bool compact, size_t* length, size_t* indention, char* buffer)
{
  *length += sizeof("[") - 1;

  if (!compact) {
    if (PARENT_TYPE(ctx, ctx->serdes.iter) == JES_ARRAY) {
      if (buffer != NULL) {
        *buffer++ = '\n';
        memset(buffer, ' ', *indention * sizeof(char));
      }
      *length += sizeof("\n") - 1;
      *length += *indention;
    }
    *indention += 2;
  }
  if (buffer != NULL) { *buffer++ = '['; }
  return buffer;
}

static inline char* jes_serializer_render_string(struct jes_context* ctx, bool compact, size_t* length, size_t* indention, char* buffer)
{
  *length += ctx->serdes.iter->json_tlv.length + sizeof("\"\"") - 1;

  if (!compact) {
    if (PARENT_TYPE(ctx, ctx->serdes.iter) != JES_KEY) {
      if (buffer != NULL) {
        *buffer++ = '\n';
        memset(buffer, ' ', *indention * sizeof(char));
      }
      *length += sizeof("\n") - 1;
      *length += *indention;
    }
  }

  if (buffer != NULL) {
    *buffer++ = '"';
    /* The JSON string has already been validated for size and structure,
       so this memcpy can proceed safely without further boundary checks. */
    //assert((buffer + iter->json_tlv.length) <= (buffer + *length));
    buffer = (char*)memcpy(buffer, ctx->serdes.iter->json_tlv.value, ctx->serdes.iter->json_tlv.length) + ctx->serdes.iter->json_tlv.length;
    *buffer++ = '"';
  }
  return buffer;
}

static inline char* jes_serializer_render_key(struct jes_context* ctx, bool compact, size_t* length, size_t* indention, char* buffer)
{
  *length += (ctx->serdes.iter->json_tlv.length + sizeof("\"\":") - 1);

  if (!compact) {
    if (buffer != NULL) {
      *buffer++ = '\n';
      memset(buffer, ' ', *indention * sizeof(char));
    }
    *length += sizeof("\n ") - 1;
    *length += *indention;
  }

  if (buffer != NULL) {
    *buffer++ = '"';
    /* The JSON string has already been validated for size and structure,
       so this memcpy can proceed safely without further boundary checks. */
    //assert((buffer + iter->json_tlv.length) <= (buffer + *length));
    buffer = (char*)memcpy(buffer, ctx->serdes.iter->json_tlv.value, ctx->serdes.iter->json_tlv.length) + ctx->serdes.iter->json_tlv.length;
    *buffer++ = '"';
    *buffer++ = ':';
  }
  return buffer;
}

static inline char* jes_serializer_render_number(struct jes_context* ctx, bool compact, size_t* length, size_t* indention, char* buffer)
{
  *length += ctx->serdes.iter->json_tlv.length;

  if (!compact) {
    if (PARENT_TYPE(ctx, ctx->serdes.iter) != JES_KEY) {
      if (buffer != NULL) {
        *buffer++ = '\n';
        memset(buffer, ' ', *indention * sizeof(char));
      }
      *length += sizeof("\n") - 1;
      *length += *indention;
    }
  }

  if(buffer != NULL) {
    /* The JSON string has already been validated for size and structure,
       so this memcpy can proceed safely without further boundary checks. */
    //assert((buffer + iter->json_tlv.length) <= (buffer + length));
    buffer = (char*)memcpy(buffer, ctx->serdes.iter->json_tlv.value, ctx->serdes.iter->json_tlv.length) + ctx->serdes.iter->json_tlv.length;
  }
  return buffer;
}

static inline char* jes_serializer_render_literal(struct jes_context* ctx, bool compact, size_t* length, size_t* indention, char* buffer)
{
  *length += ctx->serdes.iter->json_tlv.length;

  if (!compact) {
    if (PARENT_TYPE(ctx, ctx->serdes.iter) != JES_KEY) {
      if (buffer != NULL) {
        *buffer++ = '\n';
        memset(buffer, ' ', *indention * sizeof(char));
      }
      *length += sizeof("\n") - 1;
      *length += *indention;
    }
  }

  if(buffer != NULL) {
    /* The JSON string has already been validated for size and structure,
       so this memcpy can proceed safely without further boundary checks. */
    //assert((buffer + iter->json_tlv.length) <= (buffer + length));
    buffer = (char*)memcpy(buffer, ctx->serdes.iter->json_tlv.value, ctx->serdes.iter->json_tlv.length) + ctx->serdes.iter->json_tlv.length;
  }

  return buffer;
}

static inline char* jes_serializer_render_comma(struct jes_context* ctx, bool compact, size_t* length, size_t* indention, char* buffer)
{
  *length += sizeof(",") - 1;
  if (buffer != NULL) { *buffer++ = ','; }
  return buffer;
}

static inline char* jes_serializer_render_closing_brace(struct jes_context* ctx, bool compact, size_t* length, size_t* indention, char* buffer)
{
  *length += sizeof("}") - 1;

  if (!compact) {
    *indention -= 2;
    if (buffer != NULL) {
      *buffer++ = '\n';
      memset(buffer, ' ', *indention * sizeof(char));
      buffer += *indention;
    }
  }

  if (buffer != NULL) { *buffer++ = '}'; }

  return buffer;
}

static inline char* jes_serializer_render_closing_bracket(struct jes_context* ctx, bool compact, size_t* length, size_t* indention, char* buffer)
{
  *length += sizeof("]") - 1;

  if (!compact) {
    *indention -= 2;
    if (buffer != NULL) {
      *buffer++ = '\n';
      memset(buffer, ' ', *indention * sizeof(char));
      buffer += *indention;
    }
  }

  if (buffer != NULL) { *buffer++ = ']'; }
  return buffer;
}

static inline char* jes_serializer_process_start_state(struct jes_context* ctx, bool compact, size_t* length, size_t* indention, char* buffer)
{

  switch (NODE_TYPE(ctx->serdes.iter)) {
    case JES_OBJECT:
      buffer = jes_serializer_render_opening_brace(ctx, compact, length, indention, buffer);
      ctx->serdes.state = JES_EXPECT_KEY;
      break;
    case JES_KEY:
      buffer = jes_serializer_render_key(ctx, compact, length, indention, buffer);
      ctx->serdes.state = JES_EXPECT_KEY_VALUE;
      break;
    case JES_ARRAY:
      buffer = jes_serializer_render_opening_bracket(ctx, compact, length, indention, buffer);
      ctx->serdes.state = JES_EXPECT_ARRAY_VALUE;
      break;
    case JES_STRING:
      buffer = jes_serializer_render_string(ctx, compact, length, indention, buffer);
      if (PARENT_TYPE(ctx, ctx->serdes.iter) == JES_KEY) {
        ctx->serdes.state = JES_HAVE_KEY_VALUE;
      }
      else if (PARENT_TYPE(ctx, ctx->serdes.iter) == JES_ARRAY) {
        ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      }
      break;
    case JES_NUMBER:
      buffer = jes_serializer_render_number(ctx, compact, length, indention, buffer);
      if (PARENT_TYPE(ctx, ctx->serdes.iter) == JES_KEY) {
        ctx->serdes.state = JES_HAVE_KEY_VALUE;
      }
      else if (PARENT_TYPE(ctx, ctx->serdes.iter) == JES_ARRAY) {
        ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      }
      break;
    case JES_TRUE:
    case JES_FALSE:
    case JES_NULL:
      buffer = jes_serializer_render_literal(ctx, compact, length, indention, buffer);
      if (PARENT_TYPE(ctx, ctx->serdes.iter) == JES_KEY) {
        ctx->serdes.state = JES_HAVE_KEY_VALUE;
      }
      else if (PARENT_TYPE(ctx, ctx->serdes.iter) == JES_ARRAY) {
        ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      }
      break;
    default:
      assert(0);
            printf("\n -6-");
      ctx->status = JES_UNEXPECTED_ELEMENT;
      break;
  }
  return buffer;
}

static inline char* jes_serializer_process_expect_key_state(struct jes_context* ctx, bool compact, size_t* length, size_t* indention, char* buffer)
{
  switch (NODE_TYPE(ctx->serdes.iter)) {
    case JES_KEY:
      buffer = jes_serializer_render_key(ctx, compact, length, indention, buffer);
      ctx->serdes.state = JES_EXPECT_KEY_VALUE;
      break;
    default:
          printf("\n -5-");
      ctx->status = JES_UNEXPECTED_ELEMENT;
      break;
  }
  return buffer;
}

static inline char* jes_serializer_process_expect_key_value_state(struct jes_context* ctx, bool compact, size_t* length, size_t* indention, char* buffer)
{
  switch (NODE_TYPE(ctx->serdes.iter)) {
    case JES_STRING:
      buffer = jes_serializer_render_string(ctx, compact, length, indention, buffer);
      ctx->serdes.state = JES_HAVE_KEY_VALUE;
      break;
    case JES_NUMBER:
      buffer = jes_serializer_render_number(ctx, compact, length, indention, buffer);
      ctx->serdes.state = JES_HAVE_KEY_VALUE;
      break;
    case JES_TRUE:
    case JES_FALSE:
    case JES_NULL:
      buffer = jes_serializer_render_literal(ctx, compact, length, indention, buffer);
      ctx->serdes.state = JES_HAVE_KEY_VALUE;
      break;
    case JES_OBJECT:
      buffer = jes_serializer_render_opening_brace(ctx, compact, length, indention, buffer);
      ctx->serdes.state = JES_EXPECT_KEY;
      if (!HAS_CHILD(ctx->serdes.iter)) {
        buffer = jes_serializer_render_closing_brace(ctx, compact, length, indention, buffer);
        ctx->serdes.state = JES_HAVE_KEY_VALUE;
      }
      break;
    case JES_ARRAY:
      buffer = jes_serializer_render_opening_bracket(ctx, compact, length, indention, buffer);
      ctx->serdes.state = JES_EXPECT_ARRAY_VALUE;
      if (!HAS_CHILD(ctx->serdes.iter)) {
        buffer = jes_serializer_render_closing_brace(ctx, compact, length, indention, buffer);
        ctx->serdes.state = JES_HAVE_KEY_VALUE;
      }
      break;
    default:
      ctx->status = JES_UNEXPECTED_ELEMENT;
      break;
  }
  return buffer;
}

static inline char* jes_serializer_process_have_key_value_state(struct jes_context* ctx, bool compact, size_t* length, size_t* indention, char* buffer)
{
  struct jes_node* iter = ctx->serdes.iter;
  while (iter) {
    if (HAS_SIBLING(iter)) {
      buffer = jes_serializer_render_comma(ctx, compact, length, indention, buffer);
      if (PARENT_TYPE(ctx, iter) == JES_ARRAY) {
        ctx->serdes.state = JES_EXPECT_ARRAY_VALUE;
      }
      else {
        ctx->serdes.state = JES_EXPECT_KEY;
      }
      break;
    }
    else { /* node has no siblings. */
      iter = GET_PARENT(ctx, iter);
      if (NODE_TYPE(iter) == JES_ARRAY) {
        buffer = jes_serializer_render_closing_bracket(ctx, compact, length, indention, buffer);
      }
      else if (NODE_TYPE(iter) == JES_KEY) {
        ctx->serdes.state = JES_HAVE_KEY_VALUE;
      }
      else if (NODE_TYPE(iter) == JES_OBJECT) {
        buffer = jes_serializer_render_closing_brace(ctx, compact, length, indention, buffer);
        ctx->serdes.state = JES_HAVE_KEY_VALUE;
      }
    }
  }
  return buffer;
}

static inline char* jes_serializer_process_expect_array_value_state(struct jes_context* ctx, bool compact, size_t* length, size_t* indention, char* buffer)
{
  switch (NODE_TYPE(ctx->serdes.iter)) {
    case JES_STRING:
      buffer = jes_serializer_render_string(ctx, compact, length, indention, buffer);
      ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_NUMBER:
      buffer = jes_serializer_render_number(ctx, compact, length, indention, buffer);
      ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_TRUE:
    case JES_FALSE:
    case JES_NULL:
      buffer = jes_serializer_render_literal(ctx, compact, length, indention, buffer);
      ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      break;
    case JES_OBJECT:
      buffer = jes_serializer_render_opening_brace(ctx, compact, length, indention, buffer);
      ctx->serdes.state = JES_EXPECT_KEY;
      if (!HAS_CHILD(ctx->serdes.iter)) {
        buffer = jes_serializer_render_closing_brace(ctx, compact, length, indention, buffer);
        ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      }
      break;
    case JES_ARRAY:
      buffer = jes_serializer_render_opening_bracket(ctx, compact, length, indention, buffer);
      ctx->serdes.state = JES_EXPECT_ARRAY_VALUE;
      if (!HAS_CHILD(ctx->serdes.iter)) {
        buffer = jes_serializer_render_closing_bracket(ctx, compact, length, indention, buffer);
        ctx->serdes.state = JES_HAVE_ARRAY_VALUE;
      }
      break;
    default:
          printf("\n -2-");
      ctx->status = JES_UNEXPECTED_ELEMENT;
      break;
  }
  return buffer;
}

static inline char* jes_serializer_process_have_array_value_state(struct jes_context* ctx, bool compact, size_t* length, size_t* indention, char* buffer)
{
    //printf("\n#####jes_serializer_process_have_array_value_state");
  struct jes_node* iter = ctx->serdes.iter;
  while (iter) {
    if (HAS_SIBLING(iter)) {
      buffer = jes_serializer_render_comma(ctx, compact, length, indention, buffer);

      if (PARENT_TYPE(ctx, iter) == JES_ARRAY) {
        ctx->serdes.state = JES_EXPECT_ARRAY_VALUE;
      }
      else {
        ctx->serdes.state = JES_EXPECT_KEY;
      }
      break;
    }
    else { /* node has no siblings. */
      iter = GET_PARENT(ctx, iter);
      if (NODE_TYPE(iter) == JES_ARRAY) {
        buffer = jes_serializer_render_closing_bracket(ctx, compact, length, indention, buffer);
      }
      else if (NODE_TYPE(iter) == JES_KEY) {
        ctx->serdes.state = JES_HAVE_KEY_VALUE;
      }
      else if (NODE_TYPE(iter) == JES_OBJECT) {
        buffer = jes_serializer_render_closing_brace(ctx, compact, length, indention, buffer);
        ctx->serdes.state = JES_HAVE_KEY_VALUE;
      }
    }
  }
  return buffer;
}

/* Pre-order depth-first traversal
   visit node → visit children → visit siblings → backtrack to parent's siblings. */
static inline struct jes_node* jes_serializer_get_next_node(struct jes_context* ctx)
{
  assert(ctx != NULL);

  /* Start at root node on first call */
  if (ctx->serdes.iter == NULL) {
    ctx->serdes.iter = ctx->node_mng.root;
  }
  /* If current node has children, get the first child */
  else if (HAS_CHILD(ctx->serdes.iter)) {
    ctx->serdes.iter = GET_FIRST_CHILD(ctx, ctx->serdes.iter);
  }
  /* No children available: try to move to next sibling (breadth at current level) */
  else if (HAS_SIBLING(ctx->serdes.iter)) {
    ctx->serdes.iter = GET_SIBLING(ctx, ctx->serdes.iter);
  }
  else {
    /* No children or siblings: backtrack up the tree to find next un-visited branch
       Walk up parent chain until we find a parent with an un-visited sibling */
    while ((ctx->serdes.iter = GET_PARENT(ctx, ctx->serdes.iter))) {
      /* Found a parent with a sibling - this is our next branch to explore */
      if (HAS_SIBLING(ctx->serdes.iter)) {
        ctx->serdes.iter = GET_SIBLING(ctx, ctx->serdes.iter);
        break;
      }
    }
  }
  /* Return NULL if traversal is complete */
  return ctx->serdes.iter;
}

uint32_t jes_render2(struct jes_context *ctx, char* buffer, size_t length, bool compact)
{
  size_t json_len = 0;
  size_t indention = 0;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return 0;
  }

  if (ctx->node_mng.root == NULL) {
    return 0;
  }

  ctx->serdes.iter = NULL; //ctx->node_mng.root;
  ctx->serdes.state = JES_START;
  ctx->status = JES_NO_ERROR;

  jes_serializer_get_next_node(ctx);

  while ((ctx->serdes.iter != NULL) && (ctx->status == JES_NO_ERROR)) {

    switch (ctx->serdes.state) {
      case JES_START:
        buffer = jes_serializer_process_start_state(ctx, compact, &json_len, &indention, buffer);
        break;
      case JES_EXPECT_KEY:
        buffer = jes_serializer_process_expect_key_state(ctx, compact, &json_len, &indention, buffer);
        break;
      case JES_EXPECT_KEY_VALUE:
        buffer = jes_serializer_process_expect_key_value_state(ctx, compact, &json_len, &indention, buffer);
        if (ctx->serdes.state == JES_HAVE_KEY_VALUE) { continue; }
        break;
      case JES_HAVE_KEY_VALUE:
        buffer = jes_serializer_process_have_key_value_state(ctx, compact, &json_len, &indention, buffer);
        break;
      case JES_EXPECT_ARRAY_VALUE:
        buffer = jes_serializer_process_expect_array_value_state(ctx, compact, &json_len, &indention, buffer);
        if (ctx->serdes.state == JES_HAVE_ARRAY_VALUE) { continue; }
        break;
      case JES_HAVE_ARRAY_VALUE:
        buffer = jes_serializer_process_have_array_value_state(ctx, compact, &json_len, &indention, buffer);
        break;
      case JES_EXPECT_EOF:
        break;
      default:
        assert(0);
        ctx->status = JES_RENDER_FAILED;
        break;
    }

    JES_LOG_NODE("\n   ", ctx->serdes.iter - ctx->node_mng.pool,
                          ctx->serdes.iter->json_tlv.type,
                          ctx->serdes.iter->json_tlv.length,
                          ctx->serdes.iter->json_tlv.value,
                          ctx->serdes.iter->parent,
                          ctx->serdes.iter->sibling,
                          ctx->serdes.iter->first_child,
                          ctx->serdes.iter->last_child,
                          "");

    jes_serializer_get_next_node(ctx);
  }

  if (ctx->serdes.state == JES_EXPECT_KEY_VALUE) {
    ctx->status = JES_RENDER_FAILED;
    json_len = 0;
  }
  else {
    ctx->serdes.iter = ctx->node_mng.root;
  }
  return json_len;
}

uint32_t jes_evaluate2(struct jes_context *ctx, bool compact)
{
  size_t json_len = 0;
  size_t indention = 0;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return 0;
  }

  if (ctx->node_mng.root == NULL) {
    return 0;
  }

  ctx->serdes.iter = NULL; //ctx->node_mng.root;
  ctx->serdes.state = JES_START;
  ctx->status = JES_NO_ERROR;

  jes_serializer_get_next_node(ctx);

  while ((ctx->serdes.iter != NULL) && (ctx->status == JES_NO_ERROR)) {

    switch (ctx->serdes.state) {
      case JES_START:
        jes_serializer_process_start_state(ctx, compact, &json_len, &indention, NULL);
        break;
      case JES_EXPECT_KEY:
        jes_serializer_process_expect_key_state(ctx, compact, &json_len, &indention, NULL);
        break;
      case JES_EXPECT_KEY_VALUE:
        jes_serializer_process_expect_key_value_state(ctx, compact, &json_len, &indention, NULL);
        if (ctx->serdes.state == JES_HAVE_KEY_VALUE) { continue; }
        break;
      case JES_HAVE_KEY_VALUE:
        jes_serializer_process_have_key_value_state(ctx, compact, &json_len, &indention, NULL);
        break;
      case JES_EXPECT_ARRAY_VALUE:
        jes_serializer_process_expect_array_value_state(ctx, compact, &json_len, &indention, NULL);
        if (ctx->serdes.state == JES_HAVE_ARRAY_VALUE) { continue; }
        break;
      case JES_HAVE_ARRAY_VALUE:
        jes_serializer_process_have_array_value_state(ctx, compact, &json_len, &indention, NULL);
        break;
      case JES_EXPECT_EOF:
        break;
      default:
        assert(0);
        ctx->status = JES_RENDER_FAILED;
        break;
    }

    JES_LOG_NODE("\n   ", ctx->serdes.iter - ctx->node_mng.pool,
                          ctx->serdes.iter->json_tlv.type,
                          ctx->serdes.iter->json_tlv.length,
                          ctx->serdes.iter->json_tlv.value,
                          ctx->serdes.iter->parent,
                          ctx->serdes.iter->sibling,
                          ctx->serdes.iter->first_child,
                          ctx->serdes.iter->last_child,
                          "");

    jes_serializer_get_next_node(ctx);
  }

  if (ctx->serdes.state == JES_EXPECT_KEY_VALUE) {
    ctx->status = JES_RENDER_FAILED;
    json_len = 0;
  }
  else {
    ctx->serdes.iter = ctx->node_mng.root;
  }
  return json_len;
}
