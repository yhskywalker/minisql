# MiniSQL 课程设计

本仓库完成 MiniSQL C++ 内核课程设计，包含存储管理、缓冲池、记录管理、B+ 树索引、Catalog、SQL 执行，以及 Clock Replacer、LockManager 和 RecoveryManager bonus。

详细设计文档见 [doc/detailed-design.md](doc/detailed-design.md)。

## 构建

### Linux / WSL

```bash
mkdir build
cd build
cmake .. -DCMAKE_POLICY_VERSION_MINIMUM=3.5
make -j
```

> 在较新的 CMake 环境中，内置第三方 GoogleTest / glog 需要上述兼容参数。

## 测试

```bash
./test/minisql_test
```

最近一次回归结果：**34 个测试、12 个测试套件全部通过**。

单独运行 Clock Replacer 专项测试：

```bash
./test/minisql_test --gtest_filter='CLOCKReplacerTest.*'
```

## 运行主程序

```bash
./bin/main
```

## 功能范围

- DiskManager：页分配、释放、读写和 bitmap 管理。
- BufferPoolManager：页缓存、pin/unpin、脏页刷盘和默认 LRU 替换。
- Clock Replacer Bonus：额外实现 `CLOCKReplacer`，覆盖基本淘汰、二次机会、Pin 移除候选页和容量限制测试；当前 BufferPoolManager 默认策略仍为 LRU。
- Record Manager：Field、Column、Schema、Row、TablePage、TableHeap 和迭代器。
- Index Manager：GenericKey、B+ 树、叶子页/内部页、范围迭代器和 B+TreeIndex。
- Catalog：表和索引元数据持久化，支持重启加载。
- Executor：支持数据库/表/索引命令，支持 `select`、`insert`、`delete`、`update`。
- Bonus：TableHeap 的 `last_page_id_` 插入加速；记录级 S/X 锁、锁升级、2PL、死锁检测；简化日志恢复 redo/undo。

## Git 时间线

以下为关键提交，完整历史以本地 `git log` 为准：

```text
880d62e (2026-06-11) chore: import MiniSQL course framework
951097a (2026-06-11) chore: support Windows MinGW build
4c4f07d (2026-06-11) feat: implement disk and buffer managers
6defc20 (2026-06-11) feat: implement record manager
78d77a5 (2026-06-11) feat: implement b plus tree index
09bd73a (2026-06-11) feat: implement catalog metadata management
dff095c (2026-06-12) feat: implement SQL command execution
11c702e (2026-06-12) feat: implement lock manager bonus
c1cbb14 (2026-06-12) feat: implement recovery manager bonus
137ff8f (2026-06-12) docs: add course design documentation

c17862a (2026-06-08) initial — 项目初始化
b382bb1 (2026-06-08) 完善 row.cpp
a3c777e (2026-06-09) 完善 column.cpp
80c1064 (2026-06-09) 完善 schema.cpp
8e1d437 (2026-06-13) 完成 TableHeap, TableIterator 功能
2afc5dc (2026-06-13) index 初始化
3c658ba (2026-06-13) #4 catalog manager
1e220af (2026-06-13) #6 recovery manager

# 合并与修复：
432982a (2026-06-14) merge: combine 全部模块
aac2752 (2026-06-14) fix bug: row.cpp; table_iterator.cpp
a51fd29 (2026-06-15) bonus: add last_page_id to accelerate row insertion
25ee617 (2026-06-25) feat(buffer): add Clock replacer and tests
9528f71 (2026-06-25) merge: merge Clock replacer bonus into main
```

查看完整提交历史：

```powershell
git log --oneline --reverse
```

## 说明

- B+ 树删除支持公共测试场景，但没有完整实现内部节点低水位后的合并/重分配。
- BufferPoolManager 当前默认仍使用 LRU；Clock Replacer 是独立 bonus 实现并通过专项测试，不改变原有 BufferPool 主流程。
- SQL 层事务命令已接收；LockManager 和 RecoveryManager bonus 通过独立测试验证，尚未深度接入每条 SQL DML。
- 运行产生的 `databases/`、`*.db`、`syntax_tree_*.txt`、`tree_*.txt` 已被 `.gitignore` 排除。
