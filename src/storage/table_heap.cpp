#include "storage/table_heap.h"

bool TableHeap::MarkDelete(const RowId &rid, Txn *txn) {
  auto page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(rid.GetPageId()));
  if (page == nullptr) {
    return false;
  }
  page->WLatch();
  page->MarkDelete(rid, txn, lock_manager_, log_manager_);
  page->WUnlatch();
  buffer_pool_manager_->UnpinPage(page->GetTablePageId(), true);
  return true;
}

void TableHeap::RollbackDelete(const RowId &rid, Txn *txn) {
  auto page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(rid.GetPageId()));
  assert(page != nullptr);
  page->WLatch();
  page->RollbackDelete(rid, txn, log_manager_);
  page->WUnlatch();
  buffer_pool_manager_->UnpinPage(page->GetTablePageId(), true);
}

void TableHeap::DeleteTable(page_id_t page_id) {
  if (page_id != INVALID_PAGE_ID) {
    auto temp_table_page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(page_id));
    if (temp_table_page->GetNextPageId() != INVALID_PAGE_ID)
      DeleteTable(temp_table_page->GetNextPageId());
    buffer_pool_manager_->UnpinPage(page_id, false);
    buffer_pool_manager_->DeletePage(page_id);
  } else {
    DeleteTable(first_page_id_);
  }
}

bool TableHeap::InsertTuple(Row &row, Txn *txn) {
  page_id_t cur_page_id = first_page_id_;
  while (cur_page_id != INVALID_PAGE_ID) {
    auto page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(cur_page_id));
    page->WLatch();
    if (page->InsertTuple(row, schema_, txn, lock_manager_, log_manager_)) {
      page->WUnlatch();
      buffer_pool_manager_->UnpinPage(cur_page_id, true);
      return true;
    }
    page->WUnlatch();
    page_id_t next_page_id = page->GetNextPageId();
    buffer_pool_manager_->UnpinPage(cur_page_id, false);
    cur_page_id = next_page_id;
  }
  // All existing pages are full, create a new page
  page_id_t new_page_id;
  Page *new_page = buffer_pool_manager_->NewPage(new_page_id);
  if (new_page == nullptr) {
    return false;
  }
  auto new_table_page = reinterpret_cast<TablePage *>(new_page->GetData());
  new_table_page->Init(new_page_id, INVALID_PAGE_ID, log_manager_, txn);
  // Walk the page chain to find the last page and link the new page
  cur_page_id = first_page_id_;
  while (true) {
    auto page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(cur_page_id));
    page_id_t next_id = page->GetNextPageId();
    if (next_id == INVALID_PAGE_ID) {
      page->SetNextPageId(new_page_id);
      buffer_pool_manager_->UnpinPage(cur_page_id, true);
      break;
    }
    buffer_pool_manager_->UnpinPage(cur_page_id, false);
    cur_page_id = next_id;
  }
  new_table_page->SetPrevPageId(cur_page_id);
  if (!new_table_page->InsertTuple(row, schema_, txn, lock_manager_, log_manager_)) {
    buffer_pool_manager_->UnpinPage(new_page_id, false);
    return false;
  }
  buffer_pool_manager_->UnpinPage(new_page_id, true);
  return true;
}

bool TableHeap::UpdateTuple(Row &row, const RowId &rid, Txn *txn) {
  auto page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(rid.GetPageId()));
  if (page == nullptr) {
    return false;
  }
  Row old_row(rid);
  page->WLatch();
  if (page->UpdateTuple(row, &old_row, schema_, txn, lock_manager_, log_manager_)) {
    page->WUnlatch();
    buffer_pool_manager_->UnpinPage(page->GetTablePageId(), true);
    return true;
  }
  // New tuple is too large, delete old + insert new
  page->MarkDelete(rid, txn, lock_manager_, log_manager_);
  page->WUnlatch();
  buffer_pool_manager_->UnpinPage(page->GetTablePageId(), true);
  row.SetRowId(INVALID_ROWID);
  return InsertTuple(row, txn);
}

void TableHeap::ApplyDelete(const RowId &rid, Txn *txn) {
  auto page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(rid.GetPageId()));
  ASSERT(page != nullptr, "Page not found when applying delete.");
  page->WLatch();
  page->ApplyDelete(rid, txn, log_manager_);
  page->WUnlatch();
  buffer_pool_manager_->UnpinPage(page->GetTablePageId(), true);
}

bool TableHeap::GetTuple(Row *row, Txn *txn) {
  auto page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(row->GetRowId().GetPageId()));
  if (page == nullptr) {
    return false;
  }
  bool ret = page->GetTuple(row, schema_, txn, lock_manager_);
  buffer_pool_manager_->UnpinPage(page->GetTablePageId(), false);
  return ret;
}

TableIterator TableHeap::Begin(Txn *txn) {
  page_id_t cur_page_id = first_page_id_;
  while (cur_page_id != INVALID_PAGE_ID) {
    auto page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(cur_page_id));
    RowId first_rid;
    if (page->GetFirstTupleRid(&first_rid)) {
      buffer_pool_manager_->UnpinPage(cur_page_id, false);
      return TableIterator(this, first_rid, txn);
    }
    page_id_t next_page_id = page->GetNextPageId();
    buffer_pool_manager_->UnpinPage(cur_page_id, false);
    cur_page_id = next_page_id;
  }
  return End();
}

TableIterator TableHeap::End() { return TableIterator(nullptr, RowId(INVALID_PAGE_ID, 0), nullptr); }
