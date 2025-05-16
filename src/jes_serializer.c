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

  if (!ctx->root) {
    return 0;
  }

  ctx->iter = ctx->root;
  ctx->state = JES_EXPECT_OBJECT;
  ctx->status = JES_NO_ERROR;

  do {
    JES_LOG_NODE("\n   ", ctx->iter - ctx->node_pool, ctx->iter->json_tlv.type,
                  ctx->iter->json_tlv.length, ctx->iter->json_tlv.value,
                  ctx->iter->parent, ctx->iter->sibling, ctx->iter->first_child, ctx->iter->last_child, "");

    switch (ctx->iter->json_tlv.type) {

      case JES_OBJECT:
        if ((ctx->state == JES_EXPECT_OBJECT) ||
            (ctx->state == JES_EXPECT_KEY_VALUE)  ||
            (ctx->state == JES_EXPECT_ARRAY_VALUE)) {
          ctx->state = JES_EXPECT_KEY;
        }
        else {
          ctx->status = JES_UNEXPECTED_ELEMENT;
          return 0;
        }

        json_len += sizeof("{") - 1;
        if (!compact) {
          if (PARENT_TYPE(ctx, ctx->iter) == JES_ARRAY) {
            json_len += sizeof("\n") - 1;
            json_len += indention;
          }
          indention += 2;
        }
        break;

      case JES_KEY:
        if (ctx->state == JES_EXPECT_KEY) {
          ctx->state = JES_EXPECT_KEY_VALUE;
        }
        else {
          ctx->status = JES_UNEXPECTED_ELEMENT;
          return 0;
        }

        json_len += (ctx->iter->json_tlv.length + sizeof("\"\":") - 1);
        if (!compact) {
            json_len += sizeof("\n ") - 1;
            json_len += indention;
        }
        break;

      case JES_ARRAY:
        if ((ctx->state == JES_EXPECT_KEY_VALUE) || (ctx->state == JES_EXPECT_ARRAY_VALUE)) {
          ctx->state = JES_EXPECT_ARRAY_VALUE;
        }
        else {
          ctx->status = JES_UNEXPECTED_ELEMENT;
          return 0;
        }

        json_len += sizeof("[") - 1;
        if (!compact) {
          if (PARENT_TYPE(ctx, ctx->iter) == JES_ARRAY) {
            json_len += sizeof("\n") - 1;
            json_len += indention;
          }
          indention += 2;
        }
        break;

      case JES_VALUE_STRING:
        if (ctx->state == JES_EXPECT_KEY_VALUE) {
          ctx->state = JES_HAVE_KEY_VALUE;
        }
        else if (ctx->state == JES_EXPECT_ARRAY_VALUE) {
          ctx->state = JES_HAVE_ARRAY_VALUE;
        }
        else {
          ctx->status = JES_UNEXPECTED_ELEMENT;
          return 0;
        }

        json_len += (ctx->iter->json_tlv.length + sizeof("\"\"") - 1);
        if (!compact) {
          if (PARENT_TYPE(ctx, ctx->iter) != JES_KEY) {
            json_len += sizeof("\n") - 1;
            json_len += indention;
          }
        }
        break;

      case JES_VALUE_NUMBER:
      case JES_VALUE_TRUE:
      case JES_VALUE_FALSE:
      case JES_VALUE_NULL:
        if (ctx->state == JES_EXPECT_KEY_VALUE) {
          ctx->state = JES_HAVE_KEY_VALUE;
        }
        else if (ctx->state == JES_EXPECT_ARRAY_VALUE) {
          ctx->state = JES_HAVE_ARRAY_VALUE;
        }
        else {
          ctx->status = JES_UNEXPECTED_ELEMENT;
           return 0;
        }

        json_len += ctx->iter->json_tlv.length;
        if (!compact) {
          if (PARENT_TYPE(ctx, ctx->iter) != JES_KEY) {
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
    if (HAS_FIRST_CHILD(ctx->iter)) {
      ctx->iter = GET_FIRST_CHILD(ctx, ctx->iter);
      continue;
    }

    /* Node has no child. if it's an object or array, forge a closing delimiter. */
    if (ctx->iter->json_tlv.type == JES_OBJECT) {
      /* This covers empty objects */
      json_len += sizeof("}") - 1;
      if (!compact) {
        indention -= 2;
      }
    }
    else if (ctx->iter->json_tlv.type == JES_ARRAY) {
      /* This covers empty array */
      json_len += sizeof("]") - 1;
      if (!compact) {
        indention -= 2;
      }
    }

    /* If the last child has a sibling then we've an array. Get the sibling and iterate the branch.
       Siblings must always be separated using a comma. */
    if (HAS_SIBLING(ctx->iter)) {
      if ((ctx->iter->json_tlv.type == JES_KEY) &&
          ((ctx->state != JES_EXPECT_ARRAY_VALUE) || (ctx->state != JES_HAVE_ARRAY_VALUE))) {
        ctx->status = JES_UNEXPECTED_ELEMENT;
        return 0;
      }
      json_len += sizeof(",") - 1;
      ctx->iter = GET_SIBLING(ctx, ctx->iter);
      ctx->state = JES_EXPECT_ARRAY_VALUE;
      continue;
    }

    /* Node doesn't have any children or siblings. Iterate backward to the parent. */
    if (ctx->iter->json_tlv.type == JES_KEY) {
      break;
    }

    while ((ctx->iter = GET_PARENT(ctx, ctx->iter))) {
      if (ctx->iter->json_tlv.type == JES_KEY) {
        ctx->state = JES_HAVE_KEY_VALUE;
      }
      /* If the parent is an object or array, forge a closing delimiter. */
      else if (ctx->iter->json_tlv.type == JES_OBJECT) {
        if (!compact) {
          indention -= 2;
          json_len += indention + sizeof("\n") - 1;
        }
        json_len += sizeof("}") - 1;
      }
      else if (ctx->iter->json_tlv.type == JES_ARRAY) {
        if (!compact) {
          indention -= 2;
          json_len += indention + sizeof("\n") - 1;
        }
        json_len += sizeof("]") - 1;
      }
      else if ((ctx->iter->json_tlv.type == JES_KEY) && (ctx->state != JES_HAVE_KEY_VALUE)) {
        ctx->status = JES_UNEXPECTED_ELEMENT;
        return 0;
      }

      /* If the parent has a sibling, take it and iterate the branch down.
         Siblings must always be separated using a comma. */
      if (HAS_SIBLING(ctx->iter)) {
        ctx->iter = GET_SIBLING(ctx, ctx->iter);
        json_len += sizeof(",") - 1;

        if (PARENT_TYPE(ctx, ctx->iter) == JES_OBJECT) {
          ctx->state = JES_EXPECT_KEY;
        }
        else if (PARENT_TYPE(ctx, ctx->iter) == JES_ARRAY) {
          ctx->state = JES_EXPECT_ARRAY_VALUE;
        }
        else {
          ctx->status = JES_UNEXPECTED_ELEMENT;
          return 0;
        }
        break;
      }
    }

  } while (ctx->iter && (ctx->iter != ctx->root));

  if (ctx->state == JES_EXPECT_KEY_VALUE) {
    ctx->status = JES_RENDER_FAILED;
    json_len = 0;
  }
  else {
    ctx->iter = ctx->root;
  }
  return json_len;
}

uint32_t jes_render(struct jes_context *ctx, char *buffer, uint32_t length, bool compact)
{
  char *dst = buffer;
  struct jes_node *iter = ctx->root;
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
    else if (iter->json_tlv.type == JES_VALUE_STRING) {

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
    else if ((iter->json_tlv.type == JES_VALUE_NUMBER)  ||
             (iter->json_tlv.type == JES_VALUE_TRUE)    ||
             (iter->json_tlv.type == JES_VALUE_FALSE)   ||
             (iter->json_tlv.type == JES_VALUE_NULL)) {

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
    if (HAS_FIRST_CHILD(iter)) {
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

  ctx->iter = ctx->root;
  return dst - buffer;
}
