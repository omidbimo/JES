#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "jes.h"
#include "jes_private.h"
#include "jes_logger.h"

#ifndef NDEBUG
  #define JES_LOG_NODE  jes_log_node
  #define JES_LOG(...) printf(__VA_ARGS__)
#else
  #define JES_LOG_NODE(...)
  #define JES_LOG(...)
#endif

static struct jes_node* jes_tree_find_key(struct jes_context*, struct jes_node*, const char*, size_t);

static struct jes_node* jes_allocate(struct jes_context* ctx)
{
  struct jes_node_mng_context* mng_ctx = NULL;
  struct jes_node* new_node = NULL;

  assert(ctx != NULL);
  mng_ctx = &ctx->node_mng;

  if (mng_ctx->node_count < mng_ctx->capacity) {
    if (mng_ctx->freed) {
      /* Pop the first node from free list */
      new_node = (struct jes_node*)mng_ctx->freed;
      mng_ctx->freed = mng_ctx->freed->next;
    }
    else {
      assert(mng_ctx->next_free < mng_ctx->capacity);
      new_node = &mng_ctx->pool[mng_ctx->next_free];
      mng_ctx->next_free++;
    }
    /* Setting node descriptors to their default values. */
    new_node->parent = JES_INVALID_INDEX;
    new_node->sibling = JES_INVALID_INDEX;
    new_node->first_child = JES_INVALID_INDEX;
    new_node->last_child = JES_INVALID_INDEX;

    mng_ctx->node_count++;
  }
  else {
    ctx->status = JES_OUT_OF_MEMORY;
  }

  return new_node;
}

static void jes_free(struct jes_context* ctx, struct jes_node* node)
{
  struct jes_node_mng_context* mng_ctx = NULL;
  struct jes_freed_node* free_node = (struct jes_freed_node*)node;

  assert(ctx != NULL);
  assert(node != NULL);

  mng_ctx = &ctx->node_mng;

  assert(node >= mng_ctx->pool);
  assert(node < (mng_ctx->pool + mng_ctx->capacity));
  assert(mng_ctx->node_count > 0);

  if (mng_ctx->node_count > 0) {
    node->json_tlv.type = JES_UNKNOWN; /* This prevents reuse of deleted nodes. */
    free_node->next = NULL;
    mng_ctx->node_count--;
    /* prepend the node to the free LIFO */
    if (mng_ctx->freed) {
      free_node->next = mng_ctx->freed;
    }
    mng_ctx->freed = free_node;
  }
}

bool jes_validate_node(struct jes_context* ctx, struct jes_node* node)
{
  struct jes_node_mng_context* mng_ctx = NULL;

  assert(ctx != NULL);
  assert(node != NULL);
  assert(ctx->cookie == JES_CONTEXT_COOKIE);

  mng_ctx = &ctx->node_mng;
  if (((uintptr_t)node >= (uintptr_t)(mng_ctx->pool)) &&
      (((uintptr_t)node + sizeof(*node)) <= ((uintptr_t)(mng_ctx->pool) + mng_ctx->size))) {
    /* Check if the node is correctly aligned */
    if ((((void*)node - (void*)(mng_ctx->pool)) % sizeof(*node)) == 0) {
      /* Check if the node links are in bound */
      if (((node->parent == JES_INVALID_INDEX)      || (node->parent < mng_ctx->capacity))      &&
          ((node->first_child == JES_INVALID_INDEX) || (node->first_child < mng_ctx->capacity)) &&
          ((node->last_child == JES_INVALID_INDEX)  || (node->last_child < mng_ctx->capacity))  &&
          ((node->sibling == JES_INVALID_INDEX)     || (node->sibling < mng_ctx->capacity))) {
        return true;
      }
    }
  }

  return false;
}

struct jes_node* jes_tree_get_parent_node(struct jes_context* ctx,
                                          struct jes_node* node)
{
  assert(ctx != NULL);
  return GET_PARENT(ctx->node_mng, node);
}

struct jes_node* jes_tree_get_child_node(struct jes_context* ctx,
                                          struct jes_node* node)
{
  assert(ctx != NULL);
  return GET_FIRST_CHILD(ctx->node_mng, node);
}

struct jes_node* jes_tree_get_sibling_node(struct jes_context* ctx,
                                          struct jes_node* node)
{
  assert(ctx != NULL);
  return GET_SIBLING(ctx->node_mng, node);
}

struct jes_node* jes_tree_get_parent_node_by_type(struct jes_context* ctx,
                                                  struct jes_node* node,
                                                  enum jes_type type)
{
  struct jes_node *parent = NULL;

  assert(ctx != NULL);
  assert(node != NULL);
  assert(ctx->cookie == JES_CONTEXT_COOKIE);

  /* Traverse up the tree until we find a parent of the requested type, or reach NULL */
  for (parent = GET_PARENT(ctx->node_mng, node);
       parent != NULL && NODE_TYPE(parent) != type;
       parent = GET_PARENT(ctx->node_mng, parent)) {
    /* Empty body - all work done in the loop control expression */
  }

  return parent;
}

struct jes_node* jes_tree_get_container_parent_node(struct jes_context* ctx,
                                                    struct jes_node* node)
{
  struct jes_node* parent = NULL;

  assert(ctx != NULL);
  assert(node != NULL);
  assert(ctx->cookie == JES_CONTEXT_COOKIE);

  /* Traverse up the tree until we find an object or array parent, or reach NULL */
  for (parent = GET_PARENT(ctx->node_mng, node);
       parent != NULL && NODE_TYPE(parent) != JES_OBJECT && NODE_TYPE(parent) != JES_ARRAY;
       parent = GET_PARENT(ctx->node_mng, parent)) {
    /* Empty body - all work done in the loop control expression */
  }

  return parent;
}

struct jes_node* jes_tree_insert_node(struct jes_context* ctx,
                                      struct jes_node* parent, struct jes_node* anchor,
                                      uint16_t type, uint16_t length, const char* value)
{
  struct jes_node *new_node = NULL;

  new_node = jes_allocate(ctx);

  if (new_node) {
    if (parent) {
      new_node->parent = JES_NODE_INDEX(ctx->node_mng, parent);

      if (anchor) {
        /* We have to insert after an existing node */
        new_node->sibling = anchor->sibling;
        anchor->sibling = JES_NODE_INDEX(ctx->node_mng, new_node);
        if (parent->last_child == JES_NODE_INDEX(ctx->node_mng, anchor)) {
          parent->last_child = JES_NODE_INDEX(ctx->node_mng, new_node);
        }
      }
      else {
        /* There is no node before. Prepend node */
        new_node->sibling = parent->first_child;
        if (!HAS_CHILD(parent)) {
          parent->last_child = JES_NODE_INDEX(ctx->node_mng, new_node);
        }
        parent->first_child = JES_NODE_INDEX(ctx->node_mng, new_node);
      }
    }
    else {
      assert(ctx->node_mng.root == NULL);
      ctx->node_mng.root = new_node;
    }

    new_node->json_tlv.type = type;
    new_node->json_tlv.length = length;
    new_node->json_tlv.value = value;
#if defined(JES_ENABLE_PARSER_NODE_LOG)
    JES_LOG_NODE("    + ", JES_NODE_INDEX(ctx->node_mng, new_node), NODE_TYPE(new_node),
                  new_node->json_tlv.length, new_node->json_tlv.value,
                  new_node->parent, new_node->sibling, new_node->first_child, new_node->last_child, "\n");
#endif
  }

  return new_node;
}

struct jes_node* jes_tree_insert_key_node(struct jes_context* ctx,
                                          struct jes_node* parent_object,
                                          struct jes_node* anchor,
                                          uint16_t keyword_length,
                                          const char* keyword)
{
  struct jes_node* new_node = NULL;
  struct jes_node* duplicate_key_node = NULL;

  if (parent_object) {
    assert(NODE_TYPE(parent_object) == JES_OBJECT);
  }
  else {
    assert(anchor == NULL);
  }

  /* No duplicate keys in the same object are allowed. */
  duplicate_key_node = ctx->node_mng.find_key_fn(ctx, parent_object, keyword, keyword_length);

  if (duplicate_key_node) {
    ctx->status = JES_DUPLICATE_KEY;
  }
  else
  {
    new_node = jes_tree_insert_node(ctx, parent_object, anchor, JES_KEY, keyword_length, keyword);
  }

  if ((new_node) && (JES_SEARCH_HASHED == ctx->mode)) {
    assert(ctx->hash_table.add_fn != NULL);
    ctx->hash_table.add_fn(ctx, parent_object, new_node);
  }

  return new_node;
}

static struct jes_node* jes_get_leaf(struct jes_context* ctx,
                                     struct jes_node* parent)
{
  struct jes_node* leaf = NULL;

  assert(parent != NULL);

  do {
    leaf = GET_FIRST_CHILD(ctx->node_mng, parent);
    parent = leaf;
  } while (HAS_CHILD(parent));

  return leaf;
}

static struct jes_node* jes_get_left_sibling(struct jes_context* ctx,
                                             struct jes_node* node)
{
  struct jes_node* prev_sibling = NULL;
  struct jes_node* parent = NULL;
  struct jes_node* iter = NULL;
  assert(node != NULL);

  parent = GET_PARENT(ctx->node_mng, node);
  if (parent != NULL) {
    iter = GET_FIRST_CHILD(ctx->node_mng, parent);

    while ((iter != NULL) && (iter != node)) {
      prev_sibling = iter;
      iter = GET_SIBLING(ctx->node_mng, iter);
    }
  }
  return prev_sibling;
}

/**
 * @brief Deletes a JSON node and all its children from the parse tree.
 *        Iterative bottom-up leaf removal.
 */
void jes_tree_delete_node(struct jes_context* ctx, struct jes_node* node)
{
  struct jes_node* to_remove = NULL;
  struct jes_node* parent = node;

  if (node == NULL) {
    return;
  }

  /* First phase: Delete all children */
  while (true) {
    to_remove = jes_get_leaf(ctx, parent);

    if (to_remove == NULL) {
      /* Current node has no leaves anymore. Move to its parent and repeat the cycle. */

      if (parent == node) {
        /* Reached the original node requested to be removed. break phase 1 and
           proceed with phase 2. */
        break;
      }
      /* Start traversing from node again */
      parent = node;
      continue;
    }

    /* Get parent before deleting the node */
    parent = GET_PARENT(ctx->node_mng, to_remove);
    assert(parent != NULL);
    if (parent == NULL) {
      ctx->status = JES_BROKEN_TREE;
      return;
    }

    /* Detach node from its parent */
    if (parent->first_child == parent->last_child) {
      parent->last_child = JES_INVALID_INDEX;
    }
    parent->first_child = to_remove->sibling;

#if defined(JES_ENABLE_PARSER_NODE_LOG)
    JES_LOG_NODE("    - ", JES_NODE_INDEX(ctx->node_mng, to_remove),
                  NODE_TYPE(to_remove),
                  to_remove->json_tlv.length, to_remove->json_tlv.value,
                  to_remove->parent, to_remove->sibling,
                  to_remove->first_child, to_remove->last_child, "\n");
#endif

    /* Remove key from hash table if applicable */
    if (JES_SEARCH_HASHED == ctx->mode) {
      if (NODE_TYPE(to_remove) == JES_KEY) {
        assert(ctx->hash_table.remove_fn != NULL);
        ctx->hash_table.remove_fn(ctx, parent, to_remove);
      }
    }

    jes_free(ctx, to_remove);
  }

  /* Second phase: Delete the original node and update parent references */
  parent = GET_PARENT(ctx->node_mng, node);
  if (parent != NULL) {
    struct jes_node* prev_sibling = jes_get_left_sibling(ctx, node);

    if (prev_sibling) {
      assert(prev_sibling->parent == JES_NODE_INDEX(ctx->node_mng, parent));
      /* Node is not the first child of its parent */
      prev_sibling->sibling = node->sibling;

      if (parent->last_child == JES_NODE_INDEX(ctx->node_mng, node)) {
        /* Node is the last child of its parent */
        parent->last_child = JES_NODE_INDEX(ctx->node_mng, prev_sibling);
      }
    }
    else {
      /* Node is the first child of its parent */
      if (parent->last_child == parent->first_child) {
        /* parent has only one single child */
        parent->last_child = JES_INVALID_INDEX;
      }
      parent->first_child = node->sibling;
    }
  }

#if defined(JES_ENABLE_PARSER_NODE_LOG)
  JES_LOG_NODE("    - ", JES_NODE_INDEX(ctx->node_mng, node), NODE_TYPE(node),
                node->json_tlv.length, node->json_tlv.value,
                node->parent, node->sibling, node->first_child, node->last_child, "\n");
#endif
  /* Remove from hash table if it's a key */
  if (JES_SEARCH_HASHED == ctx->mode) {
    if (NODE_TYPE(node) == JES_KEY) {
      assert(ctx->hash_table.remove_fn != NULL);
      ctx->hash_table.remove_fn(ctx, parent, node);
    }
  }

  jes_free(ctx, node);
}

static struct jes_node* jes_tree_find_key(struct jes_context* ctx,
                                          struct jes_node* parent_object,
                                          const char* keyword,
                                          size_t keyword_length)
{
  struct jes_node* key = NULL;
  struct jes_node* iter = parent_object;

  assert(parent_object != NULL);

  if (NODE_TYPE(parent_object) != JES_OBJECT) {
    ctx->status = JES_UNEXPECTED_ELEMENT;
    return NULL;
  }

  iter = GET_FIRST_CHILD(ctx->node_mng, iter);

  while ((iter != NULL) && (NODE_TYPE(iter) == JES_KEY)) {
    if ((iter->json_tlv.length == keyword_length) &&
        (memcmp(iter->json_tlv.value, keyword, keyword_length) == 0)) {
      key = iter;
      break;
    }
    iter = GET_SIBLING(ctx->node_mng, iter);
  }

  return key;
}

jes_status jes_tree_resize(struct jes_node_mng_context* ctx, void *buffer, size_t buffer_size)
{
  ctx->pool = buffer;
  ctx->size = buffer_size;

  ctx->capacity = (ctx->size / sizeof(struct jes_node)) < JES_INVALID_INDEX
                ? ctx->size / sizeof(struct jes_node)
                : JES_INVALID_INDEX -1;

  return ctx->capacity == 0 ? JES_BUFFER_TOO_SMALL : JES_NO_ERROR;
}

void jes_tree_reset(struct jes_node_mng_context* ctx)
{
  ctx->node_count = 0;
  ctx->next_free = 0;
  ctx->freed = NULL;
  ctx->root = NULL;
}

jes_status jes_tree_init(struct jes_context* ctx, void *buffer, size_t buffer_size)
{
  struct jes_node_mng_context* mng_ctx = &ctx->node_mng;

  jes_tree_reset(&ctx->node_mng);

  if (JES_SEARCH_LINEAR == ctx->mode) {
    ctx->node_mng.find_key_fn = jes_tree_find_key;
  }
  else {
    ctx->node_mng.find_key_fn = jes_hash_table_find_key;
  }

  return jes_tree_resize(&ctx->node_mng, buffer, buffer_size);
}
