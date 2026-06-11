#include "storage/table_heap.h"

/**
 * TODO: Student Implement
 */
bool TableHeap::InsertTuple(Row &row, Txn *txn) {
  if (row.GetSerializedSize(schema_) > TablePage::SIZE_MAX_ROW) {
    return false;
  }

  page_id_t current_page_id = first_page_id_;
  while (current_page_id != INVALID_PAGE_ID) {
    auto page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(current_page_id));
    if (page == nullptr) {
      return false;
    }
    page->WLatch();
    if (page->InsertTuple(row, schema_, txn, lock_manager_, log_manager_)) {
      page->WUnlatch();
      buffer_pool_manager_->UnpinPage(current_page_id, true);
      return true;
    }

    page_id_t next_page_id = page->GetNextPageId();
    if (next_page_id == INVALID_PAGE_ID) {
      page_id_t new_page_id = INVALID_PAGE_ID;
      auto new_page = reinterpret_cast<TablePage *>(buffer_pool_manager_->NewPage(new_page_id));
      if (new_page == nullptr) {
        page->WUnlatch();
        buffer_pool_manager_->UnpinPage(current_page_id, false);
        return false;
      }
      new_page->Init(new_page_id, current_page_id, log_manager_, txn);
      page->SetNextPageId(new_page_id);
      page->WUnlatch();
      buffer_pool_manager_->UnpinPage(current_page_id, true);

      new_page->WLatch();
      bool inserted = new_page->InsertTuple(row, schema_, txn, lock_manager_, log_manager_);
      new_page->WUnlatch();
      buffer_pool_manager_->UnpinPage(new_page_id, true);
      return inserted;
    }

    page->WUnlatch();
    buffer_pool_manager_->UnpinPage(current_page_id, false);
    current_page_id = next_page_id;
  }
  return false;
}

bool TableHeap::MarkDelete(const RowId &rid, Txn *txn) {
  // Find the page which contains the tuple.
  auto page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(rid.GetPageId()));
  // If the page could not be found, then abort the recovery.
  if (page == nullptr) {
    return false;
  }
  // Otherwise, mark the tuple as deleted.
  page->WLatch();
  bool deleted = page->MarkDelete(rid, txn, lock_manager_, log_manager_);
  page->WUnlatch();
  buffer_pool_manager_->UnpinPage(page->GetTablePageId(), deleted);
  return deleted;
}

/**
 * TODO: Student Implement
 */
bool TableHeap::UpdateTuple(Row &row, const RowId &rid, Txn *txn) {
  auto page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(rid.GetPageId()));
  if (page == nullptr) {
    return false;
  }
  Row old_row(rid);
  page->WLatch();
  bool updated = page->UpdateTuple(row, &old_row, schema_, txn, lock_manager_, log_manager_);
  if (updated) {
    row.SetRowId(rid);
  }
  page->WUnlatch();
  buffer_pool_manager_->UnpinPage(page->GetTablePageId(), updated);
  return updated;
}

/**
 * TODO: Student Implement
 */
void TableHeap::ApplyDelete(const RowId &rid, Txn *txn) {
  auto page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(rid.GetPageId()));
  if (page == nullptr) {
    return;
  }
  page->WLatch();
  page->ApplyDelete(rid, txn, log_manager_);
  page->WUnlatch();
  buffer_pool_manager_->UnpinPage(page->GetTablePageId(), true);
}

void TableHeap::RollbackDelete(const RowId &rid, Txn *txn) {
  // Find the page which contains the tuple.
  auto page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(rid.GetPageId()));
  assert(page != nullptr);
  // Rollback to delete.
  page->WLatch();
  page->RollbackDelete(rid, txn, log_manager_);
  page->WUnlatch();
  buffer_pool_manager_->UnpinPage(page->GetTablePageId(), true);
}

/**
 * TODO: Student Implement
 */
bool TableHeap::GetTuple(Row *row, Txn *txn) {
  auto page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(row->GetRowId().GetPageId()));
  if (page == nullptr) {
    return false;
  }
  page->RLatch();
  bool found = page->GetTuple(row, schema_, txn, lock_manager_);
  page->RUnlatch();
  buffer_pool_manager_->UnpinPage(page->GetTablePageId(), false);
  return found;
}

void TableHeap::DeleteTable(page_id_t page_id) {
  if (page_id != INVALID_PAGE_ID) {
    auto temp_table_page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(page_id));  // 删除table_heap
    if (temp_table_page->GetNextPageId() != INVALID_PAGE_ID)
      DeleteTable(temp_table_page->GetNextPageId());
    buffer_pool_manager_->UnpinPage(page_id, false);
    buffer_pool_manager_->DeletePage(page_id);
  } else {
    DeleteTable(first_page_id_);
  }
}

/**
 * TODO: Student Implement
 */
TableIterator TableHeap::Begin(Txn *txn) {
  page_id_t current_page_id = first_page_id_;
  while (current_page_id != INVALID_PAGE_ID) {
    auto page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(current_page_id));
    if (page == nullptr) {
      return End();
    }
    page->RLatch();
    RowId first_rid;
    bool found = page->GetFirstTupleRid(&first_rid);
    page_id_t next_page_id = page->GetNextPageId();
    page->RUnlatch();
    buffer_pool_manager_->UnpinPage(current_page_id, false);
    if (found) {
      return TableIterator(this, first_rid, txn);
    }
    current_page_id = next_page_id;
  }
  return End();
}

/**
 * TODO: Student Implement
 */
TableIterator TableHeap::End() { return TableIterator(this, INVALID_ROWID, nullptr); }
