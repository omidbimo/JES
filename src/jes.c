#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "jes.h"

#ifdef JES_USE_32BIT_NODE_DESCRIPTOR
  #define JES_INVALID_INDEX 0xFFFFFFFF
  #define JES_MAX_VALUE_LEN 0xFFFFFFFF
#else
  #define JES_INVALID_INDEX 0xFFFF
  #define JES_MAX_VALUE_LEN 0xFFFF
#endif

#define JES_ARRAY_LEN(arr) (sizeof(arr)/sizeof(arr[0]))
#define UPDATE_TOKEN(tok, type_, offset_, size_) \
  tok.type = type_; \
  tok.offset = offset_; \
  tok.length = size_;

#define IS_SPACE(c) ((c==' ') || (c=='\t') || (c=='\r') || (c=='\n'))
#define IS_NEW_LINE(c) ((c=='\r') || (c=='\n'))
#define IS_DIGIT(c) ((c >= '0') && (c <= '9'))
#define IS_ESCAPE(c) ((c=='\\') || (c=='\"') || (c=='\/') || (c=='\b') || \
                      (c=='\f') || (c=='\n') || (c=='\r') || (c=='\t') || (c == '\u'))
#define LOOK_AHEAD(ctx_) (((ctx_->offset + 1) < ctx_->json_size) ? ctx_->json_data[ctx_->offset + 1] : '\0')

#define HAS_PARENT(node_ptr) (node_ptr->parent < JES_INVALID_INDEX)
#define HAS_SIBLING(node_ptr) (node_ptr->sibling < JES_INVALID_INDEX)
#define HAS_CHILD(node_ptr) (node_ptr->first_child < JES_INVALID_INDEX)

#define GET_PARENT(ctx_, node_ptr) (HAS_PARENT(node_ptr) ? &ctx_->pool[node_ptr->parent] : NULL)
#define GET_SIBLING(ctx_, node_ptr) (HAS_SIBLING(node_ptr) ? &ctx_->pool[node_ptr->sibling] : NULL)
#define GET_CHILD(ctx_, node_ptr) (HAS_CHILD(node_ptr) ? &ctx_->pool[node_ptr->first_child] : NULL)

#define PARENT_TYPE(ctx_, node_ptr) (HAS_PARENT(node_ptr) ? ctx_->pool[node_ptr->parent].type : JES_UNKNOWN)

#define JES_CONTEXT_COOKIE 0xABC09DEF
#define JES_IS_INITIATED(ctx_) (ctx_->cookie == JES_CONTEXT_COOKIE)

static char jes_status_str[][20] = {
  "NO_ERR",
  "PARSING_FAILED",
  "RENDER_FAILED",
  "OUT_OF_MEMORY",
  "UNEXPECTED_TOKEN",
  "UNEXPECTED_NODE",
  "UNEXPECTED_EOF",
  "INVALID_PARAMETER",
  "ELEMENT_NOT_FOUND",
};

static char jes_token_type_str[][20] = {
  "EOF",
  "OPENING_BRACE",
  "CLOSING_BRACE",
  "OPENING_BRACKET",
  "CLOSING_BRACKET",
  "COLON",
  "COMMA",
  "STRING",
  "NUMBER",
  "TRUE",
  "FALSE",
  "NULL",
  "ESC",
  "INVALID",
};

static char jes_node_type_str[][20] = {
  "NONE",
  "OBJECT",
  "KEY",
  "ARRAY",
  "STRING_VALUE",
  "NUMBER_VALUE",
  "TRUE_VALUE",
  "FALSE_VALUE",
  "NULL_VALUE",
};

static char jes_state_str[][20] = {
  "EXPECT_OBJECT",
  "EXPECT_KEY",
  "EXPECT_COLON",
  "EXPECT_VALUE",
  "HAVE_KEY_VALUE",
  "EXPECT_ARRAY_VALUE",
  "HAVE_ARRAY_VALUE",
};

enum jes_state {
  JES_EXPECT_OBJECT = 0,
  JES_EXPECT_KEY,
  JES_EXPECT_COLON,
  JES_EXPECT_VALUE,
  JES_HAVE_VALUE,
  JES_EXPECT_ARRAY_VALUE,
  JES_HAVE_ARRAY_VALUE,
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
  uint16_t length;
  uint32_t offset;
};

struct jes_free_node {
  struct jes_free_node *next;
};

struct jes_context {
  /* If the cookie value 0xABC09DEF is confirmed, the structure will be considered as initialized */
  uint32_t cookie;
  uint32_t status;
  /* Extended status code. In some cases provides more detailed information about the status. */
  uint32_t ext_status;
  /*  */
  enum jes_state state;
  /* Number of nodes in the current JSON */
  uint32_t node_count;
  /* JSON data to be parsed */
  const char *json_data;
  /* Length of JSON data in bytes. */
  uint32_t  json_size;
  /* Offset of the next symbol in the input JSON data Tokenizer is going to consume. */
  uint32_t  offset;
  uint32_t  line_number;
  /* Part of the buffer given by the user at the time of the context initialization.
   * The buffer will be used to allocate the context structure at first. Then
   * the remaining will be used as a pool of nodes (max. 65535 nodes).
   * Actually the pool member points to the memory after context. */
   struct jes_element *pool;
  /* Pool size in bytes. It is limited to 32-bit value which is more than what
   * most of embedded systems can provide. */
  uint32_t pool_size;
  /* Number of nodes that can be allocated on the given buffer. The value will
     be limited to 65535 in case of 16-bit node descriptors. */
  uint32_t capacity;
  /* Index of the last allocated node */
  jes_node_descriptor  index;
  /* Holds the last token delivered by tokenizer. */
  struct jes_token token;
  /* Internal node iterator */
  struct jes_element *iter;
  /* Holds the main object node */
  struct jes_element *root;
  /* Singly Linked list of freed nodes. This way the deleted nodes can be recycled
     by the allocator. */
  struct jes_free_node *free;
};

static inline void jes_log_token(uint16_t token_type,
                                  uint32_t token_pos,
                                  uint32_t token_len,
                                  const uint8_t *token_value)
{
  printf("\n JES.Token: [Pos: %5d, Len: %3d] %-16s \"%.*s\"",
          token_pos, token_len, jes_token_type_str[token_type],
          token_len, token_value);
}

static inline void jes_log_node( const char *pre_msg,
                                  int16_t node_id,
                                  uint32_t node_type,
                                  uint32_t node_length,
                                  const char *node_value,
                                  int16_t parent_id,
                                  int16_t right_id,
                                  int16_t child_id,
                                  const char *post_msg)
{
  printf("%sJES.Node: [%d] \"%.*s\" <%s>,    parent:[%d], right:[%d], child:[%d]%s",
    pre_msg, node_id, node_length, node_value, jes_node_type_str[node_type], parent_id, right_id, child_id, post_msg);
}

#ifndef NDEBUG
  #define JES_LOG_TOKEN jes_log_token
  #define JES_LOG_NODE  jes_log_node
  #define JES_LOG_MSG   jes_log_msg
  #define JES_STRINGIFY_ERROR  jes_get_status_info
#else
  #define JES_LOG_TOKEN(...)
  #define JES_LOG_NODE(...)
  #define JES_LOG_MSG(...)
  #define JES_STRINGIFY_ERROR(...) ""
#endif

#ifndef JES_ALLOW_DUPLICATE_KEYS
static struct jes_element *jes_find_duplicate_key(struct jes_context *ctx,
                                                  struct jes_element *object_node,
                                                  struct jes_token *key_token);
#endif

static struct jes_element* jes_allocate(struct jes_context *ctx)
{
  struct jes_element *new_element = NULL;

  if (ctx->node_count < ctx->capacity) {
    if (ctx->free) {
      /* Pop the first node from free list */
      new_element = (struct jes_element*)ctx->free;
      ctx->free = ctx->free->next;
    }
    else {
      assert(ctx->index < ctx->capacity);
      new_element = &ctx->pool[ctx->index];
      ctx->index++;
    }
    /* Setting node descriptors to their default values. */
    memset(&new_element->parent, 0xFF, sizeof(jes_node_descriptor) * 4);
    ctx->node_count++;
  }
  else {
    ctx->status = JES_OUT_OF_MEMORY;
  }

  return new_element;
}

static void jes_free(struct jes_context *ctx, struct jes_element *element)
{
  struct jes_free_node *free_node = (struct jes_free_node*)element;

  assert(element >= ctx->pool);
  assert(element < (ctx->pool + ctx->capacity));
  assert(ctx->node_count > 0);

  if (ctx->node_count > 0) {
    element->type = JES_UNKNOWN; /* This prevents reuse of deleted nodes. */
    free_node->next = NULL;
    ctx->node_count--;
    /* prepend the node to the free LIFO */
    if (ctx->free) {
      free_node->next = ctx->free->next;
    }
    ctx->free = free_node;
  }
}

static bool jes_validate_element(struct jes_context *ctx, struct jes_element *element)
{
  assert(ctx);
  assert(element);

  if ((element >= ctx->pool) &&
      ((((void*)element - (void*)ctx->pool) % sizeof(*element)) == 0) &&
      ((element >= ctx->pool) < ctx->capacity)) {
    return true;
  }

  return false;
}

struct jes_element* jes_get_parent(struct jes_context *ctx, struct jes_element *element)
{
  if (ctx && element && jes_validate_element(ctx, element)) {
    if (HAS_PARENT(element)) {
      return &ctx->pool[element->parent];
    }
  }

  return NULL;
}

struct jes_element* jes_get_sibling(struct jes_context *ctx, struct jes_element *element)
{
  if (ctx && element && jes_validate_element(ctx, element)) {
    if (HAS_SIBLING(element)) {
      return &ctx->pool[element->sibling];
    }
  }

  return NULL;
}

struct jes_element* jes_get_child(struct jes_context *ctx, struct jes_element *element)
{
  if (ctx && element && jes_validate_element(ctx, element)) {
    if (HAS_CHILD(element)) {
      return &ctx->pool[element->first_child];
    }
  }

  return NULL;
}

static struct jes_element* jes_get_parent_bytype(struct jes_context *ctx,
                                                   struct jes_element *element,
                                                   enum jes_type type)
{
  struct jes_element *parent = NULL;
  if (ctx && element && jes_validate_element(ctx, element)) {
    while (element && HAS_PARENT(element)) {
      element = &ctx->pool[element->parent];
      if (element->type == type) {
        parent = element;
        break;
      }
    }
  }

  return parent;
}

static struct jes_element* jes_get_structure_parent_node(struct jes_context *ctx,
                                                           struct jes_element *element)
{
  struct jes_element *parent = NULL;
  if (ctx && element && jes_validate_element(ctx, element)) {
    while (element && HAS_PARENT(element)) {
      element = &ctx->pool[element->parent];
      if ((element->type == JES_OBJECT) || (element->type == JES_ARRAY)) {
        parent = element;
        break;
      }
    }
  }

  return parent;
}

static struct jes_element* jes_append_element(struct jes_context *ctx,
                                              struct jes_element *parent,
                                              uint16_t type,
                                              uint16_t length,
                                              const char *value)
{
  struct jes_element *new_element = jes_allocate(ctx);

  if (new_element) {
    new_element->type = type;
    new_element->length = length;
    new_element->value = value;

    if (parent) {
      new_element->parent = (jes_node_descriptor)(parent - ctx->pool); /* parent's index */

      if (HAS_CHILD(parent)) {
        struct jes_element *last = &ctx->pool[parent->last_child];
        last->sibling = (jes_node_descriptor)(new_element - ctx->pool); /* new_element's index */
      }
      else {
        parent->first_child = (jes_node_descriptor)(new_element - ctx->pool); /* new_element's index */
      }
      parent->last_child = (jes_node_descriptor)(new_element - ctx->pool); /* new_element's index */
    }
    else {
      assert(!ctx->root);
      ctx->root = new_element;
    }
  }

  return new_element;
}

void jes_delete_element(struct jes_context *ctx, struct jes_element *element)
{
  struct jes_element *iter = element;

  if (!element || !jes_validate_element(ctx, element)) {
    return;
  }

  while (true) {
    while (HAS_CHILD(iter)) {
      iter = &ctx->pool[iter->first_child];
    }

    if (iter == element) {
      break;
    }

    if (HAS_PARENT(iter)) {
      ctx->pool[iter->parent].first_child = iter->sibling;
    }

    jes_free(ctx, iter);
    iter = &ctx->pool[iter->parent];
  }
  /* All sub-elements are deleted. To delete the element parent and sibling links need to be maintained. */
  iter = GET_PARENT(ctx, element);
  if (iter) {
    if (&ctx->pool[iter->first_child] == element) {
      iter->first_child = element->sibling;
    }
    else {
      /* Element is not the first child of it's parent. Need to iterate all children to reach element and maintain the linkage.*/
      iter = &ctx->pool[iter->first_child];
      while(iter) {
        if (&ctx->pool[iter->sibling] == element) {
          iter->sibling = element->sibling;
          break;
        }
        iter = &ctx->pool[iter->sibling];
      }
    }
  }

  jes_free(ctx, element);
}

const struct {
  char symbol;
  enum jes_token_type token_type;
} jes_symbolic_token_mapping[] = {
  {'\0', JES_TOKEN_EOF             },
  {'{',  JES_TOKEN_OPENING_BRACE   },
  {'}',  JES_TOKEN_CLOSING_BRACE   },
  {'[',  JES_TOKEN_OPENING_BRACKET },
  {']',  JES_TOKEN_CLOSING_BRACKET },
  {':',  JES_TOKEN_COLON           },
  {',',  JES_TOKEN_COMMA           },
  };

static inline bool jes_get_symbolic_token(struct jes_context *ctx,
                                           char ch, struct jes_token *token)
{
  uint32_t idx;
  for (idx = 0; idx < JES_ARRAY_LEN(jes_symbolic_token_mapping); idx++) {
    if (ch == jes_symbolic_token_mapping[idx].symbol) {
      UPDATE_TOKEN((*token), jes_symbolic_token_mapping[idx].token_type, ctx->offset, 1);
      return true;
    }
  }
  return false;
}

static inline bool jes_is_symbolic_token(char ch)
{
  uint32_t idx;
  for (idx = 0; idx < JES_ARRAY_LEN(jes_symbolic_token_mapping); idx++) {
    if (ch == jes_symbolic_token_mapping[idx].symbol) {
      return true;
    }
  }
  return false;
}

static inline bool jes_get_number_token(struct jes_context *ctx,
                                         char ch, struct jes_token *token)
{
  bool tokenizing_completed = false;
  if (IS_DIGIT(ch)) {
    token->length++;
    ch = LOOK_AHEAD(ctx);
    if (!IS_DIGIT(ch) && (ch != '.')) { /* TODO: more symbols are acceptable in the middle of a number */
      tokenizing_completed = true;
    }
  }
  else if (ch == '.') {
    token->length++;
    if (!IS_DIGIT(LOOK_AHEAD(ctx))) {
      token->type = JES_TOKEN_INVALID;
      tokenizing_completed = true;
    }
  }
  else if (IS_SPACE(ch)) {
    tokenizing_completed = true;
  }
  else {
    token->type = JES_TOKEN_INVALID;
    tokenizing_completed = true;
  }
  return tokenizing_completed;
}

static inline bool jes_get_specific_token(struct jes_context *ctx,
                          struct jes_token *token, char *cmp_str, uint16_t len)
{
  bool tokenizing_completed = false;
  token->length++;
  if (token->length == len) {
    if (0 != (strncmp(&ctx->json_data[token->offset], cmp_str, len))) {
      token->type = JES_TOKEN_INVALID;
    }
    tokenizing_completed = true;
  }
  return tokenizing_completed;
}

static struct jes_token jes_get_token(struct jes_context *ctx)
{
  struct jes_token token = { 0 };

  while (true) {

    if ((++ctx->offset >= ctx->json_size) || (ctx->json_data[ctx->offset] == '\0')) {
      /* End of data. If token is incomplete, mark it as invalid. */
      if (token.type) {
        token.type = JES_TOKEN_INVALID;
      }
      break;
    }

    char ch = ctx->json_data[ctx->offset];

    if (!token.type) {

      if (jes_get_symbolic_token(ctx, ch, &token)) {
        break;
      }

      if (ch == '\"') {
        /* '\"' won't be a part of token. Use offset of next symbol */
        UPDATE_TOKEN(token, JES_TOKEN_STRING, ctx->offset + 1, 0);
        continue;
      }

      if (IS_DIGIT(ch)) {
        UPDATE_TOKEN(token, JES_TOKEN_NUMBER, ctx->offset, 1);
        /* Unlike STRINGs, NUMBERs do not have dedicated symbols to indicate the
           end of data. To avoid consuming non-NUMBER characters, take a look ahead
           and stop the process in case of non-numeric symbols. */
        if (jes_is_symbolic_token(LOOK_AHEAD(ctx))) {
          break;
        }
        continue;
      }

      if ((ch == '-') && IS_DIGIT(LOOK_AHEAD(ctx))) {
        UPDATE_TOKEN(token, JES_TOKEN_NUMBER, ctx->offset, 1);
        continue;
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
      /* TODO: add checking for scape symbols */
      token.length++;
      continue;
    }
    else if (token.type == JES_TOKEN_NUMBER) {
      if (jes_get_number_token(ctx, ch, &token)) {
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

  return token;
}

#ifndef JES_ALLOW_DUPLICATE_KEYS
static struct jes_element *jes_find_duplicate_key(struct jes_context *ctx,
                                                    struct jes_element *object,
                                                    struct jes_token *key_token)
{
  struct jes_element *duplicate = NULL;
  struct jes_element *iter = NULL;

  assert(object->type == JES_OBJECT);
  if (object->type != JES_OBJECT) {
    return NULL;
  }

  iter = HAS_CHILD(object) ? &ctx->pool[object->first_child] : NULL;
  while(iter) {
    assert(iter->type == JES_KEY);
    if ((iter->length == key_token->length) &&
        (strncmp(iter->value, &ctx->json_data[key_token->offset], key_token->length) == 0)) {
      duplicate = iter;
      break;
    }
    iter = HAS_SIBLING(object) ? &ctx->pool[object->sibling] : NULL;
  }
  return duplicate;
}
#endif

static void jes_parser_add_object(struct jes_context *ctx)
{
  struct jes_element *new_node = NULL;
  new_node = jes_append_element(ctx, ctx->iter, JES_OBJECT, ctx->token.length, &ctx->json_data[ctx->token.offset]);
  if (new_node) {
    ctx->iter = new_node;
    JES_LOG_NODE("\n    + ", ctx->iter - ctx->pool, ctx->iter->type, ctx->iter->length, ctx->iter->value,
                  ctx->iter->parent, ctx->iter->sibling, ctx->iter->first_child, "");
  }
}

static void jes_parser_add_key(struct jes_context *ctx)
{
  struct jes_element *new_node = NULL;

#ifndef JES_ALLOW_DUPLICATE_KEYS
  /* No duplicate keys in the same object are allowed.
     Only the last key:value will be reported if the keys are duplicated. */
  struct jes_element *node = jes_find_duplicate_key(ctx, ctx->iter, &ctx->token);
  if (node) {
    jes_delete_element(ctx, jes_get_child(ctx, node));
    ctx->iter = node;
  }
  else
#endif
  {
    new_node = jes_append_element(ctx, ctx->iter, JES_KEY, ctx->token.length, &ctx->json_data[ctx->token.offset]);
  }
  if (new_node) {
    ctx->iter = new_node;
    JES_LOG_NODE("\n    + ", ctx->iter - ctx->pool, ctx->iter->type, ctx->iter->length, ctx->iter->value,
                  ctx->iter->parent, ctx->iter->sibling, ctx->iter->first_child, "");
  }
}

static void jes_parser_add_array(struct jes_context *ctx)
{
  struct jes_element *new_node = NULL;
  new_node = jes_append_element(ctx, ctx->iter, JES_ARRAY, ctx->token.length, &ctx->json_data[ctx->token.offset]);
  if (new_node) {
    ctx->iter = new_node;
    JES_LOG_NODE("\n    + ", ctx->iter - ctx->pool, ctx->iter->type, ctx->iter->length, ctx->iter->value,
                  ctx->iter->parent, ctx->iter->sibling, ctx->iter->first_child, "");
  }
}

static void jes_parser_add_value(struct jes_context *ctx, enum jes_type value_type)
{
  struct jes_element *new_node = NULL;
  new_node = jes_append_element(ctx, ctx->iter, value_type, ctx->token.length, &ctx->json_data[ctx->token.offset]);

  if (new_node) {
    ctx->iter = new_node;
    JES_LOG_NODE("\n    + ", ctx->iter - ctx->pool, ctx->iter->type, ctx->iter->length, ctx->iter->value,
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
  ctx->pool = (struct jes_element*)(ctx + 1);
  ctx->buffer_size = buffer_size - (uint32_t)(sizeof(struct jes_context));
  ctx->capacity = (ctx->buffer_size / sizeof(struct jes_element)) < JES_INVALID_INDEX
                 ? (jes_node_descriptor)(ctx->buffer_size / sizeof(struct jes_element))
                 : JES_INVALID_INDEX -1;

  ctx->iter = NULL;
  ctx->root = NULL;
  ctx->free = NULL;

#ifndef NDEBUG
  printf("\nallocator capacity is %d nodes", ctx->capacity);
#endif

  ctx->cookie = JES_CONTEXT_COOKIE;
  return ctx;
}

static inline void jes_parser_on_opening_brace(struct jes_context *ctx)
{
  if ((ctx->state != JES_EXPECT_OBJECT) &&
      (ctx->state != JES_EXPECT_VALUE)  &&
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
      (ctx->state != JES_HAVE_VALUE)) {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }

  /* Delimiter tokens can trigger upward iteration in the direction of parent node.
     A '}' indicates the end of a key:value sequence (object). */

  /* {} (empty object)is a special case that needs no iteration back to
   the parent node. */
  if ((ctx->iter->type == JES_OBJECT) && (ctx->state == JES_EXPECT_KEY)) {
    /* An object in expecting state, can only be an empty and must have no values */
    if (HAS_CHILD(ctx->iter)) {
      ctx->status = JES_UNEXPECTED_TOKEN;
      ctx->ext_status = ctx->state;
      return;
    }
  }
  if (ctx->iter->type != JES_OBJECT) {
    ctx->iter = jes_get_parent_bytype(ctx, ctx->iter, JES_OBJECT);
    assert(ctx->iter != NULL);
  }
  /* Iterator now points the object that is just closed. One more upward iteration
     is required to get the parent object or array for new insertions. */
  ctx->iter = jes_get_structure_parent_node(ctx, ctx->iter);

  if (ctx->iter) {
    if (ctx->iter->type == JES_ARRAY) {
      ctx->state = JES_HAVE_ARRAY_VALUE;
    }
    else if (ctx->iter->type == JES_OBJECT) {
      ctx->state = JES_HAVE_VALUE;
    }
    else {
      assert(0);
    }
  }
}

static inline void jes_parser_on_opening_bracket(struct jes_context *ctx)
{
  if ((ctx->state != JES_EXPECT_VALUE) &&
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
  if ((ctx->iter->type == JES_ARRAY) && (ctx->state == JES_EXPECT_ARRAY_VALUE)) {
    /* An array in expecting state, can only be an empty and must have no values */
    if (HAS_CHILD(ctx->iter)) {
      ctx->status = JES_UNEXPECTED_TOKEN;
      ctx->ext_status = ctx->state;
      return;
    }
  }

  if (ctx->iter->type != JES_ARRAY) {
    ctx->iter = jes_get_parent_bytype(ctx, ctx->iter, JES_ARRAY);
    assert(ctx->iter != NULL);
  }
  /* Iterator now points the array that is just closed. One more upward iteration
     is required to get the parent object or array for new insertions. */
  ctx->iter = jes_get_structure_parent_node(ctx, ctx->iter);

  if (!ctx->iter) {
    ctx->status = JES_PARSING_FAILED;
    return;
  }

  if (ctx->iter->type == JES_ARRAY) {
    ctx->state = JES_HAVE_ARRAY_VALUE;
  }
  else if (ctx->iter->type == JES_OBJECT) {
    ctx->state = JES_HAVE_VALUE;
  }
  else {
    assert(0);
  }
}

static inline void jes_parser_on_colon(struct jes_context *ctx)
{
  if (ctx->state == JES_EXPECT_COLON) {
    ctx->state = JES_EXPECT_VALUE;
  }
  else {
    ctx->status = JES_UNEXPECTED_TOKEN;
    ctx->ext_status = ctx->state;
    return;
  }
}

static inline void jes_parser_on_comma(struct jes_context *ctx)
{
  if (ctx->state == JES_HAVE_VALUE) {
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

  if ((ctx->iter->type == JES_OBJECT) || (ctx->iter->type == JES_ARRAY)) {
    if (!HAS_CHILD(ctx->iter)) {
      ctx->status = JES_UNEXPECTED_TOKEN;
      ctx->ext_status = ctx->state;
    }
  }
  else {
    ctx->iter = jes_get_structure_parent_node(ctx, ctx->iter);
  }
}

static inline void jes_parser_on_string(struct jes_context *ctx)
{
  if (ctx->state == JES_EXPECT_KEY) {
    jes_parser_add_key(ctx);
    ctx->state = JES_EXPECT_COLON;
  }
  else if (ctx->state == JES_EXPECT_VALUE) {
    jes_parser_add_value(ctx, JES_VALUE_STRING);
    ctx->state = JES_HAVE_VALUE;

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
  if (ctx->state == JES_EXPECT_VALUE) {
    ctx->state = JES_HAVE_VALUE;
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

uint32_t jes_load(struct jes_context *ctx, const char *json_data, uint32_t json_length)
{
  ctx->state = JES_EXPECT_OBJECT;
  ctx->json_data = json_data;
  ctx->json_size = json_length;

  do {
    ctx->token = jes_get_token(ctx);

    switch (ctx->token.type) {

      case JES_TOKEN_EOF:
        return ctx->status;
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

      default:
        assert(0);
        break;
    }
  } while (ctx->status == JES_NO_ERROR);

  if (ctx->status == 0) {
    if (ctx->token.type != JES_TOKEN_EOF) {
      ctx->status = JES_UNEXPECTED_TOKEN;
    }
    else if (ctx->iter) {
      ctx->status = JES_UNEXPECTED_EOF;
    }
  }

  return ctx->status;
}

uint32_t jes_evaluate(struct jes_context *ctx, bool compact)
{
  uint32_t json_len = 0;
  uint32_t indention = 0;

  if (!ctx->root) {
    return 0;
  }

  ctx->iter = ctx->root;
  ctx->state = JES_EXPECT_OBJECT;
  ctx->status = JES_NO_ERROR;

  do {
    JES_LOG_NODE("\n   ", ctx->iter - ctx->pool, ctx->iter->type,ctx->iter->length, ctx->iter->value,
                  ctx->iter->parent, ctx->iter->sibling, ctx->iter->first_child, "");

    switch (ctx->iter->type) {

      case JES_OBJECT:
        if ((ctx->state == JES_EXPECT_OBJECT) ||
            (ctx->state == JES_EXPECT_VALUE)  ||
            (ctx->state == JES_EXPECT_ARRAY_VALUE)) {
          ctx->state = JES_EXPECT_KEY;
        }
        else {
          ctx->status = JES_UNEXPECTED_NODE;
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
          ctx->state = JES_EXPECT_VALUE;
        }
        else {
          ctx->status = JES_UNEXPECTED_NODE;
          return 0;
        }

        json_len += (ctx->iter->length + sizeof("\"\":") - 1);
        if (!compact) {
            json_len += sizeof("\n ") - 1;
            json_len += indention;
        }
        break;

      case JES_ARRAY:
        if ((ctx->state == JES_EXPECT_VALUE) || (ctx->state == JES_EXPECT_ARRAY_VALUE)) {
          ctx->state = JES_EXPECT_ARRAY_VALUE;
        }
        else {
          ctx->status = JES_UNEXPECTED_NODE;
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
        if (ctx->state == JES_EXPECT_VALUE) {
          ctx->state = JES_HAVE_VALUE;
        }
        else if (ctx->state == JES_EXPECT_ARRAY_VALUE) {
          ctx->state = JES_HAVE_ARRAY_VALUE;
        }
        else {
          ctx->status = JES_UNEXPECTED_NODE;
          return 0;
        }

        json_len += (ctx->iter->length + sizeof("\"\"") - 1);
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
        if (ctx->state == JES_EXPECT_VALUE) {
          ctx->state = JES_HAVE_VALUE;
        }
        else if (ctx->state == JES_EXPECT_ARRAY_VALUE) {
          ctx->state = JES_HAVE_ARRAY_VALUE;
        }
        else {
          ctx->status = JES_UNEXPECTED_NODE;
           return 0;
        }

        json_len += ctx->iter->length;
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
    if (HAS_CHILD(ctx->iter)) {
      ctx->iter = jes_get_child(ctx, ctx->iter);
      continue;
    }

    /* Node has no child. if it's an object or array, forge a closing delimiter. */
    if (ctx->iter->type == JES_OBJECT) {
      /* This covers empty objects */
      json_len += sizeof("}") - 1;
      if (!compact) {
        indention -= 2;
      }
    }
    else if (ctx->iter->type == JES_ARRAY) {
      /* This covers empty array */
      json_len += sizeof("]") - 1;
      if (!compact) {
        indention -= 2;
      }
    }

    /* If the last child has a sibling then we've an array. Get the sibling and iterate the branch.
       Siblings must always be separated using a comma. */
    if (HAS_SIBLING(ctx->iter)) {

      if ((ctx->iter->type == JES_KEY) &&
          ((ctx->state != JES_EXPECT_ARRAY_VALUE) || (ctx->state != JES_HAVE_ARRAY_VALUE))) {
        ctx->status = JES_UNEXPECTED_NODE;
        return 0;
      }
      json_len += sizeof(",") - 1;
      ctx->iter = jes_get_sibling(ctx, ctx->iter);
      ctx->state = JES_EXPECT_ARRAY_VALUE;
      continue;
    }

    /* Node doesn't have any children or siblings. Iterate backward to the parent. */
    while ((ctx->iter = jes_get_parent(ctx, ctx->iter))) {

      if (ctx->iter->type == JES_KEY) {
        ctx->state = JES_HAVE_VALUE;
      }
      /* If the parent is an object or array, forge a closing delimiter. */
      else if (ctx->iter->type == JES_OBJECT) {
        if (!compact) {
          indention -= 2;
          json_len += indention + sizeof("\n") - 1;
        }
        json_len += sizeof("}") - 1;
      }
      else if (ctx->iter->type == JES_ARRAY) {
        if (!compact) {
          indention -= 2;
          json_len += indention + sizeof("\n") - 1;
        }
        json_len += sizeof("]") - 1;
      }
      else if ((ctx->iter->type == JES_KEY) && (ctx->state != JES_HAVE_VALUE)) {
        ctx->status = JES_UNEXPECTED_NODE;
        return 0;
      }

      /* If the parent has a sibling, take it and iterate the branch down.
         Siblings must always be separated using a comma. */
      if (HAS_SIBLING(ctx->iter)) {
        ctx->iter = jes_get_sibling(ctx, ctx->iter);
        json_len += sizeof(",") - 1;

        if (PARENT_TYPE(ctx, ctx->iter) == JES_OBJECT) {
          ctx->state = JES_EXPECT_KEY;
        }
        else if (PARENT_TYPE(ctx, ctx->iter) == JES_ARRAY) {
          ctx->state = JES_EXPECT_ARRAY_VALUE;
        }
        else {
          ctx->status = JES_UNEXPECTED_NODE;
          return 0;
        }
        break;
      }
    }

  } while (ctx->iter && (ctx->iter != ctx->root));

  ctx->iter = ctx->root;
  return json_len;
}

uint32_t jes_render(struct jes_context *ctx, char *buffer, uint32_t length, bool compact)
{
  char *dst = buffer;
  struct jes_element *iter = ctx->root;
  uint32_t required_buffer = 0;
  uint32_t indention = 0;

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

    if (iter->type == JES_OBJECT) {

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
    else if (iter->type == JES_KEY) {

      if (!compact) {
        *dst++ = '\n';
        memset(dst, ' ', indention*sizeof(char));
        dst += indention;
      }

      *dst++ = '"';
      dst = (char*)memcpy(dst, iter->value, iter->length) + iter->length;
      *dst++ = '"';
      *dst++ = ':';

      if (!compact) {
        *dst++ = ' ';
      }
    }
    else if (iter->type == JES_VALUE_STRING) {

      if (!compact) {
        if (PARENT_TYPE(ctx, iter) != JES_KEY) {
          *dst++ = '\n';
          memset(dst, ' ', indention*sizeof(char));
          dst += indention;
        }
      }

      *dst++ = '"';
      dst = (char*)memcpy(dst, iter->value, iter->length) + iter->length;
      *dst++ = '"';
    }
    else if ((iter->type == JES_VALUE_NUMBER)  ||
             (iter->type == JES_VALUE_TRUE)    ||
             (iter->type == JES_VALUE_FALSE)   ||
             (iter->type == JES_VALUE_NULL)) {

      if (!compact) {
        if (PARENT_TYPE(ctx, iter) != JES_KEY) {
          *dst++ = '\n';
          memset(dst, ' ', indention*sizeof(char));
          dst += indention;
        }
      }
      dst = (char*)memcpy(dst, iter->value, iter->length) + iter->length;
    }
    else if (iter->type == JES_ARRAY) {

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
      ctx->status = JES_UNEXPECTED_NODE;
      break;
    }

    /* Iterate this branch down to the last child */
    if (HAS_CHILD(iter)) {
      iter = jes_get_child(ctx, iter);
      continue;
    }

    /* Node has no child. if it's an object or array, forge a closing delimiter. */
    if (iter->type == JES_OBJECT) {
      /* This covers empty objects */
      if (!compact) {
        indention -= 2;
      }
      *dst++ = '}';
    }
    else if (iter->type == JES_ARRAY) {
      /* This covers empty array */
      if (!compact) {
        indention -= 2;
      }
      *dst++ = ']';
    }

    /* If Node has a sibling then Iterate the branch down.
       Siblings must always be separated using a comma. */
    if (HAS_SIBLING(iter)) {
      iter = jes_get_sibling(ctx, iter);
      *dst++ = ',';
      continue;
    }

    /* Node doesn't have any children or siblings. Iterate backward to the parent. */
    while ((iter = jes_get_parent(ctx, iter))) {
      /* If the parent is an object or array, forge a closing delimiter. */
      if (iter->type == JES_OBJECT) {
        if (!compact) {
          *dst++ = '\n';
          indention -= 2;
          memset(dst, ' ', indention*sizeof(char));
          dst += indention;
        }
        *dst++ = '}';
      }
      else if (iter->type == JES_ARRAY) {
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
        iter = jes_get_sibling(ctx, iter);
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
    return ctx->root;
  }
  return NULL;
}

struct jes_element* jes_get_key(struct jes_context *ctx, struct jes_element *parent_key, const char *keys)
{
  struct jes_element *target_key = NULL;
  struct jes_element *iter = NULL;
  uint32_t key_len;
  const char *key;
  char *dot;

  if (!ctx || !JES_IS_INITIATED(ctx) || !keys) {
    return NULL;
  }

  if (parent_key != NULL) {
    if (!jes_validate_element(ctx, parent_key) && (parent_key->type != JES_KEY)) {
      return NULL;
    }
    iter = GET_CHILD(ctx, parent_key);
  }
  else {
    iter = ctx->root;
  }

  while (iter) {
    key = keys;
    dot = strchr(keys, '.');
    if (dot) {
      key_len = dot - keys;
      keys = keys + key_len + sizeof(*dot);
    }
    else {
      key_len = strlen(keys);
      keys = keys + key_len;
    }

    iter = GET_CHILD(ctx, iter);

    while ((iter) && (iter->type == JES_KEY)) {
      if ((iter->length == key_len) && (0 == memcmp(iter->value, key, key_len))) {
        break;
      }
      iter = GET_SIBLING(ctx, iter);
    }

    if (*keys == '\0') {
      target_key = iter;
      break;
    }
    iter = GET_CHILD(ctx, iter);
  }

  return target_key;
}

struct jes_element* jes_get_key_value(struct jes_context *ctx, struct jes_element *key)
{
  struct jes_element *value_element = NULL;

  if (ctx && JES_IS_INITIATED(ctx) && key && jes_validate_element(ctx, key) && (key->type == JES_KEY)) {
    value_element = GET_CHILD(ctx, key);
  }

  return value_element;
}

uint16_t jes_get_array_size(struct jes_context *ctx, struct jes_element *array)
{
  uint16_t count = 0;
  struct jes_element *iter;

  if (ctx && array && jes_validate_element(ctx, array)) {
    iter = GET_CHILD(ctx, array);
    for (count = 0; iter != NULL; count++) {
      iter = GET_SIBLING(ctx, iter);
    }
  }
  return count;
}

struct jes_element* jes_get_array_value(struct jes_context *ctx, struct jes_element *array, int32_t index)
{
  struct jes_element *iter = NULL;

  if (ctx && array && jes_validate_element(ctx, array) && (array->type == JES_ARRAY)) {
    uint16_t array_count = jes_get_array_size(ctx, array);

    if (index < 0) { /* converting negative index to an index from the end of array. */
      if (-index <= array_count) {
        index = array_count + index;
      }
    }

    if (index >= 0) {
      iter = HAS_CHILD(array) ? &ctx->pool[array->first_child] : NULL;
      for (; iter && index > 0; index--) {
        iter = HAS_SIBLING(iter) ? &ctx->pool[iter->sibling] : NULL;
      }
    }
  }
  return iter;
}

struct jes_element* jes_add_element(struct jes_context *ctx, struct jes_element *parent, enum jes_type type, const char *value)
{
  struct jes_element *new_element = NULL;
  uint32_t length;
  if (!ctx) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  if (!parent && ctx->root) { /* JSON is not empty. Invalid request. */
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  if (parent && !jes_validate_element(ctx, parent)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  if (value) {
    length = strlen(value);
    if (length > 65535) {
      return NULL;
    }
    new_element = jes_append_element(ctx, parent, type, strlen(value), value);
    JES_LOG_NODE("\n    + ", (jes_node_descriptor)(new_element - ctx->pool), new_element->type, new_element->length, new_element->value,
                  new_element->parent, new_element->sibling, new_element->first_child, "");
  }
  else {
    new_element = jes_append_element(ctx, parent, type, 0, NULL);
    JES_LOG_NODE("\n    + ", (jes_node_descriptor)(new_element - ctx->pool), new_element->type, new_element->length, new_element->value,
                  new_element->parent, new_element->sibling, new_element->first_child, "");
  }

  return new_element;
}

struct jes_element* jes_add_key(struct jes_context *ctx, struct jes_element *parent_key, const char *keyword)
{
  struct jes_element *new_key = NULL;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx) || !keyword) {
    return NULL;
  }

  if (parent_key != NULL) {
    /* The key must be added to an existing key and must be embedded in an OBJECT */
     struct jes_element *object = GET_CHILD(ctx, parent_key);
    if (object == NULL) {
      object = jes_add_element(ctx, parent_key, JES_OBJECT, "");
    }

    assert(object->type == JES_OBJECT);
    new_key = jes_add_element(ctx, object, JES_KEY, keyword);
  }
  else {
    if (ctx->root == NULL) {
      jes_add_element(ctx, ctx->root, JES_OBJECT, "");
    }
    /* The key must be added to the root object */
    new_key = jes_add_element(ctx, ctx->root, JES_KEY, keyword);
  }

  return new_key;
}

struct jes_element* jes_add_key_before(struct jes_context *ctx, struct jes_element *key, const char *keyword)
{
  struct jes_element *new_key = NULL;
  struct jes_element *parent = NULL;
  struct jes_element *iter = NULL;
  struct jes_element *temp = NULL;
  uint32_t keyword_len;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if ((key == NULL) || !jes_validate_element(ctx, key) || (key->type != JES_KEY) || (keyword == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  parent = GET_PARENT(ctx, key);
  assert(parent != NULL);
  assert(parent->type == JES_OBJECT);

  keyword_len = strlen(keyword);
  if (keyword_len > 65535) {
    return NULL;
  }

  new_key = jes_allocate(ctx);
  if (new_key) {
    JES_FOR_EACH_KEY(ctx, parent, iter) {
      if (iter == key) {
        if (temp == NULL) {
          /* Key was the first element in the object group */
          new_key->sibling = parent->first_child;
          parent->first_child = (jes_node_descriptor)(new_key - ctx->pool); /* new_key's index */
        }
        else {
          new_key->sibling = temp->sibling;
          temp->sibling = (jes_node_descriptor)(new_key - ctx->pool); /* new_key's index */

        }
        new_key->parent = key->parent;
        new_key->type = JES_KEY;
        new_key->length = keyword_len;
        new_key->value = keyword;
        JES_LOG_NODE("\n    + ", (jes_node_descriptor)(new_key - ctx->pool), new_key->type, new_key->length, new_key->value,
                      new_key->parent, new_key->sibling, new_key->first_child, "");
        break;
      }
    }
    temp = iter;
  }

  return new_key;
}

struct jes_element* jes_add_key_after(struct jes_context *ctx, struct jes_element *key, const char *keyword)
{
  struct jes_element *new_key = NULL;
  struct jes_element *parent = NULL;
  uint32_t keyword_len;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if ((key == NULL) || !jes_validate_element(ctx, key) || (key->type != JES_KEY) || (keyword == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  parent = GET_PARENT(ctx, key);
  assert(parent != NULL);
  assert(parent->type == JES_OBJECT);

  keyword_len = strlen(keyword);
  if (keyword_len > 65535) {
    return NULL;
  }

  new_key = jes_allocate(ctx);
  if (new_key) {
    if (!HAS_SIBLING(key)) {
      /* Key was the last element in the object group */
      parent->last_child = (jes_node_descriptor)(new_key - ctx->pool); /* new_key's index */
    }

    new_key->sibling = key->sibling;
    key->sibling = (jes_node_descriptor)(new_key - ctx->pool); /* new_key's index */
    new_key->parent = key->parent;
    new_key->type = JES_KEY;
    new_key->length = keyword_len;
    new_key->value = keyword;
    JES_LOG_NODE("\n    + ", (jes_node_descriptor)(new_key - ctx->pool), new_key->type, new_key->length, new_key->value,
                  new_key->parent, new_key->sibling, new_key->first_child, "");
  }

  return new_key;
}


uint32_t jes_update_key(struct jes_context *ctx, struct jes_element *key, const char *keyword)
{
  uint32_t result = JES_INVALID_PARAMETER;

  if (ctx && key && jes_validate_element(ctx, key)) {
    if (key->type == JES_KEY) {
      size_t key_len = strlen(keyword);
      if (key_len < 65535) {
        key->length = key_len;
        key->value = keyword;
        result = JES_NO_ERROR;
      }
    }
  }
  return result;
}

uint32_t jes_update_key_value(struct jes_context *ctx, struct jes_element *key, enum jes_type type, const char *value)
{
  uint32_t result = JES_INVALID_PARAMETER;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return JES_INVALID_PARAMETER;
  }

  if ((key == NULL) || !jes_validate_element(ctx, key) || (key->type != JES_KEY) || (value == NULL)) {
    ctx->status = JES_INVALID_PARAMETER;
    return ctx->status;
  }

  jes_delete_element(ctx, GET_CHILD(ctx, key));
  if (jes_add_element(ctx, key, type, value)) {
    result = JES_NO_ERROR;
  }
  else {
    result = ctx->status;
  }

  return result;
}

uint32_t jes_update_key_value_to_object(struct jes_context *ctx, struct jes_element *key)
{
  return jes_update_key_value(ctx, key, JES_ARRAY, "");
}

uint32_t jes_update_key_value_to_array(struct jes_context *ctx, struct jes_element *key)
{
  return jes_update_key_value(ctx, key, JES_ARRAY, "");
}

uint32_t jes_update_key_value_to_true(struct jes_context *ctx, struct jes_element *key)
{
  return jes_update_key_value(ctx, key, JES_VALUE_TRUE, "true");
}

uint32_t jes_update_key_value_to_false(struct jes_context *ctx, struct jes_element *key)
{
  return jes_update_key_value(ctx, key, JES_VALUE_FALSE, "false");
}

uint32_t jes_update_key_value_to_null(struct jes_context *ctx, struct jes_element *key)
{
  return jes_update_key_value(ctx, key, JES_VALUE_NULL, "null");
}

struct jes_element* jes_update_array_value(struct jes_context *ctx, struct jes_element *array, int32_t index, enum jes_type type, const char *value)
{
  struct jes_element *value_element = NULL;
  int32_t array_size;

  if (!ctx || JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if (!array || !jes_validate_element(ctx, array) || (array->type == JES_ARRAY)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  if (!value || strnlen(value, JES_MAX_VALUE_LENGHT) == JES_MAX_VALUE_LENGHT) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  array_size = jes_get_array_size(ctx, array);
  if (index < 0) { /* converting negative index to an index from the end of array. */
    index = array_size + index;
  }

  if (index >= array_size) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  JES_ARRAY_FOR_EACH(ctx, array, value_element) {
    if (index-- == 0) {
      break;
    }
  }

  if (value_element) {
    while (HAS_CHILD(value_element)) {
      jes_delete_element(ctx, GET_CHILD(ctx, value_element));
    }
    value_element->type = type;
    value_element->length = (uint16_t)strlen(value);
    value_element->value = value;
  }

  return value_element;
}

size_t jes_get_node_count(struct jes_context *ctx)
{
  return ctx->node_count;
}

jes_status jes_get_status(struct jes_context *ctx)
{
  return ctx->status;
}

char* jes_stringify_status(struct jes_context *ctx, char *msg, size_t msg_len)
{
  if ((ctx == NULL) || (msg == NULL) || (msg_len == 0)) {
    return "";
  }


  switch (ctx->status) {
    case JES_NO_ERROR:
      snprintf(msg, msg_len, "%s(#%d)", jes_status_str[ctx->status], ctx->status);
      break;
    case JES_UNEXPECTED_TOKEN:
      snprintf( msg, msg_len,
                "%s(#%d): <%s> @[line:%d, pos:%d] (%.20s%.*s<<) state:<%s> after <%s> element",
                jes_status_str[ctx->status],
                ctx->status,
                jes_token_type_str[ctx->token.type],
                ctx->line_number,
                ctx->offset,
                ctx->token.offset >= 20 ? &ctx->json_data[ctx->token.offset - 20] : &ctx->json_data[0],
                ctx->token.length,
                &ctx->json_data[ctx->token.offset],
                jes_state_str[ctx->ext_status],
                jes_node_type_str[ctx->iter->type] );
      break;

    case JES_UNEXPECTED_NODE:
      snprintf( msg, msg_len, "%s(#%d) - %s: \"%.*s\" @state: %s",
                jes_status_str[ctx->status],
                ctx->status,
                jes_node_type_str[ctx->iter->type],
                ctx->iter->length,
                ctx->iter->value,
                jes_state_str[ctx->state]);
      break;

    default:
      snprintf(msg, msg_len, "%s(#%d)", jes_status_str[ctx->status], ctx->status);
      break;
  }
  return msg;
}

char* jes_stringify_element(struct jes_element *element, char *msg, size_t msg_len) {
  snprintf(msg, msg_len, "%s(%d)", jes_node_type_str[element->type], element->type);
  return msg;
}