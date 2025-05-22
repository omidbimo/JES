#ifndef JES_TOKENIZER_H
#define JES_TOKENIZER_H

void jes_tokenizer_init(struct jes_context* ctx);
jes_status jes_tokenizer_get_token(struct jes_context *ctx);

#endif