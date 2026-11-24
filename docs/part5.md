---
title: 第 5 部分 - 持久化到磁盘
date: 2017-09-08
---

> "世界上没有任何东西能取代坚持。" —— [卡尔文·柯立芝](https://en.wikiquote.org/wiki/Calvin_Coolidge)

我们的数据库可以让你插入记录并把它们读回来，但前提是你得让程序一直运行着。如果你杀掉程序再重新启动，所有记录就没了。下面是我们想要的行为的规格：

```ruby
it 'keeps data after closing connection' do
  result1 = run_script([
    "insert 1 user1 person1@example.com",
    ".exit",
  ])
  expect(result1).to match_array([
    "db > Executed.",
    "db > ",
  ])
  result2 = run_script([
    "select",
    ".exit",
  ])
  expect(result2).to match_array([
    "db > (1, user1, person1@example.com)",
    "Executed.",
    "db > ",
  ])
end
```

和 SQLite 一样，我们要通过把整个数据库保存到文件来持久化记录。

我们已经通过把行序列化成页大小的内存块为此做好了准备。要增加持久化，我们只需把这些内存块写入文件，并在下次程序启动时把它们读回内存。

为了让这件事更容易，我们要做一个叫做分页器（pager）的抽象。我们向分页器请求第 `x` 号页，分页器就返回一块内存。它先在缓存中查找。如果缓存未命中，它就（通过读取数据库文件）把磁盘上的数据复制到内存中。

{% include image.html url="assets/images/arch-part5.gif" description="我们的程序与 SQLite 架构的对应关系" %}

分页器访问页缓存和文件。Table 对象通过分页器来请求页：

```diff
+typedef struct {
+  int file_descriptor;
+  uint32_t file_length;
+  void* pages[TABLE_MAX_PAGES];
+} Pager;
+
 typedef struct {
-  void* pages[TABLE_MAX_PAGES];
+  Pager* pager;
   uint32_t num_rows;
 } Table;
```

我把 `new_table()` 重命名为 `db_open()`，因为它现在具有打开数据库连接的效果。所谓打开连接，我的意思是：

- 打开数据库文件
- 初始化分页器数据结构
- 初始化表数据结构

```diff
-Table* new_table() {
+Table* db_open(const char* filename) {
+  Pager* pager = pager_open(filename);
+  uint32_t num_rows = pager->file_length / ROW_SIZE;
+
   Table* table = malloc(sizeof(Table));
-  table->num_rows = 0;
+  table->pager = pager;
+  table->num_rows = num_rows;

   return table;
 }
```

`db_open()` 进而调用 `pager_open()`，后者打开数据库文件并跟踪它的大小。它还会把页缓存初始化为全 `NULL`。

```diff
+Pager* pager_open(const char* filename) {
+  int fd = open(filename,
+                O_RDWR |      // Read/Write mode
+                    O_CREAT,  // Create file if it does not exist
+                S_IWUSR |     // User write permission
+                    S_IRUSR   // User read permission
+                );
+
+  if (fd == -1) {
+    printf("Unable to open file\n");
+    exit(EXIT_FAILURE);
+  }
+
+  off_t file_length = lseek(fd, 0, SEEK_END);
+
+  Pager* pager = malloc(sizeof(Pager));
+  pager->file_descriptor = fd;
+  pager->file_length = file_length;
+
+  for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
+    pager->pages[i] = NULL;
+  }
+
+  return pager;
+}
```

按照我们新的抽象，把获取页的逻辑移到它自己的方法中：

```diff
 void* row_slot(Table* table, uint32_t row_num) {
   uint32_t page_num = row_num / ROWS_PER_PAGE;
-  void* page = table->pages[page_num];
-  if (page == NULL) {
-    // Allocate memory only when we try to access page
-    page = table->pages[page_num] = malloc(PAGE_SIZE);
-  }
+  void* page = get_page(table->pager, page_num);
   uint32_t row_offset = row_num % ROWS_PER_PAGE;
   uint32_t byte_offset = row_offset * ROW_SIZE;
   return page + byte_offset;
 }
```

`get_page()` 方法包含处理缓存未命中的逻辑。我们假设各个页在数据库文件中是一个接一个保存的：第 0 页在偏移量 0 处，第 1 页在偏移量 4096 处，第 2 页在偏移量 8192 处，以此类推。如果请求的页超出了文件的范围，我们就知道它应该是空白的，所以只需分配一些内存并返回它。稍后当我们把缓存刷到磁盘时，这个页会被加入文件中。

```diff
+void* get_page(Pager* pager, uint32_t page_num) {
+  if (page_num > TABLE_MAX_PAGES) {
+    printf("Tried to fetch page number out of bounds. %d > %d\n", page_num,
+           TABLE_MAX_PAGES);
+    exit(EXIT_FAILURE);
+  }
+
+  if (pager->pages[page_num] == NULL) {
+    // Cache miss. Allocate memory and load from file.
+    void* page = malloc(PAGE_SIZE);
+    uint32_t num_pages = pager->file_length / PAGE_SIZE;
+
+    // We might save a partial page at the end of the file
+    if (pager->file_length % PAGE_SIZE) {
+      num_pages += 1;
+    }
+
+    if (page_num <= num_pages) {
+      lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
+      ssize_t bytes_read = read(pager->file_descriptor, page, PAGE_SIZE);
+      if (bytes_read == -1) {
+        printf("Error reading file: %d\n", errno);
+        exit(EXIT_FAILURE);
+      }
+    }
+
+    pager->pages[page_num] = page;
+  }
+
+  return pager->pages[page_num];
+}
```

目前，我们会一直等到用户关闭与数据库的连接时才把缓存刷到磁盘。当用户退出时，我们会调用一个名为 `db_close()` 的新方法，它会：

- 把页缓存刷到磁盘
- 关闭数据库文件
- 释放分页器和表数据结构占用的内存

```diff
+void db_close(Table* table) {
+  Pager* pager = table->pager;
+  uint32_t num_full_pages = table->num_rows / ROWS_PER_PAGE;
+
+  for (uint32_t i = 0; i < num_full_pages; i++) {
+    if (pager->pages[i] == NULL) {
+      continue;
+    }
+    pager_flush(pager, i, PAGE_SIZE);
+    free(pager->pages[i]);
+    pager->pages[i] = NULL;
+  }
+
+  // There may be a partial page to write to the end of the file
+  // This should not be needed after we switch to a B-tree
+  uint32_t num_additional_rows = table->num_rows % ROWS_PER_PAGE;
+  if (num_additional_rows > 0) {
+    uint32_t page_num = num_full_pages;
+    if (pager->pages[page_num] != NULL) {
+      pager_flush(pager, page_num, num_additional_rows * ROW_SIZE);
+      free(pager->pages[page_num]);
+      pager->pages[page_num] = NULL;
+    }
+  }
+
+  int result = close(pager->file_descriptor);
+  if (result == -1) {
+    printf("Error closing db file.\n");
+    exit(EXIT_FAILURE);
+  }
+  for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
+    void* page = pager->pages[i];
+    if (page) {
+      free(page);
+      pager->pages[i] = NULL;
+    }
+  }
+  free(pager);
+  free(table);
+}
+
-MetaCommandResult do_meta_command(InputBuffer* input_buffer) {
+MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table) {
   if (strcmp(input_buffer->buffer, ".exit") == 0) {
+    db_close(table);
     exit(EXIT_SUCCESS);
   } else {
     return META_COMMAND_UNRECOGNIZED_COMMAND;
```

在我们当前的设计中，文件的长度编码了数据库中有多少行，所以我们需要在文件末尾写入一个不完整的页。这就是为什么 `pager_flush()` 同时接收页号和大小。这不是最好的设计，但当我们开始实现 B 树时，它很快就会消失。

```diff
+void pager_flush(Pager* pager, uint32_t page_num, uint32_t size) {
+  if (pager->pages[page_num] == NULL) {
+    printf("Tried to flush null page\n");
+    exit(EXIT_FAILURE);
+  }
+
+  off_t offset = lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
+
+  if (offset == -1) {
+    printf("Error seeking: %d\n", errno);
+    exit(EXIT_FAILURE);
+  }
+
+  ssize_t bytes_written =
+      write(pager->file_descriptor, pager->pages[page_num], size);
+
+  if (bytes_written == -1) {
+    printf("Error writing: %d\n", errno);
+    exit(EXIT_FAILURE);
+  }
+}
```

最后，我们需要接受文件名作为命令行参数。别忘了也要给 `do_meta_command` 加上这个额外的参数：

```diff
 int main(int argc, char* argv[]) {
-  Table* table = new_table();
+  if (argc < 2) {
+    printf("Must supply a database filename.\n");
+    exit(EXIT_FAILURE);
+  }
+
+  char* filename = argv[1];
+  Table* table = db_open(filename);
+
   InputBuffer* input_buffer = new_input_buffer();
   while (true) {
     print_prompt();
     read_input(input_buffer);

     if (input_buffer->buffer[0] == '.') {
-      switch (do_meta_command(input_buffer)) {
+      switch (do_meta_command(input_buffer, table)) {
```
有了这些改动，我们就能关闭再重新打开数据库，而记录还在！
```
~ ./db mydb.db
db > insert 1 cstack foo@bar.com
Executed.
db > insert 2 voltorb volty@example.com
Executed.
db > .exit
~
~ ./db mydb.db
db > select
(1, cstack, foo@bar.com)
(2, voltorb, volty@example.com)
Executed.
db > .exit
~
```

为了好玩，让我们看看 `mydb.db`，了解一下数据是如何存储的。我会用 vim 作为十六进制编辑器来查看文件的内存布局：

```
vim mydb.db
:%!xxd
```
{% include image.html url="assets/images/file-format.png" description="当前文件格式" %}

最前面的四个字节是第一行的 id（4 字节，因为我们存储的是 `uint32_t`）。它以小端字节序存储，所以最低有效字节在前（01），后面是高位字节（00 00 00）。我们用 `memcpy()` 把 `Row` 结构体中的字节复制到页缓存中，这意味着结构体在内存中就是以小端字节序布局的。这是与我编译程序的目标机器相关的一个属性。如果我们想在我的机器上写一个数据库文件，然后在大端机器上读取它，我们就必须修改 `serialize_row()` 和 `deserialize_row()` 方法，让它们始终以相同的顺序存储和读取字节。

接下来的 33 字节以空字符结尾的字符串形式存储 username。显然 "cstack" 的 ASCII 十六进制是 `63 73 74 61 63 6b`，后面跟着一个空字符（`00`）。这 33 字节中剩下的部分是未使用的。

再接下来的 256 字节以同样的方式存储 email。这里我们可以看到结尾空字符之后有一些随机垃圾。这很可能是由于 `Row` 结构体中未初始化的内存造成的。我们把整个 256 字节的 email 缓冲区都复制到了文件中，包括字符串结尾之后的字节。我们分配那个结构体时内存中是什么，现在就还是什么。但由于我们使用了结尾空字符，这对行为没有影响。

**注意**：如果我们想确保所有字节都被初始化，只需在 `serialize_row` 中复制行的 `username` 和 `email` 字段时，用 `strncpy` 代替 `memcpy`，像这样：

```diff
 void serialize_row(Row* source, void* destination) {
     memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
-    memcpy(destination + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
-    memcpy(destination + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
+    strncpy(destination + USERNAME_OFFSET, source->username, USERNAME_SIZE);
+    strncpy(destination + EMAIL_OFFSET, source->email, EMAIL_SIZE);
 }
```

## 结论

好了！我们实现了持久化。它不算很好。例如，如果你不输入 `.exit` 就杀掉程序，你的更改就会丢失。此外，我们把所有页都写回了磁盘，即使是那些从磁盘读入后从未改变过的页。这些都是我们以后可以解决的问题。

下次我们会引入游标（cursor），它应该能让实现 B 树更容易。

到那时见！

## 完整 Diff
```diff
+#include <errno.h>
+#include <fcntl.h>
 #include <stdbool.h>
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <stdint.h>
+#include <unistd.h>

 struct InputBuffer_t {
   char* buffer;
@@ -62,9 +65,16 @@ const uint32_t PAGE_SIZE = 4096;
 const uint32_t ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE;
 const uint32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES;

+typedef struct {
+  int file_descriptor;
+  uint32_t file_length;
+  void* pages[TABLE_MAX_PAGES];
+} Pager;
+
 typedef struct {
   uint32_t num_rows;
-  void* pages[TABLE_MAX_PAGES];
+  Pager* pager;
 } Table;

@@ -84,32 +94,81 @@ void deserialize_row(void *source, Row* destination) {
   memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
 }

+void* get_page(Pager* pager, uint32_t page_num) {
+  if (page_num > TABLE_MAX_PAGES) {
+     printf("Tried to fetch page number out of bounds. %d > %d\n", page_num,
+     	TABLE_MAX_PAGES);
+     exit(EXIT_FAILURE);
+  }
+
+  if (pager->pages[page_num] == NULL) {
+     // Cache miss. Allocate memory and load from file.
+     void* page = malloc(PAGE_SIZE);
+     uint32_t num_pages = pager->file_length / PAGE_SIZE;
+
+     // We might save a partial page at the end of the file
+     if (pager->file_length % PAGE_SIZE) {
+         num_pages += 1;
+     }
+
+     if (page_num <= num_pages) {
+         lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
+         ssize_t bytes_read = read(pager->file_descriptor, page, PAGE_SIZE);
+         if (bytes_read == -1) {
+     	printf("Error reading file: %d\n", errno);
+     	exit(EXIT_FAILURE);
+         }
+     }
+
+     pager->pages[page_num] = page;
+  }
+
+  return pager->pages[page_num];
+}
+
 void* row_slot(Table* table, uint32_t row_num) {
   uint32_t page_num = row_num / ROWS_PER_PAGE;
-  void *page = table->pages[page_num];
-  if (page == NULL) {
-     // Allocate memory only when we try to access page
-     page = table->pages[page_num] = malloc(PAGE_SIZE);
-  }
+  void *page = get_page(table->pager, page_num);
   uint32_t row_offset = row_num % ROWS_PER_PAGE;
   uint32_t byte_offset = row_offset * ROW_SIZE;
   return page + byte_offset;
 }

-Table* new_table() {
-  Table* table = malloc(sizeof(Table));
-  table->num_rows = 0;
+Pager* pager_open(const char* filename) {
+  int fd = open(filename,
+     	  O_RDWR | 	// Read/Write mode
+     	      O_CREAT,	// Create file if it does not exist
+     	  S_IWUSR |	// User write permission
+     	      S_IRUSR	// User read permission
+     	  );
+
+  if (fd == -1) {
+     printf("Unable to open file\n");
+     exit(EXIT_FAILURE);
+  }
+
+  off_t file_length = lseek(fd, 0, SEEK_END);
+
+  Pager* pager = malloc(sizeof(Pager));
+  pager->file_descriptor = fd;
+  pager->file_length = file_length;
+
   for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
-     table->pages[i] = NULL;
+     pager->pages[i] = NULL;
   }
-  return table;
+
+  return pager;
 }

-void free_table(Table* table) {
-  for (int i = 0; table->pages[i]; i++) {
-     free(table->pages[i]);
-  }
-  free(table);
+Table* db_open(const char* filename) {
+  Pager* pager = pager_open(filename);
+  uint32_t num_rows = pager->file_length / ROW_SIZE;
+
+  Table* table = malloc(sizeof(Table));
+  table->pager = pager;
+  table->num_rows = num_rows;
+
+  return table;
 }

 InputBuffer* new_input_buffer() {
@@ -142,10 +201,76 @@ void close_input_buffer(InputBuffer* input_buffer) {
   free(input_buffer);
 }

+void pager_flush(Pager* pager, uint32_t page_num, uint32_t size) {
+  if (pager->pages[page_num] == NULL) {
+     printf("Tried to flush null page\n");
+     exit(EXIT_FAILURE);
+  }
+
+  off_t offset = lseek(pager->file_descriptor, page_num * PAGE_SIZE,
+     		 SEEK_SET);
+
+  if (offset == -1) {
+     printf("Error seeking: %d\n", errno);
+     exit(EXIT_FAILURE);
+  }
+
+  ssize_t bytes_written = write(
+     pager->file_descriptor, pager->pages[page_num], size
+     );
+
+  if (bytes_written == -1) {
+     printf("Error writing: %d\n", errno);
+     exit(EXIT_FAILURE);
+  }
+}
+
+void db_close(Table* table) {
+  Pager* pager = table->pager;
+  uint32_t num_full_pages = table->num_rows / ROWS_PER_PAGE;
+
+  for (uint32_t i = 0; i < num_full_pages; i++) {
+     if (pager->pages[i] == NULL) {
+         continue;
+     }
+     pager_flush(pager, i, PAGE_SIZE);
+     free(pager->pages[i]);
+     pager->pages[i] = NULL;
+  }
+
+  // There may be a partial page to write to the end of the file
+  // This should not be needed after we switch to a B-tree
+  uint32_t num_additional_rows = table->num_rows % ROWS_PER_PAGE;
+  if (num_additional_rows > 0) {
+     uint32_t page_num = num_full_pages;
+     if (pager->pages[page_num] != NULL) {
+         pager_flush(pager, page_num, num_additional_rows * ROW_SIZE);
+         free(pager->pages[page_num]);
+         pager->pages[page_num] = NULL;
+     }
+  }
+
+  int result = close(pager->file_descriptor);
+  if (result == -1) {
+     printf("Error closing db file.\n");
+     exit(EXIT_FAILURE);
+  }
+  for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
+     void* page = pager->pages[i];
+     if (page) {
+         free(page);
+         pager->pages[i] = NULL;
+     }
+  }
+
+  free(pager);
+  free(table);
+}
+
 MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table *table) {
   if (strcmp(input_buffer->buffer, ".exit") == 0) {
     close_input_buffer(input_buffer);
-    free_table(table);
+    db_close(table);
     exit(EXIT_SUCCESS);
   } else {
     return META_COMMAND_UNRECOGNIZED_COMMAND;
@@ -182,6 +308,7 @@ PrepareResult prepare_insert(InputBuffer* input_buffer, Statement* statement) {
     return PREPARE_SUCCESS;

 }
+
 PrepareResult prepare_statement(InputBuffer* input_buffer,
                                 Statement* statement) {
   if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
@@ -227,7 +354,14 @@ ExecuteResult execute_statement(Statement* statement, Table *table) {
 }

 int main(int argc, char* argv[]) {
-  Table* table = new_table();
+  if (argc < 2) {
+      printf("Must supply a database filename.\n");
+      exit(EXIT_FAILURE);
+  }
+
+  char* filename = argv[1];
+  Table* table = db_open(filename);
+
   InputBuffer* input_buffer = new_input_buffer();
   while (true) {
     print_prompt();
```

测试的 diff：
```diff
 describe 'database' do
+  before do
+    `rm -rf test.db`
+  end
+
   def run_script(commands)
     raw_output = nil
-    IO.popen("./db", "r+") do |pipe|
+    IO.popen("./db test.db", "r+") do |pipe|
       commands.each do |command|
         pipe.puts command
       end
@@ -28,6 +32,27 @@ describe 'database' do
     ])
   end

+  it 'keeps data after closing connection' do
+    result1 = run_script([
+      "insert 1 user1 person1@example.com",
+      ".exit",
+    ])
+    expect(result1).to match_array([
+      "db > Executed.",
+      "db > ",
+    ])
+
+    result2 = run_script([
+      "select",
+      ".exit",
+    ])
+    expect(result2).to match_array([
+      "db > (1, user1, person1@example.com)",
+      "Executed.",
+      "db > ",
+    ])
+  end
+
   it 'prints error message when table is full' do
     script = (1..1401).map do |i|
       "insert #{i} user#{i} person#{i}@example.com"
```
