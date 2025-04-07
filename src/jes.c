#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "jes.h"

#ifdef JES_USE_32BIT_NODE_DESCRIPTOR
  #define JES_INVALID_INDEX 0xFFFFFFFF
#else
  #define JES_INVALID_INDEX 0xFFFF
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

#define HAS_PARENT(node_ptr) ((node_ptr)->parent < JES_INVALID_INDEX)
#define HAS_SIBLING(node_ptr) ((node_ptr)->sibling < JES_INVALID_INDEX)
#define HAS_CHILD(node_ptr) ((node_ptr)->first_child < JES_INVALID_INDEX)
#define HAS_LAST_CHILD(node_ptr) ((node_ptr)->last_child < JES_INVALID_INDEX)

#define GET_PARENT(ctx_, node_ptr) (HAS_PARENT(node_ptr) ? &ctx_->node_pool[(node_ptr)->parent] : NULL)
#define GET_SIBLING(ctx_, node_ptr) (HAS_SIBLING(node_ptr) ? &ctx_->node_pool[(node_ptr)->sibling] : NULL)
#define GET_CHILD(ctx_, node_ptr) (HAS_CHILD(node_ptr) ? &ctx_->node_pool[(node_ptr)->first_child] : NULL)
#define GET_LAST_CHILD(ctx_, node_ptr) (HAS_CHILD(node_ptr) ? &ctx_->node_pool[(node_ptr)->last_child] : NULL)

#define PARENT_TYPE(ctx_, node_ptr) (HAS_PARENT(node_ptr) ? ctx_->node_pool[(node_ptr)->parent].json_tlv.type : JES_UNKNOWN)

#define JES_GET_NODE_INDEX(ctx_, node_) ((jes_node_descriptor)((node_) - ctx_->node_pool))

#define JES_CONTEXT_COOKIE 0xABC09DEF
#define JES_IS_INITIATED(ctx_) (ctx_->cookie == JES_CONTEXT_COOKIE)

#define JES_HELPER_STR_LENGTH 20

static char jes_status_str[][JES_HELPER_STR_LENGTH] = {
  "NO_ERR",
  "PARSING_FAILED",
  "RENDER_FAILED",
  "OUT_OF_MEMORY",
  "UNEXPECTED_TOKEN",
  "UNEXPECTED_ELEMENT",
  "UNEXPECTED_EOF",
  "INVALID_PARAMETER",
  "ELEMENT_NOT_FOUND",
  "INVALID_CONTEXT",
  "BROKEN_TREE",
};

static char jes_token_type_str[][JES_HELPER_STR_LENGTH] = {
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
  "INVALID_TOKEN",
};

static char jes_node_type_str[][JES_HELPER_STR_LENGTH] = {
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

static char jes_state_str[][JES_HELPER_STR_LENGTH] = {
  "EXPECT_OBJECT",
  "EXPECT_KEY",
  "EXPECT_COLON",
  "EXPECT_KEY_VALUE",
  "HAVE_KEY_VALUE",
  "EXPECT_ARRAY_VALUE",
  "HAVE_ARRAY_VALUE",
};

enum jes_state {
  JES_EXPECT_OBJECT = 0,
  JES_EXPECT_KEY,
  JES_EXPECT_COLON,
  JES_EXPECT_KEY_VALUE,
  JES_HAVE_KEY_VALUE,
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

struct jes_node {
  /* Element containing TLV JSON data */
  struct jes_element json_tlv;
  /* Index of the parent node. Each node holds the index of its parent. */
  jes_node_descriptor parent;
  /* Index */
  jes_node_descriptor sibling;
  /* Each parent keeps only the index of its first child. The remaining child nodes
     will be tracked using the right member of the first child. */
  jes_node_descriptor first_child;
  /* The data member is a TLV (Type, Length, Value) which value is pointer to the
     actual value of the node. See jes.h */
  /* Index */
  jes_node_descriptor last_child;
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
   * The buffer will be used to allocate the context structure at first.
   * The remaining memory will be used as a pool of nodes (max. 65535 nodes). */
   struct jes_node *node_pool;
  /* node_pool size in bytes. (buffer size - context size) */
  uint32_t pool_size;
  /* Number of nodes that can be allocated on the given buffer. The value will
     be limited to 65535 in case of 16-bit node descriptors. */
  uint32_t capacity;
  /* Index of the last allocated node */
  jes_node_descriptor index;
  /* Holds the last token delivered by tokenizer. */
  struct jes_token token;
  /* Internal node iterator */
  struct jes_node *iter;
  /* Holds the main object node */
  struct jes_node *root;
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
static struct jes_element *jes_find_duplicate_key_node(struct jes_context *ctx,
                                                       struct jes_node *parent_object,
                                                       uint16_t keyword_length,
                                                       const char *keyword);
#endif

static struct jes_node* jes_allocate(struct jes_context *ctx)
{
  struct jes_node *new_node = NULL;

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
    if ((((void*)node - (void*)ctx->node_pool) % sizeof(*node)) == 0) {
      return true;
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

      if (HAS_CHILD(parent)) {
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

  return new_node;
}

static void jes_delete_node(struct jes_context *ctx, struct jes_node *node)
{
  struct jes_node *iter = node;

  if (node == NULL) {
    return;
  }

  /* TODO: Improve*/
  while (true) {
    //for (iter =
    while (HAS_CHILD(iter)) { iter = GET_CHILD(ctx, iter); }

    if (iter == node) {
      break;
    }

    if (HAS_PARENT(iter)) {
      ctx->node_pool[iter->parent].first_child = iter->sibling;
    }

    jes_free(ctx, iter);
    iter = &ctx->node_pool[iter->parent];
  }

  /* All sub-elements are deleted. To delete the node itself, all parent and sibling links need to be maintained. */
  iter = GET_PARENT(ctx, node);
  if (iter) {
    if (&ctx->node_pool[iter->first_child] == node) {
      iter->first_child = node->sibling;
    }
    else {
      /* Node is not the first child of it's parent. Need to iterate all children to reach node and maintain the linkage.*/
      iter = &ctx->node_pool[iter->first_child];
      while(iter) {
        if (&ctx->node_pool[iter->sibling] == node) {
          iter->sibling = node->sibling;
          break;
        }
        iter = &ctx->node_pool[iter->sibling];
      }
    }
  }

  jes_free(ctx, node);
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
    node = GET_CHILD(ctx, node);
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

uint32_t jes_delete_element(struct jes_context *ctx, struct jes_element *element)
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

static inline bool jes_get_delimiter_token(struct jes_context *ctx,
                                          char ch, struct jes_token *token)
{
  bool is_symbolic_token = true;

  switch (ch) {
    case '\0': UPDATE_TOKEN((*token), JES_TOKEN_EOF, ctx->offset, 1); break;
    case '{': UPDATE_TOKEN((*token), JES_TOKEN_OPENING_BRACE, ctx->offset, 1); break;
    case '}': UPDATE_TOKEN((*token), JES_TOKEN_CLOSING_BRACE, ctx->offset, 1); break;
    case '[': UPDATE_TOKEN((*token), JES_TOKEN_OPENING_BRACKET, ctx->offset, 1); break;
    case ']': UPDATE_TOKEN((*token), JES_TOKEN_CLOSING_BRACKET, ctx->offset, 1); break;
    case ':': UPDATE_TOKEN((*token), JES_TOKEN_COLON, ctx->offset, 1); break;
    case ',': UPDATE_TOKEN((*token), JES_TOKEN_COMMA, ctx->offset, 1); break;

    default:
      is_symbolic_token = false;
      break;
  }

  return is_symbolic_token;
}

static inline bool jes_is_delimiter_token(char ch)
{
  bool is_symbolic_token = false;

  if ((ch == '\0') ||
      (ch == '{')  ||
      (ch == '}')  ||
      (ch == '[')  ||
      (ch == ']')  ||
      (ch == ':')  ||
      (ch == ',')) {
      is_symbolic_token = true;
  }

  return is_symbolic_token;
}

/* The token is already a NUMBER. Try to feed it with more symbols. */
static inline bool jes_nurture_number_token(struct jes_context *ctx,
                                            char ch, struct jes_token *token)
{
  bool end_of_token = false;

  if (IS_DIGIT(ch)) {
    token->length++;
    ch = LOOK_AHEAD(ctx);
    if (!IS_DIGIT(ch) && (ch != '.')) { /* TODO: more symbols are acceptable in the middle of a number */
      end_of_token = true;
    }
  }
  else if (ch == '.') {
    token->length++;
    if (!IS_DIGIT(LOOK_AHEAD(ctx))) {
      token->type = JES_TOKEN_INVALID;
      end_of_token = true;
    }
  }
  else if (IS_SPACE(ch)) {
    end_of_token = true;
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
        /* Unlike STRINGs, there are symbols for NUMBERs to indicate the
           end of number data. To avoid consuming non-NUMBER characters, take a look ahead
           and stop the process if found of non-numeric symbols. */
        if (jes_is_delimiter_token(LOOK_AHEAD(ctx))) {
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
      if (jes_nurture_number_token(ctx, ch, &token)) {
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

  return token;
}

#ifndef JES_ALLOW_DUPLICATE_KEYS
static struct jes_node *jes_find_duplicate_key_node(struct jes_context *ctx,
                                                    struct jes_node *parent_object,
                                                    uint16_t keyword_length,
                                                    const char *keyword)
{
  struct jes_node *duplicate = NULL;
  struct jes_node *iter = NULL;

  assert(parent_object->json_tlv.type == JES_OBJECT);

  iter = GET_CHILD(ctx, parent_object);

  while(iter) {
    assert(iter->json_tlv.type == JES_KEY);
    if ((iter->json_tlv.length == keyword_length) &&
        (strncmp(iter->json_tlv.value, keyword, keyword_length) == 0)) {
      duplicate = iter;
      break;
    }
    iter = GET_SIBLING(ctx, iter);
  }
  return duplicate;
}
#endif

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

static struct jes_node* jes_add_key_node(struct jes_context *ctx, struct jes_node *parent, uint16_t keyword_length, const char *keyword)
{
  struct jes_node *new_node = NULL;
  assert(parent->json_tlv.type == JES_OBJECT);

#ifndef JES_ALLOW_DUPLICATE_KEYS
  /* No duplicate keys in the same object are allowed.
     Only the last key:value will be reported if the keys are duplicated. */
  struct jes_node *node = jes_find_duplicate_key_node(ctx, parent, keyword_length, keyword);
  if (node) {
    /* Re-use the duplicate node after deleting its value */
    jes_delete_node(ctx, GET_CHILD(ctx, node));
    new_node = node;
  }
  else
#endif
  {
    new_node = jes_append_node(ctx, parent, JES_KEY, keyword_length, keyword);
  }

  if (new_node) {
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
  ctx->pool_size = buffer_size - (uint32_t)(sizeof(struct jes_context));
  ctx->capacity = (ctx->pool_size / sizeof(struct jes_node)) < JES_INVALID_INDEX
                 ? (jes_node_descriptor)(ctx->pool_size / sizeof(struct jes_node))
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
    if (HAS_CHILD(ctx->iter)) {
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
    if (HAS_CHILD(ctx->iter)) {
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
    if (!HAS_CHILD(ctx->iter)) {
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
    ctx->iter = jes_add_key_node(ctx, ctx->iter, ctx->token.length, &ctx->json_data[ctx->token.offset]);
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

uint32_t jes_load(struct jes_context *ctx, const char *json_data, uint32_t json_length)
{
  ctx->state = JES_EXPECT_OBJECT;
  ctx->json_data = json_data;
  ctx->json_size = json_length;

  do {
#if 0
    printf("\n    state: %s", jes_state_str[ctx->state]);
#endif
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

      case JES_TOKEN_INVALID:
        ctx->status = JES_PARSING_FAILED;
        break;

      default:
        assert(0);
        ctx->status = JES_PARSING_FAILED;
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
    if (HAS_CHILD(ctx->iter)) {
      ctx->iter = GET_CHILD(ctx, ctx->iter);
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
    if (HAS_CHILD(iter)) {
      iter = GET_CHILD(ctx, iter);
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

struct jes_element* jes_get_key(struct jes_context *ctx, struct jes_element *parent_key, const char *keys)
{
  struct jes_node *target_key = NULL;
  struct jes_node *iter = NULL;
  uint32_t key_len;
  const char *key;
  char *dot;


  if (!ctx || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if (!keys) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  if (parent_key != NULL) {
    if (!jes_validate_node(ctx, (struct jes_node*)parent_key) || (parent_key->type != JES_KEY)) {
      ctx->status = JES_INVALID_PARAMETER;
      return NULL;
    }
    iter = GET_CHILD(ctx, (struct jes_node*)parent_key);
  }
  else {
    iter = ctx->root;
  }

  if (iter->json_tlv.type != JES_OBJECT) {
    ctx->status = JES_UNEXPECTED_ELEMENT;
    return NULL;
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

    while ((iter) && (iter->json_tlv.type == JES_KEY)) {
      if ((iter->json_tlv.length == key_len) && (0 == memcmp(iter->json_tlv.value, key, key_len))) {
        break;
      }
      iter = GET_SIBLING(ctx, iter);
    }

    if (iter) {
      if (*keys == '\0') {
        target_key = iter;
        break;
      }
      iter = GET_CHILD(ctx, iter);
    }
  }

  if (target_key) {
    return &target_key->json_tlv;
  }

  ctx->status = JES_ELEMENT_NOT_FOUND;
  return NULL;
}

struct jes_element* jes_get_key_value(struct jes_context *ctx, struct jes_element *key)
{
  struct jes_node *node = NULL;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if (!jes_validate_node(ctx, (struct jes_node*)key) || (key->type != JES_KEY)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  node = GET_CHILD(ctx, (struct jes_node*)key);
  return (struct jes_element*)node;
}

uint16_t jes_get_array_size(struct jes_context *ctx, struct jes_element *array)
{
  uint16_t array_size = 0;
  struct jes_node *iter = NULL;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return 0;
  }

  if (!jes_validate_node(ctx, (struct jes_node*)array) || (array->type != JES_ARRAY)) {
    ctx->status = JES_INVALID_PARAMETER;
    return 0;
  }

  iter = GET_CHILD(ctx, (struct jes_node*)array);
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

  iter = GET_CHILD(ctx, (struct jes_node*)array);
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

  if (!ctx) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  if (!parent && ctx->root) { /* JSON is not empty. Invalid request. */
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  if (parent && !jes_validate_node(ctx, (struct jes_node*)parent)) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

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

struct jes_element* jes_add_key(struct jes_context *ctx, struct jes_element *parent_key, const char *keyword)
{
  struct jes_element *new_key = NULL;
  struct jes_node *object = ctx->root;
  struct jes_node *new_node = NULL;
  uint16_t keyword_length = 0;

  if ((ctx == NULL) || !JES_IS_INITIATED(ctx)) {
    return NULL;
  }

  if (!keyword) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  keyword_length = strnlen(keyword, JES_MAX_VALUE_LENGTH);
  if (keyword_length == JES_MAX_VALUE_LENGTH) {
    ctx->status = JES_INVALID_PARAMETER;
    return NULL;
  }

  if (parent_key != NULL) {
    if (!jes_validate_node(ctx, (struct jes_node*)parent_key)) {
      ctx->status = JES_INVALID_PARAMETER;
      return NULL;
    }
    /* The key must be added to an existing key and must be embedded in an OBJECT */
    object = GET_CHILD(ctx, (struct jes_node*)parent_key);
    if (object == NULL) {
      object = jes_append_node(ctx, (struct jes_node*)parent_key, JES_OBJECT, 1, "{");
    }
    else if (object->json_tlv.type != JES_OBJECT) {
      ctx->status = JES_UNEXPECTED_ELEMENT;
      return NULL;
    }
  }
  else { /* No parent key is introduced. start from root object */
    if (ctx->root == NULL) {
      /* Tree root is empty. Create the first OBJECT node. */
      object = jes_append_node(ctx, ctx->root, JES_OBJECT, 1, "{");
    }
  }

  new_node = jes_append_node(ctx, object, JES_KEY, keyword_length, keyword);
  if (new_node) {
    new_key = &new_node->json_tlv;
    JES_LOG_NODE("\n    + ", JES_GET_NODE_INDEX(ctx, new_node),
              new_key->type, new_key->length, new_key->value,
              new_node->parent, new_node->sibling, new_node->first_child, "");
  }

  return new_key;
}

struct jes_element* jes_add_key_before(struct jes_context *ctx, struct jes_element *key, const char *keyword)
{
  struct jes_node *new_node = NULL;
  struct jes_node *parent = NULL;
  struct jes_node *iter = NULL;
  struct jes_node *temp = NULL;
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

  new_node = jes_allocate(ctx);

  if (new_node) {
    for (iter = GET_CHILD(ctx, parent); iter != NULL; iter = GET_SIBLING(ctx, iter)) {
      assert(iter->json_tlv.type == JES_KEY);
      if (iter == key_node) {
        if (temp == NULL) {
          /* Key was the first element in the object group */
          new_node->sibling = parent->first_child;
          parent->first_child = JES_GET_NODE_INDEX(ctx, new_node);
        }
        else {
          new_node->sibling = temp->sibling;
          temp->sibling = JES_GET_NODE_INDEX(ctx, new_node);

        }
        new_node->parent = key_node->parent;
        new_node->json_tlv.type = JES_KEY;
        new_node->json_tlv.length = keyword_length;
        new_node->json_tlv.value = keyword;
        JES_LOG_NODE("\n    + ", JES_GET_NODE_INDEX(ctx, new_node),
                      new_node->json_tlv.type, new_node->json_tlv.length, new_node->json_tlv.value,
                      new_node->parent, new_node->sibling, new_node->first_child, "");
        break;
      }
    }
    temp = iter;
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

  new_node = jes_allocate(ctx);
  if (new_node) {
    if (!HAS_SIBLING(key_node)) {
      /* Key was the last element in the object group */
      parent->last_child = JES_GET_NODE_INDEX(ctx, new_node);
    }

    new_node->sibling = key_node->sibling;
    key_node->sibling = JES_GET_NODE_INDEX(ctx, new_node);
    new_node->parent = key_node->parent;
    new_node->json_tlv.type = JES_KEY;
    new_node->json_tlv.length = keyword_length;
    new_node->json_tlv.value = keyword;
    JES_LOG_NODE("\n    + ", JES_GET_NODE_INDEX(ctx, new_node),
                  new_node->json_tlv.type, new_node->json_tlv.length, new_node->json_tlv.value,
                  new_node->parent, new_node->sibling, new_node->first_child, "");
  }

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

  for(target_node = GET_CHILD(ctx, (struct jes_node*)array); target_node != NULL; target_node = GET_SIBLING(ctx, target_node)) {
    if (index-- == 0) {
      break;
    }
  }

  if (target_node) {
    /* We'll not delete the target_node to keep the original array order. Just update its JSON TLV.
     * The rest of the branch however must be removed. */
    jes_delete_node(ctx, GET_CHILD(ctx, target_node));
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

  for (anchor_node = GET_CHILD(ctx, (struct jes_node*)array); anchor_node != NULL; anchor_node = GET_SIBLING(ctx, anchor_node)) {
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

      if (((struct jes_node*)array)->first_child ==  JES_GET_NODE_INDEX(ctx, anchor_node)) {
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

  /* TODO: provide more status */
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
                jes_node_type_str[ctx->iter->json_tlv.type] );
      break;

    case JES_PARSING_FAILED:
      snprintf( msg, msg_len,
                "%s(#%d): <%s> @[line:%d, pos:%d] (%.5s%.*s<<)",
                jes_status_str[ctx->status],
                ctx->status,
                jes_token_type_str[ctx->token.type],
                ctx->line_number,
                ctx->offset,
                ctx->token.offset >= 5 ? &ctx->json_data[ctx->token.offset - 5] : &ctx->json_data[0],
                ctx->token.length,
                &ctx->json_data[ctx->token.offset]);
      break;
    case JES_UNEXPECTED_ELEMENT:
      snprintf( msg, msg_len, "%s(#%d) - %s: \"%.*s\" @state: %s",
                jes_status_str[ctx->status],
                ctx->status,
                jes_node_type_str[ctx->iter->json_tlv.type],
                ctx->iter->json_tlv.length,
                ctx->iter->json_tlv.value,
                jes_state_str[ctx->state]);
      break;
    case JES_RENDER_FAILED:
      snprintf( msg, msg_len, "%s(#%d) - %s: \"%.*s\" @state: %s",
                jes_status_str[ctx->status],
                ctx->status,
                jes_node_type_str[ctx->iter->json_tlv.type],
                ctx->iter->json_tlv.length,
                ctx->iter->json_tlv.value,
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