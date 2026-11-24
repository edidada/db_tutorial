---
title: 第 14 部分 - 拆分内部节点
date: 2023-05-23
---

我们旅程的下一段将是拆分无法容纳新键的内部节点。考虑下面的例子：

{% include image.html url="assets/images/splitting-internal-node.png" description="拆分内部节点的示例" %}

在这个例子中，我们向树中添加键 "11"。这会导致根节点拆分。在拆分内部节点时，我们必须做几件事来让一切保持井然有序：

1. 创建一个兄弟节点来存储原节点 (n-1)/2 的键
2. 把这些键从原节点移到兄弟节点
3. 更新原节点在父节点中的键，以反映拆分后的新最大键
4. 把兄弟节点插入父节点（这可能导致父节点也被拆分）

我们首先把桩代码替换为对 `internal_node_split_and_insert` 的调用

```diff
+void internal_node_split_and_insert(Table* table, uint32_t parent_page_num,
+                          uint32_t child_page_num);
+
 void internal_node_insert(Table* table, uint32_t parent_page_num,
                           uint32_t child_page_num) {
   /*
@@ -685,25 +714,39 @@ void internal_node_insert(Table* table, uint32_t parent_page_num,
 
   void* parent = get_page(table->pager, parent_page_num);
   void* child = get_page(table->pager, child_page_num);
-  uint32_t child_max_key = get_node_max_key(child);
+  uint32_t child_max_key = get_node_max_key(table->pager, child);
   uint32_t index = internal_node_find_child(parent, child_max_key);
 
   uint32_t original_num_keys = *internal_node_num_keys(parent);
-  *internal_node_num_keys(parent) = original_num_keys + 1;
 
   if (original_num_keys >= INTERNAL_NODE_MAX_CELLS) {
-    printf("Need to implement splitting internal node\n");
-    exit(EXIT_FAILURE);
+    internal_node_split_and_insert(table, parent_page_num, child_page_num);
+    return;
   }
 
   uint32_t right_child_page_num = *internal_node_right_child(parent);
+  /*
+  An internal node with a right child of INVALID_PAGE_NUM is empty
+  */
+  if (right_child_page_num == INVALID_PAGE_NUM) {
+    *internal_node_right_child(parent) = child_page_num;
+    return;
+  }
+
   void* right_child = get_page(table->pager, right_child_page_num);
+  /*
+  If we are already at the max number of cells for a node, we cannot increment
+  before splitting. Incrementing without inserting a new key/child pair
+  and immediately calling internal_node_split_and_insert has the effect
+  of creating a new key at (max_cells + 1) with an uninitialized value
+  */
+  *internal_node_num_keys(parent) = original_num_keys + 1;
 
-  if (child_max_key > get_node_max_key(right_child)) {
+  if (child_max_key > get_node_max_key(table->pager, right_child)) {
     /* Replace right child */
     *internal_node_child(parent, original_num_keys) = right_child_page_num;
     *internal_node_key(parent, original_num_keys) =
-        get_node_max_key(right_child);
+        get_node_max_key(table->pager, right_child);
     *internal_node_right_child(parent) = child_page_num;
```
 
除了替换桩代码之外，我们在这里做了三个重要的改动：
 - 首先，`internal_node_split_and_insert` 被提前声明了，因为我们会在它的定义中调用 `internal_node_insert` 以避免代码重复。
 - 此外，我们把增加父节点键数的逻辑移到了函数定义中更靠后的位置，以确保这不会在拆分之前发生。
 - 最后，我们确保插入空内部节点的子节点会成为该内部节点的右子节点，而不执行任何其他操作，因为空内部节点没有键可供操作。

上面的改动要求我们能够识别一个空节点——为此，我们首先要定义一个常量，表示每个空节点的子节点所使用的无效页号。

```diff
+#define INVALID_PAGE_NUM UINT32_MAX
```
现在，当初始化一个内部节点时，我们用这个无效页号初始化它的右子节点。

```diff
@@ -330,6 +335,12 @@ void initialize_internal_node(void* node) {
   set_node_type(node, NODE_INTERNAL);
   set_node_root(node, false);
   *internal_node_num_keys(node) = 0;
+  /*
+  Necessary because the root page number is 0; by not initializing an internal 
+  node's right child to an invalid page number when initializing the node, we may
+  end up with 0 as the node's right child, which makes the node a parent of the root
+  */
+  *internal_node_right_child(node) = INVALID_PAGE_NUM;
 }
```

这一步之所以必要，是因为上面注释试图概括的一个问题——在初始化内部节点时如果不显式初始化右子字段，该字段在运行时可能为 0，具体取决于编译器或运行程序的机器架构。由于我们用 0 作为根页号，这意味着一个新分配的内部节点会成为根的父节点。

我们在 `internal_node_child` 函数中加入了一些保护措施，当试图访问无效页时抛出错误。

```diff
@@ -186,9 +188,19 @@ uint32_t* internal_node_child(void* node, uint32_t child_num) {
     printf("Tried to access child_num %d > num_keys %d\n", child_num, num_keys);
     exit(EXIT_FAILURE);
   } else if (child_num == num_keys) {
-    return internal_node_right_child(node);
+    uint32_t* right_child = internal_node_right_child(node);
+    if (*right_child == INVALID_PAGE_NUM) {
+      printf("Tried to access right child of node, but was invalid page\n");
+      exit(EXIT_FAILURE);
+    }
+    return right_child;
   } else {
-    return internal_node_cell(node, child_num);
+    uint32_t* child = internal_node_cell(node, child_num);
+    if (*child == INVALID_PAGE_NUM) {
+      printf("Tried to access child %d of node, but was invalid page\n", child_num);
+      exit(EXIT_FAILURE);
+    }
+    return child;
   }
 }
```

我们的 `print_tree` 函数中还需要一个额外的保护措施，以确保我们不会试图打印一个空节点，因为那会涉及访问一个无效页。

```diff
@@ -294,15 +305,17 @@ void print_tree(Pager* pager, uint32_t page_num, uint32_t indentation_level) {
       num_keys = *internal_node_num_keys(node);
       indent(indentation_level);
       printf("- internal (size %d)\n", num_keys);
-      for (uint32_t i = 0; i < num_keys; i++) {
-        child = *internal_node_child(node, i);
+      if (num_keys > 0) {
+        for (uint32_t i = 0; i < num_keys; i++) {
+          child = *internal_node_child(node, i);
+          print_tree(pager, child, indentation_level + 1);
+
+          indent(indentation_level + 1);
+          printf("- key %d\n", *internal_node_key(node, i));
+        }
+        child = *internal_node_right_child(node);
         print_tree(pager, child, indentation_level + 1);
-
-        indent(indentation_level + 1);
-        printf("- key %d\n", *internal_node_key(node, i));
       }
-      child = *internal_node_right_child(node);
-      print_tree(pager, child, indentation_level + 1);
       break;
   }
 }
```

现在是重头戏，`internal_node_split_and_insert`。我们先完整地给出它，然后再按步骤分解。

```diff
+void internal_node_split_and_insert(Table* table, uint32_t parent_page_num,
+                          uint32_t child_page_num) {
+  uint32_t old_page_num = parent_page_num;
+  void* old_node = get_page(table->pager,parent_page_num);
+  uint32_t old_max = get_node_max_key(table->pager, old_node);
+
+  void* child = get_page(table->pager, child_page_num); 
+  uint32_t child_max = get_node_max_key(table->pager, child);
+
+  uint32_t new_page_num = get_unused_page_num(table->pager);
+
+  /*
+  Declaring a flag before updating pointers which
+  records whether this operation involves splitting the root -
+  if it does, we will insert our newly created node during
+  the step where the table's new root is created. If it does
+  not, we have to insert the newly created node into its parent
+  after the old node's keys have been transferred over. We are not
+  able to do this if the newly created node's parent is not a newly
+  initialized root node, because in that case its parent may have existing
+  keys aside from our old node which we are splitting. If that is true, we
+  need to find a place for our newly created node in its parent, and we
+  cannot insert it at the correct index if it does not yet have any keys
+  */
+  uint32_t splitting_root = is_node_root(old_node);
+
+  void* parent;
+  void* new_node;
+  if (splitting_root) {
+    create_new_root(table, new_page_num);
+    parent = get_page(table->pager,table->root_page_num);
+    /*
+    If we are splitting the root, we need to update old_node to point
+    to the new root's left child, new_page_num will already point to
+    the new root's right child
+    */
+    old_page_num = *internal_node_child(parent,0);
+    old_node = get_page(table->pager, old_page_num);
+  } else {
+    parent = get_page(table->pager,*node_parent(old_node));
+    new_node = get_page(table->pager, new_page_num);
+    initialize_internal_node(new_node);
+  }
+  
+  uint32_t* old_num_keys = internal_node_num_keys(old_node);
+
+  uint32_t cur_page_num = *internal_node_right_child(old_node);
+  void* cur = get_page(table->pager, cur_page_num);
+
+  /*
+  First put right child into new node and set right child of old node to invalid page number
+  */
+  internal_node_insert(table, new_page_num, cur_page_num);
+  *node_parent(cur) = new_page_num;
+  *internal_node_right_child(old_node) = INVALID_PAGE_NUM;
+  /*
+  For each key until you get to the middle key, move the key and the child to the new node
+  */
+  for (int i = INTERNAL_NODE_MAX_CELLS - 1; i > INTERNAL_NODE_MAX_CELLS / 2; i--) {
+    cur_page_num = *internal_node_child(old_node, i);
+    cur = get_page(table->pager, cur_page_num);
+
+    internal_node_insert(table, new_page_num, cur_page_num);
+    *node_parent(cur) = new_page_num;
+
+    (*old_num_keys)--;
+  }
+
+  /*
+  Set child before middle key, which is now the highest key, to be node's right child,
+  and decrement number of keys
+  */
+  *internal_node_right_child(old_node) = *internal_node_child(old_node,*old_num_keys - 1);
+  (*old_num_keys)--;
+
+  /*
+  Determine which of the two nodes after the split should contain the child to be inserted,
+  and insert the child
+  */
+  uint32_t max_after_split = get_node_max_key(table->pager, old_node);
+
+  uint32_t destination_page_num = child_max < max_after_split ? old_page_num : new_page_num;
+
+  internal_node_insert(table, destination_page_num, child_page_num);
+  *node_parent(child) = destination_page_num;
+
+  update_internal_node_key(parent, old_max, get_node_max_key(table->pager, old_node));
+
+  if (!splitting_root) {
+    internal_node_insert(table,*node_parent(old_node),new_page_num);
+    *node_parent(new_node) = *node_parent(old_node);
+  }
+}
+
```

我们要做的第一件事是创建一个变量来存储我们要拆分的节点的页号（从这里开始称为旧节点）。这是必要的，因为如果旧节点恰好是表的根节点，它的页号会改变。我们还需要记住节点当前的最大值，因为那个值代表它在父节点中的键，拆分后这个键需要用旧节点新的最大值来更新。

```diff
+  uint32_t old_page_num = parent_page_num;
+  void* old_node = get_page(table->pager,parent_page_num);
+  uint32_t old_max = get_node_max_key(table->pager, old_node);
```

下一个重要步骤是分支逻辑，它取决于旧节点是否是表的根节点。我们需要记录这个值以备后用；正如注释试图传达的那样，如果我们不在函数定义的开头存储这个信息，就会遇到问题——如果不是在拆分根节点，我们就不能立刻把新创建的兄弟节点插入旧节点的父节点，因为它还没有任何键，因此无法被放到父节点中可能已经存在的其他键／子对之间的正确索引位置。

```diff
+  uint32_t splitting_root = is_node_root(old_node);
+
+  void* parent;
+  void* new_node;
+  if (splitting_root) {
+    create_new_root(table, new_page_num);
+    parent = get_page(table->pager,table->root_page_num);
+    /*
+    If we are splitting the root, we need to update old_node to point
+    to the new root's left child, new_page_num will already point to
+    the new root's right child
+    */
+    old_page_num = *internal_node_child(parent,0);
+    old_node = get_page(table->pager, old_page_num);
+  } else {
+    parent = get_page(table->pager,*node_parent(old_node));
+    new_node = get_page(table->pager, new_page_num);
+    initialize_internal_node(new_node);
+  }
```

一旦确定了是否拆分根节点的问题，我们就开始把键从旧节点移到它的兄弟节点。我们必须先把旧节点的右子节点移过去，并把它的右子字段设为一个无效页以表示它是空的。现在，我们遍历旧节点剩余的键，在每次迭代中执行以下步骤：
 1. 获取当前索引处旧节点的键和子节点的引用
 2. 把子节点插入兄弟节点
 3. 更新子节点的父值，使其指向兄弟节点
 4. 减少旧节点的键数

```diff
+  uint32_t* old_num_keys = internal_node_num_keys(old_node);
+
+  uint32_t cur_page_num = *internal_node_right_child(old_node);
+  void* cur = get_page(table->pager, cur_page_num);
+
+  /*
+  First put right child into new node and set right child of old node to invalid page number
+  */
+  internal_node_insert(table, new_page_num, cur_page_num);
+  *node_parent(cur) = new_page_num;
+  *internal_node_right_child(old_node) = INVALID_PAGE_NUM;
+  /*
+  For each key until you get to the middle key, move the key and the child to the new node
+  */
+  for (int i = INTERNAL_NODE_MAX_CELLS - 1; i > INTERNAL_NODE_MAX_CELLS / 2; i--) {
+    cur_page_num = *internal_node_child(old_node, i);
+    cur = get_page(table->pager, cur_page_num);
+
+    internal_node_insert(table, new_page_num, cur_page_num);
+    *node_parent(cur) = new_page_num;
+
+    (*old_num_keys)--;
+  }
```

第 4 步很重要，因为它起到了从旧节点中“擦除”键／子对的作用。虽然我们实际上并没有释放旧节点页中那个字节偏移处的内存，但通过减少旧节点的键数，我们让那个内存位置变得不可访问，这些字节会在下一次有子节点插入旧节点时被覆盖。

还要注意我们的循环不变量的行为——如果将来内部节点键的最大数量发生变化，我们的逻辑确保旧节点和兄弟节点在拆分后都以 (n-1)/2 个键结束，剩下的 1 个键归父节点。如果选择了偶数作为节点的最大数量，那么 old 节点保留 n/2 个节点，而 (n-1)/2 个节点被移到兄弟节点。如果需要，这段代码逻辑可以很容易地修改。

要移动的键移完之后，我们把旧节点的第 i 个子节点设为它的右子节点，并减少它的键数。

```diff
+  /*
+  Set child before middle key, which is now the highest key, to be node's right child,
+  and decrement number of keys
+  */
+  *internal_node_right_child(old_node) = *internal_node_child(old_node,*old_num_keys - 1);
+  (*old_num_keys)--;
```

然后我们根据子节点最大值把它插入旧节点或兄弟节点。

```diff
+  uint32_t max_after_split = get_node_max_key(table->pager, old_node);
+
+  uint32_t destination_page_num = child_max < max_after_split ? old_page_num : new_page_num;
+
+  internal_node_insert(table, destination_page_num, child_page_num);
+  *node_parent(child) = destination_page_num;
```

最后，我们更新旧节点在父节点中的键，并插入兄弟节点，必要时更新兄弟节点的父指针。

```diff
+  update_internal_node_key(parent, old_max, get_node_max_key(table->pager, old_node));
+
+  if (!splitting_root) {
+    internal_node_insert(table,*node_parent(old_node),new_page_num);
+    *node_parent(new_node) = *node_parent(old_node);
+  }
```

支持这个新逻辑所需的一个重要改动在 `create_new_root` 函数中。以前，我们只考虑新根节点的子节点是叶子节点的情况。如果新根节点的子节点是内部节点，我们需要做两件事：
 1. 正确地把根节点的新子节点初始化为内部节点
 2. 除了 memcpy 调用之外，我们还需要把根节点的每个键插入到它的新左子节点中，并更新这些子节点中每一个的父指针

```diff
@@ -661,22 +680,40 @@ void create_new_root(Table* table, uint32_t right_child_page_num) {
   uint32_t left_child_page_num = get_unused_page_num(table->pager);
   void* left_child = get_page(table->pager, left_child_page_num);
 
+  if (get_node_type(root) == NODE_INTERNAL) {
+    initialize_internal_node(right_child);
+    initialize_internal_node(left_child);
+  }
+
   /* Left child has data copied from old root */
   memcpy(left_child, root, PAGE_SIZE);
   set_node_root(left_child, false);
 
+  if (get_node_type(left_child) == NODE_INTERNAL) {
+    void* child;
+    for (int i = 0; i < *internal_node_num_keys(left_child); i++) {
+      child = get_page(table->pager, *internal_node_child(left_child,i));
+      *node_parent(child) = left_child_page_num;
+    }
+    child = get_page(table->pager, *internal_node_right_child(left_child));
+    *node_parent(child) = left_child_page_num;
+  }
+
   /* Root node is a new internal node with one key and two children */
   initialize_internal_node(root);
   set_node_root(root, true);
   *internal_node_num_keys(root) = 1;
   *internal_node_child(root, 0) = left_child_page_num;
-  uint32_t left_child_max_key = get_node_max_key(left_child);
+  uint32_t left_child_max_key = get_node_max_key(table->pager, left_child);
   *internal_node_key(root, 0) = left_child_max_key;
   *internal_node_right_child(root) = right_child_page_num;
   *node_parent(left_child) = table->root_page_num;
   *node_parent(right_child) = table->root_page_num;
 }
```

正如本文开头提到的，`get_node_max_key` 也做了一个重要改动。由于内部节点的键代表它左边子节点所指向的树的最大值，而那个子节点可以是一棵任意深度的树，我们需要沿着那棵树的右子节点一路向下走，直到到达叶子节点，然后取那个叶子节点的最大键。

```diff
+uint32_t get_node_max_key(Pager* pager, void* node) {
+  if (get_node_type(node) == NODE_LEAF) {
+    return *leaf_node_key(node, *leaf_node_num_cells(node) - 1);
+  }
+  void* right_child = get_page(pager,*internal_node_right_child(node));
+  return get_node_max_key(pager, right_child);
+}
```

我们写了一个测试来证明引入内部节点拆分后 `print_tree` 函数仍然能工作。

```diff
+  it 'allows printing out the structure of a 7-leaf-node btree' do
+    script = [
+      "insert 58 user58 person58@example.com",
+      "insert 56 user56 person56@example.com",
+      "insert 8 user8 person8@example.com",
+      "insert 54 user54 person54@example.com",
+      "insert 77 user77 person77@example.com",
+      "insert 7 user7 person7@example.com",
+      "insert 25 user25 person25@example.com",
+      "insert 71 user71 person71@example.com",
+      "insert 13 user13 person13@example.com",
+      "insert 22 user22 person22@example.com",
+      "insert 53 user53 person53@example.com",
+      "insert 51 user51 person51@example.com",
+      "insert 59 user59 person59@example.com",
+      "insert 32 user32 person32@example.com",
+      "insert 36 user36 person36@example.com",
+      "insert 79 user79 person79@example.com",
+      "insert 10 user10 person10@example.com",
+      "insert 33 user33 person33@example.com",
+      "insert 20 user20 person20@example.com",
+      "insert 4 user4 person4@example.com",
+      "insert 35 user35 person35@example.com",
+      "insert 76 user76 person76@example.com",
+      "insert 49 user49 person49@example.com",
+      "insert 24 user24 person24@example.com",
+      "insert 70 user70 person70@example.com",
+      "insert 48 user48 person48@example.com",
+      "insert 39 user39 person39@example.com",
+      "insert 15 user15 person15@example.com",
+      "insert 47 user47 person47@example.com",
+      "insert 30 user30 person30@example.com",
+      "insert 86 user86 person86@example.com",
+      "insert 31 user31 person31@example.com",
+      "insert 68 user68 person68@example.com",
+      "insert 37 user37 person37@example.com",
+      "insert 66 user66 person66@example.com",
+      "insert 63 user63 person63@example.com",
+      "insert 40 user40 person40@example.com",
+      "insert 78 user78 person78@example.com",
+      "insert 19 user19 person19@example.com",
+      "insert 46 user46 person46@example.com",
+      "insert 14 user14 person14@example.com",
+      "insert 81 user81 person81@example.com",
+      "insert 72 user72 person72@example.com",
+      "insert 6 user6 person6@example.com",
+      "insert 50 user50 person50@example.com",
+      "insert 85 user85 person85@example.com",
+      "insert 67 user67 person67@example.com",
+      "insert 2 user2 person2@example.com",
+      "insert 55 user55 person55@example.com",
+      "insert 69 user69 person69@example.com",
+      "insert 5 user5 person5@example.com",
+      "insert 65 user65 person65@example.com",
+      "insert 52 user52 person52@example.com",
+      "insert 1 user1 person1@example.com",
+      "insert 29 user29 person29@example.com",
+      "insert 9 user9 person9@example.com",
+      "insert 43 user43 person43@example.com",
+      "insert 75 user75 person75@example.com",
+      "insert 21 user21 person21@example.com",
+      "insert 82 user82 person82@example.com",
+      "insert 12 user12 person12@example.com",
+      "insert 18 user18 person18@example.com",
+      "insert 60 user60 person60@example.com",
+      "insert 44 user44 person44@example.com",
+      ".btree",
+      ".exit",
+    ]
+    result = run_script(script)
+
+    expect(result[64...(result.length)]).to match_array([
+      "db > Tree:",
+      "- internal (size 1)",
+      "  - internal (size 2)",
+      "    - leaf (size 7)",
+      "      - 1",
+      "      - 2",
+      "      - 4",
+      "      - 5",
+      "      - 6",
+      "      - 7",
+      "      - 8",
+      "    - key 8",
+      "    - leaf (size 11)",
+      "      - 9",
+      "      - 10",
+      "      - 12",
+      "      - 13",
+      "      - 14",
+      "      - 15",
+      "      - 18",
+      "      - 19",
+      "      - 20",
+      "      - 21",
+      "      - 22",
+      "    - key 22",
+      "    - leaf (size 8)",
+      "      - 24",
+      "      - 25",
+      "      - 29",
+      "      - 30",
+      "      - 31",
+      "      - 32",
+      "      - 33",
+      "      - 35",
+      "  - key 35",
+      "  - internal (size 3)",
+      "    - leaf (size 12)",
+      "      - 36",
+      "      - 37",
+      "      - 39",
+      "      - 40",
+      "      - 43",
+      "      - 44",
+      "      - 46",
+      "      - 47",
+      "      - 48",
+      "      - 49",
+      "      - 50",
+      "      - 51",
+      "    - key 51",
+      "    - leaf (size 11)",
+      "      - 52",
+      "      - 53",
+      "      - 54",
+      "      - 55",
+      "      - 56",
+      "      - 58",
+      "      - 59",
+      "      - 60",
+      "      - 63",
+      "      - 65",
+      "      - 66",
+      "    - key 66",
+      "    - leaf (size 7)",
+      "      - 67",
+      "      - 68",
+      "      - 69",
+      "      - 70",
+      "      - 71",
+      "      - 72",
+      "      - 75",
+      "    - key 75",
+      "    - leaf (size 8)",
+      "      - 76",
+      "      - 77",
+      "      - 78",
+      "      - 79",
+      "      - 81",
+      "      - 82",
+      "      - 85",
+      "      - 86",
+      "db > ",
+    ])
+  end
```
