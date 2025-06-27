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
#define IS_HEXADECIMAL(c) (
#define JES_TOKENIZER_LOOK_AHEAD(current_ptr, end_ptr) (((current_ptr + 1) < (end_ptr)) ? (current_ptr + 1) : NULL)
#define JES_TOKENIZER_GET_CHAR(current_ptr, end_ptr)  (((current_ptr) < (end_ptr)) ? current_ptr++ : NULL)

#ifndef NDEBUG
  #define JES_LOG_TOKEN jes_log_token
#else
  #define JES_LOG_TOKEN(...)
#endif


/**
 * RFC8259, RFC6901, RFC6902
 * JSON grammar in Backus-Naur Form based on the ECMA-404 standard

<json>         ::= <element>

<element>      ::= <ws> <value> <ws>

<value>        ::= <object>
                | <array>
                | <string>
                | <number>
                | "true"
                | "false"
                | "null"

<object>       ::= "{" <ws> "}"
                | "{" <members> "}"

<members>      ::= <member>
                | <member> <ws> "," <members>

<member>       ::= <ws> <string> <ws> ":" <element>

<array>        ::= "[" <ws> "]"
                | "[" <elements> "]"

<elements>     ::= <element>
                | <element> <ws> "," <elements>

<string>       ::= "\"" <characters> "\""

<characters>   ::= ε
                | <character> <characters>

<character>    ::= any Unicode character except " or \ or control characters
                | "\\" <escape>

<escape>       ::= "\""
                | "/"
                | "b"
                | "f"
                | "n"
                | "r"
                | "t"
                | "u" <hex> <hex> <hex> <hex>

<hex>          ::= "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7"
                | "8" | "9" | "A" | "B" | "C" | "D" | "E" | "F"
                | "a" | "b" | "c" | "d" | "e" | "f"

<number>       ::= <int> <frac>? <exp>?

<int>          ::= "-"? <digit>
                | "-"? <onenine> <digits>

<digits>       ::= <digit>
                | <digit> <digits>

<digit>        ::= "0" | <onenine>

<onenine>      ::= "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9"

<frac>         ::= "." <digits>

<exp>          ::= ("e" | "E") ("+" | "-")? <digits>

<ws>           ::= "" | <whitespace> <ws>

<whitespace>   ::= " " | "\n" | "\r" | "\t"

*/

static const char* jes_tokenizer_process_string_token(struct jes_context*,
                                                      struct jes_token*,
                                                      const char*,
                                                      const char*);

static inline bool jes_tokenizer_set_delimiter_token(struct jes_token* token,
                                                     const char* symbol)
{
  bool is_symbolic_token = true;

  switch (*symbol) {
    case '\0': UPDATE_TOKEN((*token), JES_TOKEN_EOF, 1, symbol);             break;
    case '{':  UPDATE_TOKEN((*token), JES_TOKEN_OPENING_BRACE, 1, symbol);   break;
    case '}':  UPDATE_TOKEN((*token), JES_TOKEN_CLOSING_BRACE, 1, symbol);   break;
    case '[':  UPDATE_TOKEN((*token), JES_TOKEN_OPENING_BRACKET, 1, symbol); break;
    case ']':  UPDATE_TOKEN((*token), JES_TOKEN_CLOSING_BRACKET, 1, symbol); break;
    case ':':  UPDATE_TOKEN((*token), JES_TOKEN_COLON, 1, symbol);           break;
    case ',':  UPDATE_TOKEN((*token), JES_TOKEN_COMMA, 1, symbol);           break;
    default:   is_symbolic_token = false;                                      break;
  }

  return is_symbolic_token;
}

static inline bool jes_is_delimiter_token(char ch)
{
  return ((ch == '\0') || (ch == '{') || (ch == '}') || (ch == '[') || (ch == ']') || (ch == ':') || (ch == ','));
}

/* Token type is NUMBER. Try to feed it with more symbols. */
static const char* jes_tokenizer_process_exponent_token(struct jes_context* ctx,
                                                        struct jes_token* token,
                                                        const char* current_pos,
                                                        const char* end)
{
  const char* ch;

  while ((ctx->status == JES_NO_ERROR) && ((ch = JES_TOKENIZER_GET_CHAR(current_pos, end)) != NULL)) {

    if (IS_DIGIT(*ch)) {
      token->length++;
      /* Numbers do not have a terminator symbol. Need to look ahead to decide the end of a number */
      ch = JES_TOKENIZER_LOOK_AHEAD(ch, end);
      if ((ch == NULL) || !IS_DIGIT(*ch)) {
        break;
      }
    }
    else if ((*ch == '+') || (*ch == '-')) {
      token->length++;
      ch = JES_TOKENIZER_LOOK_AHEAD(ch, end);
      if (!IS_DIGIT(*ch)) {
        ctx->status = JES_INVALID_NUMBER;
        break;
      }
    }
    else {
      ctx->status = JES_INVALID_NUMBER;
      break;
    }
  }
  return current_pos;
}

/* Token type is NUMBER. Try to feed it with more symbols. */
static const char* jes_tokenizer_process_decimal_fraction_token(struct jes_context* ctx,
                                                          struct jes_token* token,
                                                          const char* current_pos,
                                                          const char* end)
{
  const char* ch;

  while ((ctx->status == JES_NO_ERROR) && ((ch = JES_TOKENIZER_GET_CHAR(current_pos, end)) != NULL)) {

    if (IS_DIGIT(*ch)) {
      token->length++;
    }
    else if ((*ch == 'e') || (*ch == 'E')) {
      token->length++;
      current_pos = jes_tokenizer_process_exponent_token(ctx, token, current_pos, end);
      break;
    }
    else {
      ctx->status = JES_UNEXPECTED_SYMBOL;
      break;
    }

    /* Numbers do not have a terminator symbol. Need to look ahead to decide the end of a number */
    ch = JES_TOKENIZER_LOOK_AHEAD(ch, end);
    if ((ch == NULL) ||
        (!IS_DIGIT(*ch) && (*ch != 'e') && (*ch != 'E'))) {
      break;
    }
  }

  return current_pos;
}

/* Token type is NUMBER. Try to feed it with more symbols. */
static inline const char* jes_tokenizer_process_integer_token(struct jes_context* ctx,
                                                              struct jes_token* token,
                                                              const char* current_pos,
                                                              const char* end)
{
  const char* ch;

  while ((ctx->status == JES_NO_ERROR) && ((ch = JES_TOKENIZER_GET_CHAR(current_pos, end)) != NULL)) {

    if (!IS_DIGIT(*ch)) {
      if ((token->length == 1) && (*token->value == '-')) {
        ctx->status = JES_UNEXPECTED_SYMBOL;
      }
      else if (*ch == '.') {
        token->length++;
        current_pos = jes_tokenizer_process_decimal_fraction_token(ctx, token, current_pos, end);
      }
      else if ((*ch == 'e') || (*ch == 'E')) {
        token->length++;
        current_pos = jes_tokenizer_process_exponent_token(ctx, token, current_pos, end);
      }
      else {
        assert(0);
      }
      break;
    }
    else { /* It's a digit */
      /* Integers with leading zeros are invalid JSON numbers */
      if ((token->length == 1) && (*token->value == '0')) {
        ctx->status = JES_INVALID_NUMBER;
        break;
      }
      token->length++;
    }

    /* Numbers do not have a terminator symbol. Need to look ahead to decide the end of a number */
    ch = JES_TOKENIZER_LOOK_AHEAD(ch, end);
    if ((ch == NULL) ||
        (!IS_DIGIT(*ch) && (*ch != '.') && (*ch != 'e') && (*ch != 'E'))) {
      break;
    }
  }
  return current_pos;
}

/* Convert a stream 4 hexadecimal ascii encoded numbers into a 16-bit integer */
static inline bool jes_tokenizer_utf_16_str2hex(const char* xxyy, uint16_t* result)
{
  size_t index;
  bool is_valid_utf_16 = true;
  *result  = 0;

  for (index = 0; index < sizeof("xxyy") - 1; index++) {
    *result <<= 4; /* Each character represents a nibble. 4-bit left shift */
    if (xxyy[index] >= '0' && xxyy[index] <= '9') { *result += xxyy[index] - '0'; }
    else if (xxyy[index] >= 'A' && xxyy[index] <= 'F') { *result += xxyy[index] - 'A' + 0xA; }
    else if (xxyy[index] >= 'a' && xxyy[index] <= 'f') { *result += xxyy[index] - 'a' + 0xA; }
    else {
      is_valid_utf_16 = false;
      break;
    }
  }

  return is_valid_utf_16;
}

static const char* jes_tokenizer_process_escaped_utf_16_token(struct jes_context* ctx,
                                                        struct jes_token* token,
                                                        const char* current_pos,
                                                        const char* end)
{
  const char* ch;
  const char* escaped_utf_16;
  size_t escaped_utf_16_length;

  escaped_utf_16 = current_pos;
  escaped_utf_16_length = 0;

  while (ctx->status == JES_NO_ERROR) {

    ch = JES_TOKENIZER_GET_CHAR(current_pos, end);
    if ((*ch == '\"') || (*ch =='\b') || (*ch =='\f') || (*ch =='\n') ||
        (*ch =='\r') || (*ch =='\t')) {
      ctx->status = JES_UNEXPECTED_SYMBOL;
      break;
    }
    else {
      token->length++;
      escaped_utf_16_length++;
    }
    /*
      Validate utf_16 if token has the right size.
      Valid unicode code points
      Basic Multilingual Plane: U+0000 - U+D7FF and U+E000 - U+FFFF
      Surrogate pair: High Surrogate: U+D800 - U+DBFF
                      Low Surrogate:  U+DC00 - U+DFFF
    */
    if (escaped_utf_16_length == (sizeof("\\uXXXX") - 1)) {
      uint16_t utf_16_int;

      if ((escaped_utf_16[0] == '\\') && (escaped_utf_16[1] == 'u')) {
        if (jes_tokenizer_utf_16_str2hex(&escaped_utf_16[2], &utf_16_int)) {
          if (utf_16_int < 0xD800 || utf_16_int < 0xDBFF) {
            /* valid 16-bit BMP code point. Switch to normal string tokenizer. */
            ctx->status = JES_NO_ERROR;
            break;
          }
          else {
            /* NOP. The unicode value is in the range of Surrogate pair. Consume more
              bytes and validate the pair later. */
          }
        }
      }
      ctx->status = JES_INVALID_UNICODE;
    }
    else if (escaped_utf_16_length == (sizeof("\\uXXXX\\uXXXX") - 1)) {
      /* Validating the Surrogate pair. */
      uint16_t low_surrogate;
      ctx->status = JES_INVALID_UNICODE;

      if ((escaped_utf_16[6] == '\\') || (escaped_utf_16[7] == 'u')) {
        if (jes_tokenizer_utf_16_str2hex(&escaped_utf_16[8], &low_surrogate)) {
          if (low_surrogate >= 0xDC00 && low_surrogate <= 0xDFFF) {
            /* Unicode is valid. Switch to normal string tokenizer. */
            ctx->status = JES_NO_ERROR;
            break;
          }
        }
      }
      ctx->status = JES_INVALID_UNICODE;
    }
  }

  return current_pos;
}

static inline const char* jes_tokenizer_process_string_token(struct jes_context* ctx,
                                                             struct jes_token* token,
                                                             const char* current_pos,
                                                             const char* end)
{
  const char* ch;

  while (true) {

    ch = JES_TOKENIZER_GET_CHAR(current_pos, end);

    if ((ch == NULL) || (*ch == '\0')) {
      ctx->status = JES_UNEXPECTED_EOF;
    }
    else if (*ch == '\"') {
      /* End of STRING. Do not increment the token length since '\"' isn't a part of token. */
      break;
    }
    else if (*ch == '\\') {
      const char* next = JES_TOKENIZER_LOOK_AHEAD(ch, end);
      if ((next != NULL) && (*next == 'u')) {
        current_pos = jes_tokenizer_process_escaped_utf_16_token(ctx, token, ch, end);
      }
      else {
        ctx->status = JES_UNEXPECTED_SYMBOL;
        break;
      }
    }
    else if ((*ch =='\b') || (*ch =='\f') || (*ch =='\n') ||
             (*ch =='\r') || (*ch =='\t')) {
      ctx->status = JES_UNEXPECTED_SYMBOL;
      break;
    }
    else {
      token->length++;
    }
  }
  return current_pos;
}


static inline const char* jes_tokenizer_process_literal_token(struct jes_context* ctx,
                                                              struct jes_token* token,
                                                              const char* current_pos,
                                                              const char* end,
                                                              char* str,
                                                              uint16_t str_len)
{
  const char* ch;

  while (true) {
    ch = JES_TOKENIZER_GET_CHAR(current_pos, end);

    if ((ch == NULL) || (*ch == '\0')) {
      ctx->status = JES_UNEXPECTED_EOF;
      break;
    }

    token->length++;

    if (token->length == str_len) {
      if (strncmp(token->value, str, str_len) != 0) {
        ctx->status = JES_UNEXPECTED_SYMBOL;
      }
      break;
    }
  }

  return current_pos;
}

enum jes_status jes_tokenizer_get_token(struct jes_context* ctx)
{
  struct jes_token token = { 0 };
  const char* ch;

  while (true) {

    ch = JES_TOKENIZER_GET_CHAR(ctx->serdes.tokenizer.cursor.pos, ctx->json_data + ctx->json_length);
    ctx->serdes.tokenizer.cursor.column++;
    if (!token.type) {

      if (ch == NULL) {
        UPDATE_TOKEN(token, JES_TOKEN_EOF, 0, ch);
        break;
      }

      if (jes_tokenizer_set_delimiter_token(&token, ch)) {
        break;
      }

      /* Skipping space symbols including: space, tab, carriage return */
      if (IS_SPACE(*ch)) {
        /* Handling different newline conventions \r, \n or \r\n */
        if (*ch == '\n') {
          /* Unix-style LF */
          ctx->serdes.tokenizer.cursor.line_number++;
          ctx->serdes.tokenizer.cursor.column = 0;
        }
        else if (*ch == '\r') {
          /* Could be Mac-style CR or first part of Windows CRLF */
          ch = JES_TOKENIZER_LOOK_AHEAD(ch, ctx->json_data + ctx->json_length);
          if ((ch != NULL) && (*ch != '\n')) {
            /* Mac-style standalone CR */
            ctx->serdes.tokenizer.cursor.line_number++;
            ctx->serdes.tokenizer.cursor.column = 0;
          }
          /* If next is \n, we'll handle the line increment when we process the next character */
        }
        continue;
      }

      if (*ch == '\"') {
        UPDATE_TOKEN(token, JES_TOKEN_STRING, 0, ch);
        ch = JES_TOKENIZER_LOOK_AHEAD(ch, ctx->json_data + ctx->json_length);
        if ((ch == NULL) || (*ch == '\0')) {
          ctx->status = JES_UNEXPECTED_EOF;
        }
        else if (*ch == '\"') {
          token.value++;
          ch = JES_TOKENIZER_GET_CHAR(ctx->serdes.tokenizer.cursor.pos, ctx->json_data + ctx->json_length);
        }
        else {
          token.value++;
          ctx->serdes.tokenizer.cursor.pos = jes_tokenizer_process_string_token(ctx, &token, ch, ctx->json_data + ctx->json_length);
        }
        break;
      }

      if (IS_DIGIT(*ch)) {
        UPDATE_TOKEN(token, JES_TOKEN_NUMBER, 0, ch);
        ctx->serdes.tokenizer.cursor.pos = jes_tokenizer_process_integer_token(ctx, &token, ch, ctx->json_data + ctx->json_length);
        break;
      }

      if (*ch == '-') {
        UPDATE_TOKEN(token, JES_TOKEN_NUMBER, 1, ch);
        ch = JES_TOKENIZER_LOOK_AHEAD(ch, ctx->json_data + ctx->json_length);
        if ((ch == NULL) || (*ch == '\0')) {
          ctx->status = JES_UNEXPECTED_EOF;
        }
        else if (!IS_DIGIT(*ch)) {
          ctx->status = JES_UNEXPECTED_SYMBOL;
        }
        else {
          ctx->serdes.tokenizer.cursor.pos = jes_tokenizer_process_integer_token(ctx, &token, ch, ctx->json_data + ctx->json_length);
        }
        break;
      }

      if (*ch == 't') {
        UPDATE_TOKEN(token, JES_TOKEN_TRUE, 0, ch);
        ctx->serdes.tokenizer.cursor.pos = jes_tokenizer_process_literal_token(ctx, &token, ch, ctx->json_data + ctx->json_length, "true", sizeof("true") - 1);
        break;
      }

      if (*ch == 'f') {
        UPDATE_TOKEN(token, JES_TOKEN_FALSE, 0, ch);
        ctx->serdes.tokenizer.cursor.pos = jes_tokenizer_process_literal_token(ctx, &token, ch, ctx->json_data + ctx->json_length, "false", sizeof("false") - 1);
        break;
      }

      if (*ch == 'n') {
        UPDATE_TOKEN(token, JES_TOKEN_TRUE, 0, ch);
        ctx->serdes.tokenizer.cursor.pos = jes_tokenizer_process_literal_token(ctx, &token, ch, ctx->json_data + ctx->json_length, "null", sizeof("null") - 1);
        break;
      }

      UPDATE_TOKEN(token, JES_TOKEN_INVALID, 1, ch);
      break;
    }

    /* A new token is in process and EOF is not expected at this point. */
    if (ch == NULL) {
      ctx->status = JES_UNEXPECTED_EOF;
      break;
    }
  }
#if defined(JES_ENABLE_TOKEN_LOG)
  JES_LOG_TOKEN(token.type, ctx->serdes.tokenizer.cursor.line_number, (token.value - ctx->json_data) + 1, token.length, token.value);
#endif
  ctx->serdes.tokenizer.token = token;
  return ctx->status;
}

bool jes_tokenizer_validate_number(struct jes_context* ctx, const char* value, size_t length)
{
  struct jes_token token = { 0 };
  const char* ch = NULL;
  bool is_valid = false;

  assert(ctx != NULL);
  assert(value != NULL);

  ch = JES_TOKENIZER_GET_CHAR(value, value + length);

  if (IS_DIGIT(*ch) || (*ch == '-')) {
    (void)jes_tokenizer_process_integer_token(ctx, &token, ch, value + length);
    /* There are no more symbols to consume as a number. */
    if ((token.length == length) && (ctx->status == JES_NO_ERROR)) {
      is_valid = true;
    }
  }

  return is_valid;
}

bool jes_tokenizer_validate_string(struct jes_context* ctx, const char* value, size_t length)
{
  struct jes_token token = { 0 };
  const char* ch = NULL;
  bool is_valid = false;

  assert(ctx != NULL);
  assert(value != NULL);

  if (length == 0) {
    /* We consider a string with zero length a valid string */
    return true;
  }

  (void)jes_tokenizer_process_string_token(ctx, &token, value, value + length);
  /* The function will not return true until finding a double quoute symbol
     which we don't have in this case. Ignore the return value and rely on the
     token size instead. */
  if ((token.length == length) && (ctx->status == JES_UNEXPECTED_EOF)) {
    ctx->status = JES_NO_ERROR;
    is_valid = true;
  }

  return is_valid;
}

void jes_tokenizer_reset_cursor(struct jes_context* ctx)
{
  ctx->serdes.tokenizer.cursor.pos = ctx->json_data;
  ctx->serdes.tokenizer.cursor.line_number = 0;
  ctx->serdes.tokenizer.cursor.column = 0;
}
