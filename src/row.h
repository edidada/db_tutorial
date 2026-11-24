#ifndef DB_ROW_H
#define DB_ROW_H

#include "common.h"

void serialize_row(Row* source, void* destination);
void deserialize_row(void* source, Row* destination);
void print_row(Row* row);

#endif
