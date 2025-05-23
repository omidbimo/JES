#include <stdio.h>
#include "jes.h"
#include "jes_private.h"
#include "jes_logger.h"

#define JES_HELPER_STR_LENGTH 20

static char jes_status_str[][JES_HELPER_STR_LENGTH] = {
  "NO_ERROR",
  "PARSING_FAILED",
  "RENDER_FAILED",
  "OUT_OF_MEMORY",
  "UNEXPECTED_SYMBOL",
  "UNEXPECTED_TOKEN",
  "UNEXPECTED_ELEMENT",
  "UNEXPECTED_EOF",
  "INVALID_PARAMETER",
  "ELEMENT_NOT_FOUND",
  "INVALID_CONTEXT",
  "BROKEN_TREE",
  "DUPLICATE_KEY",
};

static char jes_token_type_str[][JES_HELPER_STR_LENGTH] = {
  "EOF",
  "OPENING_BRACE",
  "CLOSING_BRACE",
  "OPENING_BRACKET",
  "CLOSING_BRACKET",
  "COLON",
  "COMMA",
  "STRING",
  "NUMBER",
  "TRUE",
  "FALSE",
  "NULL",
  "ESC",
  "INVALID_TOKEN",
};

static char jes_node_type_str[][JES_HELPER_STR_LENGTH] = {
  "NONE",
  "OBJECT",
  "KEY",
  "ARRAY",
  "STRING",
  "NUMBER",
  "TRUE",
  "FALSE",
  "NULL",
};

static char jes_state_str[][JES_HELPER_STR_LENGTH] = {
  "EXPECT_OBJECT",
  "EXPECT_KEY",
  "EXPECT_COLON",
  "EXPECT_KEY_VALUE",
  "HAVE_KEY_VALUE",
  "EXPECT_ARRAY_VALUE",
  "HAVE_ARRAY_VALUE",
  "EXPECT_EOF",
};

void jes_log_token(uint16_t token_type,
                   size_t line_numebr,
                   size_t token_pos,
                   size_t token_len,
                   const char *token_value)
{
  printf("\n JES.Token: [Ln: %4d, Pos: %5d, Len: %3d] %-16s \"%.*s\"",
          line_numebr, token_pos, token_len, jes_token_type_str[token_type],
          token_len, token_value);
}

void jes_log_node(const char *pre_msg,
                  int32_t node_id,
                  uint32_t node_type,
                  uint32_t node_length,
                  const char *node_value,
                  int32_t parent_id,
                  int32_t right_id,
                  int32_t first_child_id,
                  int32_t last_child_id,
                  const char *post_msg)
{
  printf("%sJES.Node: [%d] \"%.*s\" <%s>,    parent:[%d], right:[%d], first_child:[%d], last_child:[%d]%s",
    pre_msg, node_id, node_length, node_value, jes_node_type_str[node_type],
    parent_id == JES_INVALID_INDEX ? -1 : parent_id,
    right_id == JES_INVALID_INDEX ? -1 : right_id,
    first_child_id == JES_INVALID_INDEX ? -1 : first_child_id,
    last_child_id == JES_INVALID_INDEX ? -1 : last_child_id,
    post_msg);
}

char* jes_stringify_status(struct jes_context *ctx, char *msg, size_t msg_len)
{
  if ((ctx == NULL) || (msg == NULL) || (msg_len == 0)) {
    return "";
  }

  /* TODO: provide more status */
  switch (ctx->status) {
    case JES_NO_ERROR:
      snprintf(msg, msg_len, "%s(#%d)", jes_status_str[ctx->status], ctx->status);
      break;
    case JES_UNEXPECTED_SYMBOL:
      snprintf( msg, msg_len,
                "%s(#%d): Token<%s> @[line:%d, pos:%d] (\"%.*s\") state:<%s> after <%s> element",
                jes_status_str[ctx->status],
                ctx->status,
                jes_token_type_str[ctx->token.type],
                ctx->line_number,
                ctx->tokenizer_pos - ctx->json_data,
                ctx->token.length,
                ctx->tokenizer_pos,
                jes_state_str[ctx->state],
                ctx->iter != NULL ? jes_node_type_str[ctx->iter->json_tlv.type] : "");
      break;
    case JES_UNEXPECTED_TOKEN:
      snprintf( msg, msg_len,
                "%s(#%d): Token<%s> @[line:%d, pos:%d] (\"%.*s\") state:<%s> after <%s> element",
                jes_status_str[ctx->status],
                ctx->status,
                jes_token_type_str[ctx->token.type],
                ctx->line_number,
                ctx->tokenizer_pos - ctx->json_data,
                ctx->token.length,
                ctx->tokenizer_pos,
                jes_state_str[ctx->state],
                ctx->iter != NULL ? jes_node_type_str[ctx->iter->json_tlv.type] : "");
      break;

    case JES_PARSING_FAILED:
      snprintf( msg, msg_len,
                "%s(#%d): Token<%s> @[line:%d, pos:%d] (\"%.*s\")",
                jes_status_str[ctx->status],
                ctx->status,
                jes_token_type_str[ctx->token.type],
                ctx->line_number,
                ctx->tokenizer_pos - ctx->json_data,
                ctx->token.length,
                ctx->tokenizer_pos);
      break;
    case JES_UNEXPECTED_ELEMENT:
      snprintf( msg, msg_len, "%s(#%d) - %s: \"%.*s\" @state: %s",
                jes_status_str[ctx->status],
                ctx->status,
                jes_node_type_str[ctx->iter->json_tlv.type],
                ctx->iter->json_tlv.length,
                ctx->iter->json_tlv.value,
                jes_state_str[ctx->state]);
      break;
    case JES_RENDER_FAILED:
      snprintf( msg, msg_len, "%s(#%d) - %s: \"%.*s\" @state: %s",
                jes_status_str[ctx->status],
                ctx->status,
                jes_node_type_str[ctx->iter->json_tlv.type],
                ctx->iter->json_tlv.length,
                ctx->iter->json_tlv.value,
                jes_state_str[ctx->state]);
      break;
    case JES_UNEXPECTED_EOF:
      snprintf( msg, msg_len,
                "%s(#%d): Token<%s> @[line:%d, pos:%d] (\"%.*s\") state:<%s> after <%s> element",
                jes_status_str[ctx->status],
                ctx->status,
                jes_token_type_str[ctx->token.type],
                ctx->line_number,
                ctx->tokenizer_pos - ctx->json_data,
                ctx->token.length,
                ctx->tokenizer_pos,
                jes_state_str[ctx->state],
                ctx->iter != NULL ? jes_node_type_str[ctx->iter->json_tlv.type] : "");
      break;
    default:
      snprintf(msg, msg_len, "%s(#%d)", jes_status_str[ctx->status], ctx->status);
      break;
  }
  return msg;
}

char* jes_stringify_element(struct jes_element *element, char *msg, size_t msg_len)
{
  if ((element == NULL) || (msg == NULL) || (msg_len == 0)) {
    return "";
  }
  snprintf(msg, msg_len, "%s(%d)", jes_node_type_str[element->type], element->type);
  return msg;
}
