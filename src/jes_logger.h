#ifndef JES_LOGGER_H
#define JES_LOGGER_H

#include <stdbool.h>
#include <stdint.h>

/* */
void jes_log_token(uint16_t token_type,
                   uint32_t token_pos,
                   uint32_t token_len,
                   const uint8_t *token_value);
/* */
void jes_log_node(const char *pre_msg,
                  int16_t node_id,
                  uint32_t node_type,
                  uint32_t node_length,
                  const char *node_value,
                  int16_t parent_id,
                  int16_t right_id,
                  int16_t child_id,
                  const char *post_msg);
/* Provides a textual overview of the latest failure */
char* jes_stringify_status(struct jes_context *ctx, char *msg, size_t msg_len);
/* Provides a textual type of a JES element */
char* jes_stringify_element(struct jes_element *element, char *msg, size_t msg_len);

#endif