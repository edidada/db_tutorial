# db_tutorial 项目架构（按源码模块）

这个项目用 C 语言从零实现了一个受 SQLite 启发的小型数据库。原本所有代码都在单个 `db.c` 中，现已按职责拆分到 `src/` 目录下的一组源文件。本文按照实际的源文件介绍整体架构。

## 一、分层总览

代码自底向上分为六层，每一层只依赖它下面的层：

```
┌─────────────────────────────────────────────┐
│  main.c         程序入口、主循环             │
├─────────────────────────────────────────────┤
│  repl.c         元命令与输入输出             │
│  compiler.c     SQL 前端（解析语句）         │
│  executor.c     虚拟机（执行语句）           │
├─────────────────────────────────────────────┤
│  btree.c        B 树插入、拆分、建根          │
│  table.c        表、游标、查找入口            │
├─────────────────────────────────────────────┤
│  node.c         节点格式与底层操作           │
│  row.c          行记录序列化                 │
├─────────────────────────────────────────────┤
│  pager.c        页缓存与磁盘读写             │
├─────────────────────────────────────────────┤
│  common.c/.h    公共类型与常量               │
└─────────────────────────────────────────────┘
```

模块依赖方向（箭头指向被依赖方）：

```
main → repl → table ─→ node → pager → common
  │      │       │       └──→ row
  │      │       └──→ btree → node / pager / row
  ├──→ compiler → common
  └──→ executor → btree / table / node / row
```

## 二、源文件逐一说明

### `common.h` / `common.c` — 公共定义层

所有模块共享的基础定义都在这里：

- **数据结构**：`InputBuffer`（输入缓冲区）、`Row`（一行记录）、`Statement`（一条语句）、`Pager`（分页器）、`Table`（表）、`Cursor`（游标）
- **枚举**：`ExecuteResult`、`MetaCommandResult`、`PrepareResult`、`StatementType`、`NodeType`
- **常量**：页大小 `PAGE_SIZE = 4096`、最大页数 `TABLE_MAX_PAGES = 400`、无效页号 `INVALID_PAGE_NUM`
- **布局常量**：行的字段大小/偏移、公共节点头部、内部节点与叶子节点的头部及单元格布局

这些常量在 `common.h` 中以 `extern const` 声明，在 `common.c` 中定义，保证整个程序只有一份实例。

### `pager.h` / `pager.c` — 存储层（磁盘 I/O）

分页器是数据库与磁盘文件之间的抽象层：

- `pager_open()`：以读写方式打开（或创建）数据库文件，记录文件长度和页数，并初始化页缓存
- `get_page()`：按页号取页；缓存命中直接返回，未命中则从文件读取一页；若页号超出文件范围则返回全零的新页
- `pager_flush()`：把指定页写回文件对应偏移处
- `get_unused_page_num()`：分配新页。目前简单地返回当前页数，即新页总是追加到文件末尾

这一层屏蔽了 `lseek/read/write` 等系统调用，上层只需和“页”打交道。

### `row.h` / `row.c` — 记录层

负责一行记录与页内字节之间的转换：

- `serialize_row()`：把 `Row` 结构体按固定偏移（`ID_SIZE`、`USERNAME_SIZE`、`EMAIL_SIZE`）拷入目标内存
- `deserialize_row()`：从内存还原出 `Row`
- `print_row()`：按 `(id, username, email)` 格式打印

序列化布局是：id 占 4 字节，用户名占 32 字节（外加 1 字节结尾空字符），邮箱占 255 字节（外加 1 字节），合计 `ROW_SIZE = 293` 字节。

### `node.h` / `node.c` — 节点层

定义 B 树节点在页内的二进制格式与底层操作：

- **公共头部**：节点类型（内部/叶子）、是否为根、父节点页号
- **内部节点**：键数量、最右子指针，以及 `internal_node_child()` / `internal_node_key()` 等访问器
- **叶子节点**：单元格数量、右兄弟页号（`next_leaf`，用于范围扫描），以及 `leaf_node_key()` / `leaf_node_value()` 等访问器
- `initialize_leaf_node()` / `initialize_internal_node()`：初始化节点头部
- `get_node_max_key()`：取节点最大键；对内部节点会沿最右子节点一路下探到叶子
- `internal_node_find_child()`：在内部节点中二分查找应进入的子节点索引
- `print_constants()` / `print_tree()`：调试用的 `.constants` 和 `.btree` 元命令实现

### `table.h` / `table.c` — 表层与游标

管理数据库的打开/关闭以及“位置”的抽象：

- `db_open()` / `db_close()`：打开文件时如果为空，把第 0 页初始化为叶子节点并设为根；关闭时把所有缓存页刷回磁盘并释放资源
- `table_find()`：从根开始查找键，按节点类型分派到叶子或内部节点查找
- `table_start()` / `table_find()` 的辅助函数 `leaf_node_find()`、`internal_node_find()`：递归二分查找，返回指向目标位置的游标
- `cursor_value()` / `cursor_advance()`：读取游标所指的行；推进游标时若越过当前叶子的末尾，就通过 `next_leaf` 跳到右侧兄弟叶子，实现全表扫描

游标把“树中的某个位置”封装起来，使上下层不必关心页内布局细节。

### `btree.h` / `btree.c` — B 树变更层

负责所有会改变树结构的操作，是算法最密集的部分：

- `create_new_root()`：根节点拆分时，把旧根内容复制到新页作为左子节点，原根页重新初始化为只有一个键、两个子节点的内部节点
- `leaf_node_insert()` / `leaf_node_split_and_insert()`：向叶子插入键值对；节点满时把 `LEAF_NODE_MAX_CELLS + 1` 个单元格平均分到两个叶子，更新兄弟指针与父节点
- `internal_node_insert()` / `internal_node_split_and_insert()`：向内部节点插入子指针/键对；超过 `INTERNAL_NODE_MAX_KEYS`（为测试设为 3）时拆分内部节点，可能递归导致树增高
- `update_internal_node_key()`：子节点拆分后，更新它在父节点中对应的最大键

B 树保持平衡：所有叶子位于同一深度，插入通过拆分而非移动大量行完成，查找复杂度为 O(log n)。

### `compiler.h` / `compiler.c` — SQL 前端

把用户输入解析成 `Statement`：

- `prepare_statement()`：识别 `insert` 和 `select` 两种关键字
- `prepare_insert()`（内部静态函数）：用 `strtok` 按空格切分参数，校验 id 是否为负、字符串是否超长，然后填入 `row_to_insert`

这一层不做任何存储操作，只负责语法与基本校验，并通过 `PrepareResult` 枚举返回错误。

### `executor.h` / `executor.c` — 执行层（虚拟机）

执行已解析的语句：

- `execute_statement()`：按 `StatementType` 分派
- `execute_insert()`：用 `table_find()` 定位插入点，检查重复键，调用 `leaf_node_insert()` 写入
- `execute_select()`：用 `table_start()` 得到起始游标，循环 `cursor_value()` + `cursor_advance()` 遍历所有行并打印

对应教程里的“虚拟机”概念：接收前端产出的内部表示并执行。

### `repl.h` / `repl.c` — 交互层

- `new_input_buffer()` / `read_input()` / `close_input_buffer()` / `print_prompt()`：基于 `getline()` 的输入输出管理
- `do_meta_command()`：处理以 `.` 开头的元命令——`.exit` 关闭数据库并退出、`.btree` 打印树结构、`.constants` 打印布局常量

### `main.c` — 程序入口

负责整体流程：

1. 检查命令行参数（必须提供数据库文件名）
2. `db_open()` 打开数据库
3. 进入无限循环：打印提示符 → 读输入 → 若是 `.` 开头交给元命令处理 → 否则 `prepare_statement()` 解析 → `execute_statement()` 执行 → 打印执行结果或错误信息

## 三、关键数据流

**启动**：`main` → `db_open` → `pager_open`（打开/校验文件）→ 若为空则 `initialize_leaf_node`（建根）→ 进入 REPL 循环。

**INSERT**：输入行 → `prepare_statement` → `prepare_insert`（校验并填充 Row）→ `execute_insert` → `table_find`（二分定位）→ 检查重复键 → `leaf_node_insert`；若节点满则 `leaf_node_split_and_insert` → `create_new_root` 或 `internal_node_insert`，可能继续拆分内部节点 → 修改都发生在页缓存中，直到 `.exit` 时 `db_close` → `pager_flush` 落盘。

**SELECT**：`execute_select` → `table_start`（查找键 0，得到最左叶子起点）→ 循环 `cursor_value`（反序列化并打印）与 `cursor_advance`（必要时通过 `next_leaf` 跳到下一个叶子）直到 `end_of_table`。

**持久化**：所有读写都以 4096 字节的页为单位；页缓存由 `Pager` 维护，仅在 `db_close()` 时统一刷回文件，文件长度即数据库的逻辑大小。

## 四、构建方式

- **CMake**：`cmake -S . -B build && cmake --build build`，通过 `file(GLOB ...)` 收集 `src/*.c`，头文件搜索路径为 `src/`
- **Make**：`make`，使用 `$(wildcard src/*.c)` 编译，等价于 `gcc src/*.c -o db -Isrc`
- **测试**：`bundle exec rspec`，13 个用例覆盖插入查询、持久化、字符串长度、重复键、B 树结构与常量打印
