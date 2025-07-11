#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "jes.h"
#include "jes_private.h"
#include "jes_logger.h"

#define IS_SPACE(c) ((c) ==' ' || (c) =='\t' || (c) =='\r' || (c) =='\n' || (c) =='\f')
#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')

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

static inline char jes_tokenizer_get_char(struct jes_cursor* cursor)
{
  return cursor->pos < cursor->end ? *cursor->pos : '\0';
}

static inline char jes_tokenizer_look_ahead(struct jes_cursor* cursor)
{
  return (cursor->pos + 1) < cursor->end ? *(cursor->pos + 1) : '\0';
}

static inline void jes_tokenizer_advance(struct jes_cursor* cursor)
{
  cursor->pos++;
  cursor->column++;
}

static inline void jes_tokenizer_update_token(struct jes_token* token,
                                              enum jes_token_type type,
                                              size_t length,
                                              const char* value)
{
  token->type = type;
  token->length = length;
  token->value = value;
}

static bool jes_tokenizer_process_string_token(struct jes_cursor*,
                                               struct jes_token*,
                                               enum jes_status*);

static inline void jes_tokenizer_process_spaces(struct jes_cursor* cursor)
{
  while (true) {
    char ch = jes_tokenizer_get_char(cursor);
    if (IS_SPACE(ch)) {
      /* Skipping space symbols including: space, tab, carriage return */
      /* Handling different newline conventions \r, \n or \r\n */
      if (ch == '\n') {
        /* Unix-style LF */
        cursor->line_number++;
        cursor->column = 0;
      }
      else if (ch == '\r') {
        /* Could be Mac-style CR or first part of Windows CRLF */
        ch = jes_tokenizer_look_ahead(cursor);
        if (ch != '\n') {
          /* Mac-style standalone CR */
          cursor->line_number++;
          cursor->column = 0;
        }
        /* If next is \n, we'll handle the line increment when we process the next character */
      }
      jes_tokenizer_advance(cursor);
      continue;
    }
    break;
  }
}

static inline bool jes_tokenizer_process_delimiter_token(struct jes_cursor* cursor,
                                                         struct jes_token* token)
{
  bool is_delimiter_token = true;
  char ch = jes_tokenizer_get_char(cursor);

  switch (ch) {
    case '\0': jes_tokenizer_update_token(token, JES_TOKEN_EOF, 1, cursor->pos);             break;
    case '{':  jes_tokenizer_update_token(token, JES_TOKEN_OPENING_BRACE, 1, cursor->pos);   break;
    case '}':  jes_tokenizer_update_token(token, JES_TOKEN_CLOSING_BRACE, 1, cursor->pos);   break;
    case '[':  jes_tokenizer_update_token(token, JES_TOKEN_OPENING_BRACKET, 1, cursor->pos); break;
    case ']':  jes_tokenizer_update_token(token, JES_TOKEN_CLOSING_BRACKET, 1, cursor->pos); break;
    case ':':  jes_tokenizer_update_token(token, JES_TOKEN_COLON, 1, cursor->pos);           break;
    case ',':  jes_tokenizer_update_token(token, JES_TOKEN_COMMA, 1, cursor->pos);           break;
    default:   is_delimiter_token = false;                                        break;
  }

  return is_delimiter_token;
}

/* Token type is NUMBER. Try to feed it with more symbols. */
static void jes_tokenizer_process_exponent_token(struct jes_cursor* cursor,
                                                 struct jes_token* token,
                                                 enum jes_status* status)
{
  while (true) {
    char next;
    char ch = jes_tokenizer_get_char(cursor);

    if (IS_DIGIT(ch)) {
      token->length++;
      /* Numbers do not have a terminator symbol. Need to look ahead to decide the end of a number */
      if (!IS_DIGIT(jes_tokenizer_look_ahead(cursor))) {
        break;
      }
    }
    else if ((ch == '+') || (ch == '-')) {
      token->length++;
      if (!IS_DIGIT(jes_tokenizer_look_ahead(cursor))) {
        *status = JES_INVALID_NUMBER;
        break;
      }
    }
    else {
      *status = JES_INVALID_NUMBER;
      break;
    }

    jes_tokenizer_advance(cursor);
  }
}

/* Token type is NUMBER. Try to feed it with more symbols. */
static void jes_tokenizer_process_decimal_fraction_token(struct jes_cursor* cursor,
                                                         struct jes_token* token,
                                                         enum jes_status* status)
{
  while (true) {
    char next;
    char ch = jes_tokenizer_get_char(cursor);

    if (IS_DIGIT(ch)) {
      token->length++;
    }
    else if ((ch == 'e') || (ch == 'E')) {
      token->length++;
      jes_tokenizer_advance(cursor);
      jes_tokenizer_process_exponent_token(cursor, token, status);
      break;
    }
    else {
      *status = JES_UNEXPECTED_SYMBOL;
      break;
    }

    /* Numbers do not have a terminator symbol. Need to look ahead to decide the end of a number */
    next = jes_tokenizer_look_ahead(cursor);
    if (!IS_DIGIT(next) && (next != 'e') && (next != 'E')) {
      break;
    }

    jes_tokenizer_advance(cursor);
  }
}

/* Token type is NUMBER. Try to feed it with more symbols. */
static inline bool jes_tokenizer_process_integer_token(struct jes_cursor* cursor,
                                                       struct jes_token* token,
                                                       enum jes_status* status)
{
  char ch = jes_tokenizer_get_char(cursor);

  if (!IS_DIGIT(ch) && (ch != '-')) {
    return false;
  }

  jes_tokenizer_update_token(token, JES_TOKEN_NUMBER, 1, cursor->pos);

  if ((ch == '-') && !IS_DIGIT(jes_tokenizer_look_ahead(cursor))) {
    *status = JES_UNEXPECTED_SYMBOL;
    return true;
  }

  while (true) {
    char next;

    /* Numbers do not have a terminator symbol. Need to look ahead to decide the end of a number */
    next = jes_tokenizer_look_ahead(cursor);
    if (!IS_DIGIT(next) && (next != '.') && (next != 'e') && (next != 'E')) {

      break;
    }

    jes_tokenizer_advance(cursor);
    ch = jes_tokenizer_get_char(cursor);

    if (!IS_DIGIT(ch)) {
      if ((token->length == 1) && (*token->value == '-')) {
        *status = JES_UNEXPECTED_SYMBOL;
      }
      else if (ch == '.') {
        token->length++;
        jes_tokenizer_advance(cursor);
        jes_tokenizer_process_decimal_fraction_token(cursor, token, status);
      }
      else if ((ch == 'e') || (ch == 'E')) {
        token->length++;
        jes_tokenizer_advance(cursor);
        jes_tokenizer_process_exponent_token(cursor, token, status);
      }
      else {
        *status = JES_UNEXPECTED_SYMBOL;
      }
      break;
    }
    else { /* Got a digit */
      token->length++;
      /* Integers with leading zeros are invalid JSON numbers */
      if ((token->length == 2) && (*token->value == '0')) {
        *status = JES_INVALID_NUMBER;
        break;
      }
    }
  }

  return true;
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

static void jes_tokenizer_process_escaped_utf_16_token(struct jes_cursor* cursor,
                                                       struct jes_token* token,
                                                       enum jes_status* status)
{
  char ch;
  const char* escaped_utf_16;
  size_t escaped_utf_16_length;

  escaped_utf_16 = cursor->pos;
  escaped_utf_16_length = 0;

  while (*status == JES_NO_ERROR) {

    ch = jes_tokenizer_get_char(cursor);

    if ((ch == '\"') || (ch =='\b') || (ch =='\f') || (ch =='\n') ||
        (ch =='\r') || (ch =='\t')) {
      *status = JES_UNEXPECTED_SYMBOL;
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
    if (escaped_utf_16_length == (sizeof("XXXX") - 1)) {
      uint16_t utf_16_int;

      if (jes_tokenizer_utf_16_str2hex(&escaped_utf_16[0], &utf_16_int)) {
        if (utf_16_int < 0xD800 || utf_16_int > 0xDFFF) {
          /* valid 16-bit BMP code point. Switch to normal string tokenizer. */
          *status = JES_NO_ERROR;
          break;
        }
        else {
          /* NOP. The unicode value is in the range of Surrogate pair. Consume more
            bytes and validate the pair later. */
    jes_tokenizer_advance(cursor);
    continue;
        }
      }

      *status = JES_INVALID_UNICODE;
    }
    else if (escaped_utf_16_length == (sizeof("XXXX\\uXXXX") - 1)) {
      /* Validating the Surrogate pair. */
      uint16_t low_surrogate;
      *status = JES_INVALID_UNICODE;

      if ((escaped_utf_16[4] == '\\') && (escaped_utf_16[5] == 'u')) {
        if (jes_tokenizer_utf_16_str2hex(&escaped_utf_16[6], &low_surrogate)) {
          if (low_surrogate >= 0xDC00 && low_surrogate <= 0xDFFF) {
            /* Unicode is valid. Switch to normal string tokenizer. */
            *status = JES_NO_ERROR;
            break;
          }
        }
      }
      *status = JES_INVALID_UNICODE;
    }
    jes_tokenizer_advance(cursor);
  }
}

static inline bool jes_tokenizer_process_string_token(struct jes_cursor* cursor,
                                                      struct jes_token* token,
                                                      enum jes_status* status)
{
  char ch = jes_tokenizer_get_char(cursor);

  if (ch != '\"') {
    return false;
  }

  jes_tokenizer_update_token(token, JES_TOKEN_STRING, 0, cursor->pos);
  jes_tokenizer_advance(cursor);

  while (*status == JES_NO_ERROR) {

    ch = jes_tokenizer_get_char(cursor);

    if (ch == '\0') {
      *status = JES_UNEXPECTED_EOF;
      break;
    }

    if (token->length == 0) {
      token->value = cursor->pos;
    }

    if (ch == '\"') {
      /* End of STRING. Do not increment the token length since '\"' isn't a part of token. */
      break;
    }
    else if (ch == '\\') {
      token->length++;
      jes_tokenizer_advance(cursor);

      switch (ch) {
        case '"':
        case '\\':
        case '/':
        case 'b':
        case 'f':
        case 'n':
        case 'r':
        case 't':
            token->length++;
            break;
        case 'u':
          token->length++;
          jes_tokenizer_advance(cursor);
          jes_tokenizer_process_escaped_utf_16_token(cursor, token, status);
          break;
        default:
          *status = JES_UNEXPECTED_SYMBOL;
          return true;
      }
    }
    else if ((ch =='\b') || (ch =='\f') || (ch =='\n') || (ch =='\r') || (ch =='\t')) {
      *status = JES_UNEXPECTED_SYMBOL;
      break;
    }
    else {
      token->length++;
    }

    jes_tokenizer_advance(cursor);
  }
  return true;
}


static inline bool jes_tokenizer_process_literal_token(struct jes_cursor* cursor,
                                                       struct jes_token* token,
                                                       enum jes_status* status)
{
  char* literal;
  size_t literal_len = 0;
  char ch = jes_tokenizer_get_char(cursor);
  bool is_literal_token = true;

  switch (ch) {
    case 'f':
      jes_tokenizer_update_token(token, JES_TOKEN_FALSE, 0, cursor->pos);
      literal = "false";
      literal_len = sizeof("false") -1;
      break;
    case 'n':
      literal = "null";
      literal_len = sizeof("null") -1;
      jes_tokenizer_update_token(token, JES_TOKEN_NULL, 0, cursor->pos);
      break;
    case 't':
      literal = "true";
      literal_len = sizeof("true") -1;
      jes_tokenizer_update_token(token, JES_TOKEN_TRUE, 0, cursor->pos);
      break;
    default:
      is_literal_token = false;
      break;
  }

  if (is_literal_token) {
    if (((cursor->pos + literal_len) < cursor->end) &&
         (memcmp(cursor->pos, literal, literal_len) == 0)) {
      cursor->pos += literal_len - 1;
      token->length += literal_len;
    }
    else {
      *status = JES_UNEXPECTED_SYMBOL;
    }
  }

  return is_literal_token;
}

enum jes_status jes_tokenizer_get_token(struct jes_tokenizer_context* ctx)
{
  struct jes_token token = { 0 };
  enum jes_status status = JES_NO_ERROR;
  char ch = jes_tokenizer_get_char(&ctx->cursor);

  if (ch == '\0') {
    jes_tokenizer_update_token(&token, JES_TOKEN_EOF, 1, ctx->cursor.pos);
  }
  else {
    jes_tokenizer_process_spaces(&ctx->cursor);

    if (jes_tokenizer_process_delimiter_token(&ctx->cursor, &token)) {
    }
    else if (jes_tokenizer_process_string_token(&ctx->cursor, &token, &status)) {
    }
    else if (jes_tokenizer_process_integer_token(&ctx->cursor, &token, &status)) {
    }
    else if (jes_tokenizer_process_literal_token(&ctx->cursor, &token, &status)) {
    }
    else {
      jes_tokenizer_update_token(&token, JES_TOKEN_INVALID, 0, ctx->cursor.pos);
      status = JES_UNEXPECTED_SYMBOL;
    }
  }

  if (status == JES_NO_ERROR) {
    jes_tokenizer_advance(&ctx->cursor);
  }

#if defined(JES_ENABLE_TOKEN_LOG)
  JES_LOG_TOKEN(token.type, ctx->cursor.line_number, ctx->cursor.column, (token.value - ctx->json_data) + 1, token.length, token.value);
#endif
  ctx->token = token;
  return status;
}

enum jes_status jes_tokenizer_validate_number(struct jes_context* ctx, const char* value, size_t length)
{
  struct jes_token token = { 0 };
  struct jes_cursor cursor = { 0 };
  enum jes_status status = JES_NO_ERROR;

  assert(ctx != NULL);
  assert(value != NULL);

  cursor.pos = value;
  cursor.end = value + length;

  if (!jes_tokenizer_process_integer_token(&cursor, &token, &status)) {
    status = JES_INVALID_NUMBER;
  }

  return status;
}

enum jes_status jes_tokenizer_validate_string(struct jes_context* ctx, const char* value, size_t length)
{

  struct jes_token token = { 0 };
  struct jes_cursor cursor = { 0 };
  enum jes_status status = JES_NO_ERROR;
  char ch;

  assert(ctx != NULL);
  assert(value != NULL);

  if (length == 0) {
    /* A string with zero length is a valid string */
    return true;
  }

  cursor.pos = value;
  cursor.end = value + length;

  jes_tokenizer_update_token(&token, JES_TOKEN_STRING, 0, cursor.pos);

  while ((status == JES_NO_ERROR) && (ch != '\0')) {

    ch = jes_tokenizer_get_char(&cursor);

    if ((ch == '\0') && (cursor.pos < cursor.end)) {
      status = JES_UNEXPECTED_EOF;
      break;
    }

    if (ch == '\\') {
      token.length++;
      jes_tokenizer_advance(&cursor);
      if (jes_tokenizer_get_char(&cursor) == 'u') {
        token.length++;
        jes_tokenizer_advance(&cursor);
        jes_tokenizer_process_escaped_utf_16_token(&cursor, &token, &status);
      }
      else {
        status = JES_UNEXPECTED_SYMBOL;
        break;
      }
    }
    else if ((ch == '\"') || (ch =='\b') || (ch =='\f') || (ch =='\n') || (ch =='\r') || (ch =='\t')) {
      status = JES_UNEXPECTED_SYMBOL;
      break;
    }
    else {
      token.length++;
    }

    jes_tokenizer_advance(&cursor);
  }

  return status;
}

void jes_tokenizer_reset_cursor(struct jes_tokenizer_context* ctx)
{
  ctx->cursor.pos = ctx->json_data;
  ctx->cursor.end = ctx->json_data + ctx->json_length;
  ctx->cursor.line_number = 0;
  ctx->cursor.column = 0;
}
