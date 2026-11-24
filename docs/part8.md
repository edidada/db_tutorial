---
title: 第 8 部分 - B 树叶子节点格式
date: 2017-09-25
---

我们要把表的格式从未排序的行数组改成 B 树。这是一个相当大的改动，需要多篇文章才能实现。到本文结束时，我们将定义叶子节点的布局，并支持向单节点树中插入键／值对。但首先，让我们回顾一下改用树结构的原因。

## 其他表格式

使用当前的格式，每个页只存储行（没有元数据），因此空间效率相当高。插入也很快，因为我们只需追加到末尾。然而，要找到某一特定的行，只能扫描整张表。而且如果我们想删除一行，就必须把该行之后的每一行都往前移来填补空洞。

如果我们将表存储为数组，但让行按 id 保持有序，就可以用二分查找来找到特定的 id。但插入会很慢，因为我们必须移动大量行来腾出空间。

因此，我们选择树结构。树中的每个节点可以包含数量可变的行，所以我们必须在每个节点中存储一些信息来跟踪它包含多少行。此外还有所有不存储任何行的内部节点带来的存储开销。作为更大的数据库文件的交换，我们获得了快速的插入、删除和查找。

|               | 未排序的行数组 | 已排序的行数组 | 节点组成的树 |
|---------------|----------------|----------------|--------------|
| 页中包含      | 仅数据         | 仅数据         | 元数据、主键和数据 |
| 每页行数      | 较多           | 较多           | 较少         |
| 插入          | O(1)           | O(n)           | O(log(n))    |
| 删除          | O(n)           | O(n)           | O(log(n))    |
| 按 id 查找    | O(n)           | O(log(n))      | O(log(n))    |

## 节点头部格式

叶子节点和内部节点的布局不同。我们用一个枚举来跟踪节点类型：

```diff
+typedef enum { NODE_INTERNAL, NODE_LEAF } NodeType;
```

每个节点将恰好占用一个页。内部节点通过存储保存子节点的页号来指向其子节点。btree 向分页器请求某个页号，并得到一个指向页缓存中该页的指针。各个页在数据库文件中按页号顺序一个接一个地存储。

节点需要在页开头的头部中存储一些元数据。每个节点都会存储它是什么类型的节点、它是否是根节点，以及一个指向其父节点的指针（以便找到节点的兄弟节点）。我为头部每个字段的大小和偏移量定义了常量：

```diff
+/*
+ * Common Node Header Layout
+ */
+const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
+const uint32_t NODE_TYPE_OFFSET = 0;
+const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
+const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE;
+const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
+const uint32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;
+const uint8_t COMMON_NODE_HEADER_SIZE =
+    NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;
```

## 叶子节点格式

除了这些公共头部字段，叶子节点还需要存储它包含多少个“单元格（cell）”。一个单元格就是一个键／值对。

```diff
+/*
+ * Leaf Node Header Layout
+ */
+const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
+const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
+const uint32_t LEAF_NODE_HEADER_SIZE =
+    COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE;
```

叶子节点的主体是一个单元格数组。每个单元格是一个键，后面跟着一个值（序列化后的行）。

```diff
+/*
+ * Leaf Node Body Layout
+ */
+const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
+const uint32_t LEAF_NODE_KEY_OFFSET = 0;
+const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
+const uint32_t LEAF_NODE_VALUE_OFFSET =
+    LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE;
+const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
+const uint32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
+const uint32_t LEAF_NODE_MAX_CELLS =
+    LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE;
```

基于这些常量，目前叶子节点的布局如下：

{% include image.html url="assets/images/leaf-node-format.png" description="我们的叶子节点格式" %}

在头部中为每个布尔值使用一整个字节，空间上有点浪费，但这让访问这些值的代码更容易编写。

还要注意末尾有一些浪费的空间。我们在头部之后尽可能多地存放单元格，但剩下的空间放不下完整的单元格。我们把它留空，以避免把单元格拆分到不同节点中。

## 访问叶子节点字段

访问键、值和元数据的代码都涉及使用我们刚定义的常量做指针运算。

```diff
+uint32_t* leaf_node_num_cells(void* node) {
+  return node + LEAF_NODE_NUM_CELLS_OFFSET;
+}
+
+void* leaf_node_cell(void* node, uint32_t cell_num) {
+  return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
+}
+
+uint32_t* leaf_node_key(void* node, uint32_t cell_num) {
+  return leaf_node_cell(node, cell_num);
+}
+
+void* leaf_node_value(void* node, uint32_t cell_num) {
+  return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_SIZE;
+}
+
+void initialize_leaf_node(void* node) { *leaf_node_num_cells(node) = 0; }
+
```

这些方法返回指向相关值的指针，因此它们既可以用作 getter，也可以用作 setter。

## 对分页器和表对象的改动

每个节点将恰好占用一个页，即使它没有填满。这意味着我们的分页器不再需要支持读写不完整的页。
```diff
-void pager_flush(Pager* pager, uint32_t page_num, uint32_t size) {
+void pager_flush(Pager* pager, uint32_t page_num) {
   if (pager->pages[page_num] == NULL) {
     printf("Tried to flush null page\n");
     exit(EXIT_FAILURE);
@@ -242,7 +337,7 @@ void pager_flush(Pager* pager, uint32_t page_num, uint32_t size) {
   }

   ssize_t bytes_written =
-      write(pager->file_descriptor, pager->pages[page_num], size);
+      write(pager->file_descriptor, pager->pages[page_num], PAGE_SIZE);

   if (bytes_written == -1) {
     printf("Error writing: %d\n", errno);
```

```diff
 void db_close(Table* table) {
   Pager* pager = table->pager;
-  uint32_t num_full_pages = table->num_rows / ROWS_PER_PAGE;

-  for (uint32_t i = 0; i < num_full_pages; i++) {
+  for (uint32_t i = 0; i < pager->num_pages; i++) {
     if (pager->pages[i] == NULL) {
       continue;
     }
-    pager_flush(pager, i, PAGE_SIZE);
+    pager_flush(pager, i);
     free(pager->pages[i]);
     pager->pages[i] = NULL;
   }

-  // There may be a partial page to write to the end of the file
-  // This should not be needed after we switch to a B-tree
-  uint32_t num_additional_rows = table->num_rows % ROWS_PER_PAGE;
-  if (num_additional_rows > 0) {
-    uint32_t page_num = num_full_pages;
-    if (pager->pages[page_num] != NULL) {
-      pager_flush(pager, page_num, num_additional_rows * ROW_SIZE);
-      free(pager->pages[page_num]);
-      pager->pages[page_num] = NULL;
-    }
-  }
-
   int result = close(pager->file_descriptor);
   if (result == -1) {
     printf("Error closing db file.\n");
```

现在，存储数据库中的页数比存储行数更有意义。页数应该与分页器对象关联，而不是与表关联，因为它是数据库使用的页数，而不是某个特定表的。一棵 btree 由它的根节点页号标识，所以表对象需要跟踪这个页号。

```diff
 const uint32_t PAGE_SIZE = 4096;
 const uint32_t TABLE_MAX_PAGES = 100;
-const uint32_t ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE;
-const uint32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES;

 typedef struct {
   int file_descriptor;
   uint32_t file_length;
+  uint32_t num_pages;
   void* pages[TABLE_MAX_PAGES];
 } Pager;

 typedef struct {
   Pager* pager;
-  uint32_t num_rows;
+  uint32_t root_page_num;
 } Table;
```

```diff
@@ -127,6 +200,10 @@ void* get_page(Pager* pager, uint32_t page_num) {
     }

     pager->pages[page_num] = page;
+
+    if (page_num >= pager->num_pages) {
+      pager->num_pages = page_num + 1;
+    }
   }

   return pager->pages[page_num];
```

```diff
@@ -184,6 +269,12 @@ Pager* pager_open(const char* filename) {
   Pager* pager = malloc(sizeof(Pager));
   pager->file_descriptor = fd;
   pager->file_length = file_length;
+  pager->num_pages = (file_length / PAGE_SIZE);
+
+  if (file_length % PAGE_SIZE != 0) {
+    printf("Db file is not a whole number of pages. Corrupt file.\n");
+    exit(EXIT_FAILURE);
+  }

   for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
     pager->pages[i] = NULL;
```

## 对游标对象的改动

游标表示表中的一个位置。当我们的表还是简单的行数组时，只要给出行号就能访问一行。现在它是一棵树，我们通过节点的页号和该节点内的单元格号来标识一个位置。

```diff
 typedef struct {
   Table* table;
-  uint32_t row_num;
+  uint32_t page_num;
+  uint32_t cell_num;
   bool end_of_table;  // Indicates a position one past the last element
 } Cursor;
```

```diff
 Cursor* table_start(Table* table) {
   Cursor* cursor = malloc(sizeof(Cursor));
   cursor->table = table;
-  cursor->row_num = 0;
-  cursor->end_of_table = (table->num_rows == 0);
+  cursor->page_num = table->root_page_num;
+  cursor->cell_num = 0;
+
+  void* root_node = get_page(table->pager, table->root_page_num);
+  uint32_t num_cells = *leaf_node_num_cells(root_node);
+  cursor->end_of_table = (num_cells == 0);

   return cursor;
 }
```

```diff
 Cursor* table_end(Table* table) {
   Cursor* cursor = malloc(sizeof(Cursor));
   cursor->table = table;
-  cursor->row_num = table->num_rows;
+  cursor->page_num = table->root_page_num;
+
+  void* root_node = get_page(table->pager, table->root_page_num);
+  uint32_t num_cells = *leaf_node_num_cells(root_node);
+  cursor->cell_num = num_cells;
   cursor->end_of_table = true;

   return cursor;
 }
```

```diff
 void* cursor_value(Cursor* cursor) {
-  uint32_t row_num = cursor->row_num;
-  uint32_t page_num = row_num / ROWS_PER_PAGE;
+  uint32_t page_num = cursor->page_num;
   void* page = get_page(cursor->table->pager, page_num);
-  uint32_t row_offset = row_num % ROWS_PER_PAGE;
-  uint32_t byte_offset = row_offset * ROW_SIZE;
-  return page + byte_offset;
+  return leaf_node_value(page, cursor->cell_num);
 }
```

```diff
 void cursor_advance(Cursor* cursor) {
-  cursor->row_num += 1;
-  if (cursor->row_num >= cursor->table->num_rows) {
+  uint32_t page_num = cursor->page_num;
+  void* node = get_page(cursor->table->pager, page_num);
+
+  cursor->cell_num += 1;
+  if (cursor->cell_num >= (*leaf_node_num_cells(node))) {
     cursor->end_of_table = true;
   }
 }
```

## 向叶子节点插入

在本文中我们只实现足以得到单节点树的部分。回想一下上一篇文章，树最初是一个空的叶子节点：

{% include image.html url="assets/images/btree1.png" description="空的 btree" %}

可以不断添加键／值对，直到叶子节点满：

{% include image.html url="assets/images/btree2.png" description="单节点 btree" %}

当我们第一次打开数据库时，数据库文件将是空的，所以我们将第 0 页初始化为一个空的叶子节点（根节点）：

```diff
 Table* db_open(const char* filename) {
   Pager* pager = pager_open(filename);
-  uint32_t num_rows = pager->file_length / ROW_SIZE;

   Table* table = malloc(sizeof(Table));
   table->pager = pager;
-  table->num_rows = num_rows;
+  table->root_page_num = 0;
+
+  if (pager->num_pages == 0) {
+    // New database file. Initialize page 0 as leaf node.
+    void* root_node = get_page(pager, 0);
+    initialize_leaf_node(root_node);
+  }

   return table;
 }
```

接下来我们将制作一个函数，把键／值对插入叶子节点。它以游标作为输入，表示这对数据应该插入的位置。

```diff
+void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value) {
+  void* node = get_page(cursor->table->pager, cursor->page_num);
+
+  uint32_t num_cells = *leaf_node_num_cells(node);
+  if (num_cells >= LEAF_NODE_MAX_CELLS) {
+    // Node full
+    printf("Need to implement splitting a leaf node.\n");
+    exit(EXIT_FAILURE);
+  }
+
+  if (cursor->cell_num < num_cells) {
+    // Make room for new cell
+    for (uint32_t i = num_cells; i > cursor->cell_num; i--) {
+      memcpy(leaf_node_cell(node, i), leaf_node_cell(node, i - 1),
+             LEAF_NODE_CELL_SIZE);
+    }
+  }
+
+  *(leaf_node_num_cells(node)) += 1;
+  *(leaf_node_key(node, cursor->cell_num)) = key;
+  serialize_row(value, leaf_node_value(node, cursor->cell_num));
+}
+
```

我们还没有实现拆分，所以如果节点满了就报错。接下来我们把单元格向右移动一个位置，为新单元格腾出空间。然后我们把新的键／值写入空出来的位置。

由于我们假设树只有一个节点，我们的 `execute_insert()` 函数只需调用这个辅助方法：

```diff
 ExecuteResult execute_insert(Statement* statement, Table* table) {
-  if (table->num_rows >= TABLE_MAX_ROWS) {
+  void* node = get_page(table->pager, table->root_page_num);
+  if ((*leaf_node_num_cells(node) >= LEAF_NODE_MAX_CELLS)) {
     return EXECUTE_TABLE_FULL;
   }

   Row* row_to_insert = &(statement->row_to_insert);
   Cursor* cursor = table_end(table);

-  serialize_row(row_to_insert, cursor_value(cursor));
-  table->num_rows += 1;
+  leaf_node_insert(cursor, row_to_insert->id, row_to_insert);

   free(cursor);
```

有了这些改动，我们的数据库应该能像以前一样工作！只不过现在它会更早返回 "Table Full" 错误，因为我们还不能拆分根节点。

叶子节点能容纳多少行？

## 打印常量的命令

我增加一个新的元命令来打印一些我们关心的常量。

```diff
+void print_constants() {
+  printf("ROW_SIZE: %d\n", ROW_SIZE);
+  printf("COMMON_NODE_HEADER_SIZE: %d\n", COMMON_NODE_HEADER_SIZE);
+  printf("LEAF_NODE_HEADER_SIZE: %d\n", LEAF_NODE_HEADER_SIZE);
+  printf("LEAF_NODE_CELL_SIZE: %d\n", LEAF_NODE_CELL_SIZE);
+  printf("LEAF_NODE_SPACE_FOR_CELLS: %d\n", LEAF_NODE_SPACE_FOR_CELLS);
+  printf("LEAF_NODE_MAX_CELLS: %d\n", LEAF_NODE_MAX_CELLS);
+}
+
@@ -294,6 +376,14 @@ MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table) {
   if (strcmp(input_buffer->buffer, ".exit") == 0) {
     db_close(table);
     exit(EXIT_SUCCESS);
+  } else if (strcmp(input_buffer->buffer, ".constants") == 0) {
+    printf("Constants:\n");
+    print_constants();
+    return META_COMMAND_SUCCESS;
   } else {
     return META_COMMAND_UNRECOGNIZED_COMMAND;
   }
```

我还加了一个测试，这样当这些常量发生变化时我们会收到提醒：
```diff
+  it 'prints constants' do
+    script = [
+      ".constants",
+      ".exit",
+    ]
+    result = run_script(script)
+
+    expect(result).to match_array([
+      "db > Constants:",
+      "ROW_SIZE: 293",
+      "COMMON_NODE_HEADER_SIZE: 6",
+      "LEAF_NODE_HEADER_SIZE: 10",
+      "LEAF_NODE_CELL_SIZE: 297",
+      "LEAF_NODE_SPACE_FOR_CELLS: 4086",
+      "LEAF_NODE_MAX_CELLS: 13",
+      "db > ",
+    ])
+  end
```

所以我们的表现在能容纳 13 行！

## 树的可视化

为了帮助调试和可视化，我还增加了一个元命令来打印 btree 的表示。

```diff
+void print_leaf_node(void* node) {
+  uint32_t num_cells = *leaf_node_num_cells(node);
+  printf("leaf (size %d)\n", num_cells);
+  for (uint32_t i = 0; i < num_cells; i++) {
+    uint32_t key = *leaf_node_key(node, i);
+    printf("  - %d : %d\n", i, key);
+  }
+}
+
```

```diff
@@ -294,6 +376,14 @@ MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table) {
   if (strcmp(input_buffer->buffer, ".exit") == 0) {
     db_close(table);
     exit(EXIT_SUCCESS);
+  } else if (strcmp(input_buffer->buffer, ".btree") == 0) {
+    printf("Tree:\n");
+    print_leaf_node(get_page(table->pager, 0));
+    return META_COMMAND_SUCCESS;
   } else if (strcmp(input_buffer->buffer, ".constants") == 0) {
     printf("Constants:\n");
     print_constants();
     return META_COMMAND_SUCCESS;
   } else {
     return META_COMMAND_UNRECOGNIZED_COMMAND;
   }
```

还有一个测试

```diff
+  it 'allows printing out the structure of a one-node btree' do
+    script = [3, 1, 2].map do |i|
+      "insert #{i} user#{i} person#{i}@example.com"
+    end
+    script << ".btree"
+    script << ".exit"
+    result = run_script(script)
+
+    expect(result).to match_array([
+      "db > Executed.",
+      "db > Executed.",
+      "db > Executed.",
+      "db > Tree:",
+      "leaf (size 3)",
+      "  - 0 : 3",
+      "  - 1 : 1",
+      "  - 2 : 2",
+      "db > "
+    ])
+  end
```

糟糕，我们仍然没有按排序顺序存储行。你会注意到 `execute_insert()` 是插入到 `table_end()` 返回的位置上的。所以行是按插入顺序存储的，和以前一样。

## 下次

这一切看起来可能像是倒退了一步。我们的数据库现在能存储的行比以前少了，而且仍然以未排序的顺序存储行。但就像我在开头说的，这是一个大改动，把它拆成可管理的小步骤很重要。

下次，我们将实现按主键查找记录，并开始按排序顺序存储行。

## 完整 Diff

```diff
@@ -62,29 +62,101 @@ const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;

 const uint32_t PAGE_SIZE = 4096;
 #define TABLE_MAX_PAGES 100
-const uint32_t ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE;
-const uint32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES;

 typedef struct {
   int file_descriptor;
   uint32_t file_length;
+  uint32_t num_pages;
   void* pages[TABLE_MAX_PAGES];
 } Pager;

 typedef struct {
   Pager* pager;
-  uint32_t num_rows;
+  uint32_t root_page_num;
 } Table;

 typedef struct {
   Table* table;
-  uint32_t row_num;
+  uint32_t page_num;
+  uint32_t cell_num;
   bool end_of_table;  // Indicates a position one past the last element
 } Cursor;

+typedef enum { NODE_INTERNAL, NODE_LEAF } NodeType;
+
+/*
+ * Common Node Header Layout
+ */
+const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
+const uint32_t NODE_TYPE_OFFSET = 0;
+const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
+const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE;
+const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
+const uint32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;
+const uint8_t COMMON_NODE_HEADER_SIZE =
+    NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;
+
+/*
+ * Leaf Node Header Layout
+ */
+const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
+const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
+const uint32_t LEAF_NODE_HEADER_SIZE =
+    COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE;
+
+/*
+ * Leaf Node Body Layout
+ */
+const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
+const uint32_t LEAF_NODE_KEY_OFFSET = 0;
+const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
+const uint32_t LEAF_NODE_VALUE_OFFSET =
+    LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE;
+const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
+const uint32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
+const uint32_t LEAF_NODE_MAX_CELLS =
+    LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE;
+
+uint32_t* leaf_node_num_cells(void* node) {
+  return node + LEAF_NODE_NUM_CELLS_OFFSET;
+}
+
+void* leaf_node_cell(void* node, uint32_t cell_num) {
+  return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
+}
+
+uint32_t* leaf_node_key(void* node, uint32_t cell_num) {
+  return leaf_node_cell(node, cell_num);
+}
+
+void* leaf_node_value(void* node, uint32_t cell_num) {
+  return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_SIZE;
+}
+
+void print_constants() {
+  printf("ROW_SIZE: %d\n", ROW_SIZE);
+  printf("COMMON_NODE_HEADER_SIZE: %d\n", COMMON_NODE_HEADER_SIZE);
+  printf("LEAF_NODE_HEADER_SIZE: %d\n", LEAF_NODE_HEADER_SIZE);
+  printf("LEAF_NODE_CELL_SIZE: %d\n", LEAF_NODE_CELL_SIZE);
+  printf("LEAF_NODE_SPACE_FOR_CELLS: %d\n", LEAF_NODE_SPACE_FOR_CELLS);
+  printf("LEAF_NODE_MAX_CELLS: %d\n", LEAF_NODE_MAX_CELLS);
+}
+
+void print_leaf_node(void* node) {
+  uint32_t num_cells = *leaf_node_num_cells(node);
+  printf("leaf (size %d)\n", num_cells);
+  for (uint32_t i = 0; i < num_cells; i++) {
+    uint32_t key = *leaf_node_key(node, i);
+    printf("  - %d : %d\n", i, key);
+  }
+}
+
 void print_row(Row* row) {
     printf("(%d, %s, %s)\n", row->id, row->username, row->email);
 }
@@ -101,6 +173,8 @@ void deserialize_row(void *source, Row* destination) {
     memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
 }

+void initialize_leaf_node(void* node) { *leaf_node_num_cells(node) = 0; }
+
 void* get_page(Pager* pager, uint32_t page_num) {
   if (page_num > TABLE_MAX_PAGES) {
     printf("Tried to fetch page number out of bounds. %d > %d\n", page_num,
@@ -128,6 +202,10 @@ void* get_page(Pager* pager, uint32_t page_num) {
     }

     pager->pages[page_num] = page;
+
+    if (page_num >= pager->num_pages) {
+      pager->num_pages = page_num + 1;
+    }
   }

   return pager->pages[page_num];
@@ -136,8 +214,12 @@ void* get_page(Pager* pager, uint32_t page_num) {
 Cursor* table_start(Table* table) {
   Cursor* cursor = malloc(sizeof(Cursor));
   cursor->table = table;
-  cursor->row_num = 0;
-  cursor->end_of_table = (table->num_rows == 0);
+  cursor->page_num = table->root_page_num;
+  cursor->cell_num = 0;
+
+  void* root_node = get_page(table->pager, table->root_page_num);
+  uint32_t num_cells = *leaf_node_num_cells(root_node);
+  cursor->end_of_table = (num_cells == 0);

   return cursor;
 }
@@ -145,24 +227,28 @@ Cursor* table_start(Table* table) {
 Cursor* table_end(Table* table) {
   Cursor* cursor = malloc(sizeof(Cursor));
   cursor->table = table;
-  cursor->row_num = table->num_rows;
+  cursor->page_num = table->root_page_num;
+
+  void* root_node = get_page(table->pager, table->root_page_num);
+  uint32_t num_cells = *leaf_node_num_cells(root_node);
+  cursor->cell_num = num_cells;
   cursor->end_of_table = true;

   return cursor;
 }

 void* cursor_value(Cursor* cursor) {
-  uint32_t row_num = cursor->row_num;
-  uint32_t page_num = row_num / ROWS_PER_PAGE;
+  uint32_t page_num = cursor->page_num;
   void* page = get_page(cursor->table->pager, page_num);
-  uint32_t row_offset = row_num % ROWS_PER_PAGE;
-  uint32_t byte_offset = row_offset * ROW_SIZE;
-  return page + byte_offset;
+  return leaf_node_value(page, cursor->cell_num);
 }

 void cursor_advance(Cursor* cursor) {
-  cursor->row_num += 1;
-  if (cursor->row_num >= cursor->table->num_rows) {
+  uint32_t page_num = cursor->page_num;
+  void* node = get_page(cursor->table->pager, page_num);
+
+  cursor->cell_num += 1;
+  if (cursor->cell_num >= (*leaf_node_num_cells(node))) {
     cursor->end_of_table = true;
   }
 }
@@ -185,6 +271,12 @@ Pager* pager_open(const char* filename) {
   Pager* pager = malloc(sizeof(Pager));
   pager->file_descriptor = fd;
   pager->file_length = file_length;
+  pager->num_pages = (file_length / PAGE_SIZE);
+
+  if (file_length % PAGE_SIZE != 0) {
+    printf("Db file is not a whole number of pages. Corrupt file.\n");
+    exit(EXIT_FAILURE);
+  }

   for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
     pager->pages[i] = NULL;
@@ -194,11 +285,15 @@ Pager* pager_open(const char* filename) {
@@ -195,11 +287,16 @@ Pager* pager_open(const char* filename) {

 Table* db_open(const char* filename) {
   Pager* pager = pager_open(filename);
-  uint32_t num_rows = pager->file_length / ROW_SIZE;

   Table* table = malloc(sizeof(Table));
   table->pager = pager;
-  table->num_rows = num_rows;
+  table->root_page_num = 0;
+
+  if (pager->num_pages == 0) {
+    // New database file. Initialize page 0 as leaf node.
+    void* root_node = get_page(pager, 0);
+    initialize_leaf_node(root_node);
+  }

   return table;
 }
@@ -234,7 +331,7 @@ void close_input_buffer(InputBuffer* input_buffer) {
     free(input_buffer);
 }

-void pager_flush(Pager* pager, uint32_t page_num, uint32_t size) {
+void pager_flush(Pager* pager, uint32_t page_num) {
   if (pager->pages[page_num] == NULL) {
     printf("Tried to flush null page\n");
     exit(EXIT_FAILURE);
@@ -242,7 +337,7 @@ void pager_flush(Pager* pager, uint32_t page_num, uint32_t size) {
@@ -249,7 +346,7 @@ void pager_flush(Pager* pager, uint32_t page_num, uint32_t size) {
   }

   ssize_t bytes_written =
-      write(pager->file_descriptor, pager->pages[page_num], size);
+      write(pager->file_descriptor, pager->pages[page_num], PAGE_SIZE);

   if (bytes_written == -1) {
     printf("Error writing: %d\n", errno);
@@ -252,29 +347,16 @@ void pager_flush(Pager* pager, uint32_t page_num, uint32_t size) {
@@ -260,29 +357,16 @@ void pager_flush(Pager* pager, uint32_t page_num, uint32_t size) {

 void db_close(Table* table) {
   Pager* pager = table->pager;
-  uint32_t num_full_pages = table->num_rows / ROWS_PER_PAGE;

-  for (uint32_t i = 0; i < num_full_pages; i++) {
+  for (uint32_t i = 0; i < pager->num_pages; i++) {
     if (pager->pages[i] == NULL) {
       continue;
     }
-    pager_flush(pager, i, PAGE_SIZE);
+    pager_flush(pager, i);
     free(pager->pages[i]);
     pager->pages[i] = NULL;
   }

-  // There may be a partial page to write to the end of the file
-  // This should not be needed after we switch to a B-tree
-  uint32_t num_additional_rows = table->num_rows % ROWS_PER_PAGE;
-  if (num_additional_rows > 0) {
-    uint32_t page_num = num_full_pages;
-    if (pager->pages[page_num] != NULL) {
-      pager_flush(pager, page_num, num_additional_rows * ROW_SIZE);
-      free(pager->pages[page_num]);
-      pager->pages[page_num] = NULL;
-    }
-  }
-
   int result = close(pager->file_descriptor);
   if (result == -1) {
     printf("Error closing db file.\n");
@@ -305,6 +389,14 @@ MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table *table) {
   if (strcmp(input_buffer->buffer, ".exit") == 0) {
     db_close(table);
     exit(EXIT_SUCCESS);
+  } else if (strcmp(input_buffer->buffer, ".btree") == 0) {
+    printf("Tree:\n");
+    print_leaf_node(get_page(table->pager, 0));
+    return META_COMMAND_SUCCESS;
+  } else if (strcmp(input_buffer->buffer, ".constants") == 0) {
+    printf("Constants:\n");
+    print_constants();
+    return META_COMMAND_SUCCESS;
   } else {
     return META_COMMAND_UNRECOGNIZED_COMMAND;
   }
@@ -354,16 +446,39 @@ PrepareResult prepare_statement(InputBuffer* input_buffer,
   return PREPARE_UNRECOGNIZED_STATEMENT;
 }

+void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value) {
+  void* node = get_page(cursor->table->pager, cursor->page_num);
+
+  uint32_t num_cells = *leaf_node_num_cells(node);
+  if (num_cells >= LEAF_NODE_MAX_CELLS) {
+    // Node full
+    printf("Need to implement splitting a leaf node.\n");
+    exit(EXIT_FAILURE);
+  }
+
+  if (cursor->cell_num < num_cells) {
+    // Make room for new cell
+    for (uint32_t i = num_cells; i > cursor->cell_num; i--) {
+      memcpy(leaf_node_cell(node, i), leaf_node_cell(node, i - 1),
+             LEAF_NODE_CELL_SIZE);
+    }
+  }
+
+  *(leaf_node_num_cells(node)) += 1;
+  *(leaf_node_key(node, cursor->cell_num)) = key;
+  serialize_row(value, leaf_node_value(node, cursor->cell_num));
+}
+
 ExecuteResult execute_insert(Statement* statement, Table* table) {
-  if (table->num_rows >= TABLE_MAX_ROWS) {
+  void* node = get_page(table->pager, table->root_page_num);
+  if ((*leaf_node_num_cells(node) >= LEAF_NODE_MAX_CELLS)) {
     return EXECUTE_TABLE_FULL;
   }

   Row* row_to_insert = &(statement->row_to_insert);
   Cursor* cursor = table_end(table);

-  serialize_row(row_to_insert, cursor_value(cursor));
-  table->num_rows += 1;
+  leaf_node_insert(cursor, row_to_insert->id, row_to_insert);

   free(cursor);
```

规格测试：
```diff
+  it 'allows printing out the structure of a one-node btree' do
+    script = [3, 1, 2].map do |i|
+      "insert #{i} user#{i} person#{i}@example.com"
+    end
+    script << ".btree"
+    script << ".exit"
+    result = run_script(script)
+
+    expect(result).to match_array([
+      "db > Executed.",
+      "db > Executed.",
+      "db > Executed.",
+      "db > Tree:",
+      "leaf (size 3)",
+      "  - 0 : 3",
+      "  - 1 : 1",
+      "  - 2 : 2",
+      "db > "
+    ])
+  end
+
+  it 'prints constants' do
+    script = [
+      ".constants",
+      ".exit",
+    ]
+    result = run_script(script)
+
+    expect(result).to match_array([
+      "db > Constants:",
+      "ROW_SIZE: 293",
+      "COMMON_NODE_HEADER_SIZE: 6",
+      "LEAF_NODE_HEADER_SIZE: 10",
+      "LEAF_NODE_CELL_SIZE: 297",
+      "LEAF_NODE_SPACE_FOR_CELLS: 4086",
+      "LEAF_NODE_MAX_CELLS: 13",
+      "db > ",
+    ])
+  end
 end
```
