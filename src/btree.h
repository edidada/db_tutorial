#ifndef DB_BTREE_H
#define DB_BTREE_H

#include "common.h"
#include "table.h"

/* 新建根节点（叶子或内部节点拆分根时复用） */
void create_new_root(Table* table, uint32_t right_child_page_num);

/* 内部节点：插入与拆分 */
void internal_node_insert(Table* table, uint32_t parent_page_num,
                          uint32_t child_page_num);
void internal_node_split_and_insert(Table* table, uint32_t parent_page_num,
                                    uint32_t child_page_num);
void update_internal_node_key(void* node, uint32_t old_key, uint32_t new_key);

/* 叶子节点：插入与拆分 */
void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value);
void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value);

#endif
