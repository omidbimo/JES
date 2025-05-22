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
#else
  #define JES_LOG_NODE(...)
#endif

static struct jes_node* jes_allocate(struct jes_context* ctx)
{
  struct jes_node* new_node = NULL;

  assert(ctx != NULL);

  if (ctx->node_count < ctx->capacity) {
    if (ctx->freed) {
      /* Pop the first node from free list */
      new_node = (struct jes_node*)ctx->freed;
      ctx->freed = ctx->freed->next;
    }
    else {
      assert(ctx->next_free < ctx->capacity);
      new_node = &ctx->node_pool[ctx->next_free];
      ctx->next_free++;
    }
    /* Setting node descriptors to their default values. */
    memset(((struct jes_element*)new_node) + 1, 0xFF, sizeof(jes_node_descriptor) * 4);
    ctx->node_count++;
  }
  else {
    ctx->status = JES_OUT_OF_MEMORY;
  }

  return new_node;
}

static void jes_free(struct jes_context* ctx, struct jes_node* node)
{
  struct jes_freed_node* free_node = (struct jes_freed_node*)node;

  assert(ctx != NULL);
  assert(node != NULL);
  assert(node >= ctx->node_pool);
  assert(node < (ctx->node_pool + ctx->capacity));
  assert(ctx->node_count > 0);

  if (ctx->node_count > 0) {
    node->json_tlv.type = JES_UNKNOWN; /* This prevents reuse of deleted nodes. */
    free_node->next = NULL;
    ctx->node_count--;
    /* prepend the node to the free LIFO */
    if (ctx->freed) {
      free_node->next = ctx->freed->next;
    }
    ctx->freed = free_node;
  }
}

bool jes_validate_node(struct jes_context* ctx, struct jes_node* node)
{
  assert(ctx != NULL);
  assert(node != NULL);
  assert(ctx->node_pool != NULL);
  assert(ctx->cookie == JES_CONTEXT_COOKIE);

  if (((void*)node >= (void*)ctx->node_pool) &&
      (((void*)node + sizeof(*node)) <= ((void*)ctx->node_pool + ctx->pool_size))) {
    /* Check if the node is correctly aligned */
    if ((((void*)node - (void*)ctx->node_pool) % sizeof(*node)) == 0) {
      /* Check if the node links are in bound */
      if (((node->parent == JES_INVALID_INDEX) || (node->parent <= ctx->capacity)) &&
          ((node->first_child == JES_INVALID_INDEX) || (node->first_child <= ctx->capacity)) &&
          ((node->last_child == JES_INVALID_INDEX) || (node->last_child <= ctx->capacity)) &&
          ((node->sibling == JES_INVALID_INDEX) || (node->sibling <= ctx->capacity))) {
        return true;
      }
    }
  }

  return false;
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
  for (parent = GET_PARENT(ctx, node);
       parent != NULL && NODE_TYPE(parent) != type;
       parent = GET_PARENT(ctx, parent)) {
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
  for (parent = GET_PARENT(ctx, node);
       parent != NULL && NODE_TYPE(parent) != JES_OBJECT && NODE_TYPE(parent) != JES_ARRAY;
       parent = GET_PARENT(ctx, parent)) {
    /* Empty body - all work done in the loop control expression */
  }

  return parent;
}

struct jes_node* jes_tree_insert_node(struct jes_context* ctx,
                                      struct jes_node* parent, struct jes_node* anchor,
                                      uint16_t type, uint16_t length, const char* value)
{

  struct jes_node *new_node = NULL;
#if 0
  if (parent) {
    if (!anchor) {
      ctx->JES_INVALID_PARAMETER;
      return NULL;
    }
    else if (anchor->parent != JES_NODE_INDEX(ctx, parent) {
      ctx->JES_INVALID_PARAMETER;
      return NULL;
    }
  }
  else {
    ctx->JES_INVALID_PARAMETER;
    return NULL;
  }
#endif
  new_node = jes_allocate(ctx);

  if (new_node) {
    if (parent) {
      new_node->parent = JES_NODE_INDEX(ctx, parent);

      if (anchor) {
        /* We have to insert after an existing node */
        new_node->sibling = anchor->sibling;
        anchor->sibling = JES_NODE_INDEX(ctx, new_node);
        if (parent->last_child == JES_NODE_INDEX(ctx, anchor)) {
          parent->last_child = JES_NODE_INDEX(ctx, new_node);
        }
      }
      else {
        /* There is no node before. Prepend node */
        new_node->sibling = parent->first_child;
        parent->first_child = JES_NODE_INDEX(ctx, new_node);
        if (!HAS_CHILD(parent)) {
          parent->last_child = JES_NODE_INDEX(ctx, new_node);
        }
      }
    }
    else {
      assert(!ctx->root);
      ctx->root = new_node;
    }

    new_node->json_tlv.type = type;
    new_node->json_tlv.length = length;
    new_node->json_tlv.value = value;

    JES_LOG_NODE("\n    + ", JES_NODE_INDEX(ctx, new_node), NODE_TYPE(new_node),
                  new_node->json_tlv.length, new_node->json_tlv.value,
                  new_node->parent, new_node->sibling, new_node->first_child, new_node->last_child, "");
  }

  return new_node;
}

struct jes_node* jes_tree_insert_key_node(struct jes_context* ctx,
                                     struct jes_node* parent_object,
                                     struct jes_node* anchor,
                                     uint16_t keyword_length, const char* keyword)
{
  struct jes_node* new_node = NULL;
  if (parent_object) {
    assert(NODE_TYPE(parent_object) == JES_OBJECT);
  }
  else {
    assert(anchor == NULL);
  }


  /* No duplicate keys in the same object are allowed. */
  struct jes_node* node = ctx->find_key_fn(ctx, parent_object, keyword, keyword_length);
  if (node) {
    ctx->status = JES_DUPLICATE_KEY;
  }
  else
  {
    new_node = jes_tree_insert_node(ctx, parent_object, anchor, JES_KEY, keyword_length, keyword);
  }

  if (new_node) {
#ifdef JES_ENABLE_FAST_KEY_SEARCH
    jes_hash_table_add(ctx, parent_object, new_node);
#endif
  }
  return new_node;
}

/**
 * @brief Deletes a JSON node and all its children from the parse tree.
 *
 * This function performs a post-order traversal to delete a node and its entire subtree.
 */
void jes_tree_delete_node(struct jes_context* ctx, struct jes_node* node)
{
  struct jes_node* iter = node;
  struct jes_node* parent = NULL;

  if (node == NULL) {
    return;
  }

  /* First phase: Delete all children using post-order traversal
   * The deletion process:
   * 1. Traverse to the deepest leaf node in the subtree
   * 2. Delete leaf nodes, working back up the tree
   * 3. Update parent-child and sibling references
   * 4. Remove any hash table entries for keys
   */
  while (true) {

    /* Navigate to the deepest leaf node in this branch */
    while (HAS_CHILD(iter)) {
      iter = GET_FIRST_CHILD(ctx, iter);
    }

    /* If we're back at the original node, all children have been deleted */
    if (iter == node) {
      break;
    }

    /* Get parent before deleting the node */
    parent = GET_PARENT(ctx, iter);
    if (parent == NULL) {
      ctx->status = JES_BROKEN_TREE;
      return;
    }

    /* Update parent's first_child to skip the node being deleted */
    parent->first_child = iter->sibling;

    JES_LOG_NODE("\n    - ", JES_NODE_INDEX(ctx, iter), NODE_TYPE(iter),
                  iter->json_tlv.length, iter->json_tlv.value,
                  iter->parent, iter->sibling, iter->first_child, iter->last_child, "");

    /* Remove key from hash table if applicable */
#ifdef JES_ENABLE_FAST_KEY_SEARCH
    if (NODE_TYPE(iter) == JES_KEY) {
      jes_hash_table_remove(ctx, parent, iter);
    }
#endif

    jes_free(ctx, iter);
    iter = parent;
  }

  /* Second phase: Delete the original node and update parent references */
  parent = GET_PARENT(ctx, node);
  if (parent != NULL) {
    if (parent->first_child == JES_NODE_INDEX(ctx, node)) {
      /* Node is the first child of its parent */
      parent->first_child = node->sibling;

      /* If this was the only child, update last_child as well */
      if (parent->last_child == JES_NODE_INDEX(ctx, node)) {
        parent->last_child = node->sibling;
      }
    }
    else {
      /* Node is not the first child - find the sibling that points to it */
      for (iter = GET_FIRST_CHILD(ctx, parent);
           iter != NULL && iter->sibling != JES_NODE_INDEX(ctx, node);
           iter = GET_SIBLING(ctx, iter)) {
        /* Just find the node whose sibling pointer points to our target node */
      }

      if (iter != NULL) {
        iter->sibling = node->sibling;
        /* Update parent's last_child if necessary */
        if (parent->last_child == JES_NODE_INDEX(ctx, node)) {
          parent->last_child = JES_NODE_INDEX(ctx, iter);
        }
      }
    }
  }

  JES_LOG_NODE("\n    - ", JES_NODE_INDEX(ctx, node), NODE_TYPE(node),
                node->json_tlv.length, node->json_tlv.value,
                node->parent, node->sibling, node->first_child, node->last_child, "");

/* Remove from hash table if it's a key */
#ifdef JES_ENABLE_FAST_KEY_SEARCH
  if (NODE_TYPE(node) == JES_KEY) {
    jes_hash_table_remove(ctx, parent, node);
  }
#endif
  jes_free(ctx, node);
}

struct jes_node* jes_tree_find_key(struct jes_context* ctx,
                                   struct jes_node* parent_object,
                                   const char* keyword,
                                   size_t keyword_lenngth)
{
  struct jes_node* key = NULL;
  struct jes_node* iter = parent_object;

  assert(parent_object != NULL);

  if (NODE_TYPE(parent_object) != JES_OBJECT) {
    ctx->status = JES_UNEXPECTED_ELEMENT;
    return NULL;
  }

  iter = GET_FIRST_CHILD(ctx, iter);

  while ((iter != NULL) && (NODE_TYPE(iter) == JES_KEY)) {
                JES_LOG_NODE("\n    ~ ", JES_NODE_INDEX(ctx, iter),
                      NODE_TYPE(iter), iter->json_tlv.length, iter->json_tlv.value,
                      iter->parent, iter->sibling, iter->first_child, iter->last_child, "");
    if ((iter->json_tlv.length == keyword_lenngth) &&
        (memcmp(iter->json_tlv.value, keyword, keyword_lenngth) == 0)) {
      key = iter;
      break;
    }
    iter = GET_SIBLING(ctx, iter);
  }

  return key;
}