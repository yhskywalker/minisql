# MiniSQL 课程设计

本仓库完成 MiniSQL C++ 内核课程设计，包含存储管理、缓冲池、记录管理、B+ 树索引、Catalog、SQL 执行，以及 LockManager 和 RecoveryManager bonus。

详细设计文档见 [doc/detailed-design.md](doc/detailed-design.md)。

## 构建

Windows MinGW / PowerShell：

```powershell
cmake -S . -B build-native -G "MinGW Makefiles"
cmake --build build-native -j 4
```

运行主程序：

```powershell
$env:PATH="$PWD\build-native\bin;$PWD\build-native\glog-build;$PWD\build-native\googletest-build;$env:PATH"
.\build-native\bin\main.exe
```

## 测试

建议顺序运行测试，避免多个测试同时访问运行期 `.db` 文件：

```powershell
$env:PATH="$PWD\build-native\bin;$PWD\build-native\glog-build;$PWD\build-native\googletest-build;$env:PATH"

.\build-native\test\disk_manager_test.exe
.\build-native\test\lru_replacer_test.exe
.\build-native\test\buffer_pool_manager_test.exe
.\build-native\test\tuple_test.exe
.\build-native\test\table_heap_test.exe
.\build-native\test\b_plus_tree_test.exe
.\build-native\test\index_iterator_test.exe
.\build-native\test\b_plus_tree_index_test.exe
.\build-native\test\catalog_test.exe
.\build-native\test\executor_test.exe
.\build-native\test\lock_manager_test.exe
.\build-native\test\recovery_manager_test.exe
```

最近一次回归结果：上述 12 个测试目标全部通过。

## 功能范围

- DiskManager：页分配、释放、读写和 bitmap 管理。
- BufferPoolManager：页缓存、pin/unpin、脏页刷盘和 LRU 替换。
- Record Manager：Field、Column、Schema、Row、TablePage、TableHeap 和迭代器。
- Index Manager：GenericKey、B+ 树、叶子页/内部页、范围迭代器和 B+TreeIndex。
- Catalog：表和索引元数据持久化，支持重启加载。
- Executor：支持数据库/表/索引命令，支持 `select`、`insert`、`delete`、`update`。
- Bonus：记录级 S/X 锁、锁升级、2PL、死锁检测；简化日志恢复 redo/undo。

## Git 时间线

主要实现提交：

```text
880d62e chore: import MiniSQL course framework
951097a chore: support Windows MinGW build
4c4f07d feat: implement disk and buffer managers
6defc20 feat: implement record manager
78d77a5 feat: implement b plus tree index
09bd73a feat: implement catalog metadata management
dff095c feat: implement SQL command execution
11c702e feat: implement lock manager bonus
c1cbb14 feat: implement recovery manager bonus
```

查看完整提交历史，以本地 Git log 为准：

```powershell
git log --oneline --reverse
```

## 说明

- B+ 树删除支持公共测试场景，但没有完整实现内部节点低水位后的合并/重分配。
- SQL 层事务命令已接收；LockManager 和 RecoveryManager bonus 通过独立测试验证，尚未深度接入每条 SQL DML。
- 运行产生的 `databases/`、`*.db`、`syntax_tree_*.txt`、`tree_*.txt` 已被 `.gitignore` 排除。
