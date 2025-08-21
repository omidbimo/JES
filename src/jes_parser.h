#ifndef JES_PARSER_H
#define JES_PARSER_H

struct jes_element* jes_parse(struct jes_context *ctx);
void jes_parser_state_machine(struct jes_context *ctx);

#endif