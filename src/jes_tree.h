#ifndef JES_TREE_H
#define JES_TREE_H

bool jes_validate_node(struct jes_context *ctx, struct jes_node *node);
void jes_add_object_node(struct jes_context *ctx);
void jes_add_array_node(struct jes_context *ctx);
void jes_add_value_node(struct jes_context *ctx, enum jes_type value_type);
struct jes_node* jes_add_node_after(struct jes_context* ctx,
                                    struct jes_node* parent, struct jes_node* anchor_node,
                                    uint16_t type, uint16_t length, const char* value);
struct jes_node* jes_add_key_node_after(struct jes_context *ctx,
                                        struct jes_node *parent_object,
                                        struct jes_node *anchor,
                                        uint16_t keyword_length, const char *keyword);
void jes_delete_node(struct jes_context *ctx, struct jes_node *node);

struct jes_node* jes_get_parent_node_of_type(struct jes_context *ctx,
                                              struct jes_node *node,
                                              enum jes_type type);

struct jes_node* jes_get_parent_node_of_type_object_or_array(struct jes_context *ctx,
                                                             struct jes_node *node);

struct jes_node* jes_append_node(struct jes_context *ctx, struct jes_node *parent,
                                 uint16_t type, uint16_t length, const char *value);
#endif