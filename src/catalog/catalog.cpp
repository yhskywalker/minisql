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
  // check valid
  uint32_t magic_num = MACH_READ_UINT32(buf);
  buf += 4;
  ASSERT(magic_num == CATALOG_METADATA_MAGIC_NUM, "Failed to deserialize catalog metadata from disk.");
  // get table and index nums
  uint32_t table_nums = MACH_READ_UINT32(buf);
  buf += 4;
  uint32_t index_nums = MACH_READ_UINT32(buf);
  buf += 4;
  // create metadata and read value
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
  return 3 * sizeof(uint32_t) +
         (table_meta_pages_.size() + index_meta_pages_.size()) * (sizeof(uint32_t) + sizeof(page_id_t));
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
  } else {
    auto page = buffer_pool_manager_->FetchPage(CATALOG_META_PAGE_ID);
    ASSERT(page != nullptr, "Failed to fetch catalog meta page.");
    catalog_meta_ = CatalogMeta::DeserializeFrom(page->GetData());
    buffer_pool_manager_->UnpinPage(CATALOG_META_PAGE_ID, false);
  }

  next_table_id_ = catalog_meta_->GetNextTableId();
  next_index_id_ = catalog_meta_->GetNextIndexId();

  if (!init) {
    for (auto &entry : *catalog_meta_->GetTableMetaPages()) {
      ASSERT(LoadTable(entry.first, entry.second) == DB_SUCCESS, "Failed to load table metadata.");
    }
    for (auto &entry : *catalog_meta_->GetIndexMetaPages()) {
      ASSERT(LoadIndex(entry.first, entry.second) == DB_SUCCESS, "Failed to load index metadata.");
    }
  } else {
    FlushCatalogMetaPage();
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

  page_id_t meta_page_id = INVALID_PAGE_ID;
  auto meta_page = buffer_pool_manager_->NewPage(meta_page_id);
  if (meta_page == nullptr) {
    return DB_FAILED;
  }

  auto table_id = next_table_id_++;
  auto schema_copy = Schema::DeepCopySchema(schema);
  auto table_heap = TableHeap::Create(buffer_pool_manager_, schema_copy, txn, log_manager_, lock_manager_);
  auto table_meta = TableMetadata::Create(table_id, table_name, table_heap->GetFirstPageId(), schema_copy);
  table_meta->SerializeTo(meta_page->GetData());
  buffer_pool_manager_->UnpinPage(meta_page_id, true);

  table_info = TableInfo::Create();
  table_info->Init(table_meta, table_heap);
  table_names_[table_name] = table_id;
  tables_[table_id] = table_info;
  catalog_meta_->table_meta_pages_[table_id] = meta_page_id;
  index_names_[table_name];
  return FlushCatalogMetaPage();
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::GetTable(const string &table_name, TableInfo *&table_info) {
  auto name_iter = table_names_.find(table_name);
  if (name_iter == table_names_.end()) {
    table_info = nullptr;
    return DB_TABLE_NOT_EXIST;
  }
  return GetTable(name_iter->second, table_info);
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::GetTables(vector<TableInfo *> &tables) const {
  tables.clear();
  tables.reserve(tables_.size());
  for (auto &entry : tables_) {
    tables.emplace_back(entry.second);
  }
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::CreateIndex(const std::string &table_name, const string &index_name,
                                    const std::vector<std::string> &index_keys, Txn *txn, IndexInfo *&index_info,
                                    const string &index_type) {
  TableInfo *table_info = nullptr;
  auto table_status = GetTable(table_name, table_info);
  if (table_status != DB_SUCCESS) {
    index_info = nullptr;
    return table_status;
  }

  auto &table_indexes = index_names_[table_name];
  if (table_indexes.find(index_name) != table_indexes.end()) {
    index_info = nullptr;
    return DB_INDEX_ALREADY_EXIST;
  }

  std::vector<uint32_t> key_map;
  key_map.reserve(index_keys.size());
  for (auto &key_name : index_keys) {
    uint32_t column_index = 0;
    if (table_info->GetSchema()->GetColumnIndex(key_name, column_index) != DB_SUCCESS) {
      index_info = nullptr;
      return DB_COLUMN_NAME_NOT_EXIST;
    }
    key_map.emplace_back(column_index);
  }

  page_id_t meta_page_id = INVALID_PAGE_ID;
  auto meta_page = buffer_pool_manager_->NewPage(meta_page_id);
  if (meta_page == nullptr) {
    index_info = nullptr;
    return DB_FAILED;
  }

  auto index_id = next_index_id_++;
  auto index_meta = IndexMetadata::Create(index_id, index_name, table_info->GetTableId(), key_map);
  index_meta->SerializeTo(meta_page->GetData());
  buffer_pool_manager_->UnpinPage(meta_page_id, true);

  index_info = IndexInfo::Create();
  index_info->Init(index_meta, table_info, buffer_pool_manager_, index_type);
  if (index_info->GetIndex() == nullptr) {
    delete index_info;
    index_info = nullptr;
    buffer_pool_manager_->DeletePage(meta_page_id);
    return DB_FAILED;
  }

  for (auto iter = table_info->GetTableHeap()->Begin(txn); iter != table_info->GetTableHeap()->End(); ++iter) {
    Row row = *iter;
    Row key_row;
    row.GetKeyFromRow(table_info->GetSchema(), index_info->GetIndexKeySchema(), key_row);
    index_info->GetIndex()->InsertEntry(key_row, row.GetRowId(), txn);
  }

  table_indexes[index_name] = index_id;
  indexes_[index_id] = index_info;
  catalog_meta_->index_meta_pages_[index_id] = meta_page_id;
  return FlushCatalogMetaPage();
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::GetIndex(const std::string &table_name, const std::string &index_name,
                                 IndexInfo *&index_info) const {
  auto table_iter = index_names_.find(table_name);
  if (table_iter == index_names_.end()) {
    index_info = nullptr;
    return DB_TABLE_NOT_EXIST;
  }
  auto index_iter = table_iter->second.find(index_name);
  if (index_iter == table_iter->second.end()) {
    index_info = nullptr;
    return DB_INDEX_NOT_FOUND;
  }
  auto info_iter = indexes_.find(index_iter->second);
  if (info_iter == indexes_.end()) {
    index_info = nullptr;
    return DB_INDEX_NOT_FOUND;
  }
  index_info = info_iter->second;
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::GetTableIndexes(const std::string &table_name, std::vector<IndexInfo *> &indexes) const {
  indexes.clear();
  auto table_iter = index_names_.find(table_name);
  if (table_iter == index_names_.end()) {
    return DB_TABLE_NOT_EXIST;
  }
  for (auto &index_entry : table_iter->second) {
    auto info_iter = indexes_.find(index_entry.second);
    if (info_iter != indexes_.end()) {
      indexes.emplace_back(info_iter->second);
    }
  }
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::DropTable(const string &table_name) {
  auto table_iter = table_names_.find(table_name);
  if (table_iter == table_names_.end()) {
    return DB_TABLE_NOT_EXIST;
  }
  return DropTable(table_iter->second);
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::DropIndex(const string &table_name, const string &index_name) {
  auto table_iter = index_names_.find(table_name);
  if (table_iter == index_names_.end()) {
    return DB_TABLE_NOT_EXIST;
  }
  auto index_iter = table_iter->second.find(index_name);
  if (index_iter == table_iter->second.end()) {
    return DB_INDEX_NOT_FOUND;
  }

  auto index_id = index_iter->second;
  auto info_iter = indexes_.find(index_id);
  if (info_iter != indexes_.end()) {
    info_iter->second->GetIndex()->Destroy();
    delete info_iter->second;
    indexes_.erase(info_iter);
  }
  catalog_meta_->DeleteIndexMetaPage(buffer_pool_manager_, index_id);
  table_iter->second.erase(index_iter);
  return FlushCatalogMetaPage();
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::FlushCatalogMetaPage() const {
  auto page = buffer_pool_manager_->FetchPage(CATALOG_META_PAGE_ID);
  if (page == nullptr) {
    return DB_FAILED;
  }
  catalog_meta_->SerializeTo(page->GetData());
  buffer_pool_manager_->UnpinPage(CATALOG_META_PAGE_ID, true);
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::LoadTable(const table_id_t table_id, const page_id_t page_id) {
  auto page = buffer_pool_manager_->FetchPage(page_id);
  if (page == nullptr) {
    return DB_FAILED;
  }
  TableMetadata *table_meta = nullptr;
  TableMetadata::DeserializeFrom(page->GetData(), table_meta);
  buffer_pool_manager_->UnpinPage(page_id, false);

  auto table_heap =
      TableHeap::Create(buffer_pool_manager_, table_meta->GetFirstPageId(), table_meta->GetSchema(), log_manager_,
                        lock_manager_);
  auto table_info = TableInfo::Create();
  table_info->Init(table_meta, table_heap);
  table_names_[table_meta->GetTableName()] = table_id;
  tables_[table_id] = table_info;
  index_names_[table_meta->GetTableName()];
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::LoadIndex(const index_id_t index_id, const page_id_t page_id) {
  auto page = buffer_pool_manager_->FetchPage(page_id);
  if (page == nullptr) {
    return DB_FAILED;
  }
  IndexMetadata *index_meta = nullptr;
  IndexMetadata::DeserializeFrom(page->GetData(), index_meta);
  buffer_pool_manager_->UnpinPage(page_id, false);

  TableInfo *table_info = nullptr;
  if (GetTable(index_meta->GetTableId(), table_info) != DB_SUCCESS) {
    delete index_meta;
    return DB_TABLE_NOT_EXIST;
  }

  auto index_info = IndexInfo::Create();
  index_info->Init(index_meta, table_info, buffer_pool_manager_);
  index_names_[table_info->GetTableName()][index_meta->GetIndexName()] = index_id;
  indexes_[index_id] = index_info;
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::GetTable(const table_id_t table_id, TableInfo *&table_info) {
  auto iter = tables_.find(table_id);
  if (iter == tables_.end()) {
    table_info = nullptr;
    return DB_TABLE_NOT_EXIST;
  }
  table_info = iter->second;
  return DB_SUCCESS;
}

dberr_t CatalogManager::DropTable(table_id_t table_id) {
  TableInfo *table_info = nullptr;
  if (GetTable(table_id, table_info) != DB_SUCCESS) {
    return DB_TABLE_NOT_EXIST;
  }
  auto table_name = table_info->GetTableName();

  auto index_iter = index_names_.find(table_name);
  if (index_iter != index_names_.end()) {
    std::vector<std::string> index_names;
    index_names.reserve(index_iter->second.size());
    for (auto &entry : index_iter->second) {
      index_names.emplace_back(entry.first);
    }
    for (auto &index_name : index_names) {
      DropIndex(table_name, index_name);
    }
    index_names_.erase(table_name);
  }

  auto meta_iter = catalog_meta_->table_meta_pages_.find(table_id);
  if (meta_iter != catalog_meta_->table_meta_pages_.end()) {
    buffer_pool_manager_->DeletePage(meta_iter->second);
    catalog_meta_->table_meta_pages_.erase(meta_iter);
  }
  table_info->GetTableHeap()->DeleteTable();
  table_names_.erase(table_name);
  tables_.erase(table_id);
  delete table_info;
  return FlushCatalogMetaPage();
}
