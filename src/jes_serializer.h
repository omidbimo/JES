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

struct jes_node* jes_serializer_get_node(struct jes_context* ctx);

#endif /* JES_SERIALIZER_H */