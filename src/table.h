#ifndef DB_TABLE_H
#define DB_TABLE_H

#include "common.h"
#include "node.h"
#include "pager.h"

/* 数据库生命周期 */
Table* db_open(const char* filename);
void db_close(Table* table);

/* 查找与游标 */
Cursor* table_find(Table* table, uint32_t key);
Cursor* table_start(Table* table);
void* cursor_value(Cursor* cursor);
void cursor_advance(Cursor* cursor);

#endif
