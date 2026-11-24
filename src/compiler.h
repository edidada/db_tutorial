#ifndef DB_COMPILER_H
#define DB_COMPILER_H

#include "common.h"

/* SQL 前端：把输入行解析为 Statement */
PrepareResult prepare_statement(InputBuffer* input_buffer,
                                Statement* statement);

#endif
