---
title: 第 1 部分 - 简介与搭建 REPL
date: 2017-08-30
---

作为一名 Web 开发者，我每天都在工作中使用关系型数据库，但对我来说它们就像一个黑盒。我有一些疑问：
- 数据是以什么格式保存的？（在内存中和在磁盘上）
- 数据什么时候从内存移到磁盘？
- 为什么每张表只能有一个主键？
- 事务回滚是如何工作的？
- 索引是以什么格式保存的？
- 全表扫描在什么时候、以什么方式发生？
- 预编译语句（prepared statement）是以什么格式保存的？

换句话说，数据库到底是**如何**工作的？

为了弄明白这些，我从零开始写一个数据库。它以 SQLite 为原型，因为 SQLite 的设计目标是小巧、特性比 MySQL 或 PostgreSQL 少，所以我更有希望理解它。整个数据库都存储在单个文件中！

# SQLite

SQLite 官网有很多[关于 SQLite 内部机制的文档](https://www.sqlite.org/arch.html)，另外我还有一本[《SQLite Database System: Design and Implementation》](https://play.google.com/store/books/details?id=9Z6IQQnX1JEC)的副本。

{% include image.html url="assets/images/arch1.gif" description="sqlite 架构（https://www.sqlite.org/zipvfs/doc/trunk/www/howitworks.wiki）" %}

一条查询会依次经过一系列组件来读取或修改数据。**前端（front-end）**由以下部分组成：
- 分词器（tokenizer）
- 解析器（parser）
- 代码生成器（code generator）

前端的输入是一条 SQL 查询，输出是 SQLite 虚拟机字节码（本质上是一个可以对数据库进行操作的已编译程序）。

_后端（back-end）_由以下部分组成：
- 虚拟机（virtual machine）
- B 树（B-tree）
- 分页器（pager）
- 操作系统接口（os interface）

**虚拟机**把前端生成的字节码当作指令来执行。随后它可以对一张或多张表或索引执行操作，而这些表或索引都存储在一种叫做 B 树的数据结构中。虚拟机本质上就是一个针对字节码指令类型的大 switch 语句。

每个 **B 树**由许多节点组成，每个节点的长度恰好是一页。B 树通过向分页器发出命令，从磁盘读取一个页面或把页面写回磁盘。

**分页器**接收读写数据页的命令。它负责在数据库文件中适当的偏移量处进行读写。它还在内存中维护一个最近访问页面的缓存，并决定这些页面何时需要写回磁盘。

**操作系统接口**是因 SQLite 编译时目标操作系统不同而有所差异的那一层。在本教程中，我不打算支持多个平台。

[千里之行，始于足下](https://en.wiktionary.org/wiki/a_journey_of_a_thousand_miles_begins_with_a_single_step)，所以让我们从更简单一点的东西开始：REPL（读取-执行-打印循环）。

## 制作一个简单的 REPL

当你从命令行启动 SQLite 时，它会开启一个读取-执行-打印循环：

```shell
~ sqlite3
SQLite version 3.16.0 2016-11-04 19:09:39
Enter ".help" for usage hints.
Connected to a transient in-memory database.
Use ".open FILENAME" to reopen on a persistent database.
sqlite> create table users (id int, username varchar(255), email varchar(255));
sqlite> .tables
users
sqlite> .exit
~
```

为了实现这一点，我们的 main 函数会有一个无限循环：打印提示符、读取一行输入，然后处理这行输入：

```c
int main(int argc, char* argv[]) {
  InputBuffer* input_buffer = new_input_buffer();
  while (true) {
    print_prompt();
    read_input(input_buffer);

    if (strcmp(input_buffer->buffer, ".exit") == 0) {
      close_input_buffer(input_buffer);
      exit(EXIT_SUCCESS);
    } else {
      printf("Unrecognized command '%s'.\n", input_buffer->buffer);
    }
  }
}
```

我们会把 `InputBuffer` 定义为一个小的包装结构，用来保存与 [getline()](http://man7.org/linux/man-pages/man3/getline.3.html) 交互所需的状态。（稍后会详细说明）

```c
typedef struct {
  char* buffer;
  size_t buffer_length;
  ssize_t input_length;
} InputBuffer;

InputBuffer* new_input_buffer() {
  InputBuffer* input_buffer = (InputBuffer*)malloc(sizeof(InputBuffer));
  input_buffer->buffer = NULL;
  input_buffer->buffer_length = 0;
  input_buffer->input_length = 0;

  return input_buffer;
}
```

接下来，`print_prompt()` 向用户打印一个提示符。我们在读取每一行输入之前执行它。

```c
void print_prompt() { printf("db > "); }
```

要读取一行输入，使用 [getline()](http://man7.org/linux/man-pages/man3/getline.3.html)：
```c
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
```
`lineptr`：指向某个变量的指针，该变量用来指向保存所读取内容的缓冲区。如果它被设为 `NULL`，`getline` 会为它分配内存，因此即使命令执行失败，也应该由使用者来释放它。

`n`：指向某个变量的指针，该变量用来保存已分配缓冲区的大小。

`stream`：要读取的输入流。我们会从标准输入读取。

`return value`：读取的字节数，它可能小于缓冲区的大小。

我们让 `getline` 把读取的行存入 `input_buffer->buffer`，把已分配缓冲区的大小存入 `input_buffer->buffer_length`，并把返回值存入 `input_buffer->input_length`。

`buffer` 最初是 null，因此 `getline` 会分配足够的内存来容纳这行输入，并让 `buffer` 指向它。

```c
void read_input(InputBuffer* input_buffer) {
  ssize_t bytes_read =
      getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);

  if (bytes_read <= 0) {
    printf("Error reading input\n");
    exit(EXIT_FAILURE);
  }

  // Ignore trailing newline
  input_buffer->input_length = bytes_read - 1;
  input_buffer->buffer[bytes_read - 1] = 0;
}
```

现在，恰当地定义一个函数，用来释放为 `InputBuffer *` 实例分配的内存，以及相应结构体中 `buffer` 成员所占的内存（`getline` 在 `read_input` 中为 `input_buffer->buffer` 分配了内存）。

```c
void close_input_buffer(InputBuffer* input_buffer) {
    free(input_buffer->buffer);
    free(input_buffer);
}
```

最后，我们解析并执行命令。目前只有一个可识别的命令：`.exit`，它用于终止程序。其他情况我们会打印一条错误信息并继续循环。

```c
if (strcmp(input_buffer->buffer, ".exit") == 0) {
  close_input_buffer(input_buffer);
  exit(EXIT_SUCCESS);
} else {
  printf("Unrecognized command '%s'.\n", input_buffer->buffer);
}
```

我们来试一下！
```shell
~ ./db
db > .tables
Unrecognized command '.tables'.
db > .exit
~
```

好了，我们有了一个可以工作的 REPL。在下一部分中，我们将开始开发自己的命令语言。与此同时，下面是这一部分完整的程序：

```c
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char* buffer;
  size_t buffer_length;
  ssize_t input_length;
} InputBuffer;

InputBuffer* new_input_buffer() {
  InputBuffer* input_buffer = malloc(sizeof(InputBuffer));
  input_buffer->buffer = NULL;
  input_buffer->buffer_length = 0;
  input_buffer->input_length = 0;

  return input_buffer;
}

void print_prompt() { printf("db > "); }

void read_input(InputBuffer* input_buffer) {
  ssize_t bytes_read =
      getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);

  if (bytes_read <= 0) {
    printf("Error reading input\n");
    exit(EXIT_FAILURE);
  }

  // Ignore trailing newline
  input_buffer->input_length = bytes_read - 1;
  input_buffer->buffer[bytes_read - 1] = 0;
}

void close_input_buffer(InputBuffer* input_buffer) {
    free(input_buffer->buffer);
    free(input_buffer);
}

int main(int argc, char* argv[]) {
  InputBuffer* input_buffer = new_input_buffer();
  while (true) {
    print_prompt();
    read_input(input_buffer);

    if (strcmp(input_buffer->buffer, ".exit") == 0) {
      close_input_buffer(input_buffer);
      exit(EXIT_SUCCESS);
    } else {
      printf("Unrecognized command '%s'.\n", input_buffer->buffer);
    }
  }
}
```
