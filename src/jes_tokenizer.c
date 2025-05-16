#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "jes.h"
#include "jes_private.h"
#include "jes_logger.h"


#define UPDATE_TOKEN(tok, type_, offset_, size_) \
  tok.type = type_; \
  tok.offset = offset_; \
  tok.length = size_;

#define IS_SPACE(c) ((c==' ') || (c=='\t') || (c=='\r') || (c=='\n') || (c=='\f'))
#define IS_NEW_LINE(c) ((c=='\r') || (c=='\n'))
#define IS_DIGIT(c) ((c >= '0') && (c <= '9'))
#define IS_ESCAPE(c) ((c=='\\') || (c=='\"') || (c=='\/') || (c=='\b') || \
                      (c=='\f') || (c=='\n') || (c=='\r') || (c=='\t') || (c == '\u'))
#define LOOK_AHEAD(ctx_) (((ctx_->offset + 1) < ctx_->json_size) ? ctx_->json_data[ctx_->offset + 1] : '\0')

#ifndef NDEBUG
  #define JES_LOG_TOKEN jes_log_token
#else
  #define JES_LOG_TOKEN(...)
#endif

static inline bool jes_get_delimiter_token(struct jes_context *ctx,
                                          char ch, struct jes_token *token)
{
  bool is_symbolic_token = true;

  switch (ch) {
    case '\0': UPDATE_TOKEN((*token), JES_TOKEN_EOF, ctx->offset, 1);             break;
    case '{':  UPDATE_TOKEN((*token), JES_TOKEN_OPENING_BRACE, ctx->offset, 1);   break;
    case '}':  UPDATE_TOKEN((*token), JES_TOKEN_CLOSING_BRACE, ctx->offset, 1);   break;
    case '[':  UPDATE_TOKEN((*token), JES_TOKEN_OPENING_BRACKET, ctx->offset, 1); break;
    case ']':  UPDATE_TOKEN((*token), JES_TOKEN_CLOSING_BRACKET, ctx->offset, 1); break;
    case ':':  UPDATE_TOKEN((*token), JES_TOKEN_COLON, ctx->offset, 1);           break;
    case ',':  UPDATE_TOKEN((*token), JES_TOKEN_COMMA, ctx->offset, 1);           break;
    default:   is_symbolic_token = false;                                         break;
  }

  return is_symbolic_token;
}

static inline bool jes_is_delimiter_token(char ch)
{
  return ((ch == '\0') || (ch == '{') || (ch == '}') || (ch == '[') || (ch == ']') || (ch == ':') || (ch == ','));
}

/* Token type is NUMBER. Try to feed it with more symbols. */
static inline bool jes_exponent_number_tokenizer(struct jes_context *ctx,
                                            char ch, struct jes_token *token)
{
  bool end_of_token = false;

  if (IS_DIGIT(ch)) {
    token->length++;
    if (!IS_DIGIT(LOOK_AHEAD(ctx))) {
      end_of_token = true;
    }
  }
  else if ((ch == '+') || (ch == '-')) {
    token->length++;
    ch = LOOK_AHEAD(ctx);
    if (!IS_DIGIT(ch)) {
      token->type = JES_TOKEN_INVALID;
      end_of_token = true;
    }
  }
  else {
    token->type = JES_TOKEN_INVALID;
    end_of_token = true;
  }

  return end_of_token;
}

/* Token type is NUMBER. Try to feed it with more symbols. */
static inline bool jes_decimal_fraction_number_tokenizer(struct jes_context *ctx,
                                            char ch, struct jes_token *token)
{
  bool end_of_token = false;

  if (IS_DIGIT(ch)) {
    token->length++;
    ch = LOOK_AHEAD(ctx);
    if (!IS_DIGIT(ch)) {
      if ((ch != 'e') && (ch != 'E')) {
        end_of_token = true;
      }
    }
  }
  else if ((ch == 'e') || (ch == 'E')) {
    token->length++;
    ctx->number_tokenizer_fn = jes_exponent_number_tokenizer;
  }
  else {
    token->type = JES_TOKEN_INVALID;
    end_of_token = true;
  }

  return end_of_token;
}

/* Token type is NUMBER. Try to feed it with more symbols. */
static inline bool jes_integer_tokenizer(struct jes_context *ctx,
                                         char ch, struct jes_token *token)
{
  bool end_of_token = false;

  if (IS_DIGIT(ch)) {
    /* Integers with leading zeros are invalid JSON numbers */
    if ((token->length == 1) && (ctx->json_data[token->offset] == '0')) {
      token->type = JES_TOKEN_INVALID;
      end_of_token = true;
    }
    token->length++;
    ch = LOOK_AHEAD(ctx);
    if (!IS_DIGIT(ch)) {
      if ((ch != '.') && (ch != 'e') && (ch != 'E')) {
        end_of_token = true;
      }
    }
  }
  else if (ch == '.') {
    token->length++;
    ctx->number_tokenizer_fn = jes_decimal_fraction_number_tokenizer;
  }
  else if ((ch == 'e') || (ch == 'E')) {
    token->length++;
    ctx->number_tokenizer_fn = jes_exponent_number_tokenizer;
  }
  else {
    token->type = JES_TOKEN_INVALID;
    end_of_token = true;
  }

  return end_of_token;
}

static inline bool jes_get_null_true_false_token(struct jes_context* ctx,
                                                 struct jes_token* token,
                                                 char* target_str,
                                                 uint16_t target_str_len)
{
  bool tokenizing_completed = false;
  token->length++;
  if (token->length == target_str_len) {
    if (strncmp(&ctx->json_data[token->offset], target_str, target_str_len) != 0) {
      token->type = JES_TOKEN_INVALID;
    }
    tokenizing_completed = true;
  }
  return tokenizing_completed;
}

jes_status jes_get_token(struct jes_context *ctx)
{
  struct jes_token token = { 0 };

  while (true) {

    if ((++ctx->offset >= ctx->json_size) || (ctx->json_data[ctx->offset] == '\0')) {
      /* End of data. If token is incomplete, mark it as invalid. */
      if (token.type) {
        ctx->status = JES_UNEXPECTED_EOF;
      }
      break;
    }

    char ch = ctx->json_data[ctx->offset];

    if (!token.type) {

      if (jes_get_delimiter_token(ctx, ch, &token)) {
        break;
      }

      if (ch == '\"') {
        /* '\"' won't be a part of token. Use offset of next symbol */
        UPDATE_TOKEN(token, JES_TOKEN_STRING, ctx->offset + 1, 0);
        continue;
      }

      if (IS_DIGIT(ch)) {
        UPDATE_TOKEN(token, JES_TOKEN_NUMBER, ctx->offset, 1);
        ctx->number_tokenizer_fn = jes_integer_tokenizer;
        /* Unlike STRINGs, there are symbols for NUMBERs to indicate the
           end of number data. To avoid consuming non-NUMBER characters, take a look ahead
           and stop the process if found of non-numeric symbols. */
        ch = LOOK_AHEAD(ctx);
        if (jes_is_delimiter_token(ch) || IS_SPACE(ch)) {
          break;
        }
        continue;
      }

      if (ch == '-') {
        if (IS_DIGIT(LOOK_AHEAD(ctx))) {
          UPDATE_TOKEN(token, JES_TOKEN_NUMBER, ctx->offset, 1);
          ctx->number_tokenizer_fn = jes_integer_tokenizer;
          continue;
        }
        token.type = JES_TOKEN_INVALID;
        break;
      }

      if (ch == 't') {
        UPDATE_TOKEN(token, JES_TOKEN_TRUE, ctx->offset, 1);
        continue;
      }

      if (ch == 'f') {
        UPDATE_TOKEN(token, JES_TOKEN_FALSE, ctx->offset, 1);
        continue;
      }

      if (ch == 'n') {
        UPDATE_TOKEN(token, JES_TOKEN_NULL, ctx->offset, 1);
        continue;
      }

      /* Skipping space symbols including: space, tab, carriage return */
      if (IS_SPACE(ch)) {
        if (IS_NEW_LINE(ch)) {
          ctx->line_number++;
        }
        continue;
      }

      UPDATE_TOKEN(token, JES_TOKEN_INVALID, ctx->offset, 1);
      break;
    }
    else if (token.type == JES_TOKEN_STRING) {
      if (ch == '\"') { /* End of STRING. '\"' symbol isn't a part of token. */
        break;
      }
      if ((ch =='\b') || (ch =='\f') || (ch =='\n') || (ch =='\r') || (ch =='\t')) {
        ctx->status = JES_UNEXPECTED_SYMBOL;
        break;
      }
      token.length++;
      continue;
    }
    else if (token.type == JES_TOKEN_NUMBER) {
      if (ctx->number_tokenizer_fn(ctx, ch, &token)) {
        /* There are no more symbols to consume as a number. Deliver the token. */
        break;
      }
      continue;
    }
    else if (token.type == JES_TOKEN_TRUE) {
      if (jes_get_null_true_false_token(ctx, &token, "true", sizeof("true") - 1)) {
        break;
      }
      continue;
    }
    else if (token.type == JES_TOKEN_FALSE) {
      if (jes_get_null_true_false_token(ctx, &token, "false", sizeof("false") - 1)) {
        break;
      }
      continue;
    }
    else if (token.type == JES_TOKEN_NULL) {
      if (jes_get_null_true_false_token(ctx, &token, "null", sizeof("null") - 1)) {
        break;
      }
      continue;
    }
    token.type = JES_TOKEN_INVALID;
    break;
  }

  JES_LOG_TOKEN(token.type, token.offset, token.length, &ctx->json_data[token.offset]);

  ctx->token = token;
  return ctx->status;
}

void jes_init_tokenizer(struct jes_context* ctx)
{
  ctx->offset = (uint32_t)-1;
  ctx->index = 0;
  ctx->number_tokenizer_fn = jes_integer_tokenizer;
}