#ifndef JES_PRIVATE_H
#define JES_PRIVATE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef JES_ENABLE_KEY_HASHING
  #include "jes_hash_table.h"
#endif

#ifdef JES_USE_32BIT_NODE_DESCRIPTOR
  #define JES_INVALID_INDEX 0xFFFFFFFF
#else
  #define JES_INVALID_INDEX 0xFFFF
#endif

#define HAS_PARENT(node_ptr) (((node_ptr) != NULL) ? (node_ptr)->parent < JES_INVALID_INDEX : false)
#define HAS_SIBLING(node_ptr) (((node_ptr) != NULL) ? (node_ptr)->sibling < JES_INVALID_INDEX : false)
#define HAS_CHILD(node_ptr) (((node_ptr) != NULL) ? (node_ptr)->last_child < JES_INVALID_INDEX : false)

#define GET_PARENT(node_mng_, node_ptr) (HAS_PARENT(node_ptr) ? &node_mng_.pool[(node_ptr)->parent] : NULL)
#define GET_SIBLING(node_mng_, node_ptr) (HAS_SIBLING(node_ptr) ? &node_mng_.pool[(node_ptr)->sibling] : NULL)
#define GET_FIRST_CHILD(node_mng_, node_ptr) (HAS_CHILD(node_ptr) ? &node_mng_.pool[(node_ptr)->first_child] : NULL)
#define GET_LAST_CHILD(node_mng_, node_ptr) (HAS_CHILD(node_ptr) ? &node_mng_.pool[(node_ptr)->last_child] : NULL)

#define NODE_TYPE(node_ptr) ((node_ptr != NULL) ? node_ptr->json_tlv.type : JES_UNKNOWN)
#define PARENT_TYPE(node_mng_, node_ptr) (HAS_PARENT(node_ptr) ? node_mng_.pool[(node_ptr)->parent].json_tlv.type : JES_UNKNOWN)

#define JES_NODE_INDEX(node_mng_, node_ptr) ((node_ptr != NULL) ? (jes_node_descriptor)((node_ptr) - node_mng_.pool) : JES_INVALID_INDEX)

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
  JES_EXPECT_KEY,
  JES_EXPECT_COLON,
  JES_EXPECT_VALUE,
  JES_HAVE_VALUE,
  JES_EXPECT_EOF,
  JES_END,
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

struct jes_node_mng_context {
  /* Part of the buffer given by the user at the time of the context initialization.
   * The buffer will be used to allocate the context structure at first.
   * The remaining memory will be used as a pool of nodes (max. 65535 nodes). */
   struct jes_node* pool;
  /* Node pool size in bytes. (buffer size - context size) */
  size_t size;
  /* Number of nodes that can be allocated on the given buffer. The value will
     be limited to 65535 in case of 16-bit node descriptors. */
  size_t capacity;
  /* Index of the pool's next free node */
  jes_node_descriptor next_free;
  /* Singly Linked list of previously freed nodes to be recycled by the allocator. */
  struct jes_freed_node* freed;
  /* Number of nodes in the current JSON */
  size_t node_count;
  /* Key search function pointer. The function might switch to linear or lookup table search based on compile-time configurations. */
  struct jes_node* (*find_key_fn) (struct jes_context* ctx, struct jes_node* parent, const char* key, size_t key_len);
  /* Holds the main object node */
  struct jes_node* root;
};

struct jes_cursor {
  /* Points to the next character in JSON document to be tokenized */
  const char* pos;
  const char* end;
  /* The current column number in the JSON input that is being processed */
  size_t  column;
  /* The current line number in the JSON input that is being processed */
  size_t  line_number;
};

struct jes_tokenizer_context {
  /* Pointer to the JSON data buffer to be parsed.
   * Must remain valid throughout the parsing operation.
   * The library does not take ownership - caller must manage memory. */
  const char* json_data;
  /* Size of the JSON data buffer in bytes.
   * Must accurately reflect the length of data at json_data pointer (without NUL termination) */
  size_t  json_length;
  /* The cursor contains the information about the current character in the
     input JSON that is being processed. */
  struct jes_cursor cursor;
  /* Holds the last token delivered by tokenizer. */
  struct jes_token token;
};

struct jes_serdes_context {
  /* State of the parser state machine or the serializer state machine */
  enum jes_state state;
  /* Internal node iterator */
  struct jes_node* iter;
  /* Tokenizer subsystem state. */
  struct jes_tokenizer_context tokenizer;
};

struct jes_hash_table_context {
  /* Pre-allocated pool of hash table entries. This is a partition of workspace buffer provided by the user.
   * The hash pool will be placed at the end of the workspace buffer and will be managed reversely. */
  struct jes_hash_entry* pool;
  /* Size of buffer partition that is dedicated to hash entry allocations */
  size_t size;
  /* Maximum number of entries the hash table can hold. */
  size_t capacity;
  /* Hash function pointer for generating table indices from keys.
   * @param parent_id   Unique identifier of the parent JSON object
   * @param key         key name to hash (non-NUL terminated)
   * @param key_length  Length of the key string in bytes
   * @return            Hash value used for table indexing
   *
   * Incorporates parent_id to handle nested objects with identical keys. */
  uint32_t (*hash_fn) (uint32_t parent_id, const char* key, size_t keyword_length);
  /* Function to add a key-value pair to the hash table.
   *
   * @param ctx     Main JES context containing all parsing state
   * @param parent  JSON object node that contains this key-value pair
   * @param key     JSON node representing the property key (string)
   * @return        JES_STATUS_OK on success, error code on failure
   *
   * Handles collision resolution and table expansion if needed.
   * May fail if the hash table is full or memory constraints are exceeded. */
  jes_status (*add_fn) (struct jes_context* ctx, struct jes_node* parent, struct jes_node* key);
  /* Function pointer to remove a key from the hash table.
   * @param ctx     JES context
   * @param parent  JSON object node that contains this key
   * @param key     JSON node representing the property key to remove
   *
   * When dynamically turning the hash table off, this function pointer must be safely grounded. */
  void (*remove_fn) (struct jes_context* ctx, struct jes_node* parent, struct jes_node* key);
};

/**
 * Main context structure for the JES library.
 * Contains all state and configuration needed for JSON parsing operations.
 */
struct jes_context {
  /* Cookie for structure validation (0xABC09DEF when properly initialized). */
  uint32_t cookie;
  /* Indicates success/failure state of the most recent JES operation.
   * Check against JES_STATUS_* constants for specific meanings. */
  enum jes_status status;
  /* User-provided working memory buffer for all JES operations.
   * It will be fragmented to hold the jes context, node allocations and the hash table entries.
   * Must remain available throughout the context's lifetime. */
  void* workspace;
  /* Size of the workspace buffer in bytes. This will be used to reconstruct the workspace when needed. */
  size_t workspace_size;

  /* Serialization/Deserialization subsystem state. */
  struct jes_serdes_context serdes;
  /* Node management subsystem state. */
  struct jes_node_mng_context node_mng;

#ifdef JES_ENABLE_KEY_HASHING
  /* Hash table state for accelerated key lookups */
  struct jes_hash_table_context hash_table;
#endif

  char path_separator;
};

#endif