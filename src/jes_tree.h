#ifndef JES_TREE_H
#define JES_TREE_H

bool jes_validate_node(struct jes_context* ctx, struct jes_node* node);
struct jes_node* jes_insert_node(struct jes_context* ctx,
                                 struct jes_node* parent, struct jes_node* anchor,
                                 uint16_t type, uint16_t length, const char* value);

struct jes_node* jes_insert_key_node(struct jes_context* ctx,
                                        struct jes_node* parent_object,
                                        struct jes_node* anchor,
                                        uint16_t keyword_length, const char* keyword);
void jes_delete_node(struct jes_context* ctx, struct jes_node* node);

struct jes_node* jes_get_parent_node_of_type(struct jes_context* ctx,
                                              struct jes_node* node,
                                              enum jes_type type);

struct jes_node* jes_get_parent_node_of_type_object_or_array(struct jes_context* ctx,
                                                             struct jes_node* node);

struct jes_node* jes_find_key(struct jes_context* ctx,
                              struct jes_node* parent_object,
                              const char* keyword,
                              size_t keyword_lenngth);

#endif