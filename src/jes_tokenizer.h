#ifndef JES_TOKENIZER_H
#define JES_TOKENIZER_H

void jes_tokenizer_init(struct jes_context* ctx);
jes_status jes_tokenizer_get_token(struct jes_context *ctx);
bool jes_tokenizer_validate_number(struct jes_context* ctx, const char* value, size_t length);
bool jes_tokenizer_validate_string(struct jes_context* ctx, const char* value, size_t length);

#endif