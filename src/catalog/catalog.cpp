#include "catalog/catalog.h"

void CatalogMeta::SerializeTo(char *buf) const {
  ASSERT(GetSerializedSize() <= PAGE_SIZE, "Failed to serialize catalog metadata to disk.");
  MACH_WRITE_UINT32(buf, CATALOG_METADATA_MAGIC_NUM);
  buf += 4;
  MACH_WRITE_UINT32(buf, table_meta_pages_.size());
  buf += 4;
  MACH_WRITE_UINT32(buf, index_meta_pages_.size());
  buf += 4;
  for (auto iter : table_meta_pages_) {
    MACH_WRITE_TO(table_id_t, buf, iter.first);
    buf += 4;
    MACH_WRITE_TO(page_id_t, buf, iter.second);
    buf += 4;
  }
  for (auto iter : index_meta_pages_) {
    MACH_WRITE_TO(index_id_t, buf, iter.first);
    buf += 4;
    MACH_WRITE_TO(page_id_t, buf, iter.second);
    buf += 4;
  }
}

CatalogMeta *CatalogMeta::DeserializeFrom(char *buf) {
  uint32_t magic_num = MACH_READ_UINT32(buf);
  buf += 4;
  ASSERT(magic_num == CATALOG_METADATA_MAGIC_NUM, "Failed to deserialize catalog metadata from disk.");
  uint32_t table_nums = MACH_READ_UINT32(buf);
  buf += 4;
  uint32_t index_nums = MACH_READ_UINT32(buf);
  buf += 4;
  CatalogMeta *meta = new CatalogMeta();
  for (uint32_t i = 0; i < table_nums; i++) {
    auto table_id = MACH_READ_FROM(table_id_t, buf);
    buf += 4;
    auto table_heap_page_id = MACH_READ_FROM(page_id_t, buf);
    buf += 4;
    meta->table_meta_pages_.emplace(table_id, table_heap_page_id);
  }
  for (uint32_t i = 0; i < index_nums; i++) {
    auto index_id = MACH_READ_FROM(index_id_t, buf);
    buf += 4;
    auto index_page_id = MACH_READ_FROM(page_id_t, buf);
    buf += 4;
    meta->index_meta_pages_.emplace(index_id, index_page_id);
  }
  return meta;
}

/**
 * TODO: Student Implement
 */
uint32_t CatalogMeta::GetSerializedSize() const {
  return 4
       + 4
       + 4
       + table_meta_pages_.size() * 8
       + index_meta_pages_.size() * 8;
}

CatalogMeta::CatalogMeta() {}

/**
 * TODO: Student Implement
 */
CatalogManager::CatalogManager(BufferPoolManager *buffer_pool_manager, LockManager *lock_manager,
                               LogManager *log_manager, bool init)
    : buffer_pool_manager_(buffer_pool_manager), lock_manager_(lock_manager), log_manager_(log_manager) {
  if (init) {
    catalog_meta_ = CatalogMeta::NewInstance();
    next_table_id_ = 0;
    next_index_id_ = 0;
    FlushCatalogMetaPage();
  } else {
    auto page = buffer_pool_manager_->FetchPage(CATALOG_META_PAGE_ID);
    catalog_meta_ = CatalogMeta::DeserializeFrom(page->GetData());
    for (auto &[table_id, page_id] : *catalog_meta_->GetTableMetaPages()) {
      LoadTable(table_id, page_id);
    }
    for (auto &[index_id, page_id] : *catalog_meta_->GetIndexMetaPages()) {
      LoadIndex(index_id, page_id);
    }
    next_table_id_ = catalog_meta_->GetNextTableId();
    next_index_id_ = catalog_meta_->GetNextIndexId();
    buffer_pool_manager_->UnpinPage(CATALOG_META_PAGE_ID, false);
  }
}

CatalogManager::~CatalogManager() {
  FlushCatalogMetaPage();
  delete catalog_meta_;
  for (auto iter : tables_) {
    delete iter.second;
  }
  for (auto iter : indexes_) {
    delete iter.second;
  }
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::CreateTable(const string &table_name, TableSchema *schema, Txn *txn, TableInfo *&table_info) {
  if (table_names_.find(table_name) != table_names_.end()) {
    return DB_TABLE_ALREADY_EXIST;
  }
  auto *deep_schema = Schema::DeepCopySchema(schema);
  table_id_t table_id = catalog_meta_->GetNextTableId();
  auto *table_heap = TableHeap::Create(buffer_pool_manager_, deep_schema, txn, log_manager_, lock_manager_);
  auto *table_meta = TableMetadata::Create(table_id, table_name, table_heap->GetFirstPageId(), deep_schema);
  page_id_t meta_page_id;
  if (buffer_pool_manager_->NewPage(meta_page_id) == nullptr) {
    return DB_FAILED;
  }
  char meta_page_data[PAGE_SIZE];
  table_meta->SerializeTo(meta_page_data);
  char *page_ptr = buffer_pool_manager_->FetchPage(meta_page_id)->GetData();
  memcpy(page_ptr, meta_page_data, PAGE_SIZE);
  buffer_pool_manager_->UnpinPage(meta_page_id, true);
  table_info = TableInfo::Create();
  table_info->Init(table_meta, table_heap);
  table_names_[table_name] = table_id;
  tables_[table_id] = table_info;
  catalog_meta_->table_meta_pages_.emplace(table_id, meta_page_id);
  FlushCatalogMetaPage();
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::GetTable(const string &table_name, TableInfo *&table_info) {
  auto it = table_names_.find(table_name);
  if (it == table_names_.end()) {
    return DB_TABLE_NOT_EXIST;
  }
  return GetTable(it->second, table_info);
}

dberr_t CatalogManager::GetTables(vector<TableInfo *> &tables) const {
  for (auto &[id, info] : tables_) {
    tables.push_back(info);
  }
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::CreateIndex(const std::string &table_name, const string &index_name,
                                    const std::vector<std::string> &index_keys, Txn *txn, IndexInfo *&index_info,
                                    const string &index_type) {
  auto table_it = table_names_.find(table_name);
  if (table_it == table_names_.end()) {
    return DB_TABLE_NOT_EXIST;
  }
  auto *table_info = tables_[table_it->second];
  auto *table_schema = table_info->GetSchema();
  std::vector<uint32_t> key_map;
  for (auto &key : index_keys) {
    uint32_t col_idx;
    if (table_schema->GetColumnIndex(key, col_idx) != DB_SUCCESS) {
      return DB_COLUMN_NAME_NOT_EXIST;
    }
    key_map.push_back(col_idx);
  }
  if (index_names_.find(table_name) != index_names_.end() &&
      index_names_[table_name].find(index_name) != index_names_[table_name].end()) {
    return DB_INDEX_ALREADY_EXIST;
  }
  index_id_t index_id = catalog_meta_->GetNextIndexId();
  auto *index_meta = IndexMetadata::Create(index_id, index_name, table_it->second, key_map);
  page_id_t meta_page_id;
  if (buffer_pool_manager_->NewPage(meta_page_id) == nullptr) {
    return DB_FAILED;
  }
  char meta_page_data[PAGE_SIZE];
  index_meta->SerializeTo(meta_page_data);
  char *page_ptr = buffer_pool_manager_->FetchPage(meta_page_id)->GetData();
  memcpy(page_ptr, meta_page_data, PAGE_SIZE);
  buffer_pool_manager_->UnpinPage(meta_page_id, true);
  index_info = IndexInfo::Create();
  index_info->Init(index_meta, table_info, buffer_pool_manager_);
  index_names_[table_name][index_name] = index_id;
  indexes_[index_id] = index_info;
  catalog_meta_->index_meta_pages_.emplace(index_id, meta_page_id);
  FlushCatalogMetaPage();
  auto idx = index_info->GetIndex();
  for (auto iter = table_info->GetTableHeap()->Begin(txn);
       iter != table_info->GetTableHeap()->End(); ++iter) {
    Row current_row = *iter;
    Row key_row;
    current_row.GetKeyFromRow(table_schema, index_info->GetIndexKeySchema(), key_row);
    idx->InsertEntry(key_row, current_row.GetRowId(), txn);
  }
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::GetIndex(const std::string &table_name, const std::string &index_name,
                                 IndexInfo *&index_info) const {
  auto table_it = index_names_.find(table_name);
  if (table_it == index_names_.end()) return DB_INDEX_NOT_FOUND;
  auto idx_it = table_it->second.find(index_name);
  if (idx_it == table_it->second.end()) return DB_INDEX_NOT_FOUND;
  auto info_it = indexes_.find(idx_it->second);
  if (info_it == indexes_.end()) return DB_INDEX_NOT_FOUND;
  index_info = info_it->second;
  return DB_SUCCESS;
}

dberr_t CatalogManager::GetTableIndexes(const std::string &table_name, std::vector<IndexInfo *> &indexes) const {
  auto table_it = index_names_.find(table_name);
  if (table_it == index_names_.end()) return DB_INDEX_NOT_FOUND;
  for (auto &[name, idx_id] : table_it->second) {
    indexes.push_back(indexes_.at(idx_id));
  }
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::DropTable(const string &table_name) {
  auto table_it = table_names_.find(table_name);
  if (table_it == table_names_.end()) return DB_TABLE_NOT_EXIST;
  return DropTable(table_it->second);
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::DropIndex(const string &table_name, const string &index_name) {
  auto table_it = index_names_.find(table_name);
  if (table_it == index_names_.end()) return DB_INDEX_NOT_FOUND;
  auto idx_it = table_it->second.find(index_name);
  if (idx_it == table_it->second.end()) return DB_INDEX_NOT_FOUND;
  index_id_t idx_id = idx_it->second;
  auto *index_info = indexes_[idx_id];
  index_info->GetIndex()->Destroy();
  catalog_meta_->DeleteIndexMetaPage(buffer_pool_manager_, idx_id);
  table_it->second.erase(idx_it);
  if (table_it->second.empty()) index_names_.erase(table_it);
  delete index_info;
  indexes_.erase(idx_id);
  FlushCatalogMetaPage();
  return DB_SUCCESS;
}

dberr_t CatalogManager::FlushCatalogMetaPage() const {
  auto page = buffer_pool_manager_->FetchPage(CATALOG_META_PAGE_ID);
  catalog_meta_->SerializeTo(page->GetData());
  buffer_pool_manager_->UnpinPage(CATALOG_META_PAGE_ID, true);
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::LoadTable(const table_id_t table_id, const page_id_t page_id) {
  auto page = buffer_pool_manager_->FetchPage(page_id);
  TableMetadata *table_meta = nullptr;
  TableMetadata::DeserializeFrom(page->GetData(), table_meta);
  buffer_pool_manager_->UnpinPage(page_id, false);
  auto *table_heap = TableHeap::Create(buffer_pool_manager_, table_meta->GetFirstPageId(),
                                        table_meta->GetSchema(), log_manager_, lock_manager_);
  auto *table_info = TableInfo::Create();
  table_info->Init(table_meta, table_heap);
  table_names_[table_meta->GetTableName()] = table_id;
  tables_[table_id] = table_info;
  return DB_SUCCESS;
}

dberr_t CatalogManager::LoadIndex(const index_id_t index_id, const page_id_t page_id) {
  auto page = buffer_pool_manager_->FetchPage(page_id);
  IndexMetadata *index_meta = nullptr;
  IndexMetadata::DeserializeFrom(page->GetData(), index_meta);
  buffer_pool_manager_->UnpinPage(page_id, false);
  auto *table_info = tables_[index_meta->GetTableId()];
  auto *index_info = IndexInfo::Create();
  index_info->Init(index_meta, table_info, buffer_pool_manager_);
  indexes_[index_id] = index_info;
  index_names_[table_info->GetTableName()][index_meta->GetIndexName()] = index_id;
  return DB_SUCCESS;
}

dberr_t CatalogManager::GetTable(const table_id_t table_id, TableInfo *&table_info) {
  auto it = tables_.find(table_id);
  if (it == tables_.end()) return DB_TABLE_NOT_EXIST;
  table_info = it->second;
  return DB_SUCCESS;
}

dberr_t CatalogManager::DropTable(table_id_t table_id) {
  auto table_it = tables_.find(table_id);
  if (table_it == tables_.end()) return DB_TABLE_NOT_EXIST;
  auto *table_info = table_it->second;
  auto idx_it = index_names_.find(table_info->GetTableName());
  if (idx_it != index_names_.end()) {
    auto idx_names = idx_it->second;
    for (auto &[idx_name, idx_id] : idx_names) {
      DropIndex(table_info->GetTableName(), idx_name);
    }
  }
  table_info->GetTableHeap()->FreeTableHeap();
  page_id_t meta_page_id = catalog_meta_->table_meta_pages_[table_id];
  buffer_pool_manager_->DeletePage(meta_page_id);
  table_names_.erase(table_info->GetTableName());
  tables_.erase(table_it);
  catalog_meta_->table_meta_pages_.erase(table_id);
  delete table_info;
  FlushCatalogMetaPage();
  return DB_SUCCESS;
}
