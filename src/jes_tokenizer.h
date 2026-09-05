#ifndef JES_TOKENIZER_H
#define JES_TOKENIZER_H

enum jes_status jes_tokenizer_get_token(struct jes_tokenizer_context* tokenizer);
enum jes_status jes_tokenizer_validate_user_number(struct jes_context* ctx, const char* value, size_t length);
enum jes_status jes_tokenizer_validate_user_string(struct jes_context* ctx, const char* value, size_t length);
void jes_tokenizer_reset_cursor(struct jes_tokenizer_context* ctx);

#endif