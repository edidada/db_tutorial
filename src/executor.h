#ifndef DB_EXECUTOR_H
#define DB_EXECUTOR_H

#include "common.h"
#include "table.h"

/* 虚拟机：执行已解析的语句 */
ExecuteResult execute_statement(Statement* statement, Table* table);

#endif
