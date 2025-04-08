#ifndef JES_PRIVATE_H
#define JES_PRIVATE_H

#include <stdbool.h>
#include <stdint.h>


#ifdef JES_USE_32BIT_NODE_DESCRIPTOR
/* A 32bit node descriptor limits the total number of nodes to 4294967295.
   Note that 0xFFFFFFFF is used as an invalid node index. */
typedef uint32_t jes_node_descriptor;
#else
/* A 16bit node descriptor limits the total number of nodes to 65535.
   Note that 0xFFFF is used as an invalid node index. */
typedef uint16_t jes_node_descriptor;
#endif

struct jes_element;

enum jes_state {
  JES_EXPECT_OBJECT = 0,
  JES_EXPECT_KEY,
  JES_EXPECT_COLON,
  JES_EXPECT_KEY_VALUE,
  JES_HAVE_KEY_VALUE,
  JES_EXPECT_ARRAY_VALUE,
  JES_HAVE_ARRAY_VALUE,
};

enum jes_token_type {
  JES_TOKEN_EOF = 0,
  JES_TOKEN_OPENING_BRACE,   /* { */
  JES_TOKEN_CLOSING_BRACE,   /* } */
  JES_TOKEN_OPENING_BRACKET, /* [ */
  JES_TOKEN_CLOSING_BRACKET, /* ] */
  JES_TOKEN_COLON,
  JES_TOKEN_COMMA,
  JES_TOKEN_STRING,
  JES_TOKEN_NUMBER,
  JES_TOKEN_TRUE,
  JES_TOKEN_FALSE,
  JES_TOKEN_NULL,
  JES_TOKEN_ESC,
  JES_TOKEN_INVALID,
};

struct jes_token {
  enum jes_token_type type;
  uint16_t length;
  uint32_t offset;
};

struct jes_node {
  /* Element containing TLV JSON data */
  struct jes_element json_tlv;
  /* Index of the parent node. Each node holds the index of its parent. */
  jes_node_descriptor parent;
  /* Index */
  jes_node_descriptor sibling;
  /* Each parent keeps only the index of its first child. The remaining child nodes
     will be tracked using the right member of the first child. */
  jes_node_descriptor first_child;
  /* The data member is a TLV (Type, Length, Value) which value is pointer to the
     actual value of the node. See jes.h */
  /* Index */
  jes_node_descriptor last_child;
};

struct jes_free_node {
  struct jes_free_node *next;
};

struct jes_context {
  /* If the cookie value 0xABC09DEF is confirmed, the structure will be considered as initialized */
  uint32_t cookie;
  uint32_t status;
  /* Extended status code. In some cases provides more detailed information about the status. */
  uint32_t ext_status;
  /*  */
  enum jes_state state;
  /* Number of nodes in the current JSON */
  uint32_t node_count;
  /* JSON data to be parsed */
  const char *json_data;
  /* Length of JSON data in bytes. */
  uint32_t  json_size;
  /* Offset of the next symbol in the input JSON data Tokenizer is going to consume. */
  uint32_t  offset;
  uint32_t  line_number;
  /* Part of the buffer given by the user at the time of the context initialization.
   * The buffer will be used to allocate the context structure at first.
   * The remaining memory will be used as a pool of nodes (max. 65535 nodes). */
   struct jes_node *node_pool;
  /* node_pool size in bytes. (buffer size - context size) */
  uint32_t pool_size;
  /* Number of nodes that can be allocated on the given buffer. The value will
     be limited to 65535 in case of 16-bit node descriptors. */
  uint32_t capacity;
  /* Index of the last allocated node */
  jes_node_descriptor index;
  /* Holds the last token delivered by tokenizer. */
  struct jes_token token;
  /* Internal node iterator */
  struct jes_node *iter;
  /* Holds the main object node */
  struct jes_node *root;
  /* Singly Linked list of freed nodes. This way the deleted nodes can be recycled
     by the allocator. */
  struct jes_free_node *free;
};

#endif