#ifndef JES_TOKENIZER_H
#define JES_TOKENIZER_H

struct jes_tokenizer_error_info {
  enum jes_status status;
  size_t line;
  size_t pos;
  enum jes_token_type type;
};


jes_status jes_tokenizer_get_token(struct jes_context* ctx);
bool jes_tokenizer_validate_number(struct jes_context* ctx, const char* value, size_t length);
bool jes_tokenizer_validate_string(struct jes_context* ctx, const char* value, size_t length);
void jes_tokenizer_reset_cursor(struct jes_context* ctx);

#endif