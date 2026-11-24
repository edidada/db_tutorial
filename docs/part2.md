---
title: 第 2 部分 - 世界上最简陋的 SQL 编译器和虚拟机
date: 2017-08-31
---

我们要做一个 SQLite 的仿制品。SQLite 的“前端”是一个 SQL 编译器，它解析字符串并输出一种叫做字节码（bytecode）的内部表示。

这些字节码被传给虚拟机，由虚拟机执行。

{% include image.html url="assets/images/arch2.gif" description="SQLite 架构（https://www.sqlite.org/arch.html）" %}

像这样把事情拆成两个步骤有几个好处：
- 降低了每个部分的复杂度（例如虚拟机不需要操心语法错误）
- 可以把常见查询只编译一次并缓存字节码，从而提升性能

基于这个思路，让我们重构 `main` 函数，并在此过程中支持两个新的关键字：

```diff
 int main(int argc, char* argv[]) {
   InputBuffer* input_buffer = new_input_buffer();
   while (true) {
     print_prompt();
     read_input(input_buffer);

-    if (strcmp(input_buffer->buffer, ".exit") == 0) {
-      exit(EXIT_SUCCESS);
-    } else {
-      printf("Unrecognized command '%s'.\n", input_buffer->buffer);
+    if (input_buffer->buffer[0] == '.') {
+      switch (do_meta_command(input_buffer)) {
+        case (META_COMMAND_SUCCESS):
+          continue;
+        case (META_COMMAND_UNRECOGNIZED_COMMAND):
+          printf("Unrecognized command '%s'\n", input_buffer->buffer);
+          continue;
+      }
     }
+
+    Statement statement;
+    switch (prepare_statement(input_buffer, &statement)) {
+      case (PREPARE_SUCCESS):
+        break;
+      case (PREPARE_UNRECOGNIZED_STATEMENT):
+        printf("Unrecognized keyword at start of '%s'.\n",
+               input_buffer->buffer);
+        continue;
+    }
+
+    execute_statement(&statement);
+    printf("Executed.\n");
   }
 }
```

像 `.exit` 这样非 SQL 的语句被称为“元命令（meta-command）”。它们都以一个点开头，所以我们检查这种输入，并在一个单独的函数中处理它们。

接下来，我们增加一个步骤，把输入的这行内容转换成语句的内部表示。这就是我们简易版的 SQLite 前端。

最后，我们把准备好的语句传给 `execute_statement`。这个函数最终会演变成我们的虚拟机。

注意，我们新增的两个函数都会返回一个枚举值，用来表示成功还是失败：

```c
typedef enum {
  META_COMMAND_SUCCESS,
  META_COMMAND_UNRECOGNIZED_COMMAND
} MetaCommandResult;

typedef enum { PREPARE_SUCCESS, PREPARE_UNRECOGNIZED_STATEMENT } PrepareResult;
```

“无法识别的语句”？这看起来有点像异常。我不喜欢使用异常（而且 C 语言甚至不支持异常），所以只要可行，我就用枚举结果码。如果我的 switch 语句没有处理枚举中的某个成员，C 编译器就会报错，这样我们就能稍微更有信心地认为函数的所有返回结果都被处理到了。预计将来还会增加更多的结果码。

`do_meta_command` 只是对现有功能的一个包装，并为将来加入更多命令留出了空间：

```c
MetaCommandResult do_meta_command(InputBuffer* input_buffer) {
  if (strcmp(input_buffer->buffer, ".exit") == 0) {
    exit(EXIT_SUCCESS);
  } else {
    return META_COMMAND_UNRECOGNIZED_COMMAND;
  }
}
```

我们目前的“预编译语句”只包含一个有两个取值的枚举。随着我们允许语句中带参数，它会包含更多数据：

```c
typedef enum { STATEMENT_INSERT, STATEMENT_SELECT } StatementType;

typedef struct {
  StatementType type;
} Statement;
```

`prepare_statement`（我们的“SQL 编译器”）目前还不懂 SQL。事实上，它只认识两个单词：
```c
PrepareResult prepare_statement(InputBuffer* input_buffer,
                                Statement* statement) {
  if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
    statement->type = STATEMENT_INSERT;
    return PREPARE_SUCCESS;
  }
  if (strcmp(input_buffer->buffer, "select") == 0) {
    statement->type = STATEMENT_SELECT;
    return PREPARE_SUCCESS;
  }

  return PREPARE_UNRECOGNIZED_STATEMENT;
}
```

注意，对于 "insert" 我们使用的是 `strncmp`，因为 "insert" 关键字后面会跟着数据。（例如 `insert 1 cstack foo@bar.com`）

最后，`execute_statement` 里有几个桩函数（stub）：
```c
void execute_statement(Statement* statement) {
  switch (statement->type) {
    case (STATEMENT_INSERT):
      printf("This is where we would do an insert.\n");
      break;
    case (STATEMENT_SELECT):
      printf("This is where we would do a select.\n");
      break;
  }
}
```

注意，它不返回任何错误码，因为目前还没有任何可能出错的地方。

经过这些重构，我们现在能识别两个新的关键字了！
```command-line
~ ./db
db > insert foo bar
This is where we would do an insert.
Executed.
db > delete foo
Unrecognized keyword at start of 'delete foo'.
db > select
This is where we would do a select.
Executed.
db > .tables
Unrecognized command '.tables'
db > .exit
~
```

我们数据库的骨架正在成形……如果它能存储数据岂不是很棒？在下一部分中，我们将实现 `insert` 和 `select`，打造出世界上最差的数据存储。与此同时，下面是这一部分的完整 diff：

```diff
@@ -10,6 +10,23 @@ struct InputBuffer_t {
 } InputBuffer;
 
+typedef enum {
+  META_COMMAND_SUCCESS,
+  META_COMMAND_UNRECOGNIZED_COMMAND
+} MetaCommandResult;
+
+typedef enum { PREPARE_SUCCESS, PREPARE_UNRECOGNIZED_STATEMENT } PrepareResult;
+
+typedef enum { STATEMENT_INSERT, STATEMENT_SELECT } StatementType;
+
+typedef struct {
+  StatementType type;
+} Statement;
+
 InputBuffer* new_input_buffer() {
   InputBuffer* input_buffer = malloc(sizeof(InputBuffer));
   input_buffer->buffer = NULL;
@@ -40,17 +57,67 @@ void close_input_buffer(InputBuffer* input_buffer) {
     free(input_buffer);
 }
 
+MetaCommandResult do_meta_command(InputBuffer* input_buffer) {
+  if (strcmp(input_buffer->buffer, ".exit") == 0) {
+    close_input_buffer(input_buffer);
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
+void execute_statement(Statement* statement) {
+  switch (statement->type) {
+    case (STATEMENT_INSERT):
+      printf("This is where we would do an insert.\n");
+      break;
+    case (STATEMENT_SELECT):
+      printf("This is where we would do a select.\n");
+      break;
+  }
+}
+
 int main(int argc, char* argv[]) {
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
+      switch (do_meta_command(input_buffer)) {
+        case (META_COMMAND_SUCCESS):
+          continue;
+        case (META_COMMAND_UNRECOGNIZED_COMMAND):
+          printf("Unrecognized command '%s'\n", input_buffer->buffer);
+          continue;
+      }
     }
+
+    Statement statement;
+    switch (prepare_statement(input_buffer, &statement)) {
+      case (PREPARE_SUCCESS):
+        break;
+      case (PREPARE_UNRECOGNIZED_STATEMENT):
+        printf("Unrecognized keyword at start of '%s'.\n",
+               input_buffer->buffer);
+        continue;
+    }
+
+    execute_statement(&statement);
+    printf("Executed.\n");
   }
 }
```
