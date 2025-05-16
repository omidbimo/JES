#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "jes.h"
#include "jes_private.h"
#include "jes_keymap.h"

#define JES_FNV_PRIME_32BIT         16777619
#define JES_FNV_OFFSET_BASIS_32BIT  2166136261

static size_t jes_fnv1a_compound_hash(uint32_t parent_id, const char* keyword, size_t keyword_length) {
  uint8_t* parent_id_bytes = (uint8_t*)&parent_id;
  size_t hash = JES_FNV_OFFSET_BASIS_32BIT;
  size_t index;

  assert(keyword != NULL);

  for (index = 0; index < sizeof(parent_id); index++) {
    hash ^= parent_id_bytes[index];
    hash *= JES_FNV_PRIME_32BIT;
  }

  for (index = 0; index < keyword_length; index++) {
    hash ^= (unsigned char)keyword[index];
    hash *= JES_FNV_PRIME_32BIT;
  }

  return hash;
}

static struct jes_node* jes_find_key_lookup_table(struct jes_context* ctx,
                                                  struct jes_node* parent_object,
                                                  const char* keyword,
                                                  size_t keyword_length)
{
  struct jes_hash_table* table = ctx->hash_table;
  struct jes_node* key = NULL;
  size_t hash;
  size_t index;

  assert(parent_object != NULL);

  hash = table->hash_fn(JES_GET_NODE_INDEX(ctx, parent_object), keyword, keyword_length);
  index = hash % table->capacity;

  /* Linear probing to handle collisions */
  while (table->entries[index].key_element != NULL) {

    if ((table->entries[index].hash == hash) &&
        (table->entries[index].key_element->length == keyword_length) &&
        (memcmp(table->entries[index].key_element->value, keyword, keyword_length) == 0)) {
      key = (struct jes_node*)table->entries[index].key_element;
      break;
    }
    index = (index + 1) % table->capacity;
  }

  return key;
}

jes_status jes_hash_table_add(struct jes_context* ctx, struct jes_node* parent_object, struct jes_node* key)
{
  struct jes_hash_table* table = ctx->hash_table;
  size_t hash = table->hash_fn(JES_GET_NODE_INDEX(ctx, parent_object), key->json_tlv.value, key->json_tlv.length);
  size_t index = hash % table->capacity;
  size_t start_index = index;

  ctx->status = JES_OUT_OF_MEMORY;

  /* Linear probing to find a free slot */
  do {
    /* Empty slot found */
    if (table->entries[index].key_element == NULL) {
      /* Store the key */
      table->entries[index].hash = hash;
      table->entries[index].key_element = (struct jes_element*)key;
      ctx->status = JES_NO_ERROR;
      break;
    }

    /* Key already exists, replace the value */
    if ((table->entries[index].hash == hash) &&
        (key->json_tlv.length = table->entries[index].key_element->length) &&
        (memcmp(table->entries[index].key_element->value, key->json_tlv.value, key->json_tlv.length) == 0)) {
      ctx->status = JES_DUPLICATE_KEY;
      break;
    }
    /* Move to next slot */
    index = (index + 1) % table->capacity;
  } while (index != start_index);

  return ctx->status;
}

void jes_hash_table_remove(struct jes_context* ctx, struct jes_node* parent_object, struct jes_node* key)
{
  struct jes_hash_table* table = ctx->hash_table;
  struct jes_hash_entry* lookup_entries = table->entries;
  size_t hash;
  size_t index;

  assert(parent_object != NULL);

  hash = table->hash_fn(JES_GET_NODE_INDEX(ctx, parent_object), key->json_tlv.value, key->json_tlv.length);
  index = hash % table->capacity;

  /* Linear probing to find the key */
  while (lookup_entries[index].key_element != NULL) {
    if ((lookup_entries[index].hash == hash) &&
        (lookup_entries[index].key_element->length == key->json_tlv.length) &&
        (memcmp(lookup_entries[index].key_element->value, key->json_tlv.value, key->json_tlv.length) == 0)) {
      lookup_entries[index].key_element = NULL;
      break;
    }
    index = (index + 1) % table->capacity;
  }
}

struct jes_hash_table* jes_init_hash_table(struct jes_context* ctx, void *buffer, uint32_t buffer_size)
{
  struct jes_hash_table* table = buffer;
  memset(buffer, 0, buffer_size);
  table->hash_fn = jes_fnv1a_compound_hash;
  table->size = buffer_size - sizeof(struct jes_hash_table);
  table->capacity = table->size / sizeof(struct jes_hash_entry);
  table->entries = (struct jes_hash_entry*)(table + 1);
  ctx->find_key_fn = jes_find_key_lookup_table;

  return table;
}



