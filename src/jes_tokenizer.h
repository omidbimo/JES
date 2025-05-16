#ifndef JES_TOKENIZER_H
#define JES_TOKENIZER_H

void jes_init_tokenizer(struct jes_context* ctx);
jes_status jes_get_token(struct jes_context *ctx);

#endif