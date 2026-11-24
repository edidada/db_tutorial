#ifndef DB_REPL_H
#define DB_REPL_H

#include "common.h"
#include "table.h"

/* 读取-执行-打印循环 */
InputBuffer* new_input_buffer(void);
void print_prompt(void);
void read_input(InputBuffer* input_buffer);
void close_input_buffer(InputBuffer* input_buffer);

/* 元命令（以 '.' 开头，如 .exit / .btree / .constants） */
MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table);

#endif
