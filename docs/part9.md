---
title: 第 9 部分 - 二分查找与重复键
date: 2017-10-01
---

上次我们指出仍然在以未排序的顺序存储键。我们要解决这个问题，同时检测并拒绝重复的键。

目前，我们的 `execute_insert()` 函数总是选择在表的末尾插入。相反，我们应该在表中搜索正确的插入位置，然后在那里插入。如果键已经存在，就返回一个错误。

```diff
ExecuteResult execute_insert(Statement* statement, Table* table) {
   void* node = get_page(table->pager, table->root_page_num);
-  if ((*leaf_node_num_cells(node) >= LEAF_NODE_MAX_CELLS)) {
+  uint32_t num_cells = (*leaf_node_num_cells(node));
+  if (num_cells >= LEAF_NODE_MAX_CELLS) {
     return EXECUTE_TABLE_FULL;
   }

   Row* row_to_insert = &(statement->row_to_insert);
-  Cursor* cursor = table_end(table);
+  uint32_t key_to_insert = row_to_insert->id;
+  Cursor* cursor = table_find(table, key_to_insert);
+
+  if (cursor->cell_num < num_cells) {
+    uint32_t key_at_index = *leaf_node_key(node, cursor->cell_num);
+    if (key_at_index == key_to_insert) {
+      return EXECUTE_DUPLICATE_KEY;
+    }
+  }

   leaf_node_insert(cursor, row_to_insert->id, row_to_insert);
```

我们不再需要 `table_end()` 函数了。

```diff
-Cursor* table_end(Table* table) {
-  Cursor* cursor = malloc(sizeof(Cursor));
-  cursor->table = table;
-  cursor->page_num = table->root_page_num;
-
-  void* root_node = get_page(table->pager, table->root_page_num);
-  uint32_t num_cells = *leaf_node_num_cells(root_node);
-  cursor->cell_num = num_cells;
-  cursor->end_of_table = true;
-
-  return cursor;
-}
```

我们将用一个在树中搜索给定键的方法来替代它。

```diff
+/*
+Return the position of the given key.
+If the key is not present, return the position
+where it should be inserted
+*/
+Cursor* table_find(Table* table, uint32_t key) {
+  uint32_t root_page_num = table->root_page_num;
+  void* root_node = get_page(table->pager, root_page_num);
+
+  if (get_node_type(root_node) == NODE_LEAF) {
+    return leaf_node_find(table, root_page_num, key);
+  } else {
+    printf("Need to implement searching an internal node\n");
+    exit(EXIT_FAILURE);
+  }
+}
```

我把内部节点的分支先写成桩代码，因为我们还没有实现内部节点。我们可以用二分查找来搜索叶子节点。

```diff
+Cursor* leaf_node_find(Table* table, uint32_t page_num, uint32_t key) {
+  void* node = get_page(table->pager, page_num);
+  uint32_t num_cells = *leaf_node_num_cells(node);
+
+  Cursor* cursor = malloc(sizeof(Cursor));
+  cursor->table = table;
+  cursor->page_num = page_num;
+
+  // Binary search
+  uint32_t min_index = 0;
+  uint32_t one_past_max_index = num_cells;
+  while (one_past_max_index != min_index) {
+    uint32_t index = (min_index + one_past_max_index) / 2;
+    uint32_t key_at_index = *leaf_node_key(node, index);
+    if (key == key_at_index) {
+      cursor->cell_num = index;
+      return cursor;
+    }
+    if (key < key_at_index) {
+      one_past_max_index = index;
+    } else {
+      min_index = index + 1;
+    }
+  }
+
+  cursor->cell_num = min_index;
+  return cursor;
+}
```

它会返回以下之一：
- 该键的位置，
- 如果我们要插入新键，需要移动的另一个键的位置，或者
- 最后一个键之后的位置

由于我们现在要检查节点类型，我们需要函数来读取和设置节点中的这个值。

```diff
+NodeType get_node_type(void* node) {
+  uint8_t value = *((uint8_t*)(node + NODE_TYPE_OFFSET));
+  return (NodeType)value;
+}
+
+void set_node_type(void* node, NodeType type) {
+  uint8_t value = type;
+  *((uint8_t*)(node + NODE_TYPE_OFFSET)) = value;
+}
```

我们必须先转换成 `uint8_t`，以确保它被序列化为单个字节。

我们还需要初始化节点类型。

```diff
-void initialize_leaf_node(void* node) { *leaf_node_num_cells(node) = 0; }
+void initialize_leaf_node(void* node) {
+  set_node_type(node, NODE_LEAF);
+  *leaf_node_num_cells(node) = 0;
+}
```

最后，我们需要创建并处理一个新的错误码。

```diff
-enum ExecuteResult_t { EXECUTE_SUCCESS, EXECUTE_TABLE_FULL };
+enum ExecuteResult_t {
+  EXECUTE_SUCCESS,
+  EXECUTE_DUPLICATE_KEY,
+  EXECUTE_TABLE_FULL
+};
```

```diff
       case (EXECUTE_SUCCESS):
         printf("Executed.\n");
         break;
+      case (EXECUTE_DUPLICATE_KEY):
+        printf("Error: Duplicate key.\n");
+        break;
       case (EXECUTE_TABLE_FULL):
         printf("Error: Table full.\n");
         break;
```

有了这些改动，我们的测试可以改成检查排序顺序：

```diff
       "db > Executed.",
       "db > Tree:",
       "leaf (size 3)",
-      "  - 0 : 3",
-      "  - 1 : 1",
-      "  - 2 : 2",
+      "  - 0 : 1",
+      "  - 1 : 2",
+      "  - 2 : 3",
       "db > "
     ])
   end
```

我们还可以为重复键添加一个新的测试：

```diff
+  it 'prints an error message if there is a duplicate id' do
+    script = [
+      "insert 1 user1 person1@example.com",
+      "insert 1 user1 person1@example.com",
+      "select",
+      ".exit",
+    ]
+    result = run_script(script)
+    expect(result).to match_array([
+      "db > Executed.",
+      "db > Error: Duplicate key.",
+      "db > (1, user1, person1@example.com)",
+      "Executed.",
+      "db > ",
+    ])
+  end
```

就这样！接下来：实现拆分叶子节点和创建内部节点。
