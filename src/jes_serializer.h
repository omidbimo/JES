#ifndef JES_SERIALIZER_H
#define JES_SERIALIZER_H

#include "jes.h"
#include "jes_private.h"
#include "jes_logger.h"

struct jes_streaming_container {
  /* Container type: Object | Array */
  uint16_t type;
  /* Number of elements within a container */
  uint16_t member_count;
};

/**
 * Context for streaming (tree-less) JSON serialization.
 */
struct jes_streaming_serializer_context {
  char*               out_buffer;         /* Output buffer for rendered JSON */
  size_t              out_buffer_size;    /* Size of out_buffer in bytes */
  struct jes_streaming_container* stack;  /* User-provided stack memory */
  size_t              stack_size;         /* Stack size in bytes */
  int                 stack_top;          /* Current stack depth */
  enum jes_status     sticky_error;
  unsigned int        state;
};

struct jes_node* jes_serializer_get_node(struct jes_context* ctx);

#endif /* JES_SERIALIZER_H */