#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "jes.h"
#include "jes_private.h"
#include "jes_logger.h"

#ifdef JES_ENABLE_FAST_KEY_SEARCH
  #include "jes_keymap.h"
#endif

#ifdef JES_USE_32BIT_NODE_DESCRIPTOR
  #define JES_INVALID_INDEX 0xFFFFFFFF
#else
  #define JES_INVALID_INDEX 0xFFFF
#endif

#define UPDATE_TOKEN(tok, type_, offset_, size_) \
  tok.type = type_; \
  tok.offset = offset_; \
  tok.length = size_;

#define IS_SPACE(c) ((c==' ') || (c=='\t') || (c=='\r') || (c=='\n') || (c=='\f'))
#define IS_NEW_LINE(c) ((c=='\r') || (c=='\n'))
#define IS_DIGIT(c) ((c >= '0') && (c <= '9'))
#define IS_ESCAPE(c) ((c=='\\') || (c=='\"') || (c=='\/') || (c=='\b') || \
                      (c=='\f') || (c=='\n') || (c=='\r') || (c=='\t') || (c == '\u'))
#define LOOK_AHEAD(ctx_) (((ctx_->offset + 1) < ctx_->json_size) ? ctx_->json_data[ctx_->offset + 1] : '\0')

#define JES_CONTEXT_COOKIE 0xABC09DEF
#define JES_IS_INITIATED(ctx_) (ctx_->cookie == JES_CONTEXT_COOKIE)

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

static bool jes_validate_node(struct jes_context *ctx, struct jes_node *node)
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

static struct jes_node* jes_get_parent_node_of_type(struct jes_context *ctx,
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

static struct jes_node* jes_get_parent_node_of_type_object_or_array(struct jes_context *ctx,
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

static struct jes_node* jes_append_node(struct jes_context *ctx, struct jes_node *parent,
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

static struct jes_node* jes_add_node_after(struct jes_context* ctx,
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

/* To delete a node and its whole branch */
static void jes_delete_node(struct jes_context *ctx, struct jes_node *node)
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

static inline bool jes_get_delimiter_token(struct jes_context *ctx,
                                          char ch, struct jes_token *token)
{
  bool is_symbolic_token = true;

  switch (ch) {
    case '\0': UPDATE_TOKEN((*token), JES_TOKEN_EOF, ctx->offset, 1);             break;
    case '{':  UPDATE_TOKEN((*token), JES_TOKEN_OPENING_BRACE, ctx->offset, 1);   break;
    case '}':  UPDATE_TOKEN((*token), JES_TOKEN_CLOSING_BRACE, ctx->offset, 1);   break;
    case '[':  UPDATE_TOKEN((*token), JES_TOKEN_OPENING_BRACKET, ctx->offset, 1); break;
    case ']':  UPDATE_TOKEN((*token), JES_TOKEN_CLOSING_BRACKET, ctx->offset, 1); break;
    case ':':  UPDATE_TOKEN((*token), JES_TOKEN_COLON, ctx->offset, 1);           break;
    case ',':  UPDATE_TOKEN((*token), JES_TOKEN_COMMA, ctx->offset, 1);           break;
    default:   is_symbolic_token = false;                                         break;
  }

  return is_symbolic_token;
}

static inline bool jes_is_delimiter_token(char ch)
{
  return ((ch == '\0') || (ch == '{') || (ch == '}') || (ch == '[') || (ch == ']') || (ch == ':') || (ch == ','));
}

/* Token type is NUMBER. Try to feed it with more symbols. */
static inline bool jes_exponent_number_tokenizer(struct jes_context *ctx,
                                            char ch, struct jes_token *token)
{
  bool end_of_token = false;

  if (IS_DIGIT(ch)) {
    token->length++;
    if (!IS_DIGIT(LOOK_AHEAD(ctx))) {
      end_of_token = true;
    }
  }
  else if ((ch == '+') || (ch == '-')) {
    token->length++;
    ch = LOOK_AHEAD(ctx);
    if (!IS_DIGIT(ch)) {
      token->type = JES_TOKEN_INVALID;
      end_of_token = true;
    }
  }
  else {
    token->type = JES_TOKEN_INVALID;
    end_of_token = true;
  }

  return end_of_token;
}

/* Token type is NUMBER. Try to feed it with more symbols. */
static inline bool jes_decimal_fraction_number_tokenizer(struct jes_context *ctx,
                                            char ch, struct jes_token *token)
{
  bool end_of_token = false;

  if (IS_DIGIT(ch)) {
    token->length++;
    ch = LOOK_AHEAD(ctx);
    if (!IS_DIGIT(ch)) {
      if ((ch != 'e') && (ch != 'E')) {
        end_of_token = true;
      }
    }
  }
  else if ((ch == 'e') || (ch == 'E')) {
    token->length++;
    ctx->number_tokenizer_fn = jes_exponent_number_tokenizer;
  }
  else {
    token->type = JES_TOKEN_INVALID;
    end_of_token = true;
  }

  return end_of_token;
}

/* Token type is NUMBER. Try to feed it with more symbols. */
static inline bool jes_integer_tokenizer(struct jes_context *ctx,
                                         char ch, struct jes_token *token)
{
  bool end_of_token = false;

  if (IS_DIGIT(ch)) {
    /* Integers with leading zeros are invalid JSON numbers */
    if ((token->length == 1) && (ctx->json_data[token->offset] == '0')) {
      token->type = JES_TOKEN_INVALID;
      end_of_token = true;
    }
    token->length++;
    ch = LOOK_AHEAD(ctx);
    if (!IS_DIGIT(ch)) {
      if ((ch != '.') && (ch != 'e') && (ch != 'E')) {
        end_of_token = true;
      }
    }
  }
  else if (ch == '.') {
    token->length++;
    ctx->number_tokenizer_fn = jes_decimal_fraction_number_tokenizer;
  }
  else if ((ch == 'e') || (ch == 'E')) {
    token->length++;
    ctx->number_tokenizer_fn = jes_exponent_number_tokenizer;
  }
  else {
    token->type = JES_TOKEN_INVALID;
    end_of_token = true;
  }

  return end_of_token;
}

static inline bool jes_get_specific_token(struct jes_context *ctx,
                          struct jes_token *token, char *cmp_str, uint16_t len)
{
  bool tokenizing_completed = false;
  token->length++;
  if (token->length == len) {
    if (strncmp(&ctx->json_data[token->offset], cmp_str, len) != 0) {
      token->type = JES_TOKEN_INVALID;
    }
    tokenizing_completed = true;
  }
  return tokenizing_completed;
}

static jes_status jes_get_token(struct jes_context *ctx)
{
  struct jes_token token = { 0 };

  while (true) {

    if ((++ctx->offset >= ctx->json_size) || (ctx->json_data[ctx->offset] == '\0')) {
      /* End of data. If token is incomplete, mark it as invalid. */
      if (token.type) {
        ctx->status = JES_UNEXPECTED_EOF;
      }
      break;
    }

    char ch = ctx->json_data[ctx->offset];

    if (!token.type) {

      if (jes_get_delimiter_token(ctx, ch, &token)) {
        break;
      }

      if (ch == '\"') {
        /* '\"' won't be a part of token. Use offset of next symbol */
        UPDATE_TOKEN(token, JES_TOKEN_STRING, ctx->offset + 1, 0);
        continue;
      }

      if (IS_DIGIT(ch)) {
        UPDATE_TOKEN(token, JES_TOKEN_NUMBER, ctx->offset, 1);
        ctx->number_tokenizer_fn = jes_integer_tokenizer;
        /* Unlike STRINGs, there are symbols for NUMBERs to indicate the
           end of number data. To avoid consuming non-NUMBER characters, take a look ahead
           and stop the process if found of non-numeric symbols. */
        ch = LOOK_AHEAD(ctx);
        if (jes_is_delimiter_token(ch) || IS_SPACE(ch)) {
          break;
        }
        continue;
      }

      if (ch == '-') {
        if (IS_DIGIT(LOOK_AHEAD(ctx))) {
          UPDATE_TOKEN(token, JES_TOKEN_NUMBER, ctx->offset, 1);
          ctx->number_tokenizer_fn = jes_integer_tokenizer;
          continue;
        }
        token.type = JES_TOKEN_INVALID;
        break;
      }

      if (ch == 't') {
        UPDATE_TOKEN(token, JES_TOKEN_TRUE, ctx->offset, 1);
        continue;
      }

      if (ch == 'f') {
        UPDATE_TOKEN(token, JES_TOKEN_FALSE, ctx->offset, 1);
        continue;
      }

      if (ch == 'n') {
        UPDATE_TOKEN(token, JES_TOKEN_NULL, ctx->offset, 1);
        continue;
      }

      /* Skipping space symbols including: space, tab, carriage return */
      if (IS_SPACE(ch)) {
        if (IS_NEW_LINE(ch)) {
          ctx->line_number++;
        }
        continue;
      }

      UPDATE_TOKEN(token, JES_TOKEN_INVALID, ctx->offset, 1);
      break;
    }
    else if (token.type == JES_TOKEN_STRING) {
      if (ch == '\"') { /* End of STRING. '\"' symbol isn't a part of token. */
        break;
      }
      if ((ch =='\b') || (ch =='\f') || (ch =='\n') || (ch =='\r') || (ch =='\t')) {
        ctx->status = JES_UNEXPECTED_SYMBOL;
        break;
      }
      token.length++;
      continue;
    }
    else if (token.type == JES_TOKEN_NUMBER) {
      if (ctx->number_tokenizer_fn(ctx, ch, &token)) {
        /* There are no more symbols to consume as a number. Deliver the token. */
        break;
      }
      continue;
    }
    else if (token.type == JES_TOKEN_TRUE) {
      if (jes_get_specific_token(ctx, &token, "true", sizeof("true") - 1)) {
        break;
      }
      continue;
    }
    else if (token.type == JES_TOKEN_FALSE) {
      if (jes_get_specific_token(ctx, &token, "false", sizeof("false") - 1)) {
        break;
      }
      continue;
    }
    else if (token.type == JES_TOKEN_NULL) {
      if (jes_get_specific_token(ctx, &token, "null", sizeof("null") - 1)) {
        break;
      }
      continue;
    }
    token.type = JES_TOKEN_INVALID;
    break;
  }

  JES_LOG_TOKEN(token.type, token.offset, token.length, &ctx->json_data[token.offset]);

  ctx->token = token;
  return ctx->status;
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

static void jes_parser_add_object(struct jes_context *ctx)
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

static struct jes_node* jes_add_key_node_after(struct jes_context *ctx,
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

static void jes_parser_add_array(struct jes_context *ctx)
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

static void jes_parser_add_value(struct jes_context *ctx, enum jes_type value_type)
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

struct jes_context* jes_init(void *buffer, uint32_t buffer_size)
{
  if (buffer_size < sizeof(struct jes_context)) {
    return NULL;
  }

  struct jes_context *ctx = buffer;
  memset(ctx, 0, sizeof(*ctx));

  ctx->status = JES_NO_ERROR;
  ctx->node_count = 0;

  ctx->json_data = NULL;
  ctx->json_size = 0;
  ctx->offset = (uint32_t)-1;
  ctx->index = 0;
  ctx->node_pool = (struct jes_node*)(ctx + 1);
#ifndef JES_ENABLE_FAST_KEY_SEARCH
  ctx->pool_size = buffer_size - (uint32_t)(sizeof(struct jes_context));
  ctx->capacity = (ctx->pool_size / sizeof(struct jes_node)) < JES_INVALID_INDEX
                 ? (jes_node_descriptor)(ctx->pool_size / sizeof(struct jes_node))
                 : JES_INVALID_INDEX -1;
  ctx->hash_table = NULL;
  ctx->find_key_fn = jes_find_key;
#else
  {
    size_t usable_size = buffer_size - sizeof(struct jes_context) - sizeof(struct jes_hash_table);
    size_t node_count = usable_size / (sizeof(struct jes_node) + sizeof(struct jes_hash_entry));

    ctx->capacity = node_count < JES_INVALID_INDEX
                   ? (jes_node_descriptor)node_count
                   : JES_INVALID_INDEX -1;
    ctx->pool_size = ctx->capacity * sizeof(struct jes_node);
    ctx->hash_table = jes_init_hash_table(ctx, (uint8_t*)ctx->node_pool + ctx->pool_size, buffer_size - sizeof(struct jes_context) - ctx->pool_size);
  }
#endif
  ctx->number_tokenizer_fn = jes_integer_tokenizer;
  ctx->iter = NULL;
  ctx->root = NULL;
  ctx->free = NULL;

  ctx->cookie = JES_CONTEXT_COOKIE;
  return ctx;
}

void jes_reset(struct jes_context *ctx)
{
  if (JES_IS_INITIATED(ctx)) {
    ctx->status = JES_NO_ERROR;
    ctx->node_count = 0;
    ctx->json_data = NULL;
    ctx->offset = (uint32_t)-1;
    ctx->index = 0;
    ctx->number_tokenizer_fn = jes_integer_tokenizer;
    ctx->iter = NULL;
    ctx->root = NULL;
    ctx->free = NULL;
  }
}

static inline void jes_parser_on_opening_brace(struct jes_context *ctx)
{
  if ((ctx->state != JES_EXPECT_OBJECT) &&
      (ctx->state != JES_EXPECT_KEY_VALUE)  &&
      (ctx->state != JES_EXPECT_ARRAY_VALUE)) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }

  jes_parser_add_object(ctx);
  ctx->state = JES_EXPECT_KEY;
}

static inline void jes_parser_on_closing_brace(struct jes_context *ctx)
{
  if ((ctx->state != JES_EXPECT_KEY) &&
      (ctx->state != JES_HAVE_KEY_VALUE)) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }

  /* Delimiter tokens can trigger upward iteration in the direction of parent node.
     A '}' indicates the end of a key:value sequence (object). */

  /* {} (empty object)is a special case that needs no iteration back to
   the parent node. */
  if ((ctx->iter->json_tlv.type == JES_OBJECT) && (ctx->state == JES_EXPECT_KEY)) {
    /* An object in EXPECT_KEY state, can only be an empty object and must have no values */
    if (HAS_FIRST_CHILD(ctx->iter)) {
      ctx->status = JES_UNEXPECTED_TOKEN;
      ctx->ext_status = ctx->state;
      return;
    }
  }

  /* The current node isn't an OBJECT? then iterate the parents to find a matching OBJECT. */
  /* TODO: It's probably not a solid way to find the correct OBJECT. */
  if (ctx->iter->json_tlv.type != JES_OBJECT) {
    ctx->iter = jes_get_parent_node_of_type(ctx, ctx->iter, JES_OBJECT);
    assert(ctx->iter != NULL);
  }

  /* Internal iterator now points to the object that is just closed. One more iteration
     is needed to get the parent object or array for further insertions. */
  ctx->iter = jes_get_parent_node_of_type_object_or_array(ctx, ctx->iter);

  if (ctx->iter) {
    if (ctx->iter->json_tlv.type == JES_ARRAY) {
      ctx->state = JES_HAVE_ARRAY_VALUE;
    }
    else if (ctx->iter->json_tlv.type == JES_OBJECT) {
      ctx->state = JES_HAVE_KEY_VALUE;
    }
    else {
      assert(0);
    }
  }
  else {
    ctx->state = JES_EXPECT_EOF;
  }
}

static inline void jes_parser_on_opening_bracket(struct jes_context *ctx)
{
  if ((ctx->state != JES_EXPECT_KEY_VALUE) &&
      (ctx->state != JES_EXPECT_ARRAY_VALUE)) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }

  jes_parser_add_array(ctx);
  ctx->state = JES_EXPECT_ARRAY_VALUE;
}

static inline void jes_parser_on_closing_bracket(struct jes_context *ctx)
{
  if ((ctx->state != JES_EXPECT_ARRAY_VALUE) &&
      (ctx->state != JES_HAVE_ARRAY_VALUE)) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }

  /* Delimiter tokens can trigger upward iteration in the direction of parent node.
     A ']' indicates the end of an Array or possibly the end of a key:value sequence.
  */

  /* [] (empty array) is a special case that needs no backward iteration in the
     parent node direction.
  */
  if ((ctx->iter->json_tlv.type == JES_ARRAY) && (ctx->state == JES_EXPECT_ARRAY_VALUE)) {
    /* An array in expecting state, can only be an empty and must have no values */
    if (HAS_FIRST_CHILD(ctx->iter)) {
      ctx->status = JES_UNEXPECTED_TOKEN;
      ctx->ext_status = ctx->state;
      return;
    }
  }

  if (ctx->iter->json_tlv.type != JES_ARRAY) {
    ctx->iter = jes_get_parent_node_of_type(ctx, ctx->iter, JES_ARRAY);
    assert(ctx->iter != NULL);
  }
  /* Iterator now points the array that is just closed. One more upward iteration
     is required to get the parent object or array for new insertions. */
  ctx->iter = jes_get_parent_node_of_type_object_or_array(ctx, ctx->iter);

  if (!ctx->iter) {
    ctx->status = JES_PARSING_FAILED;
    return;
  }

  if (ctx->iter->json_tlv.type == JES_ARRAY) {
    ctx->state = JES_HAVE_ARRAY_VALUE;
  }
  else if (ctx->iter->json_tlv.type == JES_OBJECT) {
    ctx->state = JES_HAVE_KEY_VALUE;
  }
  else {
    assert(0);
  }
}

static inline void jes_parser_on_colon(struct jes_context *ctx)
{
  if (ctx->state == JES_EXPECT_COLON) {
    ctx->state = JES_EXPECT_KEY_VALUE;
  }
  else {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }
}

static inline void jes_parser_on_comma(struct jes_context *ctx)
{
  if (ctx->state == JES_HAVE_KEY_VALUE) {
    ctx->state = JES_EXPECT_KEY;
  }
  else if (ctx->state == JES_HAVE_ARRAY_VALUE) {
    ctx->state = JES_EXPECT_ARRAY_VALUE;
  }
  else {
    ctx->status = JES_UNEXPECTED_TOKEN;
        ctx->ext_status = ctx->state;
    return;
  }

  /* Delimiter tokens can trigger upward iteration in the direction of parent node.
     A ',' indicates the end of a value.
       - If the value is inside an array, iterate back to the parent array node.
       - Otherwise, iterate back to the parent object.
  */

  if ((ctx->iter->json_tlv.type == JES_OBJECT) || (ctx->iter->json_tlv.type == JES_ARRAY)) {
    if (!HAS_FIRST_CHILD(ctx->iter)) {
      ctx->status = JES_UNEXPECTED_TOKEN;
      ctx->ext_status = ctx->state;
    }
  }
  else {
    ctx->iter = jes_get_parent_node_of_type_object_or_array(ctx, ctx->iter);
  }
}

static inline void jes_parser_on_string(struct jes_context *ctx)
{
  if (ctx->state == JES_EXPECT_KEY) {
    /* Append the key */
    ctx->iter = jes_add_key_node_after(ctx, ctx->iter, GET_LAST_CHILD(ctx, ctx->iter), ctx->token.length, &ctx->json_data[ctx->token.offset]);
    ctx->state = JES_EXPECT_COLON;
  }
  else if (ctx->state == JES_EXPECT_KEY_VALUE) {
    jes_parser_add_value(ctx, JES_VALUE_STRING);
    ctx->state = JES_HAVE_KEY_VALUE;

  }
  else if (ctx->state == JES_EXPECT_ARRAY_VALUE) {
    jes_parser_add_value(ctx, JES_VALUE_STRING);
    ctx->state = JES_HAVE_ARRAY_VALUE;
  }
  else {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }
}

static inline void jes_parser_on_value(struct jes_context *ctx, enum jes_type value_type)
{
  if (ctx->state == JES_EXPECT_KEY_VALUE) {
    ctx->state = JES_HAVE_KEY_VALUE;
  }
  else if (ctx->state == JES_EXPECT_ARRAY_VALUE) {
    ctx->state = JES_HAVE_ARRAY_VALUE;
  }
  else {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }

  jes_parser_add_value(ctx, value_type);
}

struct jes_element* jes_load(struct jes_context *ctx, const char *json_data, uint32_t json_length)
{
  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if (json_data == NULL) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  jes_reset(ctx);

  ctx->state = JES_EXPECT_OBJECT;
  ctx->json_data = json_data;
  ctx->json_size = json_length;

  do {
    if (jes_get_token(ctx) != JES_NO_ERROR) break;

    switch (ctx->token.type) {

      case JES_TOKEN_EOF:
        break;

      case JES_TOKEN_OPENING_BRACE:
        jes_parser_on_opening_brace(ctx);
        break;

      case JES_TOKEN_CLOSING_BRACE:
        jes_parser_on_closing_brace(ctx);
        break;

      case JES_TOKEN_OPENING_BRACKET:
        jes_parser_on_opening_bracket(ctx);
        break;

      case JES_TOKEN_CLOSING_BRACKET:
        jes_parser_on_closing_bracket(ctx);
        break;

      case JES_TOKEN_COLON:
        jes_parser_on_colon(ctx);
        break;

      case JES_TOKEN_COMMA:
        jes_parser_on_comma(ctx);
        break;

      case JES_TOKEN_STRING:
        jes_parser_on_string(ctx);
        break;

      case JES_TOKEN_FALSE:
        jes_parser_on_value(ctx, JES_VALUE_FALSE);
        break;

      case JES_TOKEN_TRUE:
        jes_parser_on_value(ctx, JES_VALUE_TRUE);
        break;

      case JES_TOKEN_NULL:
        jes_parser_on_value(ctx, JES_VALUE_NULL);
        break;

      case JES_TOKEN_NUMBER:
        jes_parser_on_value(ctx, JES_VALUE_NUMBER);
        break;

      case JES_TOKEN_INVALID:
        /* Do not overwrite errors from Tokenizer */
        if (ctx->status == JES_NO_ERROR) {
          ctx->status = JES_PARSING_FAILED;
        }
        break;

      default:
        assert(0);
        ctx->status = JES_PARSING_FAILED;
        break;
    }
  } while ((ctx->status == JES_NO_ERROR) && (ctx->token.type != JES_TOKEN_EOF));

  if ((ctx->status == JES_NO_ERROR) && (ctx->iter != NULL)) {
      ctx->status = JES_UNEXPECTED_EOF;
  }

  return ctx->status == JES_NO_ERROR ? (struct jes_element*)ctx->root : NULL;
}

uint32_t jes_evaluate(struct jes_context *ctx, bool compact)
{
  uint32_t json_len = 0;
  uint32_t indention = 0;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return 0;
  }

  if (!ctx->root) {
    return 0;
  }

  ctx->iter = ctx->root;
  ctx->state = JES_EXPECT_OBJECT;
  ctx->status = JES_NO_ERROR;

  do {
    JES_LOG_NODE("\n   ", ctx->iter - ctx->node_pool, ctx->iter->json_tlv.type,
                  ctx->iter->json_tlv.length, ctx->iter->json_tlv.value,
                  ctx->iter->parent, ctx->iter->sibling, ctx->iter->first_child, "");

    switch (ctx->iter->json_tlv.type) {

      case JES_OBJECT:
        if ((ctx->state == JES_EXPECT_OBJECT) ||
            (ctx->state == JES_EXPECT_KEY_VALUE)  ||
            (ctx->state == JES_EXPECT_ARRAY_VALUE)) {
          ctx->state = JES_EXPECT_KEY;
        }
        else {
          ctx->status = JES_UNEXPECTED_ELEMENT;
          return 0;
        }

        json_len += sizeof("{") - 1;
        if (!compact) {
          if (PARENT_TYPE(ctx, ctx->iter) == JES_ARRAY) {
            json_len += sizeof("\n") - 1;
            json_len += indention;
          }
          indention += 2;
        }
        break;

      case JES_KEY:
        if (ctx->state == JES_EXPECT_KEY) {
          ctx->state = JES_EXPECT_KEY_VALUE;
        }
        else {
          ctx->status = JES_UNEXPECTED_ELEMENT;
          return 0;
        }

        json_len += (ctx->iter->json_tlv.length + sizeof("\"\":") - 1);
        if (!compact) {
            json_len += sizeof("\n ") - 1;
            json_len += indention;
        }
        break;

      case JES_ARRAY:
        if ((ctx->state == JES_EXPECT_KEY_VALUE) || (ctx->state == JES_EXPECT_ARRAY_VALUE)) {
          ctx->state = JES_EXPECT_ARRAY_VALUE;
        }
        else {
          ctx->status = JES_UNEXPECTED_ELEMENT;
          return 0;
        }

        json_len += sizeof("[") - 1;
        if (!compact) {
          if (PARENT_TYPE(ctx, ctx->iter) == JES_ARRAY) {
            json_len += sizeof("\n") - 1;
            json_len += indention;
          }
          indention += 2;
        }
        break;

      case JES_VALUE_STRING:
        if (ctx->state == JES_EXPECT_KEY_VALUE) {
          ctx->state = JES_HAVE_KEY_VALUE;
        }
        else if (ctx->state == JES_EXPECT_ARRAY_VALUE) {
          ctx->state = JES_HAVE_ARRAY_VALUE;
        }
        else {
          ctx->status = JES_UNEXPECTED_ELEMENT;
          return 0;
        }

        json_len += (ctx->iter->json_tlv.length + sizeof("\"\"") - 1);
        if (!compact) {
          if (PARENT_TYPE(ctx, ctx->iter) != JES_KEY) {
            json_len += sizeof("\n") - 1;
            json_len += indention;
          }
        }
        break;

      case JES_VALUE_NUMBER:
      case JES_VALUE_TRUE:
      case JES_VALUE_FALSE:
      case JES_VALUE_NULL:
        if (ctx->state == JES_EXPECT_KEY_VALUE) {
          ctx->state = JES_HAVE_KEY_VALUE;
        }
        else if (ctx->state == JES_EXPECT_ARRAY_VALUE) {
          ctx->state = JES_HAVE_ARRAY_VALUE;
        }
        else {
          ctx->status = JES_UNEXPECTED_ELEMENT;
           return 0;
        }

        json_len += ctx->iter->json_tlv.length;
        if (!compact) {
          if (PARENT_TYPE(ctx, ctx->iter) != JES_KEY) {
            json_len += sizeof("\n") - 1;
            json_len += indention;
          }
        }
        break;

      default:
        assert(0);
        break;
    }

    /* Iterate this branch down to the last child */
    if (HAS_FIRST_CHILD(ctx->iter)) {
      ctx->iter = GET_FIRST_CHILD(ctx, ctx->iter);
      continue;
    }

    /* Node has no child. if it's an object or array, forge a closing delimiter. */
    if (ctx->iter->json_tlv.type == JES_OBJECT) {
      /* This covers empty objects */
      json_len += sizeof("}") - 1;
      if (!compact) {
        indention -= 2;
      }
    }
    else if (ctx->iter->json_tlv.type == JES_ARRAY) {
      /* This covers empty array */
      json_len += sizeof("]") - 1;
      if (!compact) {
        indention -= 2;
      }
    }

    /* If the last child has a sibling then we've an array. Get the sibling and iterate the branch.
       Siblings must always be separated using a comma. */
    if (HAS_SIBLING(ctx->iter)) {
      if ((ctx->iter->json_tlv.type == JES_KEY) &&
          ((ctx->state != JES_EXPECT_ARRAY_VALUE) || (ctx->state != JES_HAVE_ARRAY_VALUE))) {
        ctx->status = JES_UNEXPECTED_ELEMENT;
        return 0;
      }
      json_len += sizeof(",") - 1;
      ctx->iter = GET_SIBLING(ctx, ctx->iter);
      ctx->state = JES_EXPECT_ARRAY_VALUE;
      continue;
    }

    /* Node doesn't have any children or siblings. Iterate backward to the parent. */
    if (ctx->iter->json_tlv.type == JES_KEY) {
      break;
    }

    while ((ctx->iter = GET_PARENT(ctx, ctx->iter))) {
      if (ctx->iter->json_tlv.type == JES_KEY) {
        ctx->state = JES_HAVE_KEY_VALUE;
      }
      /* If the parent is an object or array, forge a closing delimiter. */
      else if (ctx->iter->json_tlv.type == JES_OBJECT) {
        if (!compact) {
          indention -= 2;
          json_len += indention + sizeof("\n") - 1;
        }
        json_len += sizeof("}") - 1;
      }
      else if (ctx->iter->json_tlv.type == JES_ARRAY) {
        if (!compact) {
          indention -= 2;
          json_len += indention + sizeof("\n") - 1;
        }
        json_len += sizeof("]") - 1;
      }
      else if ((ctx->iter->json_tlv.type == JES_KEY) && (ctx->state != JES_HAVE_KEY_VALUE)) {
        ctx->status = JES_UNEXPECTED_ELEMENT;
        return 0;
      }

      /* If the parent has a sibling, take it and iterate the branch down.
         Siblings must always be separated using a comma. */
      if (HAS_SIBLING(ctx->iter)) {
        ctx->iter = GET_SIBLING(ctx, ctx->iter);
        json_len += sizeof(",") - 1;

        if (PARENT_TYPE(ctx, ctx->iter) == JES_OBJECT) {
          ctx->state = JES_EXPECT_KEY;
        }
        else if (PARENT_TYPE(ctx, ctx->iter) == JES_ARRAY) {
          ctx->state = JES_EXPECT_ARRAY_VALUE;
        }
        else {
          ctx->status = JES_UNEXPECTED_ELEMENT;
          return 0;
        }
        break;
      }
    }

  } while (ctx->iter && (ctx->iter != ctx->root));

  if (ctx->state == JES_EXPECT_KEY_VALUE) {
    ctx->status = JES_RENDER_FAILED;
    json_len = 0;
  }
  else {
    ctx->iter = ctx->root;
  }
  return json_len;
}

uint32_t jes_render(struct jes_context *ctx, char *buffer, uint32_t length, bool compact)
{
  char *dst = buffer;
  struct jes_node *iter = ctx->root;
  uint32_t required_buffer = 0;
  uint32_t indention = 0;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return 0;
  }

  if (buffer == NULL) {
    ctx->status = JES_INVALID_PARAMETER;
    return 0;
  }

  required_buffer = jes_evaluate(ctx, compact);

  if (length < required_buffer) {
    ctx->status = JES_OUT_OF_MEMORY;
    return 0;
  }

  if (required_buffer == 0) {
    return 0;
  }

  buffer[0] = '\0';

  while (iter) {

    if (iter->json_tlv.type == JES_OBJECT) {

      if (!compact) {
        if (PARENT_TYPE(ctx, iter) == JES_ARRAY) {
          *dst++ = '\n';
          memset(dst, ' ', indention*sizeof(char));
          dst += indention;
        }
        indention += 2;
      }
      *dst++ = '{';
    }
    else if (iter->json_tlv.type == JES_KEY) {

      if (!compact) {
        *dst++ = '\n';
        memset(dst, ' ', indention*sizeof(char));
        dst += indention;
      }

      *dst++ = '"';
      /* The JSON string has already been validated for size and structure,
         so this memcpy can proceed safely without further boundary checks. */
      assert((dst + iter->json_tlv.length) <= (buffer + length));
      dst = (char*)memcpy(dst, iter->json_tlv.value, iter->json_tlv.length) + iter->json_tlv.length;
      *dst++ = '"';
      *dst++ = ':';

      if (!compact) {
        *dst++ = ' ';
      }
    }
    else if (iter->json_tlv.type == JES_VALUE_STRING) {

      if (!compact) {
        if (PARENT_TYPE(ctx, iter) != JES_KEY) {
          *dst++ = '\n';
          memset(dst, ' ', indention*sizeof(char));
          dst += indention;
        }
      }

      *dst++ = '"';
      /* The JSON string has already been validated for size and structure,
         so this memcpy can proceed safely without further boundary checks. */
      assert((dst + iter->json_tlv.length) <= (buffer + length));
      dst = (char*)memcpy(dst, iter->json_tlv.value, iter->json_tlv.length) + iter->json_tlv.length;
      *dst++ = '"';
    }
    else if ((iter->json_tlv.type == JES_VALUE_NUMBER)  ||
             (iter->json_tlv.type == JES_VALUE_TRUE)    ||
             (iter->json_tlv.type == JES_VALUE_FALSE)   ||
             (iter->json_tlv.type == JES_VALUE_NULL)) {

      if (!compact) {
        if (PARENT_TYPE(ctx, iter) != JES_KEY) {
          *dst++ = '\n';
          memset(dst, ' ', indention*sizeof(char));
          dst += indention;
        }
      }
      /* The JSON string has already been validated for size and structure,
         so this memcpy can proceed safely without further boundary checks. */
      assert((dst + iter->json_tlv.length) <= (buffer + length));
      dst = (char*)memcpy(dst, iter->json_tlv.value, iter->json_tlv.length) + iter->json_tlv.length;
    }
    else if (iter->json_tlv.type == JES_ARRAY) {

      if (!compact) {
        if (PARENT_TYPE(ctx, iter) == JES_ARRAY) {
          *dst++ = '\n';
          memset(dst, ' ', indention*sizeof(char));
          dst += indention;
        }
        indention += 2;
      }

      *dst++ = '[';
    }
    else {
      assert(0);
      ctx->status = JES_UNEXPECTED_ELEMENT;
      break;
    }

    /* Iterate this branch down to the last child */
    if (HAS_FIRST_CHILD(iter)) {
      iter = GET_FIRST_CHILD(ctx, iter);
      continue;
    }

    /* Node has no child. if it's an object or array, forge a closing delimiter. */
    if (iter->json_tlv.type == JES_OBJECT) {
      /* This covers empty objects */
      if (!compact) {
        indention -= 2;
      }
      *dst++ = '}';
    }
    else if (iter->json_tlv.type == JES_ARRAY) {
      /* This covers empty array */
      if (!compact) {
        indention -= 2;
      }
      *dst++ = ']';
    }

    /* If Node has a sibling then Iterate the branch down.
       Siblings must always be separated using a comma. */
    if (HAS_SIBLING(iter)) {
      iter = GET_SIBLING(ctx, iter);
      *dst++ = ',';
      continue;
    }

    /* Node doesn't have any children or siblings. Iterate backward to the parent. */
    while ((iter = GET_PARENT(ctx, iter))) {
      /* If the parent is an object or array, forge a closing delimiter. */
      if (iter->json_tlv.type == JES_OBJECT) {
        if (!compact) {
          *dst++ = '\n';
          indention -= 2;
          memset(dst, ' ', indention*sizeof(char));
          dst += indention;
        }
        *dst++ = '}';
      }
      else if (iter->json_tlv.type == JES_ARRAY) {
        if (!compact) {
          *dst++ = '\n';
          indention -= 2;
          memset(dst, ' ', indention*sizeof(char));
          dst += indention;
        }
        *dst++ = ']';
      }
      /* If the parent has a sibling, take it and iterate the new branch down.
         Siblings must always be separated using a comma. */
      if (HAS_SIBLING(iter)) {
        iter = GET_SIBLING(ctx, iter);
        *dst++ = ',';
        break;
      }
    }
  }

  ctx->iter = ctx->root;
  return dst - buffer;
}

struct jes_element* jes_get_root(struct jes_context *ctx)
{
  if (ctx) {
    return &ctx->root->json_tlv;
  }
  return NULL;
}

struct jes_element* jes_get_parent(struct jes_context *ctx, struct jes_element *element)
{
  if (ctx && element && jes_validate_node(ctx, (struct jes_node*)element)) {
    struct jes_node *node = (struct jes_node*)element;
    node = GET_PARENT(ctx, node);
    if (node) {
      return &node->json_tlv;
    }
  }

  return NULL;
}

struct jes_element* jes_get_sibling(struct jes_context *ctx, struct jes_element *element)
{
  if (ctx && element && jes_validate_node(ctx, (struct jes_node*)element)) {
    struct jes_node *node = (struct jes_node*)element;
    node = GET_SIBLING(ctx, node);
    if (node) {
      return &node->json_tlv;
    }
  }

  return NULL;
}

struct jes_element* jes_get_child(struct jes_context *ctx, struct jes_element *element)
{
  if (ctx && element && jes_validate_node(ctx, (struct jes_node*)element)) {
    struct jes_node *node = (struct jes_node*)element;
    node = GET_FIRST_CHILD(ctx, node);
    if (node) {
      return &node->json_tlv;
    }
  }

  return NULL;
}

enum jes_type jes_get_parent_type(struct jes_context *ctx, struct jes_element *element)
{
  struct jes_element *parent = jes_get_parent(ctx, element);
  if (parent) {
    return parent->type;
  }

  return JES_UNKNOWN;
}

jes_status jes_delete_element(struct jes_context *ctx, struct jes_element *element)
{

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return JES_INVALID_CONTEXT;
  }

  if (element == NULL) {
    return JES_NO_ERROR;
  }

  if (!jes_validate_node(ctx, (struct jes_node*)element)) {
    ctx->status = JES_INVALID_PARAMETER;
    return ctx->status;
  }

  ctx->status = JES_NO_ERROR;
  jes_delete_node(ctx, (struct jes_node*)element);
  return ctx->status;
}

struct jes_element* jes_get_key(struct jes_context *ctx, struct jes_element *parent, const char *keys)
{
  struct jes_element *target_key = NULL;
  struct jes_node *iter = (struct jes_node*)parent;
  uint32_t key_len;
  const char *key;
  char *dot;

  if (!ctx || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if ((parent == NULL) || (!jes_validate_node(ctx, (struct jes_node*)parent)) || (keys == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  key_len = strnlen(keys, JES_MAX_VALUE_LENGTH);
  if (key_len == JES_MAX_VALUE_LENGTH) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  if ((parent->type != JES_OBJECT) && (parent->type != JES_KEY)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }
  /* TODO: Cleanup */
  while (iter != NULL) {
    key = keys;
    dot = strchr(keys, '.');
    if (dot) {
      key_len = dot - keys;
      keys = keys + key_len + sizeof(*dot);
    }
    else {
      /* keys length has already been validated to make sure a buffer over read won't happen. */
      key_len = strlen(keys);
      keys = keys + key_len;
    }

    if (iter->json_tlv.type == JES_KEY) {
      iter = GET_FIRST_CHILD(ctx, iter);
    }
    iter = ctx->find_key_fn(ctx, iter, key, key_len);

    if ((iter != NULL) && (*keys == '\0')) {
      target_key = (struct jes_element*)iter;
      break;
    }
  }

  if ((target_key == NULL) && (ctx->status == JES_NO_ERROR)) {
    ctx->status = JES_ELEMENT_NOT_FOUND;
  }

  return target_key;
}

struct jes_element* jes_get_key_value(struct jes_context *ctx, struct jes_element *key)
{
  struct jes_node *node = NULL;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if ((key == NULL) || (!jes_validate_node(ctx, (struct jes_node*)key)) || (key->type != JES_KEY)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  node = GET_FIRST_CHILD(ctx, (struct jes_node*)key);
  return (struct jes_element*)node;
}

uint16_t jes_get_array_size(struct jes_context *ctx, struct jes_element *array)
{
  uint16_t array_size = 0;
  struct jes_node *iter = NULL;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return 0;
  }

  if ((array == NULL) || (!jes_validate_node(ctx, (struct jes_node*)array)) || (array->type != JES_ARRAY)) {
    ctx->status = JES_INVALID_PARAMETER;
    return 0;
  }

  iter = GET_FIRST_CHILD(ctx, (struct jes_node*)array);
  if (iter) {
    for (array_size = 0; iter != NULL; array_size++) {
      iter = GET_SIBLING(ctx, iter);
    }
  }

  return array_size;
}

struct jes_element* jes_get_array_value(struct jes_context *ctx, struct jes_element *array, int32_t index)
{
  struct jes_node *iter = NULL;
  /* Skip checking ctx and array only when calling jes_get_array_size */
  uint16_t array_size = jes_get_array_size(ctx, array);

  if (ctx->status != JES_NO_ERROR) {
    return NULL;
  }

  if (index < 0) { /* converting negative index to an index from the end of array. */
    if (-index <= array_size) {
      index = array_size + index;
    }
  }

  if ((index < 0) || (index > array_size)) {
    ctx->status = JES_ELEMENT_NOT_FOUND;
    return NULL;
  }

  iter = GET_FIRST_CHILD(ctx, (struct jes_node*)array);
  for (; iter && index > 0; index--) {
    iter = GET_SIBLING(ctx, iter);
  }

  if (iter) {
    return &iter->json_tlv;
  }

  /* We shouldn't land here. */
  ctx->status = JES_BROKEN_TREE;
  assert(0);
  return NULL;
}

struct jes_element* jes_add_element(struct jes_context *ctx, struct jes_element *parent, enum jes_type type, const char *value)
{
  struct jes_element *new_element = NULL;
  struct jes_node *new_node = NULL;
  uint32_t value_length = 0;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if ((parent == NULL) && (ctx->root != NULL)) {
    /* JSON is not empty. Invalid request. */
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  if ((parent != NULL) && (!jes_validate_node(ctx, (struct jes_node*)parent))) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }
  /* TODO: review */
  if (value) {
    value_length = strnlen(value, JES_MAX_VALUE_LENGTH);
    if (value_length == JES_MAX_VALUE_LENGTH) {
      ctx->status = JES_INVALID_PARAMETER;
      return NULL;
    }
  }

  new_node = jes_append_node(ctx, (struct jes_node*)parent, type, value_length, value);

  if (new_node) {
    new_element = &new_node->json_tlv;
    JES_LOG_NODE("\n    + ", JES_GET_NODE_INDEX(ctx, new_node),
              new_element->type, new_element->length, new_element->value,
              new_node->parent, new_node->sibling, new_node->first_child, "");

  }

  return new_element;
}

struct jes_element* jes_add_key(struct jes_context *ctx, struct jes_element *parent, const char *keyword)
{
  struct jes_node *object = NULL;
  struct jes_node *new_node = NULL;
  uint16_t keyword_length = 0;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if ((parent == NULL) || (!jes_validate_node(ctx, (struct jes_node*)parent)) || (keyword == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  if ((parent->type != JES_OBJECT) && (parent->type != JES_KEY)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  keyword_length = strnlen(keyword, JES_MAX_VALUE_LENGTH);
  if (keyword_length == JES_MAX_VALUE_LENGTH) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  if (parent->type == JES_KEY) {
    /* The key must be added to an existing key and must be embedded in an OBJECT */
    object = GET_FIRST_CHILD(ctx, (struct jes_node*)parent);
    if (object == NULL) {
      object = jes_append_node(ctx, (struct jes_node*)parent, JES_OBJECT, 1, "{");
    }
    else if (object->json_tlv.type != JES_OBJECT) {
      /* We should not land here */
      ctx->status = JES_UNEXPECTED_ELEMENT;
      return NULL;
    }
  }
  else { /* parent is an OBJECT */
    object = (struct jes_node*)parent;
  }
  /* Append the key */
  new_node = jes_add_key_node_after(ctx, object, GET_LAST_CHILD(ctx, object), keyword_length, keyword);

  return (struct jes_element*)new_node;
}

struct jes_element* jes_add_key_before(struct jes_context *ctx, struct jes_element *key, const char *keyword)
{
  struct jes_node *new_node = NULL;
  struct jes_node *parent = NULL;
  struct jes_node *iter = NULL;
  struct jes_node *before = NULL;
  struct jes_node *key_node = (struct jes_node*)key;
  uint32_t keyword_length = 0;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if ((key == NULL) || !jes_validate_node(ctx, key_node) || (key->type != JES_KEY) || (keyword == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  keyword_length = strnlen(keyword, JES_MAX_VALUE_LENGTH);
  if (keyword_length == JES_MAX_VALUE_LENGTH) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  parent = GET_PARENT(ctx, key_node);
  assert(parent != NULL);
  assert(parent->json_tlv.type == JES_OBJECT);


  for (iter = GET_FIRST_CHILD(ctx, parent); iter != NULL; iter = GET_SIBLING(ctx, iter)) {
    assert(iter->json_tlv.type == JES_KEY);
    if (iter == key_node) {
      new_node = jes_add_key_node_after(ctx, parent, before, keyword_length, keyword);
      break;
    }
    before = iter;
  }

  return (struct jes_element*)new_node;
}

struct jes_element* jes_add_key_after(struct jes_context *ctx, struct jes_element *key, const char *keyword)
{
  struct jes_node *key_node = (struct jes_node*)key;
  struct jes_node *new_node = NULL;
  struct jes_node *parent = NULL;
  uint32_t keyword_length = 0;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if ((key == NULL) || !jes_validate_node(ctx, key_node) || (key->type != JES_KEY) || (keyword == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  parent = GET_PARENT(ctx, key_node);
  assert(parent != NULL);
  assert(parent->json_tlv.type == JES_OBJECT);

  keyword_length = strnlen(keyword, JES_MAX_VALUE_LENGTH);
  if (keyword_length == JES_MAX_VALUE_LENGTH) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  new_node = jes_add_key_node_after(ctx, parent, (struct jes_node*)key, keyword_length, keyword);

  return (struct jes_element*)new_node;
}

uint32_t jes_update_key(struct jes_context *ctx, struct jes_element *key, const char *keyword)
{
  uint32_t keyword_length = 0;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return JES_INVALID_CONTEXT;
  }

  if (!jes_validate_node(ctx, (struct jes_node*)key) || (key->type != JES_KEY) || (keyword == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return ctx->status;
  }

  keyword_length = strnlen(keyword, JES_MAX_VALUE_LENGTH);
  if (keyword_length == JES_MAX_VALUE_LENGTH) {
    ctx->status = JES_INVALID_PARAMETER;
    return ctx->status;
  }

  key->length = keyword_length;
  key->value = keyword;

  return JES_NO_ERROR;
}

struct jes_element* jes_update_key_value(struct jes_context *ctx, struct jes_element *key, enum jes_type type, const char *value)
{
  struct jes_element *key_value = NULL;

  /* First delete the old value of the key if exists. */
  if (jes_delete_element(ctx, jes_get_child(ctx, key)) == JES_NO_ERROR) {
    /* It's possible that the key loses its value if the add_element fails.
       The user should stop using the context or try to assign a value again.  */
    key_value = jes_add_element(ctx, key, type, value);
  }

  return key_value;
}

struct jes_element* jes_update_key_value_to_object(struct jes_context *ctx, struct jes_element *key)
{
  return jes_update_key_value(ctx, key, JES_OBJECT, "");
}

struct jes_element* jes_update_key_value_to_array(struct jes_context *ctx, struct jes_element *key)
{
  return jes_update_key_value(ctx, key, JES_ARRAY, "");
}

struct jes_element* jes_update_key_value_to_true(struct jes_context *ctx, struct jes_element *key)
{
  return jes_update_key_value(ctx, key, JES_VALUE_TRUE, "true");
}

struct jes_element* jes_update_key_value_to_false(struct jes_context *ctx, struct jes_element *key)
{
  return jes_update_key_value(ctx, key, JES_VALUE_FALSE, "false");
}

struct jes_element* jes_update_key_value_to_null(struct jes_context *ctx, struct jes_element *key)
{
  return jes_update_key_value(ctx, key, JES_VALUE_NULL, "null");
}

struct jes_element* jes_update_array_value(struct jes_context *ctx, struct jes_element *array, int32_t index, enum jes_type type, const char *value)
{
  struct jes_node *target_node = NULL;
  uint32_t value_length = 0;
  int32_t array_size;

  if (!ctx || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if ((array == NULL) || !jes_validate_node(ctx, (struct jes_node*)array) || (array->type != JES_ARRAY) || (value == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  value_length = strnlen(value, JES_MAX_VALUE_LENGTH);
  if (value_length == JES_MAX_VALUE_LENGTH) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  array_size = jes_get_array_size(ctx, array);
  if (index < 0) { /* converting negative index to an index from the end of array. */
    index = array_size + index;
  }

  if ((array_size == 0) || (index < 0) || (index > array_size)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  for(target_node = GET_FIRST_CHILD(ctx, (struct jes_node*)array); target_node != NULL; target_node = GET_SIBLING(ctx, target_node)) {
    if (index-- == 0) {
      break;
    }
  }

  if (target_node) {
    /* We'll not delete the target_node to keep the original array order. Just update its JSON TLV.
     * The rest of the branch however must be removed. */
    jes_delete_node(ctx, GET_FIRST_CHILD(ctx, target_node));
    target_node->json_tlv.type = type;
    target_node->json_tlv.length = value_length;
    target_node->json_tlv.value = value;
  }
  else {
    ctx->status = JES_BROKEN_TREE;
    assert(0);
  }

  return (struct jes_element*)target_node;
}

struct jes_element* jes_append_array_value(struct jes_context *ctx, struct jes_element *array, enum jes_type type, const char *value)
{
  struct jes_node *anchor_node = NULL;
  struct jes_node *new_node = NULL;
  uint32_t value_length = 0;

  if (!ctx || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if ((array == NULL) || !jes_validate_node(ctx, (struct jes_node*)array) || (array->type != JES_ARRAY) || (value == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  value_length = strnlen(value, JES_MAX_VALUE_LENGTH);
  if (value_length == JES_MAX_VALUE_LENGTH) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  new_node = jes_allocate(ctx);
  if (new_node) {
    /* Appending the new node to the end of array. */
    anchor_node = GET_LAST_CHILD(ctx, (struct jes_node*)array);
    if (anchor_node != NULL) {
      /* Array is not empty. */
      anchor_node->sibling = JES_GET_NODE_INDEX(ctx, new_node);
    }
    else {
      /* Array is empty. */
      ((struct jes_node*)array)->first_child = JES_GET_NODE_INDEX(ctx, new_node);
    }

    ((struct jes_node*)array)->last_child = JES_GET_NODE_INDEX(ctx, new_node);

    new_node->parent = JES_GET_NODE_INDEX(ctx, (struct jes_node*)array);
    new_node->json_tlv.type = type;
    new_node->json_tlv.length = value_length;
    new_node->json_tlv.value = value;

    JES_LOG_NODE("\n    + ", JES_GET_NODE_INDEX(ctx, new_node),
                  new_node->json_tlv.type, new_node->json_tlv.length, new_node->json_tlv.value,
                  new_node->parent, new_node->sibling, new_node->first_child, "");
  }

  return (struct jes_element*)new_node;
}

struct jes_element* jes_add_array_value(struct jes_context *ctx, struct jes_element *array, int32_t index, enum jes_type type, const char *value)
{
  struct jes_node *anchor_node = NULL;
  struct jes_node *prev_node = NULL;
  struct jes_node *new_node = NULL;
  uint32_t value_length = 0;
  int32_t array_size;

  if (!ctx || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if ((array == NULL) || !jes_validate_node(ctx, (struct jes_node*)array) || (array->type != JES_ARRAY) || (value == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  array_size = jes_get_array_size(ctx, array);
  if (index < 0) { /* converting negative index to an index from the end of array. */
    index = array_size + index;
  }

  /* Handling out of the bound indices as prepend or append */
  if (index < 0) { index = 0; }
  if (index >= array_size) {
    return jes_append_array_value(ctx, array, type, value);
  }

  value_length = strnlen(value, JES_MAX_VALUE_LENGTH);
  if (value_length == JES_MAX_VALUE_LENGTH) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  for (anchor_node = GET_FIRST_CHILD(ctx, (struct jes_node*)array); anchor_node != NULL; anchor_node = GET_SIBLING(ctx, anchor_node)) {
    if (index == 0) {
      break;
    }
    prev_node = anchor_node;
    index--;
  }

  if ((anchor_node == NULL) && (array_size != 0)) {
    ctx->status = JES_BROKEN_TREE;
    assert(0);
    return NULL;
  }

  new_node = jes_allocate(ctx);
  if (new_node != NULL) {
    if (anchor_node != NULL) {
      new_node->sibling = JES_GET_NODE_INDEX(ctx, anchor_node);

      if (((struct jes_node*)array)->first_child == JES_GET_NODE_INDEX(ctx, anchor_node)) {
        /* Inserting the new node at index 0 */
        ((struct jes_node*)array)->first_child = JES_GET_NODE_INDEX(ctx, new_node);
      }
      else {
       assert(prev_node != NULL);
       prev_node->sibling = JES_GET_NODE_INDEX(ctx, new_node);
      }
    }
    else {
      assert(0);
    }

    new_node->parent = JES_GET_NODE_INDEX(ctx, (struct jes_node*)array);
    new_node->json_tlv.type = type;
    new_node->json_tlv.length = value_length;
    new_node->json_tlv.value = value;
    JES_LOG_NODE("\n    + ", JES_GET_NODE_INDEX(ctx, new_node),
                  new_node->json_tlv.type, new_node->json_tlv.length, new_node->json_tlv.value,
                  new_node->parent, new_node->sibling, new_node->first_child, "");
  }

  return (struct jes_element*)new_node;
}

size_t jes_get_element_count(struct jes_context *ctx)
{
  return ctx->node_count;
}

jes_status jes_get_status(struct jes_context *ctx)
{
  return ctx->status;
}
