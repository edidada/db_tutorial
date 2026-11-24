#ifndef DB_NODE_H
#define DB_NODE_H

#include "common.h"
#include "pager.h"

/* 节点头部字段访问 */
NodeType get_node_type(void* node);
void set_node_type(void* node, NodeType type);
bool is_node_root(void* node);
void set_node_root(void* node, bool is_root);
uint32_t* node_parent(void* node);

/* 内部节点单元格访问 */
uint32_t* internal_node_num_keys(void* node);
uint32_t* internal_node_right_child(void* node);
uint32_t* internal_node_cell(void* node, uint32_t cell_num);
uint32_t* internal_node_child(void* node, uint32_t child_num);
uint32_t* internal_node_key(void* node, uint32_t key_num);

/* 叶子节点单元格访问 */
uint32_t* leaf_node_num_cells(void* node);
uint32_t* leaf_node_next_leaf(void* node);
void* leaf_node_cell(void* node, uint32_t cell_num);
uint32_t* leaf_node_key(void* node, uint32_t cell_num);
void* leaf_node_value(void* node, uint32_t cell_num);

/* 初始化 */
void initialize_leaf_node(void* node);
void initialize_internal_node(void* node);

/* 工具 */
uint32_t get_node_max_key(Pager* pager, void* node);
uint32_t internal_node_find_child(void* node, uint32_t key);
void print_constants(void);
void print_tree(Pager* pager, uint32_t page_num, uint32_t indentation_level);

#endif
