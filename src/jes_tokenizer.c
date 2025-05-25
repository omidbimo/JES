#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "jes.h"
#include "jes_private.h"
#include "jes_logger.h"

#define UPDATE_TOKEN(tok, type_, length_, value_) \
  tok.type = type_; \
  tok.length = length_; \
  tok.value = value_;

#define IS_SPACE(c) ((c==' ') || (c=='\t') || (c=='\r') || (c=='\n') || (c=='\f'))
#define IS_DIGIT(c) ((c >= '0') && (c <= '9'))
#define IS_ESCAPE(c) ((c=='\\') || (c=='\"') || (c=='\/') || (c=='\b') || \
                      (c=='\f') || (c=='\n') || (c=='\r') || (c=='\t') || (c == '\u'))

#define JES_TOKENIZER_LOOK_AHEAD(current_ptr, end_ptr) (((current_ptr + 1) < (end_ptr)) ? *(current_ptr + 1) : '\0')
#define JES_TOKENIZER_GET_CHAR(current_ptr, end_ptr)  (((current_ptr) < (end_ptr)) ? current_ptr++ : NULL)

#ifndef NDEBUG
  #define JES_LOG_TOKEN jes_log_token
#else
  #define JES_LOG_TOKEN(...)
#endif

static inline bool jes_tokenizer_set_delimiter_token(struct jes_token *token,
                                                     const char* char_ptr)
{
  bool is_symbolic_token = true;

  switch (*char_ptr) {
    case '\0': UPDATE_TOKEN((*token), JES_TOKEN_EOF, 1, char_ptr);             break;
    case '{':  UPDATE_TOKEN((*token), JES_TOKEN_OPENING_BRACE, 1, char_ptr);   break;
    case '}':  UPDATE_TOKEN((*token), JES_TOKEN_CLOSING_BRACE, 1, char_ptr);   break;
    case '[':  UPDATE_TOKEN((*token), JES_TOKEN_OPENING_BRACKET, 1, char_ptr); break;
    case ']':  UPDATE_TOKEN((*token), JES_TOKEN_CLOSING_BRACKET, 1, char_ptr); break;
    case ':':  UPDATE_TOKEN((*token), JES_TOKEN_COLON, 1, char_ptr);           break;
    case ',':  UPDATE_TOKEN((*token), JES_TOKEN_COMMA, 1, char_ptr);           break;
    default:   is_symbolic_token = false;                                      break;
  }

  return is_symbolic_token;
}

static inline bool jes_is_delimiter_token(char ch)
{
  return ((ch == '\0') || (ch == '{') || (ch == '}') || (ch == '[') || (ch == ']') || (ch == ':') || (ch == ','));
}

/* Token type is NUMBER. Try to feed it with more symbols. */
static inline bool jes_tokenizer_process_exponent_token(struct jes_context* ctx,
                                                        struct jes_token* token,
                                                        const char* current,
                                                        const char* end)
{
  bool end_of_token = false;
  char ch = *current;

  if (IS_DIGIT(ch)) {
    token->length++;
    if (!IS_DIGIT(JES_TOKENIZER_LOOK_AHEAD(current, end))) {
      end_of_token = true;
    }
  }
  else if ((ch == '+') || (ch == '-')) {
    token->length++;
    ch = JES_TOKENIZER_LOOK_AHEAD(current, end);
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
static inline bool jes_tokenizer_process_decimal_fraction_token(struct jes_context* ctx,
                                                                struct jes_token* token,
                                                                const char* current,
                                                                const char* end)
{
  bool end_of_token = false;
  char ch = *current;

  if (IS_DIGIT(ch)) {
    token->length++;
    ch = JES_TOKENIZER_LOOK_AHEAD(current, end);
    if (!IS_DIGIT(ch)) {
      if ((ch != 'e') && (ch != 'E')) {
        end_of_token = true;
      }
    }
  }
  else if ((ch == 'e') || (ch == 'E')) {
    token->length++;
    ctx->typed_tokenizer_fn = jes_tokenizer_process_exponent_token;
  }
  else {
    token->type = JES_TOKEN_INVALID;
    end_of_token = true;
  }

  return end_of_token;
}

/* Token type is NUMBER. Try to feed it with more symbols. */
static inline bool jes_tokenizer_process_integer_token(struct jes_context* ctx,
                                                       struct jes_token* token,
                                                       const char* current,
                                                       const char* end)
{
  bool end_of_token = false;
  char ch = *current;

  if (IS_DIGIT(ch)) {
    /* Integers with leading zeros are invalid JSON numbers */
    if ((token->length == 1) && (*token->value == '0')) {
      token->type = JES_TOKEN_INVALID;
      end_of_token = true;
    }
    token->length++;
    ch = JES_TOKENIZER_LOOK_AHEAD(current, end);
    if (!IS_DIGIT(ch)) {
      if ((ch != '.') && (ch != 'e') && (ch != 'E')) {
        end_of_token = true;
      }
    }
  }
  else if (ch == '.') {
    token->length++;
    ctx->typed_tokenizer_fn = jes_tokenizer_process_decimal_fraction_token;
  }
  else if ((ch == 'e') || (ch == 'E')) {
    token->length++;
    ctx->typed_tokenizer_fn = jes_tokenizer_process_exponent_token;
  }
  else {
    token->type = JES_TOKEN_INVALID;
    end_of_token = true;
  }

  return end_of_token;
}

static inline bool jes_tokenizer_process_string_token(struct jes_context* ctx,
                                                      struct jes_token* token,
                                                      const char* current,
                                                      const char* end)
{
  bool tokenizing_completed = false;
  char ch = *current;

  if (ch == '\"') { /* End of STRING. '\"' symbol isn't a part of token. */
    tokenizing_completed = true;
  }
  else if ((ch =='\b') || (ch =='\f') || (ch =='\n') || (ch =='\r') ||
           (ch =='\t') || (ch =='\\')) {
    ctx->status = JES_UNEXPECTED_SYMBOL;
    tokenizing_completed = true;
  }
  else {
    token->length++;
  }

  return tokenizing_completed;
}


static inline bool jes_tokenizer_process_literal_token(struct jes_context* ctx,
                                                       struct jes_token* token,
                                                       char* str,
                                                       uint16_t str_len)
{
  bool tokenizing_completed = false;
  token->length++;
  if (token->length == str_len) {
    if (strncmp(token->value, str, str_len) != 0) {
      token->type = JES_TOKEN_INVALID;
    }
    tokenizing_completed = true;
  }
  return tokenizing_completed;
}

static inline bool jes_tokenizer_get_null_true_false(struct jes_context* ctx,
                                                     struct jes_token* token,
                                                     const char* current,
                                                     const char* end)
{
  bool tokenizing_completed = true;

  switch (token->type) {
    case JES_TOKEN_NULL:
      tokenizing_completed = jes_tokenizer_process_literal_token(ctx, token, "null", sizeof("null") - 1);
      break;
    case JES_TOKEN_TRUE:
      tokenizing_completed = jes_tokenizer_process_literal_token(ctx, token, "true", sizeof("true") - 1);
      break;
    case JES_TOKEN_FALSE:
      tokenizing_completed = jes_tokenizer_process_literal_token(ctx, token, "false", sizeof("false") - 1);
      break;
    default:
      assert(0);
      break;
  }
  return tokenizing_completed;
}

jes_status jes_tokenizer_get_token(struct jes_context *ctx)
{
  struct jes_token token = { 0 };
  const char* char_ptr;

  while (true) {

    char_ptr = JES_TOKENIZER_GET_CHAR(ctx->cursor, ctx->json_data + ctx->json_size);

    if (!token.type) {

      if (char_ptr == NULL) {
        UPDATE_TOKEN(token, JES_TOKEN_EOF, 0, char_ptr);
        break;
      }

      if (jes_tokenizer_set_delimiter_token(&token, char_ptr)) {
        break;
      }

      if (*char_ptr == '\"') {
        UPDATE_TOKEN(token, JES_TOKEN_STRING, 0, char_ptr);
        ctx->typed_tokenizer_fn = jes_tokenizer_process_string_token;
        /* '\"' won't be a part of token. Use next symbol if available */
        if (JES_TOKENIZER_LOOK_AHEAD(char_ptr, ctx->json_data + ctx->json_size) != '\0') {
          token.value++;
        }
        continue;
      }

      if (IS_DIGIT(*char_ptr)) {
        char ch = JES_TOKENIZER_LOOK_AHEAD(char_ptr, ctx->json_data + ctx->json_size);
        UPDATE_TOKEN(token, JES_TOKEN_NUMBER, 1, char_ptr);
        ctx->typed_tokenizer_fn = jes_tokenizer_process_integer_token;
        /* Unlike STRINGs, there are symbols for NUMBERs to indicate the
           end of number data. To avoid consuming non-NUMBER characters, take a look ahead
           and stop the process if found of non-numeric symbols. */
        if (jes_is_delimiter_token(ch) || IS_SPACE(ch)) { break; }
        continue;
      }

      if (*char_ptr == '-') {
        if (IS_DIGIT(JES_TOKENIZER_LOOK_AHEAD(char_ptr, ctx->json_data + ctx->json_size))) {
          UPDATE_TOKEN(token, JES_TOKEN_NUMBER, 1, char_ptr);
          ctx->typed_tokenizer_fn = jes_tokenizer_process_integer_token;
          continue;
        }
        token.type = JES_TOKEN_INVALID;
        break;
      }

      if (*char_ptr == 't') {
        UPDATE_TOKEN(token, JES_TOKEN_TRUE, 1, char_ptr);
        ctx->typed_tokenizer_fn = jes_tokenizer_get_null_true_false;
        continue;
      }

      if (*char_ptr == 'f') {
        UPDATE_TOKEN(token, JES_TOKEN_FALSE, 1, char_ptr);
        ctx->typed_tokenizer_fn = jes_tokenizer_get_null_true_false;
        continue;
      }

      if (*char_ptr == 'n') {
        UPDATE_TOKEN(token, JES_TOKEN_NULL, 1, char_ptr);
        ctx->typed_tokenizer_fn = jes_tokenizer_get_null_true_false;
        continue;
      }

      /* Skipping space symbols including: space, tab, carriage return */
      if (IS_SPACE(*char_ptr)) {
        /* Handling different newline conventions \r, \n or \r\n */
        if (*char_ptr == '\n') {
          /* Unix-style LF */
          ctx->line_number++;
        }
        else if (*char_ptr == '\r') {
          /* Could be Mac-style CR or first part of Windows CRLF */
          char ch = JES_TOKENIZER_LOOK_AHEAD(char_ptr, ctx->json_data + ctx->json_size);
          if (ch != '\n') {
            /* Mac-style standalone CR */
            ctx->line_number++;
          }
          /* If next is \n, we'll handle the line increment when we process the next character */
        }

        continue;
      }

      UPDATE_TOKEN(token, JES_TOKEN_INVALID, 1, char_ptr);
      break;
    }

    /* A new token is in process and EOF is not expected at this point. */
    if (char_ptr == NULL) {
      ctx->status = JES_UNEXPECTED_EOF;
      break;
    }

    /* Further process the current token based on its type. */
    if (ctx->typed_tokenizer_fn(ctx, &token, char_ptr, ctx->json_data + ctx->json_size) == true) {
      break;
    }
  }

  JES_LOG_TOKEN(token.type, ctx->line_number, ctx->cursor - ctx->json_data, token.length, token.value);

  ctx->token = token;
  return ctx->status;
}

bool jes_tokenizer_validate_number(struct jes_context* ctx, const char* value, size_t length)
{
  struct jes_token token = { 0 };
  const char* end = NULL;
  const char* char_ptr = NULL;
  bool is_valid = false;

  assert(ctx != NULL);
  assert(value != NULL);

  end = value + length;
  char_ptr = JES_TOKENIZER_GET_CHAR(value, end);

  if (IS_DIGIT(*char_ptr) || (*char_ptr == '-')) {
    UPDATE_TOKEN(token, JES_TOKEN_NUMBER, 1, char_ptr);
    ctx->typed_tokenizer_fn = jes_tokenizer_process_integer_token;

    while (char_ptr = JES_TOKENIZER_GET_CHAR(value, end)) {
      if (ctx->typed_tokenizer_fn(ctx, &token, char_ptr, end)) {
        /* There are no more symbols to consume as a number. */
        if ((token.length == length) && (ctx->status == JES_NO_ERROR)) {
          is_valid = true;
        }
        break;
      }
      continue;
    }
  }

  return is_valid;
}

bool jes_tokenizer_validate_string(struct jes_context* ctx, const char* value, size_t length)
{
  struct jes_token token = { 0 };
  const char* end = NULL;
  const char* char_ptr = NULL;
  bool is_valid = false;

  assert(ctx != NULL);
  assert(value != NULL);

  if (length == 0) {
    /* We consider a string with zero length a valid string */
    return true;
  }

  end = value + length;

  while (char_ptr = JES_TOKENIZER_GET_CHAR(value, end)) {
    (void)jes_tokenizer_process_string_token(ctx, &token, char_ptr, end);
    /* The function will not return true until finding a double quoute symbol
       which we don't have in this case. Ignore the return value and rely on the
       token size instead. */
    if ((token.length == length) && (ctx->status == JES_NO_ERROR)) {
      is_valid = true;
      break;
    }
  }

  return is_valid;
}

void jes_tokenizer_init(struct jes_context* ctx)
{
  ctx->typed_tokenizer_fn = NULL;
  ctx->line_number = 1;
}