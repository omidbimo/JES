#ifndef JES_PRIVATE_H
#define JES_PRIVATE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef JES_ENABLE_FAST_KEY_SEARCH
  #include "jes_keymap.h"
#endif

#ifdef JES_USE_32BIT_NODE_DESCRIPTOR
  #define JES_INVALID_INDEX 0xFFFFFFFF
#else
  #define JES_INVALID_INDEX 0xFFFF
#endif

#define HAS_PARENT(node_ptr) (((node_ptr) != NULL) ? (node_ptr)->parent < JES_INVALID_INDEX : false)
#define HAS_SIBLING(node_ptr) (((node_ptr) != NULL) ? (node_ptr)->sibling < JES_INVALID_INDEX : false)
#define HAS_CHILD(node_ptr) (((node_ptr) != NULL) ? (node_ptr)->last_child < JES_INVALID_INDEX : false)

#define GET_PARENT(ctx_, node_ptr) (HAS_PARENT(node_ptr) ? &ctx_->node_pool[(node_ptr)->parent] : NULL)
#define GET_SIBLING(ctx_, node_ptr) (HAS_SIBLING(node_ptr) ? &ctx_->node_pool[(node_ptr)->sibling] : NULL)
#define GET_FIRST_CHILD(ctx_, node_ptr) (HAS_CHILD(node_ptr) ? &ctx_->node_pool[(node_ptr)->first_child] : NULL)
#define GET_LAST_CHILD(ctx_, node_ptr) (HAS_CHILD(node_ptr) ? &ctx_->node_pool[(node_ptr)->last_child] : NULL)

#define NODE_TYPE(node_ptr) ((node_ptr != NULL) ? node_ptr->json_tlv.type : JES_UNKNOWN)
#define PARENT_TYPE(ctx_, node_ptr) (HAS_PARENT(node_ptr) ? ctx_->node_pool[(node_ptr)->parent].json_tlv.type : JES_UNKNOWN)

#define JES_NODE_INDEX(ctx_, node_ptr) ((node_ptr != NULL) ? (jes_node_descriptor)((node_ptr) - ctx_->node_pool) : JES_INVALID_INDEX)

#define JES_CONTEXT_COOKIE 0xABC09DEF
#define JES_IS_INITIATED(ctx_) (ctx_->cookie == JES_CONTEXT_COOKIE)

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
  JES_EXPECT_EOF,
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
  size_t length;
  const char* value;
};

struct jes_node {
  /* Element containing TLV JSON data.
   * This should be the first member of the node structure. */
  struct jes_element json_tlv;
  /* Index of the parent node. Each node holds the index of its parent. */
  jes_node_descriptor parent;
  /* Index of sibling node. Siblings are always on the right of nodes.
   * That means reaching a specific sibling, requires iterations from left side
   * to the right side of the branch. */
  jes_node_descriptor sibling;
  /* Each parent keeps only the index of its first child and its last child.
   * The remaining child nodes can be reached using the sibling indices. */
  jes_node_descriptor first_child;
  jes_node_descriptor last_child;
};

struct jes_freed_node {
  struct jes_freed_node* next;
};

struct jes_context {
  /* If the cookie value 0xABC09DEF is confirmed, the structure will be considered as initialized */
  uint32_t cookie;
  uint32_t status;
  /* Extended status code. In some cases provides more detailed information about the status. */
  uint32_t ext_status;
  /* State of the parser state machine or the serializer state machine */
  enum jes_state state;
  /* Number of nodes in the current JSON */
  uint32_t node_count;
  /* JSON data to be parsed */
  const char* json_data;
  /* Length of JSON data in bytes. */
  uint32_t  json_size;
  /* */
  const char* tokenizer_pos;
  uint32_t  line_number;
  /* To dynamically switch tokenizer functions when detecting Integers, fractions and exponents */
  bool (*typed_tokenizer_fn) (struct jes_context* ctx, struct jes_token* token, const char* char_ptr);
  /* Part of the buffer given by the user at the time of the context initialization.
   * The buffer will be used to allocate the context structure at first.
   * The remaining memory will be used as a pool of nodes (max. 65535 nodes). */
   struct jes_node* node_pool;
  /* node_pool size in bytes. (buffer size - context size) */
  uint32_t pool_size;
  /* Number of nodes that can be allocated on the given buffer. The value will
     be limited to 65535 in case of 16-bit node descriptors. */
  uint32_t capacity;
  /* Index of the pool's next free node */
  jes_node_descriptor next_free;
  /* Holds the last token delivered by tokenizer. */
  struct jes_token token;
  /* Internal node iterator */
  struct jes_node* iter;
  /* Holds the main object node */
  struct jes_node* root;
  /* Singly Linked list of previously freed nodes to be recycled by the allocator. */
  struct jes_freed_node* freed;

  struct jes_node* (*find_key_fn) (struct jes_context* ctx, struct jes_node* parent, const char* key, size_t key_len);

  struct jes_hash_table* hash_table;
};

#endif