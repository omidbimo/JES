#ifndef JES_SERIALIZER_H
#define JES_SERIALIZER_H

#include "jes.h"
#include "jes_private.h"
#include "jes_logger.h"

struct jes_container {
  /* Container type: Object | Array */
  uint16_t type;
  /* Number of elements within a container */
  uint16_t member_count;
};

enum jes_streaming_command {
  JES_RENDER_OBJECT_START = 1,
  JES_RENDER_OBJECT_END,
  JES_RENDER_ARRAY_START,
  JES_RENDER_ARRAY_END,
  JES_RENDER_KEY,
  JES_RENDER_TRUE,
  JES_RENDER_FALSE,
  JES_RENDER_NULL,
  JES_RENDER_STRING,
  JES_RENDER_INT32,
  JES_RENDER_INT64,
  JES_RENDER_UINT32,
  JES_RENDER_UINT64,
  JES_RENDER_DOUBLE,
};

struct jes_node* jes_serializer_get_node(struct jes_context* ctx);

#endif /* JES_SERIALIZER_H */