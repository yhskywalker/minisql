#include "executor/execute_engine.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#ifdef _WIN32
#include <direct.h>
#endif

#include "common/result_writer.h"
#include "executor/executors/delete_executor.h"
#include "executor/executors/index_scan_executor.h"
#include "executor/executors/insert_executor.h"
#include "executor/executors/seq_scan_executor.h"
#include "executor/executors/update_executor.h"
#include "executor/executors/values_executor.h"
#include "glog/logging.h"
#include "planner/planner.h"
#include "utils/utils.h"

extern "C" {
int yyparse(void);
#include "parser/minisql_lex.h"
#include "parser/parser.h"
}

namespace {

constexpr uint32_t CATALOG_METADATA_MAGIC_NUM = 89849;

bool NeedCurrentDatabase(const std::string &current_db) {
  if (current_db.empty()) {
    std::cout << "No database selected" << std::endl;
    return true;
  }
  return false;
}

std::string NodeValue(pSyntaxNode node) {
  return node != nullptr && node->val_ != nullptr ? node->val_ : "";
}

std::vector<std::string> ParseColumnList(pSyntaxNode node) {
  std::vector<std::string> columns;
  if (node == nullptr) {
    return columns;
  }
  auto child = node->type_ == kNodeColumnList ? node->child_ : node;
  while (child != nullptr) {
    if (child->type_ == kNodeIdentifier) {
      columns.emplace_back(NodeValue(child));
    }
    child = child->next_;
  }
  return columns;
}

TypeId ParseColumnType(pSyntaxNode type_node, uint32_t &length) {
  auto type_name = NodeValue(type_node);
  if (type_name == "int") {
    length = sizeof(int32_t);
    return TypeId::kTypeInt;
  }
  if (type_name == "float") {
    length = sizeof(float);
    return TypeId::kTypeFloat;
  }
  if (type_name == "char") {
    length = static_cast<uint32_t>(std::stoi(NodeValue(type_node->child_)));
    return TypeId::kTypeChar;
  }
  throw std::logic_error("Unsupported column type.");
}

void PrintSingleColumnTable(const std::string &header, const std::vector<std::string> &values) {
  if (values.empty()) {
    std::cout << "Empty set (0.00 sec)" << std::endl;
    return;
  }
  size_t max_width = header.length();
  for (const auto &value : values) {
    max_width = std::max(max_width, value.length());
  }
  std::cout << "+" << std::setfill('-') << std::setw(static_cast<int>(max_width + 2)) << "" << "+" << std::endl;
  std::cout << "| " << std::left << std::setfill(' ') << std::setw(static_cast<int>(max_width)) << header << " |"
            << std::endl;
  std::cout << "+" << std::setfill('-') << std::setw(static_cast<int>(max_width + 2)) << "" << "+" << std::endl;
  for (const auto &value : values) {
    std::cout << "| " << std::left << std::setfill(' ') << std::setw(static_cast<int>(max_width)) << value << " |"
              << std::endl;
  }
  std::cout << "+" << std::setfill('-') << std::setw(static_cast<int>(max_width + 2)) << "" << "+" << std::endl;
}

bool IsCatalogDatabaseFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }
  DiskFileMetaPage meta_page{};
  file.read(reinterpret_cast<char *>(&meta_page), sizeof(meta_page));
  if (file.gcount() < static_cast<std::streamsize>(sizeof(uint32_t) * 2)) {
    return false;
  }
  if (meta_page.num_extents_ == 0 || meta_page.extent_used_page_[0] < 2) {
    return false;
  }
  char catalog_page[PAGE_SIZE];
  file.clear();
  file.seekg(static_cast<std::streamoff>(2 * PAGE_SIZE));
  file.read(catalog_page, PAGE_SIZE);
  if (file.gcount() < static_cast<std::streamsize>(sizeof(uint32_t))) {
    return false;
  }
  return MACH_READ_UINT32(catalog_page) == CATALOG_METADATA_MAGIC_NUM;
}

}  // namespace

ExecuteEngine::ExecuteEngine() {
  char path[] = "./databases";
  DIR *dir;
  if ((dir = opendir(path)) == nullptr) {
#ifdef _WIN32
    _mkdir("./databases");
#else
    mkdir("./databases", 0777);
#endif
    dir = opendir(path);
  }
  struct dirent *stdir;
  while((stdir = readdir(dir)) != nullptr) {
    if( strcmp( stdir->d_name , "." ) == 0 ||
        strcmp( stdir->d_name , "..") == 0 ||
        stdir->d_name[0] == '.')
      continue;
    std::string db_name = stdir->d_name;
    std::string db_path = std::string(path) + "/" + db_name;
    if (!IsCatalogDatabaseFile(db_path)) {
      continue;
    }
    try {
      dbs_[db_name] = new DBStorageEngine(db_name, false);
    } catch (const std::exception &ex) {
      LOG(WARNING) << "Skip invalid database file " << db_name << ": " << ex.what() << std::endl;
    }
  }
  closedir(dir);
}

std::unique_ptr<AbstractExecutor> ExecuteEngine::CreateExecutor(ExecuteContext *exec_ctx,
                                                                const AbstractPlanNodeRef &plan) {
  switch (plan->GetType()) {
    // Create a new sequential scan executor
    case PlanType::SeqScan: {
      return std::make_unique<SeqScanExecutor>(exec_ctx, dynamic_cast<const SeqScanPlanNode *>(plan.get()));
    }
    // Create a new index scan executor
    case PlanType::IndexScan: {
      return std::make_unique<IndexScanExecutor>(exec_ctx, dynamic_cast<const IndexScanPlanNode *>(plan.get()));
    }
    // Create a new update executor
    case PlanType::Update: {
      auto update_plan = dynamic_cast<const UpdatePlanNode *>(plan.get());
      auto child_executor = CreateExecutor(exec_ctx, update_plan->GetChildPlan());
      return std::make_unique<UpdateExecutor>(exec_ctx, update_plan, std::move(child_executor));
    }
      // Create a new delete executor
    case PlanType::Delete: {
      auto delete_plan = dynamic_cast<const DeletePlanNode *>(plan.get());
      auto child_executor = CreateExecutor(exec_ctx, delete_plan->GetChildPlan());
      return std::make_unique<DeleteExecutor>(exec_ctx, delete_plan, std::move(child_executor));
    }
    case PlanType::Insert: {
      auto insert_plan = dynamic_cast<const InsertPlanNode *>(plan.get());
      auto child_executor = CreateExecutor(exec_ctx, insert_plan->GetChildPlan());
      return std::make_unique<InsertExecutor>(exec_ctx, insert_plan, std::move(child_executor));
    }
    case PlanType::Values: {
      return std::make_unique<ValuesExecutor>(exec_ctx, dynamic_cast<const ValuesPlanNode *>(plan.get()));
    }
    default:
      throw std::logic_error("Unsupported plan type.");
  }
}

dberr_t ExecuteEngine::ExecutePlan(const AbstractPlanNodeRef &plan, std::vector<Row> *result_set, Txn *txn,
                                   ExecuteContext *exec_ctx) {
  // Construct the executor for the abstract plan node
  auto executor = CreateExecutor(exec_ctx, plan);

  try {
    executor->Init();
    RowId rid{};
    Row row{};
    while (executor->Next(&row, &rid)) {
      if (result_set != nullptr) {
        result_set->push_back(row);
      }
    }
  } catch (const exception &ex) {
    std::cout << "Error Encountered in Executor Execution: " << ex.what() << std::endl;
    if (result_set != nullptr) {
      result_set->clear();
    }
    return DB_FAILED;
  }
  return DB_SUCCESS;
}

dberr_t ExecuteEngine::Execute(pSyntaxNode ast) {
  if (ast == nullptr) {
    return DB_FAILED;
  }
  auto start_time = std::chrono::system_clock::now();
  unique_ptr<ExecuteContext> context(nullptr);
  if (!current_db_.empty()) context = dbs_[current_db_]->MakeExecuteContext(nullptr);
  switch (ast->type_) {
    case kNodeCreateDB:
      return ExecuteCreateDatabase(ast, context.get());
    case kNodeDropDB:
      return ExecuteDropDatabase(ast, context.get());
    case kNodeShowDB:
      return ExecuteShowDatabases(ast, context.get());
    case kNodeUseDB:
      return ExecuteUseDatabase(ast, context.get());
    case kNodeShowTables:
      return ExecuteShowTables(ast, context.get());
    case kNodeCreateTable:
      return ExecuteCreateTable(ast, context.get());
    case kNodeDropTable:
      return ExecuteDropTable(ast, context.get());
    case kNodeShowIndexes:
      return ExecuteShowIndexes(ast, context.get());
    case kNodeCreateIndex:
      return ExecuteCreateIndex(ast, context.get());
    case kNodeDropIndex:
      return ExecuteDropIndex(ast, context.get());
    case kNodeTrxBegin:
      return ExecuteTrxBegin(ast, context.get());
    case kNodeTrxCommit:
      return ExecuteTrxCommit(ast, context.get());
    case kNodeTrxRollback:
      return ExecuteTrxRollback(ast, context.get());
    case kNodeExecFile:
      return ExecuteExecfile(ast, context.get());
    case kNodeQuit:
      return ExecuteQuit(ast, context.get());
    default:
      break;
  }
  // Plan the query.
  Planner planner(context.get());
  std::vector<Row> result_set{};
  try {
    planner.PlanQuery(ast);
    // Execute the query.
    ExecutePlan(planner.plan_, &result_set, nullptr, context.get());
  } catch (const exception &ex) {
    std::cout << "Error Encountered in Planner: " << ex.what() << std::endl;
    return DB_FAILED;
  }
  auto stop_time = std::chrono::system_clock::now();
  double duration_time =
      double((std::chrono::duration_cast<std::chrono::milliseconds>(stop_time - start_time)).count());
  // Return the result set as string.
  std::stringstream ss;
  ResultWriter writer(ss);

  if (planner.plan_->GetType() == PlanType::SeqScan || planner.plan_->GetType() == PlanType::IndexScan) {
    auto schema = planner.plan_->OutputSchema();
    auto num_of_columns = schema->GetColumnCount();
    if (!result_set.empty()) {
      // find the max width for each column
      vector<int> data_width(num_of_columns, 0);
      for (const auto &row : result_set) {
        for (uint32_t i = 0; i < num_of_columns; i++) {
          data_width[i] = max(data_width[i], int(row.GetField(i)->toString().size()));
        }
      }
      int k = 0;
      for (const auto &column : schema->GetColumns()) {
        data_width[k] = max(data_width[k], int(column->GetName().length()));
        k++;
      }
      // Generate header for the result set.
      writer.Divider(data_width);
      k = 0;
      writer.BeginRow();
      for (const auto &column : schema->GetColumns()) {
        writer.WriteHeaderCell(column->GetName(), data_width[k++]);
      }
      writer.EndRow();
      writer.Divider(data_width);

      // Transforming result set into strings.
      for (const auto &row : result_set) {
        writer.BeginRow();
        for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
          writer.WriteCell(row.GetField(i)->toString(), data_width[i]);
        }
        writer.EndRow();
      }
      writer.Divider(data_width);
    }
    writer.EndInformation(result_set.size(), duration_time, true);
  } else {
    writer.EndInformation(result_set.size(), duration_time, false);
  }
  std::cout << writer.stream_.rdbuf();
  // todo:: use shared_ptr for schema
  if (ast->type_ == kNodeSelect)
      delete planner.plan_->OutputSchema();
  return DB_SUCCESS;
}

void ExecuteEngine::ExecuteInformation(dberr_t result) {
  switch (result) {
    case DB_ALREADY_EXIST:
      cout << "Database already exists." << endl;
      break;
    case DB_NOT_EXIST:
      cout << "Database not exists." << endl;
      break;
    case DB_TABLE_ALREADY_EXIST:
      cout << "Table already exists." << endl;
      break;
    case DB_TABLE_NOT_EXIST:
      cout << "Table not exists." << endl;
      break;
    case DB_INDEX_ALREADY_EXIST:
      cout << "Index already exists." << endl;
      break;
    case DB_INDEX_NOT_FOUND:
      cout << "Index not exists." << endl;
      break;
    case DB_COLUMN_NAME_NOT_EXIST:
      cout << "Column not exists." << endl;
      break;
    case DB_KEY_NOT_FOUND:
      cout << "Key not exists." << endl;
      break;
    case DB_QUIT:
      cout << "Bye." << endl;
      break;
    default:
      break;
  }
}

dberr_t ExecuteEngine::ExecuteCreateDatabase(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteCreateDatabase" << std::endl;
#endif
  string db_name = ast->child_->val_;
  if (dbs_.find(db_name) != dbs_.end()) {
    return DB_ALREADY_EXIST;
  }
  dbs_.insert(make_pair(db_name, new DBStorageEngine(db_name, true)));
  return DB_SUCCESS;
}

dberr_t ExecuteEngine::ExecuteDropDatabase(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteDropDatabase" << std::endl;
#endif
  string db_name = ast->child_->val_;
  if (dbs_.find(db_name) == dbs_.end()) {
    return DB_NOT_EXIST;
  }
  remove(("./databases/" + db_name).c_str());
  delete dbs_[db_name];
  dbs_.erase(db_name);
  if (db_name == current_db_)
    current_db_ = "";
  return DB_SUCCESS;
}

dberr_t ExecuteEngine::ExecuteShowDatabases(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteShowDatabases" << std::endl;
#endif
  if (dbs_.empty()) {
    cout << "Empty set (0.00 sec)" << endl;
    return DB_SUCCESS;
  }
  int max_width = 8;
  for (const auto &itr : dbs_) {
    if (itr.first.length() > max_width) max_width = itr.first.length();
  }
  cout << "+" << setfill('-') << setw(max_width + 2) << ""
       << "+" << endl;
  cout << "| " << std::left << setfill(' ') << setw(max_width) << "Database"
       << " |" << endl;
  cout << "+" << setfill('-') << setw(max_width + 2) << ""
       << "+" << endl;
  for (const auto &itr : dbs_) {
    cout << "| " << std::left << setfill(' ') << setw(max_width) << itr.first << " |" << endl;
  }
  cout << "+" << setfill('-') << setw(max_width + 2) << ""
       << "+" << endl;
  return DB_SUCCESS;
}

dberr_t ExecuteEngine::ExecuteUseDatabase(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteUseDatabase" << std::endl;
#endif
  string db_name = ast->child_->val_;
  if (dbs_.find(db_name) != dbs_.end()) {
    current_db_ = db_name;
    cout << "Database changed" << endl;
    return DB_SUCCESS;
  }
  return DB_NOT_EXIST;
}

dberr_t ExecuteEngine::ExecuteShowTables(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteShowTables" << std::endl;
#endif
  if (current_db_.empty()) {
    cout << "No database selected" << endl;
    return DB_FAILED;
  }
  vector<TableInfo *> tables;
  if (dbs_[current_db_]->catalog_mgr_->GetTables(tables) == DB_FAILED) {
    cout << "Empty set (0.00 sec)" << endl;
    return DB_FAILED;
  }
  string table_in_db("Tables_in_" + current_db_);
  size_t max_width = table_in_db.length();
  for (const auto &itr : tables) {
    if (itr->GetTableName().length() > max_width) max_width = itr->GetTableName().length();
  }
  cout << "+" << setfill('-') << setw(max_width + 2) << ""
       << "+" << endl;
  cout << "| " << std::left << setfill(' ') << setw(max_width) << table_in_db << " |" << endl;
  cout << "+" << setfill('-') << setw(max_width + 2) << ""
       << "+" << endl;
  for (const auto &itr : tables) {
    cout << "| " << std::left << setfill(' ') << setw(max_width) << itr->GetTableName() << " |" << endl;
  }
  cout << "+" << setfill('-') << setw(max_width + 2) << ""
       << "+" << endl;
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t ExecuteEngine::ExecuteCreateTable(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteCreateTable" << std::endl;
#endif
  if (NeedCurrentDatabase(current_db_)) {
    return DB_FAILED;
  }
  auto table_name_node = ast->child_;
  auto definitions_node = table_name_node == nullptr ? nullptr : table_name_node->next_;
  if (table_name_node == nullptr || definitions_node == nullptr) {
    return DB_FAILED;
  }

  std::set<std::string> primary_keys;
  for (auto def = definitions_node->child_; def != nullptr; def = def->next_) {
    if (def->type_ == kNodeColumnList) {
      for (auto &key : ParseColumnList(def)) {
        primary_keys.insert(key);
      }
    }
  }

  std::vector<Column *> columns;
  std::vector<std::string> auto_index_columns;
  uint32_t index = 0;
  try {
    for (auto def = definitions_node->child_; def != nullptr; def = def->next_) {
      if (def->type_ != kNodeColumnDefinition) {
        continue;
      }
      auto name_node = def->child_;
      auto type_node = name_node == nullptr ? nullptr : name_node->next_;
      if (name_node == nullptr || type_node == nullptr) {
        throw std::logic_error("Invalid column definition.");
      }
      auto column_name = NodeValue(name_node);
      uint32_t length = 0;
      auto type = ParseColumnType(type_node, length);
      bool unique = def->val_ != nullptr || primary_keys.find(column_name) != primary_keys.end();
      if (type == TypeId::kTypeChar) {
        columns.emplace_back(new Column(column_name, type, length, index, false, unique));
      } else {
        columns.emplace_back(new Column(column_name, type, index, false, unique));
      }
      if (unique) {
        auto_index_columns.emplace_back(column_name);
      }
      index++;
    }
  } catch (const std::exception &ex) {
    for (auto column : columns) {
      delete column;
    }
    std::cout << ex.what() << std::endl;
    return DB_FAILED;
  }

  auto schema = std::make_unique<Schema>(columns);
  TableInfo *table_info = nullptr;
  auto result = dbs_[current_db_]->catalog_mgr_->CreateTable(NodeValue(table_name_node), schema.get(), nullptr, table_info);
  if (result != DB_SUCCESS) {
    return result;
  }
  for (const auto &column_name : auto_index_columns) {
    IndexInfo *index_info = nullptr;
    std::string index_name = "__auto_" + NodeValue(table_name_node) + "_" + column_name;
    dbs_[current_db_]->catalog_mgr_->CreateIndex(NodeValue(table_name_node), index_name, {column_name}, nullptr,
                                                 index_info, "bptree");
  }
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t ExecuteEngine::ExecuteDropTable(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteDropTable" << std::endl;
#endif
  if (NeedCurrentDatabase(current_db_)) {
    return DB_FAILED;
  }
  return dbs_[current_db_]->catalog_mgr_->DropTable(NodeValue(ast->child_));
}

/**
 * TODO: Student Implement
 */
dberr_t ExecuteEngine::ExecuteShowIndexes(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteShowIndexes" << std::endl;
#endif
  if (NeedCurrentDatabase(current_db_)) {
    return DB_FAILED;
  }
  std::vector<TableInfo *> tables;
  auto result = dbs_[current_db_]->catalog_mgr_->GetTables(tables);
  if (result != DB_SUCCESS) {
    return result;
  }
  std::vector<std::string> index_rows;
  for (auto table : tables) {
    std::vector<IndexInfo *> indexes;
    dbs_[current_db_]->catalog_mgr_->GetTableIndexes(table->GetTableName(), indexes);
    for (auto index : indexes) {
      std::stringstream ss;
      ss << table->GetTableName() << "." << index->GetIndexName() << " (";
      bool first = true;
      for (auto column : index->GetIndexKeySchema()->GetColumns()) {
        if (!first) {
          ss << ", ";
        }
        ss << column->GetName();
        first = false;
      }
      ss << ")";
      index_rows.emplace_back(ss.str());
    }
  }
  PrintSingleColumnTable("Indexes_in_" + current_db_, index_rows);
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t ExecuteEngine::ExecuteCreateIndex(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteCreateIndex" << std::endl;
#endif
  if (NeedCurrentDatabase(current_db_)) {
    return DB_FAILED;
  }
  auto index_name_node = ast->child_;
  auto table_name_node = index_name_node == nullptr ? nullptr : index_name_node->next_;
  auto keys_node = table_name_node == nullptr ? nullptr : table_name_node->next_;
  auto type_node = keys_node == nullptr ? nullptr : keys_node->next_;
  if (index_name_node == nullptr || table_name_node == nullptr || keys_node == nullptr) {
    return DB_FAILED;
  }
  std::string index_type = "bptree";
  if (type_node != nullptr && type_node->child_ != nullptr) {
    index_type = NodeValue(type_node->child_);
  }
  IndexInfo *index_info = nullptr;
  return dbs_[current_db_]->catalog_mgr_->CreateIndex(NodeValue(table_name_node), NodeValue(index_name_node),
                                                      ParseColumnList(keys_node), nullptr, index_info, index_type);
}

/**
 * TODO: Student Implement
 */
dberr_t ExecuteEngine::ExecuteDropIndex(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteDropIndex" << std::endl;
#endif
  if (NeedCurrentDatabase(current_db_)) {
    return DB_FAILED;
  }
  std::vector<TableInfo *> tables;
  auto result = dbs_[current_db_]->catalog_mgr_->GetTables(tables);
  if (result != DB_SUCCESS) {
    return result;
  }
  for (auto table : tables) {
    IndexInfo *index_info = nullptr;
    if (dbs_[current_db_]->catalog_mgr_->GetIndex(table->GetTableName(), NodeValue(ast->child_), index_info) ==
        DB_SUCCESS) {
      return dbs_[current_db_]->catalog_mgr_->DropIndex(table->GetTableName(), NodeValue(ast->child_));
    }
  }
  return DB_INDEX_NOT_FOUND;
}

dberr_t ExecuteEngine::ExecuteTrxBegin(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteTrxBegin" << std::endl;
#endif
  std::cout << "Transaction begin is accepted as a no-op in this MiniSQL build." << std::endl;
  return DB_SUCCESS;
}

dberr_t ExecuteEngine::ExecuteTrxCommit(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteTrxCommit" << std::endl;
#endif
  std::cout << "Transaction commit is accepted as a no-op in this MiniSQL build." << std::endl;
  return DB_SUCCESS;
}

dberr_t ExecuteEngine::ExecuteTrxRollback(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteTrxRollback" << std::endl;
#endif
  std::cout << "Transaction rollback is accepted as a no-op in this MiniSQL build." << std::endl;
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t ExecuteEngine::ExecuteExecfile(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteExecfile" << std::endl;
#endif
  std::ifstream file(NodeValue(ast->child_));
  if (!file.is_open()) {
    std::cout << "Failed to open file: " << NodeValue(ast->child_) << std::endl;
    return DB_FAILED;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();
  std::string command;
  dberr_t last_result = DB_SUCCESS;
  for (char ch : content) {
    command.push_back(ch);
    if (ch != ';') {
      continue;
    }
    YY_BUFFER_STATE bp = yy_scan_string(command.c_str());
    if (bp == nullptr) {
      return DB_FAILED;
    }
    MinisqlParserInit();
    yy_switch_to_buffer(bp);
    yyparse();
    if (MinisqlParserGetError()) {
      std::cout << MinisqlParserGetErrorMessage() << std::endl;
      last_result = DB_FAILED;
    } else {
      last_result = Execute(MinisqlGetParserRootNode());
      ExecuteInformation(last_result);
    }
    MinisqlParserFinish();
    yy_delete_buffer(bp);
    yylex_destroy();
    command.clear();
    if (last_result == DB_QUIT) {
      return DB_QUIT;
    }
  }
  return last_result;
}

/**
 * TODO: Student Implement
 */
dberr_t ExecuteEngine::ExecuteQuit(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteQuit" << std::endl;
#endif
  return DB_QUIT;
}
