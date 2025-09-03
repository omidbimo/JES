#ifndef JES_TREE_H
#define JES_TREE_H

jes_status jes_tree_init(struct jes_context* ctx, void *buffer, size_t buffer_size);
jes_status jes_tree_resize(struct jes_node_mng_context* ctx, void *buffer, size_t buffer_size);

bool jes_validate_node(struct jes_context* ctx, struct jes_node* node);

struct jes_node* jes_tree_insert_node(struct jes_context* ctx,
                                      struct jes_node* parent, struct jes_node* anchor,
                                      uint16_t type, uint16_t length, const char* value);

struct jes_node* jes_tree_insert_key_node(struct jes_context* ctx,
                                          struct jes_node* parent_object,
                                          struct jes_node* anchor,
                                          uint16_t keyword_length, const char* keyword);

void jes_tree_delete_node(struct jes_context* ctx, struct jes_node* node);

/**
 * @brief Finds the nearest parent node of a specific type in the JSON tree.
 *
 * This function traverses up the JSON tree hierarchy starting from the given node,
 * searching for the first ancestor that matches the specified type (e.g., JES_OBJECT,
 * JES_ARRAY, JES_KEY, etc.).
 */
struct jes_node* jes_tree_get_parent_node_by_type(struct jes_context* ctx,
                                                  struct jes_node* node,
                                                  enum jes_type type);

/**
 * @brief Finds the nearest container parent (object or array) of a JSON node.
 *
 * This function traverses up the JSON tree hierarchy starting from the given node,
 * looking for the first ancestor that is either an object or array.
 */
struct jes_node* jes_tree_get_container_parent_node(struct jes_context* ctx,
                                                    struct jes_node* node);

struct jes_node* jes_tree_get_parent_node(struct jes_context* ctx,
                                          struct jes_node* node);

struct jes_node* jes_tree_get_child_node(struct jes_context* ctx,
                                          struct jes_node* node);

struct jes_node* jes_tree_get_sibling_node(struct jes_context* ctx,
                                          struct jes_node* node);

#endif