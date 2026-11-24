---
title: 第 12 部分 - 扫描多层 B 树
date: 2017-11-11
---

我们现在支持构造多层 btree，但在这个过程中把 `select` 语句弄坏了。下面是一个插入 15 行然后尝试打印它们的测试用例。

```diff
+  it 'prints all rows in a multi-level tree' do
+    script = []
+    (1..15).each do |i|
+      script << "insert #{i} user#{i} person#{i}@example.com"
+    end
+    script << "select"
+    script << ".exit"
+    result = run_script(script)
+
+    expect(result[15...result.length]).to match_array([
+      "db > (1, user1, person1@example.com)",
+      "(2, user2, person2@example.com)",
+      "(3, user3, person3@example.com)",
+      "(4, user4, person4@example.com)",
+      "(5, user5, person5@example.com)",
+      "(6, user6, person6@example.com)",
+      "(7, user7, person7@example.com)",
+      "(8, user8, person8@example.com)",
+      "(9, user9, person9@example.com)",
+      "(10, user10, person10@example.com)",
+      "(11, user11, person11@example.com)",
+      "(12, user12, person12@example.com)",
+      "(13, user13, person13@example.com)",
+      "(14, user14, person14@example.com)",
+      "(15, user15, person15@example.com)",
+      "Executed.", "db > ",
+    ])
+  end
```

但当我们现在运行这个测试用例时，实际发生的是：

```
db > select
(2, user1, person1@example.com)
Executed.
```

这很奇怪。它只打印了一行，而且那一行看起来是损坏的（注意 id 和 username 对不上）。

这种奇怪现象是因为 `execute_select()` 从表的开头开始，而我们目前对 `table_start()` 的实现返回的是根节点的单元格 0。但我们的树根现在是一个内部节点，它不包含任何行。打印出来的数据一定是根节点还是叶子节点时遗留下来的。`execute_select()` 真正应该返回的是最左边叶子节点的单元格 0。

所以去掉旧的实现：

```diff
-Cursor* table_start(Table* table) {
-  Cursor* cursor = malloc(sizeof(Cursor));
-  cursor->table = table;
-  cursor->page_num = table->root_page_num;
-  cursor->cell_num = 0;
-
-  void* root_node = get_page(table->pager, table->root_page_num);
-  uint32_t num_cells = *leaf_node_num_cells(root_node);
-  cursor->end_of_table = (num_cells == 0);
-
-  return cursor;
-}
```

换成一个搜索键 0（可能的最小键）的新实现。即使键 0 在表中不存在，这个方法也会返回最小 id 的位置（即最左边叶子节点的开头）。

```diff
+Cursor* table_start(Table* table) {
+  Cursor* cursor =  table_find(table, 0);
+
+  void* node = get_page(table->pager, cursor->page_num);
+  uint32_t num_cells = *leaf_node_num_cells(node);
+  cursor->end_of_table = (num_cells == 0);
+
+  return cursor;
+}
```

做了这些改动后，它仍然只打印出一个节点那么多的行：

```
db > select
(1, user1, person1@example.com)
(2, user2, person2@example.com)
(3, user3, person3@example.com)
(4, user4, person4@example.com)
(5, user5, person5@example.com)
(6, user6, person6@example.com)
(7, user7, person7@example.com)
Executed.
db >
```

有 15 个条目时，我们的 btree 由一个内部节点和两个叶子节点组成，看起来像这样：

{% include image.html url="assets/images/btree3.png" description="我们的 btree 结构" %}

要扫描整张表，我们需要在到达第一个叶子节点的末尾时跳到第二个叶子节点。为此，我们要在叶子节点头部保存一个新字段，叫做 `next_leaf`，它将保存该叶子右侧兄弟节点的页号。最右边的叶子节点的 `next_leaf` 值为 0，表示没有兄弟节点（反正第 0 页是保留给表的根节点的）。

更新叶子节点头部格式以包含新字段：

```diff
 const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
 const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
-const uint32_t LEAF_NODE_HEADER_SIZE =
-    COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE;
+const uint32_t LEAF_NODE_NEXT_LEAF_SIZE = sizeof(uint32_t);
+const uint32_t LEAF_NODE_NEXT_LEAF_OFFSET =
+    LEAF_NODE_NUM_CELLS_OFFSET + LEAF_NODE_NUM_CELLS_SIZE;
+const uint32_t LEAF_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE +
+                                       LEAF_NODE_NUM_CELLS_SIZE +
+                                       LEAF_NODE_NEXT_LEAF_SIZE;
 
 ```

添加一个访问新字段的方法：
```diff
+uint32_t* leaf_node_next_leaf(void* node) {
+  return node + LEAF_NODE_NEXT_LEAF_OFFSET;
+}
```

初始化新叶子节点时默认把 `next_leaf` 设为 0：

```diff
@@ -322,6 +330,7 @@ void initialize_leaf_node(void* node) {
   set_node_type(node, NODE_LEAF);
   set_node_root(node, false);
   *leaf_node_num_cells(node) = 0;
+  *leaf_node_next_leaf(node) = 0;  // 0 represents no sibling
 }
```

每当我们拆分叶子节点时，更新兄弟指针。旧叶子的兄弟变成新叶子，而新叶子的兄弟变成旧叶子原来的兄弟。

```diff
@@ -659,6 +671,8 @@ void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value) {
   uint32_t new_page_num = get_unused_page_num(cursor->table->pager);
   void* new_node = get_page(cursor->table->pager, new_page_num);
   initialize_leaf_node(new_node);
+  *leaf_node_next_leaf(new_node) = *leaf_node_next_leaf(old_node);
+  *leaf_node_next_leaf(old_node) = new_page_num;
```

添加一个新字段会改变几个常量：
```diff
   it 'prints constants' do
     script = [
       ".constants",
@@ -199,9 +228,9 @@ describe 'database' do
       "db > Constants:",
       "ROW_SIZE: 293",
       "COMMON_NODE_HEADER_SIZE: 6",
-      "LEAF_NODE_HEADER_SIZE: 10",
+      "LEAF_NODE_HEADER_SIZE: 14",
       "LEAF_NODE_CELL_SIZE: 297",
-      "LEAF_NODE_SPACE_FOR_CELLS: 4086",
+      "LEAF_NODE_SPACE_FOR_CELLS: 4082",
       "LEAF_NODE_MAX_CELLS: 13",
       "db > ",
     ])
```

现在，每当我们想让游标越过叶子节点的末尾时，就可以检查该叶子节点是否有兄弟节点。如果有，就跳过去。否则，我们就到了表的末尾。

```diff
@@ -428,7 +432,15 @@ void cursor_advance(Cursor* cursor) {
 
   cursor->cell_num += 1;
   if (cursor->cell_num >= (*leaf_node_num_cells(node))) {
-    cursor->end_of_table = true;
+    /* Advance to next leaf node */
+    uint32_t next_page_num = *leaf_node_next_leaf(node);
+    if (next_page_num == 0) {
+      /* This was rightmost leaf */
+      cursor->end_of_table = true;
+    } else {
+      cursor->page_num = next_page_num;
+      cursor->cell_num = 0;
+    }
   }
 }
```

这些改动之后，我们真的打印出了 15 行……
```
db > select
(1, user1, person1@example.com)
(2, user2, person2@example.com)
(3, user3, person3@example.com)
(4, user4, person4@example.com)
(5, user5, person5@example.com)
(6, user6, person6@example.com)
(7, user7, person7@example.com)
(8, user8, person8@example.com)
(9, user9, person9@example.com)
(10, user10, person10@example.com)
(11, user11, person11@example.com)
(12, user12, person12@example.com)
(13, user13, person13@example.com)
(1919251317, 14, on14@example.com)
(15, user15, person15@example.com)
Executed.
db >
```

……但其中一行看起来损坏了
```
(1919251317, 14, on14@example.com)
```

经过一些调试，我发现这是因为我们拆分叶子节点的方式有一个 bug：

```diff
@@ -676,7 +690,9 @@ void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value) {
     void* destination = leaf_node_cell(destination_node, index_within_node);
 
     if (i == cursor->cell_num) {
-      serialize_row(value, destination);
+      serialize_row(value,
+                    leaf_node_value(destination_node, index_within_node));
+      *leaf_node_key(destination_node, index_within_node) = key;
     } else if (i > cursor->cell_num) {
       memcpy(destination, leaf_node_cell(old_node, i - 1), LEAF_NODE_CELL_SIZE);
     } else {
```

记住叶子节点中的每个单元格都是先一个键，再一个值：

{% include image.html url="assets/images/leaf-node-format.png" description="原始的叶子节点格式" %}

我们之前把新行（值）写到了单元格的开头，也就是本该放键的地方。这意味着 username 的一部分跑到了 id 的区间（因此出现了那个大得离谱的 id）。

修复这个 bug 后，我们终于按预期打印出了整张表：

```
db > select
(1, user1, person1@example.com)
(2, user2, person2@example.com)
(3, user3, person3@example.com)
(4, user4, person4@example.com)
(5, user5, person5@example.com)
(6, user6, person6@example.com)
(7, user7, person7@example.com)
(8, user8, person8@example.com)
(9, user9, person9@example.com)
(10, user10, person10@example.com)
(11, user11, person11@example.com)
(12, user12, person12@example.com)
(13, user13, person13@example.com)
(14, user14, person14@example.com)
(15, user15, person15@example.com)
Executed.
db >
```

呼！一个接一个的 bug，但我们正在取得进展。

下次见。
