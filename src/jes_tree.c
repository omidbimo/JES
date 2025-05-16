#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "jes.h"
#include "jes_private.h"
#include "jes_logger.h"

#ifndef NDEBUG
  #define JES_LOG_TOKEN jes_log_token
  #define JES_LOG_NODE  jes_log_node
#else
  #define JES_LOG_TOKEN(...)
  #define JES_LOG_NODE(...)
#endif

static struct jes_node* jes_allocate(struct jes_context *ctx)
{
  struct jes_node *new_node = NULL;

  assert(ctx != NULL);

  if (ctx->node_count < ctx->capacity) {
    if (ctx->free) {
      /* Pop the first node from free list */
      new_node = (struct jes_node*)ctx->free;
      ctx->free = ctx->free->next;
    }
    else {
      assert(ctx->index < ctx->capacity);
      new_node = &ctx->node_pool[ctx->index];
      ctx->index++;
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

static void jes_free(struct jes_context *ctx, struct jes_node *node)
{
  struct jes_free_node *free_node = (struct jes_free_node*)node;

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
    if (ctx->free) {
      free_node->next = ctx->free->next;
    }
    ctx->free = free_node;
  }
}

bool jes_validate_node(struct jes_context *ctx, struct jes_node *node)
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

struct jes_node* jes_get_parent_node_of_type(struct jes_context *ctx,
                                              struct jes_node *node,
                                              enum jes_type type)
{
  struct jes_node *parent = NULL;

  assert(ctx != NULL);
  assert(node != NULL);
  assert(ctx->cookie == JES_CONTEXT_COOKIE);

  parent = GET_PARENT(ctx, node);
  while (parent) {
    if (parent->json_tlv.type == type) {
      return parent;
    }
    parent = GET_PARENT(ctx, parent);
  }

  return NULL;
}

struct jes_node* jes_get_parent_node_of_type_object_or_array(struct jes_context *ctx,
                                                             struct jes_node *node)
{
  struct jes_node *parent = NULL;

  assert(ctx != NULL);
  assert(node != NULL);
  assert(ctx->cookie == JES_CONTEXT_COOKIE);

  parent = GET_PARENT(ctx, node);
  while (parent) {
    if ((parent->json_tlv.type == JES_OBJECT) ||
        (parent->json_tlv.type == JES_ARRAY)) {
      return parent;
    }
    parent = GET_PARENT(ctx, parent);
  }

  return parent;
}

struct jes_node* jes_append_node(struct jes_context *ctx, struct jes_node *parent,
                                 uint16_t type, uint16_t length, const char *value)
{
  struct jes_node *new_node = jes_allocate(ctx);

  if (new_node) {
    if (parent) {
      new_node->parent = JES_GET_NODE_INDEX(ctx, parent);

      if (HAS_FIRST_CHILD(parent)) {
        struct jes_node *last = &ctx->node_pool[parent->last_child];
        last->sibling = JES_GET_NODE_INDEX(ctx, new_node);
      }
      else {
        parent->first_child = JES_GET_NODE_INDEX(ctx, new_node);
      }
      parent->last_child = JES_GET_NODE_INDEX(ctx, new_node);
    }
    else {
      assert(!ctx->root);
      ctx->root = new_node;
    }

    new_node->json_tlv.type = type;
    new_node->json_tlv.length = length;
    new_node->json_tlv.value = value;
  }

  if (new_node) {
    JES_LOG_NODE("\n    + ", JES_GET_NODE_INDEX(ctx, new_node), new_node->json_tlv.type,
                  new_node->json_tlv.length, new_node->json_tlv.value,
                  new_node->parent, new_node->sibling, new_node->first_child, "");
  }

  return new_node;
}

struct jes_node* jes_add_node_after(struct jes_context* ctx,
                                    struct jes_node* parent, struct jes_node* anchor_node,
                                    uint16_t type, uint16_t length, const char* value)
{
  struct jes_node *new_node = NULL;
#if 0
  if (parent) {
    if (!anchor_node) {
      ctx->JES_INVALID_PARAMETER;
      return NULL;
    }
    else if (anchor_node->parent != JES_GET_NODE_INDEX(ctx, parent) {
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
      new_node->parent = JES_GET_NODE_INDEX(ctx, parent);

      if (anchor_node) {
        /* We have to insert after an existing node */
        new_node->sibling = anchor_node->sibling;
        anchor_node->sibling = JES_GET_NODE_INDEX(ctx, new_node);
        if (parent->last_child == JES_GET_NODE_INDEX(ctx, anchor_node)) {
          parent->last_child = JES_GET_NODE_INDEX(ctx, new_node);
        }
      }
      else {
        /* There is no node before. Prepend node */
        new_node->sibling = parent->first_child;
        parent->first_child = JES_GET_NODE_INDEX(ctx, new_node);
        if (!HAS_LAST_CHILD(parent)) {
          parent->last_child = JES_GET_NODE_INDEX(ctx, new_node);
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

    JES_LOG_NODE("\n    + ", JES_GET_NODE_INDEX(ctx, new_node), new_node->json_tlv.type,
                  new_node->json_tlv.length, new_node->json_tlv.value,
                  new_node->parent, new_node->sibling, new_node->first_child, "");
  }

  return new_node;
}

void jes_add_object_node(struct jes_context *ctx)
{
  struct jes_node *new_node = NULL;
  new_node = jes_append_node(ctx, ctx->iter, JES_OBJECT, ctx->token.length, &ctx->json_data[ctx->token.offset]);
  if (new_node) {
    ctx->iter = new_node;
    JES_LOG_NODE("\n    + ", ctx->iter - ctx->node_pool, ctx->iter->json_tlv.type,
                  ctx->iter->json_tlv.length, ctx->iter->json_tlv.value,
                  ctx->iter->parent, ctx->iter->sibling, ctx->iter->first_child, "");
  }
}

struct jes_node* jes_add_key_node_after(struct jes_context *ctx,
                                        struct jes_node *parent_object,
                                        struct jes_node *anchor,
                                        uint16_t keyword_length, const char *keyword)
{
  struct jes_node *new_node = NULL;
  if (parent_object) {
    assert(parent_object->json_tlv.type == JES_OBJECT);
  }
  else {
    assert(anchor == NULL);
  }


  /* No duplicate keys in the same object are allowed. */
  struct jes_node *node = ctx->find_key_fn(ctx, parent_object, keyword, keyword_length);
  if (node) {
    ctx->status = JES_DUPLICATE_KEY;
  }
  else
  {
    new_node = jes_add_node_after(ctx, parent_object, anchor, JES_KEY, keyword_length, keyword);
  }

  if (new_node) {
#ifdef JES_ENABLE_FAST_KEY_SEARCH
    jes_hash_table_add(ctx, parent_object, new_node);
#endif
    JES_LOG_NODE("\n    + ", JES_GET_NODE_INDEX(ctx, new_node), new_node->json_tlv.type,
                  new_node->json_tlv.length, new_node->json_tlv.value,
                  new_node->parent, new_node->sibling, new_node->first_child, "");
  }
  return new_node;
}

void jes_add_array_node(struct jes_context *ctx)
{
  struct jes_node *new_node = NULL;
  new_node = jes_append_node(ctx, ctx->iter, JES_ARRAY, ctx->token.length, &ctx->json_data[ctx->token.offset]);
  if (new_node) {
    ctx->iter = new_node;
    JES_LOG_NODE("\n    + ", ctx->iter - ctx->node_pool, ctx->iter->json_tlv.type,
                  ctx->iter->json_tlv.length, ctx->iter->json_tlv.value,
                  ctx->iter->parent, ctx->iter->sibling, ctx->iter->first_child, "");
  }
}

void jes_add_value_node(struct jes_context *ctx, enum jes_type value_type)
{
  struct jes_node *new_node = NULL;
  new_node = jes_append_node(ctx, ctx->iter, value_type, ctx->token.length, &ctx->json_data[ctx->token.offset]);

  if (new_node) {
    ctx->iter = new_node;
    JES_LOG_NODE("\n    + ", ctx->iter - ctx->node_pool, ctx->iter->json_tlv.type,
                  ctx->iter->json_tlv.length, ctx->iter->json_tlv.value,
                  ctx->iter->parent, ctx->iter->sibling, ctx->iter->first_child, "");
  }
}

/* To delete a node and its whole branch */
void jes_delete_node(struct jes_context *ctx, struct jes_node *node)
{
  struct jes_node *iter = node;

  if (node == NULL) {
    return;
  }

  while (true) {
    /* Reaching the last child */
    while (HAS_FIRST_CHILD(iter)) { iter = UNSAFE_GET_FIRST_CHILD(ctx, iter); }
    assert(iter != NULL);
    /* Do not delete the node yet */
    if (iter == node) {
      break;
    }

    /* Set the parent first child link to node's possible sibling. So we don't lost the branch. */
    assert(HAS_PARENT(iter));
    UNSAFE_GET_PARENT(ctx, iter)->first_child = iter->sibling;
    JES_LOG_NODE("\n    - ", JES_GET_NODE_INDEX(ctx, iter), iter->json_tlv.type,
                  iter->json_tlv.length, iter->json_tlv.value,
                  iter->parent, iter->sibling, iter->first_child, "");

#ifdef JES_ENABLE_FAST_KEY_SEARCH
    if (iter->json_tlv.type == JES_KEY) {
      jes_hash_table_remove(ctx, UNSAFE_GET_PARENT(ctx, iter), iter);
    }
#endif
    jes_free(ctx, iter);
    iter = UNSAFE_GET_PARENT(ctx, iter);
  }

  /* All sub-elements are deleted. To delete the node itself, all parent and sibling links need to be maintained. */
  iter = GET_PARENT(ctx, node);
  if (iter) {
    /* The node has parent. re-link if node is parent's first child */
    if (UNSAFE_GET_FIRST_CHILD(ctx, iter) == node) {
      iter->first_child = node->sibling;
    }
    else {
      /* Node is not parent's first child. Iterate all children from the first child to reach the node and restore the links. */
      for (iter = UNSAFE_GET_FIRST_CHILD(ctx, iter); iter != NULL; iter = UNSAFE_GET_SIBLING(ctx, iter)) {
        /* If node is iter's next sibling, update iter's sibling then the node is ready to be freed. */
        if (UNSAFE_GET_SIBLING(ctx, iter) == node) {
          iter->sibling = node->sibling;
          break;
        }
      }
    }
  }

  JES_LOG_NODE("\n    - ", JES_GET_NODE_INDEX(ctx, node), node->json_tlv.type,
                node->json_tlv.length, node->json_tlv.value,
                node->parent, node->sibling, node->first_child, "");

#ifdef JES_ENABLE_FAST_KEY_SEARCH
  if (iter->json_tlv.type == JES_KEY) {
    jes_hash_table_remove(ctx, UNSAFE_GET_PARENT(ctx, iter), iter);
  }
#endif
  jes_free(ctx, node);
}

static struct jes_node* jes_find_key(struct jes_context* ctx,
                                     struct jes_node* parent_object,
                                     const char* keyword,
                                     size_t keyword_lenngth)
{
  struct jes_node* key = NULL;
  struct jes_node* iter = parent_object;

  assert(parent_object != NULL);

  if (parent_object->json_tlv.type != JES_OBJECT) {
    ctx->status = JES_UNEXPECTED_ELEMENT;
    return NULL;
  }

  iter = GET_FIRST_CHILD(ctx, iter);

  while ((iter != NULL) && (iter->json_tlv.type == JES_KEY)) {
                JES_LOG_NODE("\n    ~ ", JES_GET_NODE_INDEX(ctx, iter),
                      iter->json_tlv.type, iter->json_tlv.length, iter->json_tlv.value,
                      iter->parent, iter->sibling, iter->first_child, "");
    if ((iter->json_tlv.length == keyword_lenngth) &&
        (memcmp(iter->json_tlv.value, keyword, keyword_lenngth) == 0)) {
      key = iter;
      break;
    }
    iter = GET_SIBLING(ctx, iter);
  }

  return key;
}