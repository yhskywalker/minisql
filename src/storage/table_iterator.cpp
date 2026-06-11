#include "storage/table_iterator.h"

#include "common/macros.h"
#include "storage/table_heap.h"

/**
 * TODO: Student Implement
 */
TableIterator::TableIterator(TableHeap *table_heap, RowId rid, Txn *txn)
    : table_heap_(table_heap), txn_(txn), current_row_(rid) {
  if (table_heap_ != nullptr && rid.Get() != INVALID_ROWID.Get()) {
    if (!table_heap_->GetTuple(&current_row_, txn_)) {
      current_row_ = Row(INVALID_ROWID);
    }
  }
}

TableIterator::TableIterator(const TableIterator &other)
    : table_heap_(other.table_heap_), txn_(other.txn_), current_row_(other.current_row_) {}

TableIterator::~TableIterator() = default;

bool TableIterator::operator==(const TableIterator &itr) const {
  return table_heap_ == itr.table_heap_ && current_row_.GetRowId() == itr.current_row_.GetRowId();
}

bool TableIterator::operator!=(const TableIterator &itr) const {
  return !(*this == itr);
}

const Row &TableIterator::operator*() {
  return current_row_;
}

Row *TableIterator::operator->() {
  return &current_row_;
}

TableIterator &TableIterator::operator=(const TableIterator &itr) noexcept {
  if (this != &itr) {
    table_heap_ = itr.table_heap_;
    txn_ = itr.txn_;
    current_row_ = itr.current_row_;
  }
  return *this;
}

// ++iter
TableIterator &TableIterator::operator++() {
  if (table_heap_ == nullptr || current_row_.GetRowId().Get() == INVALID_ROWID.Get()) {
    return *this;
  }

  RowId next_rid;
  page_id_t current_page_id = current_row_.GetRowId().GetPageId();
  auto page = reinterpret_cast<TablePage *>(table_heap_->buffer_pool_manager_->FetchPage(current_page_id));
  if (page == nullptr) {
    current_row_ = Row(INVALID_ROWID);
    return *this;
  }

  page->RLatch();
  bool found = page->GetNextTupleRid(current_row_.GetRowId(), &next_rid);
  page_id_t next_page_id = page->GetNextPageId();
  page->RUnlatch();
  table_heap_->buffer_pool_manager_->UnpinPage(current_page_id, false);

  while (!found && next_page_id != INVALID_PAGE_ID) {
    auto next_page = reinterpret_cast<TablePage *>(table_heap_->buffer_pool_manager_->FetchPage(next_page_id));
    if (next_page == nullptr) {
      current_row_ = Row(INVALID_ROWID);
      return *this;
    }
    next_page->RLatch();
    found = next_page->GetFirstTupleRid(&next_rid);
    page_id_t following_page_id = next_page->GetNextPageId();
    next_page->RUnlatch();
    table_heap_->buffer_pool_manager_->UnpinPage(next_page_id, false);
    next_page_id = following_page_id;
  }

  if (found) {
    Row next_row(next_rid);
    if (table_heap_->GetTuple(&next_row, txn_)) {
      current_row_ = next_row;
    } else {
      current_row_ = Row(INVALID_ROWID);
    }
  } else {
    current_row_ = Row(INVALID_ROWID);
  }
  return *this;
}

// iter++
TableIterator TableIterator::operator++(int) {
  TableIterator tmp(*this);
  ++(*this);
  return tmp;
}
