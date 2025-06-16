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
  "START",
  "EXPECT_KEY",
  "EXPECT_COLON",
  "EXPECT_VALUE",
  "HAVE_VALUE",
  "EXPECT_KEY_VALUE",
  "HAVE_KEY_VALUE",
  "EXPECT_ARRAY_VALUE",
  "HAVE_ARRAY_VALUE",
  "EXPECT_EOF",
  "END",
};

void jes_log_token(uint16_t token_type,
                   size_t line_numebr,
                   size_t token_pos,
                   size_t token_len,
                   const char *token_value)
{
  printf("\nJES.Token: [Ln: %4d, Pos: %5d, Len: %3d] %-16s \"%.*s\"",
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

void jes_log_state(const char *pre_msg, uint32_t state, const char *post_msg)
{
  printf("%s<%s>(%d)%s", pre_msg, jes_state_str[state], state, post_msg);
}

char* jes_stringify_status(struct jes_context *ctx, char *msg, size_t msg_len)
{
  *msg = '\0';
  #if 0
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
                jes_token_type_str[ctx->tokenizer.token.type],
                ctx->tokenizer.line_number,
                ctx->tokenizer.cursor - ctx->json_data,
                ctx->tokenizer.token.length,
                ctx->tokenizer.cursor,
                jes_state_str[ctx->serdes.state],
                ctx->serdes.iter != NULL ? jes_node_type_str[ctx->serdes.iter->json_tlv.type] : "");
      break;

    case JES_OUT_OF_MEMORY:
#ifdef JES_ENABLE_FAST_KEY_SEARCH
      snprintf( msg, msg_len, "%s(#%d) - element capacity: %d, hash table capacity: %d, node count: %d",
                jes_status_str[ctx->status],
                ctx->status,
                ctx->node_mng.capacity,
                ctx->hash_table.capacity,
                ctx->node_mng.node_count
                );
#else
      snprintf( msg, msg_len, "%s(#%d) - element capacity: %lu, node count: %lu",
                jes_status_str[ctx->status],
                ctx->status,
                ctx->node_mng.capacity,
                ctx->node_mng.node_count
                );
#endif
      break;
    case JES_UNEXPECTED_TOKEN:
      snprintf( msg, msg_len,
                "%s(#%d): Token<%s> @[line:%d, pos:%d] (\"%.*s\") state:<%s> after <%s> element",
                jes_status_str[ctx->status],
                ctx->status,
                jes_token_type_str[ctx->tokenizer.token.type],
                ctx->tokenizer.line_number,
                ctx->tokenizer.cursor - ctx->json_data,
                ctx->tokenizer.token.length,
                ctx->tokenizer.cursor,
                jes_state_str[ctx->serdes.state],
                ctx->serdes.iter != NULL ? jes_node_type_str[ctx->serdes.iter->json_tlv.type] : "");
      break;

    case JES_PARSING_FAILED:
      snprintf( msg, msg_len,
                "%s(#%d): Token<%s> @[line:%d, pos:%d] (\"%.*s\")",
                jes_status_str[ctx->status],
                ctx->status,
                jes_token_type_str[ctx->tokenizer.token.type],
                ctx->tokenizer.line_number,
                ctx->tokenizer.cursor - ctx->json_data,
                ctx->tokenizer.token.length,
                ctx->tokenizer.cursor);
      break;
    case JES_UNEXPECTED_ELEMENT:
      snprintf( msg, msg_len, "%s(#%d) - %s: \"%.*s\" @state: %s",
                jes_status_str[ctx->status],
                ctx->status,
                jes_node_type_str[ctx->serdes.iter->json_tlv.type],
                ctx->serdes.iter->json_tlv.length,
                ctx->serdes.iter->json_tlv.value,
                jes_state_str[ctx->serdes.state]);
      break;
    case JES_RENDER_FAILED:
      snprintf( msg, msg_len, "%s(#%d) - %s: \"%.*s\" @state: %s",
                jes_status_str[ctx->status],
                ctx->status,
                jes_node_type_str[ctx->serdes.iter->json_tlv.type],
                ctx->serdes.iter->json_tlv.length,
                ctx->serdes.iter->json_tlv.value,
                jes_state_str[ctx->serdes.state]);
      break;
    case JES_UNEXPECTED_EOF:
      snprintf( msg, msg_len,
                "%s(#%d): Token<%s> @[line:%d, pos:%d] (\"%.*s\") state:<%s> after <%s> element",
                jes_status_str[ctx->status],
                ctx->status,
                jes_token_type_str[ctx->tokenizer.token.type],
                ctx->tokenizer.line_number,
                ctx->tokenizer.cursor - ctx->json_data,
                ctx->tokenizer.token.length,
                ctx->tokenizer.cursor,
                jes_state_str[ctx->serdes.state],
                ctx->serdes.iter != NULL ? jes_node_type_str[ctx->serdes.iter->json_tlv.type] : "");
      break;
#if 0
    case JES_DUPLICATE_KEY:
      snprintf( msg, msg_len,
                "%s(#%d): Found duplicate key: \"%.*s\" inside \"%.*s\"",
                jes_status_str[ctx->status],
                ctx->status,
                ctx->serdes.iter->json_tlv.length,
                ctx->serdes.iter->json_tlv.value,
                ctx->serdes.iter->json_tlv.length,
                ctx->serdes.iter->json_tlv.value);
      break;
#endif
    default:
      snprintf(msg, msg_len, "%s(#%d)", jes_status_str[ctx->status], ctx->status);
      break;
  }
#endif
snprintf(msg, msg_len, "%s(#%d)", jes_status_str[ctx->status], ctx->status);
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
