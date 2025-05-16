#ifndef JES_KEYMAP_H
#define JES_KEYMAP_H

//#include "jes.h"

struct jes_hash_table;
struct jes_context;
struct jes_node;

struct jes_hash_entry {
  size_t hash;
  struct jes_element *key_element;
};

struct jes_hash_table {
  uint32_t size;
  uint32_t capacity;

  struct jes_hash_entry* entries;

  /* */
  size_t (*hash_fn) (uint32_t parent_id, const char* key, size_t keyword_length);
};

struct jes_hash_table* jes_init_hash_table(struct jes_context* ctx, void *buffer, uint32_t buffer_size);
jes_status jes_hash_table_add(struct jes_context* ctx, struct jes_node* parent, struct jes_node* key);
void jes_hash_table_remove(struct jes_context* ctx, struct jes_node* parent, struct jes_node* key);

#endif