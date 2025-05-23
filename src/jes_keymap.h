#ifndef JES_KEYMAP_H
#define JES_KEYMAP_H

//#include "jes.h"

struct jes_hash_table_context;
struct jes_context;
struct jes_node;

struct jes_hash_entry {
  size_t hash;
  struct jes_element *key_element;
};

enum jes_status jes_hash_table_init(struct jes_context* ctx);
jes_status jes_hash_table_add(struct jes_context* ctx, struct jes_node* parent, struct jes_node* key);
void jes_hash_table_remove(struct jes_context* ctx, struct jes_node* parent, struct jes_node* key);

#endif