---
title: 第 11 部分 - 递归搜索 B 树
date: 2017-10-22
---

上次我们以插入第 15 行时的错误结束：

```
db > insert 15 user15 person15@example.com
Need to implement searching an internal node
```

首先，把代码桩替换成一个新的函数调用。

```diff
   if (get_node_type(root_node) == NODE_LEAF) {
     return leaf_node_find(table, root_page_num, key);
   } else {
-    printf("Need to implement searching an internal node\n");
-    exit(EXIT_FAILURE);
+    return internal_node_find(table, root_page_num, key);
   }
 }
```

这个函数将执行二分查找，找到应该包含给定键的子节点。请记住，每个子指针右边的键就是该子节点包含的最大键。

{% include image.html url="assets/images/btree6.png" description="三层 btree" %}

因此我们的二分查找把要找的键和子指针右边的键进行比较：

```diff
+Cursor* internal_node_find(Table* table, uint32_t page_num, uint32_t key) {
+  void* node = get_page(table->pager, page_num);
+  uint32_t num_keys = *internal_node_num_keys(node);
+
+  /* Binary search to find index of child to search */
+  uint32_t min_index = 0;
+  uint32_t max_index = num_keys; /* there is one more child than key */
+
+  while (min_index != max_index) {
+    uint32_t index = (min_index + max_index) / 2;
+    uint32_t key_to_right = *internal_node_key(node, index);
+    if (key_to_right >= key) {
+      max_index = index;
+    } else {
+      min_index = index + 1;
+    }
+  }
```

还要记住，内部节点的子节点既可以是叶子节点，也可以是更多的内部节点。找到正确的子节点后，对它调用相应的搜索函数：

```diff
+  uint32_t child_num = *internal_node_child(node, min_index);
+  void* child = get_page(table->pager, child_num);
+  switch (get_node_type(child)) {
+    case NODE_LEAF:
+      return leaf_node_find(table, child_num, key);
+    case NODE_INTERNAL:
+      return internal_node_find(table, child_num, key);
+  }
+}
```

# 测试

现在向多节点 btree 插入键不再报错了。我们可以更新测试：

```diff
       "    - 12",
       "    - 13",
       "    - 14",
-      "db > Need to implement searching an internal node",
+      "db > Executed.",
+      "db > ",
     ])
   end
```

我还认为是时候重新审视另一个测试了。就是那个尝试插入 1400 行的测试。它仍然会出错，但错误信息是新的。目前，当程序崩溃时我们的测试处理得不太好。如果发生这种情况，我们就使用目前为止得到的输出：

```diff
     raw_output = nil
     IO.popen("./db test.db", "r+") do |pipe|
       commands.each do |command|
-        pipe.puts command
+        begin
+          pipe.puts command
+        rescue Errno::EPIPE
+          break
+        end
       end

       pipe.close_write
```

这揭示了我们的 1400 行测试输出了这个错误：

```diff
     end
     script << ".exit"
     result = run_script(script)
-    expect(result[-2]).to eq('db > Error: Table full.')
+    expect(result.last(2)).to match_array([
+      "db > Executed.",
+      "db > Need to implement updating parent after split",
+    ])
   end
```

看来这就是我们待办清单上的下一项了！
