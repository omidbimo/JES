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
void jes_hash_table_turn_off(struct jes_context* ctx);

struct jes_node* jes_find_key_lookup_table(struct jes_context* ctx,
                                           struct jes_node* parent_object,
                                           const char* keyword,
                                           size_t keyword_length);
#endif