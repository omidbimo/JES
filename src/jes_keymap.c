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

/**
 * @brief Generates a compound hash using the FNV-1a algorithm.
 *
 * This function calculates a hash value based on two inputs:
 * 1. A parent ID (uint32_t)
 * 2. A keyword string
 *
 * The function implements the FNV-1a hashing algorithm, which first processes
 * the bytes of the parent ID and then the characters of the keyword string.
 * The resulting hash uniquely represents the combination of a parent node and a
 * key, allowing keys with identical names to be distinguished when stored in
 * separate objects or namespaces.
 *
 * @param parent_id The ID of the parent node (typically another hash value)
 * @param keyword Pointer to the keyword string to be hashed
 * @param keyword_length Length of the keyword string in bytes
 *
 * @return size_t The calculated hash value
 */
static size_t jes_fnv1a_compound_hash(uint32_t parent_id, const char* keyword, size_t keyword_length) {
  uint8_t* parent_id_bytes = (uint8_t*)&parent_id;
  size_t hash = JES_FNV_OFFSET_BASIS_32BIT;
  size_t index;

  assert(keyword != NULL);

  /* Process each byte of the parent ID */
  for (index = 0; index < sizeof(parent_id); index++) {
    hash ^= parent_id_bytes[index];
    hash *= JES_FNV_PRIME_32BIT;
  }

  /* Process each character of the keyword */
  for (index = 0; index < keyword_length; index++) {
    hash ^= (unsigned char)keyword[index];
    hash *= JES_FNV_PRIME_32BIT;
  }

  return hash;
}

/**
 * @brief Searches for a specific key within the JES context's hash table, where keys are
 * indexed based on their parent object and the key string. It uses a compound hash
 * of the parent object's index and the key string to locate entries, and handles
 * hash collisions using linear probing.
 */
static struct jes_node* jes_find_key_lookup_table(struct jes_context* ctx,
                                                  struct jes_node* parent_object,
                                                  const char* keyword,
                                                  size_t keyword_length)
{
  struct jes_hash_table_context* table = &ctx->hash_table;
  struct jes_node* key = NULL;
  size_t hash;
  size_t index;

  assert(parent_object != NULL);

  hash = table->hash_fn(JES_NODE_INDEX(ctx, parent_object), keyword, keyword_length);
  index = hash % table->capacity;

  /* Linear probing to handle collisions */
  while (table->pool[index].key_element != NULL) {

    if ((table->pool[index].hash == hash) &&
        (table->pool[index].key_element->length == keyword_length) &&
        (memcmp(table->pool[index].key_element->value, keyword, keyword_length) == 0)) {
      key = (struct jes_node*)table->pool[index].key_element;
      break;
    }
    index = (index + 1) % table->capacity;
  }

  return key;
}

jes_status jes_hash_table_add(struct jes_context* ctx, struct jes_node* parent_object, struct jes_node* key)
{
  struct jes_hash_table_context* table = &ctx->hash_table;
  size_t hash = table->hash_fn(JES_NODE_INDEX(ctx, parent_object), key->json_tlv.value, key->json_tlv.length);
  size_t index = hash % table->capacity;
  size_t start_index = index;

  ctx->status = JES_OUT_OF_MEMORY;

  /* Linear probing to find a free slot */
  do {
    /* Empty slot found */
    if (table->pool[index].key_element == NULL) {
      /* Store the key */
      table->pool[index].hash = hash;
      table->pool[index].key_element = (struct jes_element*)key;
      ctx->status = JES_NO_ERROR;
      break;
    }

    /* Key already exists, replace the value */
    if ((table->pool[index].hash == hash) &&
        (key->json_tlv.length = table->pool[index].key_element->length) &&
        (memcmp(table->pool[index].key_element->value, key->json_tlv.value, key->json_tlv.length) == 0)) {
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
  struct jes_hash_table_context* table = &ctx->hash_table;
  struct jes_hash_entry* lookup_entries = table->pool;
  size_t hash;
  size_t index;

  assert(parent_object != NULL);

  hash = table->hash_fn(JES_NODE_INDEX(ctx, parent_object), key->json_tlv.value, key->json_tlv.length);
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

static enum jes_status jes_hash_table_resize(struct jes_context* ctx)
{
  if (ctx->hash_table.size == 0) {
#if 0
    ctx->hash_table.pool = ctx->node_pool.pool + ctx->node_pool.size;
#else
    ctx->hash_table.pool = (struct jes_hash_entry*)(ctx->node_pool.pool + ctx->node_pool.size);
#endif
    ctx->hash_table.size = ctx->node_pool.size / 4;
    ctx->node_pool.size -= ctx->hash_table.size;
    ctx->node_pool.capacity = ctx->node_pool.size / sizeof(*ctx->node_pool.pool);
    ctx->hash_table.capacity = ctx->hash_table.size / sizeof(*ctx->hash_table.pool);
    ctx->hash_table.pool = (struct jes_hash_entry*)((uint8_t*)ctx->node_pool.pool + ctx->node_pool.size);
#ifndef NDEBUG
    printf("\nResized node pool and hashed key pool.");
    printf("\n  Node Pool: size=%d bytes, capacity=%d nodes", ctx->node_pool.size, ctx->node_pool.capacity);
    printf("\n  hashed key Pool: size=%d bytes, capacity=%d entries", ctx->hash_table.size, ctx->hash_table.capacity);
#endif
  }
  return 0;
}

enum jes_status jes_hash_table_init(struct jes_context* ctx)
{
  struct jes_hash_table_context* hash_table_ctx = &ctx->hash_table;
  jes_hash_table_resize(ctx);
  hash_table_ctx->hash_fn = jes_fnv1a_compound_hash;
  ctx->find_key_fn = jes_find_key_lookup_table;
  memset(hash_table_ctx->pool, 0, hash_table_ctx->size);
#if 0



    size_t usable_size = buffer_size - sizeof(struct jes_context) - sizeof(struct jes_hash_table_context);
    size_t node_count = usable_size / (sizeof(struct jes_node) + sizeof(struct jes_hash_entry));

    ctx->capacity = node_count < JES_INVALID_INDEX
                   ? (jes_node_descriptor)node_count
                   : JES_INVALID_INDEX -1;
    ctx->pool_size = ctx->capacity * sizeof(struct jes_node);
    ctx->hash_table = jes_init_hash_table(ctx, (uint8_t*)ctx->node_pool + ctx->pool_size, buffer_size - sizeof(struct jes_context) - ctx->pool_size);
#endif
  return 0;
}



