---
title: 第 3 部分 - 内存中的、仅追加的、单表数据库
date: 2017-09-01
---

我们会从很小的规模开始，给数据库加上很多限制。目前它会：

- 支持两种操作：插入一行和打印所有行
- 只驻留在内存中（不持久化到磁盘）
- 只支持一张硬编码的表

我们硬编码的这张表用来存储用户，长这样：

| 列       | 类型         |
|----------|--------------|
| id       | integer      |
| username | varchar(32)  |
| email    | varchar(255) |

这是一个简单的模式（schema），但它让我们能够支持多种数据类型以及多种尺寸的文本数据类型。

`insert` 语句现在长这样：

```
insert 1 cstack foo@bar.com
```

这意味着我们需要升级 `prepare_statement` 函数来解析参数

```diff
   if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
     statement->type = STATEMENT_INSERT;
+    int args_assigned = sscanf(
+        input_buffer->buffer, "insert %d %s %s", &(statement->row_to_insert.id),
+        statement->row_to_insert.username, statement->row_to_insert.email);
+    if (args_assigned < 3) {
+      return PREPARE_SYNTAX_ERROR;
+    }
     return PREPARE_SUCCESS;
   }
   if (strcmp(input_buffer->buffer, "select") == 0) {
```

我们把这些解析出来的参数存进语句对象里一个新的 `Row` 数据结构中：

```diff
+#define COLUMN_USERNAME_SIZE 32
+#define COLUMN_EMAIL_SIZE 255
+typedef struct {
+  uint32_t id;
+  char username[COLUMN_USERNAME_SIZE];
+  char email[COLUMN_EMAIL_SIZE];
+} Row;
+
 typedef struct {
   StatementType type;
+  Row row_to_insert;  // only used by insert statement
 } Statement;
```

现在我们需要把这些数据复制到某个表示表的数据结构中。SQLite 使用 B 树来实现快速查找、插入和删除。我们会从更简单的东西开始。和 B 树一样，它会把行分组放进页（page）里，但它不是把这些页组织成树，而是组织成数组。

这是我的计划：

- 把行存储在叫做“页”的内存块中
- 每个页尽量存放更多的行
- 行在每个页内被序列化为紧凑的表示形式
- 页只在需要时才分配
- 维护一个固定大小的指针数组来指向各个页

首先我们定义一行的紧凑表示：
```diff
+#define size_of_attribute(Struct, Attribute) sizeof(((Struct*)0)->Attribute)
+
+const uint32_t ID_SIZE = size_of_attribute(Row, id);
+const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);
+const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);
+const uint32_t ID_OFFSET = 0;
+const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
+const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
+const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;
```

这意味着一行序列化后的布局如下：

| 列       | 大小（字节） | 偏移量 |
|----------|--------------|--------|
| id       | 4            | 0      |
| username | 32           | 4      |
| email    | 255          | 36     |
| 合计     | 291          |        |

我们还需要代码来实现与紧凑表示之间的相互转换。
```diff
+void serialize_row(Row* source, void* destination) {
+  memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
+  memcpy(destination + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
+  memcpy(destination + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
+}
+
+void deserialize_row(void* source, Row* destination) {
+  memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
+  memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
+  memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
+}
```

接下来是一个 `Table` 结构体，它指向存放行的页，并跟踪一共有多少行：
```diff
+const uint32_t PAGE_SIZE = 4096;
+#define TABLE_MAX_PAGES 100
+const uint32_t ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE;
+const uint32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES;
+
+typedef struct {
+  uint32_t num_rows;
+  void* pages[TABLE_MAX_PAGES];
+} Table;
```

我把页大小设为 4 KB，因为它和大多数计算机体系结构的虚拟内存系统所使用的页大小相同。这意味着我们数据库中的一个页对应于操作系统使用的一个页。操作系统会以整个页为单位把页移入和移出内存，而不是把它们拆开。

我随意设定了 100 页的分配上限。当我们切换到树结构时，数据库的最大尺寸将只受文件最大尺寸的限制。（不过我们仍然会限制同时保存在内存中的页数）

行不应该跨越页的边界。由于各个页在内存中可能并不相邻，这个假设让我们读写行更容易。

说到这个，下面是我们如何计算访问某一行时应该在内存的什么位置读写：
```diff
+void* row_slot(Table* table, uint32_t row_num) {
+  uint32_t page_num = row_num / ROWS_PER_PAGE;
+  void* page = table->pages[page_num];
+  if (page == NULL) {
+    // Allocate memory only when we try to access page
+    page = table->pages[page_num] = malloc(PAGE_SIZE);
+  }
+  uint32_t row_offset = row_num % ROWS_PER_PAGE;
+  uint32_t byte_offset = row_offset * ROW_SIZE;
+  return page + byte_offset;
+}
```

现在我们能让 `execute_statement` 从表结构中读写数据了：
```diff
-void execute_statement(Statement* statement) {
+ExecuteResult execute_insert(Statement* statement, Table* table) {
+  if (table->num_rows >= TABLE_MAX_ROWS) {
+    return EXECUTE_TABLE_FULL;
+  }
+
+  Row* row_to_insert = &(statement->row_to_insert);
+
+  serialize_row(row_to_insert, row_slot(table, table->num_rows));
+  table->num_rows += 1;
+
+  return EXECUTE_SUCCESS;
+}
+
+ExecuteResult execute_select(Statement* statement, Table* table) {
+  Row row;
+  for (uint32_t i = 0; i < table->num_rows; i++) {
+    deserialize_row(row_slot(table, i), &row);
+    print_row(&row);
+  }
+  return EXECUTE_SUCCESS;
+}
+
+ExecuteResult execute_statement(Statement* statement, Table* table) {
   switch (statement->type) {
     case (STATEMENT_INSERT):
-      printf("This is where we would do an insert.\n");
-      break;
+      return execute_insert(statement, table);
     case (STATEMENT_SELECT):
-      printf("This is where we would do a select.\n");
-      break;
+      return execute_select(statement, table);
   }
 }
```

最后，我们需要初始化表、创建相应的内存释放函数，并处理另外几种错误情况：

```diff
+ Table* new_table() {
+  Table* table = (Table*)malloc(sizeof(Table));
+  table->num_rows = 0;
+  for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
+     table->pages[i] = NULL;
+  }
+  return table;
+}
+
+void free_table(Table* table) {
+    for (int i = 0; table->pages[i]; i++) {
+	free(table->pages[i]);
+    }
+    free(table);
+}
```
```diff
 int main(int argc, char* argv[]) {
+  Table* table = new_table();
   InputBuffer* input_buffer = new_input_buffer();
   while (true) {
     print_prompt();
@@ -105,13 +203,22 @@ int main(int argc, char* argv[]) {
     switch (prepare_statement(input_buffer, &statement)) {
       case (PREPARE_SUCCESS):
         break;
+      case (PREPARE_SYNTAX_ERROR):
+        printf("Syntax error. Could not parse statement.\n");
+        continue;
       case (PREPARE_UNRECOGNIZED_STATEMENT):
         printf("Unrecognized keyword at start of '%s'.\n",
                input_buffer->buffer);
         continue;
     }

-    execute_statement(&statement);
-    printf("Executed.\n");
+    switch (execute_statement(&statement, table)) {
+      case (EXECUTE_SUCCESS):
+        printf("Executed.\n");
+        break;
+      case (EXECUTE_TABLE_FULL):
+        printf("Error: Table full.\n");
+        break;
+    }
   }
 }
 ```

有了这些改动，我们真的可以在数据库里保存数据了！
 ```command-line
~ ./db
db > insert 1 cstack foo@bar.com
Executed.
db > insert 2 bob bob@example.com
Executed.
db > select
(1, cstack, foo@bar.com)
(2, bob, bob@example.com)
Executed.
db > insert foo bar 1
Syntax error. Could not parse statement.
db > .exit
~
```

现在是写一些测试的好时机，原因有两个：
- 我们计划大幅改变存储表的数据结构，而测试可以捕捉回归问题。
- 有几个边界情况我们还没有手动测试过（例如把表填满）。

我们会在下一部分解决这些问题。现在，下面是这一部分的完整 diff：
```diff
@@ -2,6 +2,7 @@
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
+#include <stdint.h>

 typedef struct {
   char* buffer;
@@ -10,6 +11,105 @@ typedef struct {
 } InputBuffer;

+typedef enum { EXECUTE_SUCCESS, EXECUTE_TABLE_FULL } ExecuteResult;
+
+typedef enum {
+  META_COMMAND_SUCCESS,
+  META_COMMAND_UNRECOGNIZED_COMMAND
+} MetaCommandResult;
+
+typedef enum {
+  PREPARE_SUCCESS,
+  PREPARE_SYNTAX_ERROR,
+  PREPARE_UNRECOGNIZED_STATEMENT
+ } PrepareResult;
+
+typedef enum { STATEMENT_INSERT, STATEMENT_SELECT } StatementType;
+
+#define COLUMN_USERNAME_SIZE 32
+#define COLUMN_EMAIL_SIZE 255
+typedef struct {
+  uint32_t id;
+  char username[COLUMN_USERNAME_SIZE];
+  char email[COLUMN_EMAIL_SIZE];
+} Row;
+
+typedef struct {
+  StatementType type;
+  Row row_to_insert; //only used by insert statement
+} Statement;
+
+#define size_of_attribute(Struct, Attribute) sizeof(((Struct*)0)->Attribute)
+
+const uint32_t ID_SIZE = size_of_attribute(Row, id);
+const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);
+const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);
+const uint32_t ID_OFFSET = 0;
+const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
+const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
+const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;
+
+const uint32_t PAGE_SIZE = 4096;
+#define TABLE_MAX_PAGES 100
+const uint32_t ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE;
+const uint32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES;
+
+typedef struct {
+  uint32_t num_rows;
+  void* pages[TABLE_MAX_PAGES];
+} Table;
+
+void print_row(Row* row) {
+  printf("(%d, %s, %s)\n", row->id, row->username, row->email);
+}
+
+void serialize_row(Row* source, void* destination) {
+  memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
+  memcpy(destination + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
+  memcpy(destination + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
+}
+
+void deserialize_row(void *source, Row* destination) {
+  memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
+  memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
+  memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
+}
+
+void* row_slot(Table* table, uint32_t row_num) {
+  uint32_t page_num = row_num / ROWS_PER_PAGE;
+  void *page = table->pages[page_num];
+  if (page == NULL) {
+     // Allocate memory only when we try to access page
+     page = table->pages[page_num] = malloc(PAGE_SIZE);
+  }
+  uint32_t row_offset = row_num % ROWS_PER_PAGE;
+  uint32_t byte_offset = row_offset * ROW_SIZE;
+  return page + byte_offset;
+}
+
+Table* new_table() {
+  Table* table = (Table*)malloc(sizeof(Table));
+  table->num_rows = 0;
+  for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
+     table->pages[i] = NULL;
+  }
+  return table;
+}
+
+void free_table(Table* table) {
+  for (int i = 0; table->pages[i]; i++) {
+     free(table->pages[i]);
+  }
+  free(table);
+}
+
 InputBuffer* new_input_buffer() {
   InputBuffer* input_buffer = (InputBuffer*)malloc(sizeof(InputBuffer));
   input_buffer->buffer = NULL;
@@ -40,17 +140,105 @@ void close_input_buffer(InputBuffer* input_buffer) {
     free(input_buffer);
 }

+MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table *table) {
+  if (strcmp(input_buffer->buffer, ".exit") == 0) {
+    close_input_buffer(input_buffer);
+    free_table(table);
+    exit(EXIT_SUCCESS);
+  } else {
+    return META_COMMAND_UNRECOGNIZED_COMMAND;
+  }
+}
+
+PrepareResult prepare_statement(InputBuffer* input_buffer,
+                                Statement* statement) {
+  if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
+    statement->type = STATEMENT_INSERT;
+    int args_assigned = sscanf(
+	input_buffer->buffer, "insert %d %s %s", &(statement->row_to_insert.id),
+	statement->row_to_insert.username, statement->row_to_insert.email
+	);
+    if (args_assigned < 3) {
+	return PREPARE_SYNTAX_ERROR;
+    }
+    return PREPARE_SUCCESS;
+  }
+  if (strcmp(input_buffer->buffer, "select") == 0) {
+    statement->type = STATEMENT_SELECT;
+    return PREPARE_SUCCESS;
+  }
+
+  return PREPARE_UNRECOGNIZED_STATEMENT;
+}
+
+ExecuteResult execute_insert(Statement* statement, Table* table) {
+  if (table->num_rows >= TABLE_MAX_ROWS) {
+     return EXECUTE_TABLE_FULL;
+  }
+
+  Row* row_to_insert = &(statement->row_to_insert);
+
+  serialize_row(row_to_insert, row_slot(table, table->num_rows));
+  table->num_rows += 1;
+
+  return EXECUTE_SUCCESS;
+}
+
+ExecuteResult execute_select(Statement* statement, Table* table) {
+  Row row;
+  for (uint32_t i = 0; i < table->num_rows; i++) {
+     deserialize_row(row_slot(table, i), &row);
+     print_row(&row);
+  }
+  return EXECUTE_SUCCESS;
+}
+
+ExecuteResult execute_statement(Statement* statement, Table *table) {
+  switch (statement->type) {
+    case (STATEMENT_INSERT):
+       	return execute_insert(statement, table);
+    case (STATEMENT_SELECT):
+	return execute_select(statement, table);
+  }
+}
+
 int main(int argc, char* argv[]) {
+  Table* table = new_table();
   InputBuffer* input_buffer = new_input_buffer();
   while (true) {
     print_prompt();
     read_input(input_buffer);

-    if (strcmp(input_buffer->buffer, ".exit") == 0) {
-      close_input_buffer(input_buffer);
-      exit(EXIT_SUCCESS);
-    } else {
-      printf("Unrecognized command '%s'.\n", input_buffer->buffer);
+    if (input_buffer->buffer[0] == '.') {
+      switch (do_meta_command(input_buffer, table)) {
+        case (META_COMMAND_SUCCESS):
+          continue;
+        case (META_COMMAND_UNRECOGNIZED_COMMAND):
+          printf("Unrecognized command '%s'\n", input_buffer->buffer);
+          continue;
+      }
+    }
+
+    Statement statement;
+    switch (prepare_statement(input_buffer, &statement)) {
+      case (PREPARE_SUCCESS):
+        break;
+      case (PREPARE_SYNTAX_ERROR):
+	printf("Syntax error. Could not parse statement.\n");
+	continue;
+      case (PREPARE_UNRECOGNIZED_STATEMENT):
+        printf("Unrecognized keyword at start of '%s'.\n",
+               input_buffer->buffer);
+        continue;
+    }
+
+    switch (execute_statement(&statement, table)) {
+	case (EXECUTE_SUCCESS):
+	    printf("Executed.\n");
+	    break;
+	case (EXECUTE_TABLE_FULL):
+	    printf("Error: Table full.\n");
+	    break;
     }
   }
 }
```
