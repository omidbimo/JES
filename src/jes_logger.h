#ifndef JES_LOGGER_H
#define JES_LOGGER_H

#include <stdbool.h>
#include <stdint.h>

/* */
void jes_log_token(uint16_t token_type,
                   size_t line_numebr,
                   size_t column,
                   size_t token_pos,
                   size_t token_len,
                   const char* token_value);
/* */
void jes_log_node(const char *pre_msg,
                  int32_t node_id,
                  uint32_t node_type,
                  size_t node_length,
                  const char *node_value,
                  int32_t parent_id,
                  int32_t right_id,
                  int32_t first_child_id,
                  int32_t last_child_id,
                  const char *post_msg);
void jes_log_state(const char *pre_msg, uint32_t state, const char *post_msg);
/* Provides a textual overview of the latest failure */
char* jes_stringify_status(struct jes_context *ctx, char *msg, size_t msg_len);
/* Provides a textual type of a JES element */
char* jes_stringify_element(struct jes_element *element, char *msg, size_t msg_len);

void jes_print_workspace_stat(struct jes_workspace_stat ws_stat);
#endif