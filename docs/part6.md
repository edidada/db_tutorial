---
title: 第 6 部分 - 游标抽象
date: 2017-09-10
---

这一部分应该比上一部分短一些。我们只是做一些重构，让 B 树的实现更容易开始。

我们要增加一个 `Cursor`（游标）对象，它表示表中的一个位置。你可能想用游标做的事情有：

- 在表的开头创建一个游标
- 在表的末尾创建一个游标
- 访问游标指向的行
- 把游标前进到下一行

这些是我们现在要实现的行为。之后，我们还会想要：

- 删除游标指向的行
- 修改游标指向的行
- 在表中搜索给定的 ID，并创建一个指向具有该 ID 的行的游标

事不宜迟，下面是 `Cursor` 类型：

```diff
+typedef struct {
+  Table* table;
+  uint32_t row_num;
+  bool end_of_table;  // Indicates a position one past the last element
+} Cursor;
```

根据我们当前的表数据结构，标识表中一个位置只需要行号就够了。

游标还持有它所属表的引用（这样我们的游标函数就可以只接收游标作为参数）。

最后，它有一个名为 `end_of_table` 的布尔值。这是为了让我们能够表示表末尾之后的一个位置（那可能是我们想要插入一行的地方）。

`table_start()` 和 `table_end()` 创建新的游标：

```diff
+Cursor* table_start(Table* table) {
+  Cursor* cursor = malloc(sizeof(Cursor));
+  cursor->table = table;
+  cursor->row_num = 0;
+  cursor->end_of_table = (table->num_rows == 0);
+
+  return cursor;
+}
+
+Cursor* table_end(Table* table) {
+  Cursor* cursor = malloc(sizeof(Cursor));
+  cursor->table = table;
+  cursor->row_num = table->num_rows;
+  cursor->end_of_table = true;
+
+  return cursor;
+}
```

我们的 `row_slot()` 函数会变成 `cursor_value()`，它返回一个指向游标所描述位置的指针：

```diff
-void* row_slot(Table* table, uint32_t row_num) {
+void* cursor_value(Cursor* cursor) {
+  uint32_t row_num = cursor->row_num;
   uint32_t page_num = row_num / ROWS_PER_PAGE;
-  void* page = get_page(table->pager, page_num);
+  void* page = get_page(cursor->table->pager, page_num);
   uint32_t row_offset = row_num % ROWS_PER_PAGE;
   uint32_t byte_offset = row_offset * ROW_SIZE;
   return page + byte_offset;
 }
```

在当前的表结构中，让游标前进就像递增行号一样简单。在 B 树中这会稍微复杂一些。

```diff
+void cursor_advance(Cursor* cursor) {
+  cursor->row_num += 1;
+  if (cursor->row_num >= cursor->table->num_rows) {
+    cursor->end_of_table = true;
+  }
+}
```

最后我们可以把“虚拟机”的方法改成使用游标抽象。插入一行时，我们在表的末尾打开一个游标，向游标所在位置写入数据，然后关闭游标。

```diff
   Row* row_to_insert = &(statement->row_to_insert);
+  Cursor* cursor = table_end(table);

-  serialize_row(row_to_insert, row_slot(table, table->num_rows));
+  serialize_row(row_to_insert, cursor_value(cursor));
   table->num_rows += 1;

+  free(cursor);
+
   return EXECUTE_SUCCESS;
 }
 ```

选择表中的所有行时，我们在表的开头打开一个游标，打印这一行，然后把游标前进到下一行。重复这个过程直到到达表的末尾。

```diff
 ExecuteResult execute_select(Statement* statement, Table* table) {
+  Cursor* cursor = table_start(table);
+
   Row row;
-  for (uint32_t i = 0; i < table->num_rows; i++) {
-    deserialize_row(row_slot(table, i), &row);
+  while (!(cursor->end_of_table)) {
+    deserialize_row(cursor_value(cursor), &row);
     print_row(&row);
+    cursor_advance(cursor);
   }
+
+  free(cursor);
+
   return EXECUTE_SUCCESS;
 }
 ```

好了，就这样！正如我说过的，这是一次较短的重构，在我们把表数据结构重写成 B 树时会帮上忙。`execute_select()` 和 `execute_insert()` 可以完全通过游标与表交互，而不需要对表的存储方式做任何假设。

下面是这一部分完整的 diff：
```diff
@@ -78,6 +78,13 @@ struct {
 } Table;

+typedef struct {
+  Table* table;
+  uint32_t row_num;
+  bool end_of_table; // Indicates a position one past the last element
+} Cursor;
+
 void print_row(Row* row) {
     printf("(%d, %s, %s)\n", row->id, row->username, row->email);
 }
@@ -126,12 +133,38 @@ void* get_page(Pager* pager, uint32_t page_num) {
     return pager->pages[page_num];
 }

-void* row_slot(Table* table, uint32_t row_num) {
-  uint32_t page_num = row_num / ROWS_PER_PAGE;
-  void *page = get_page(table->pager, page_num);
-  uint32_t row_offset = row_num % ROWS_PER_PAGE;
-  uint32_t byte_offset = row_offset * ROW_SIZE;
-  return page + byte_offset;
+Cursor* table_start(Table* table) {
+  Cursor* cursor = malloc(sizeof(Cursor));
+  cursor->table = table;
+  cursor->row_num = 0;
+  cursor->end_of_table = (table->num_rows == 0);
+
+  return cursor;
+}
+
+Cursor* table_end(Table* table) {
+  Cursor* cursor = malloc(sizeof(Cursor));
+  cursor->table = table;
+  cursor->row_num = table->num_rows;
+  cursor->end_of_table = true;
+
+  return cursor;
+}
+
+void* cursor_value(Cursor* cursor) {
+  uint32_t row_num = cursor->row_num;
+  uint32_t page_num = row_num / ROWS_PER_PAGE;
+  void *page = get_page(cursor->table->pager, page_num);
+  uint32_t row_offset = row_num % ROWS_PER_PAGE;
+  uint32_t byte_offset = row_offset * ROW_SIZE;
+  return page + byte_offset;
+}
+
+void cursor_advance(Cursor* cursor) {
+  cursor->row_num += 1;
+  if (cursor->row_num >= cursor->table->num_rows) {
+    cursor->end_of_table = true;
+  }
 }

 Pager* pager_open(const char* filename) {
@@ -327,19 +360,28 @@ ExecuteResult execute_insert(Statement* statement, Table* table) {
     }

   Row* row_to_insert = &(statement->row_to_insert);
+  Cursor* cursor = table_end(table);

-  serialize_row(row_to_insert, row_slot(table, table->num_rows));
+  serialize_row(row_to_insert, cursor_value(cursor));
   table->num_rows += 1;

+  free(cursor);
+
   return EXECUTE_SUCCESS;
 }

 ExecuteResult execute_select(Statement* statement, Table* table) {
+  Cursor* cursor = table_start(table);
+
   Row row;
-  for (uint32_t i = 0; i < table->num_rows; i++) {
-     deserialize_row(row_slot(table, i), &row);
+  while (!(cursor->end_of_table)) {
+     deserialize_row(cursor_value(cursor), &row);
      print_row(&row);
+     cursor_advance(cursor);
   }
+
+  free(cursor);
+
   return EXECUTE_SUCCESS;
 }
```
