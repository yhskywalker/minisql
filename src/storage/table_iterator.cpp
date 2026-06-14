#include "storage/table_iterator.h"

#include "common/macros.h"
#include "page/table_page.h"
#include "storage/table_heap.h"

TableIterator::TableIterator(TableHeap *table_heap, RowId rid, Txn *txn)
    : table_heap_(table_heap), rid_(rid), txn_(txn) {
  if (rid.GetPageId() != INVALID_PAGE_ID && table_heap != nullptr) {
    row_.destroy();
    row_.SetRowId(rid);
    table_heap_->GetTuple(&row_, txn_);
  }
}

TableIterator::TableIterator(const TableIterator &other)
    : table_heap_(other.table_heap_), row_(other.row_), rid_(other.rid_), txn_(other.txn_) {}

TableIterator::~TableIterator() {}

bool TableIterator::operator==(const TableIterator &itr) const {
  return rid_ == itr.rid_ && table_heap_ == itr.table_heap_;
}

bool TableIterator::operator!=(const TableIterator &itr) const {
  return !(*this == itr);
}

const Row &TableIterator::operator*() {
  return row_;
}

Row *TableIterator::operator->() {
  return &row_;
}

TableIterator &TableIterator::operator=(const TableIterator &itr) noexcept {
  if (this != &itr) {
    table_heap_ = itr.table_heap_;
    row_ = itr.row_;
    rid_ = itr.rid_;
    txn_ = itr.txn_;
  }
  return *this;
}

// ++iter
TableIterator &TableIterator::operator++() {
  if (table_heap_ == nullptr) {
    return *this;
  }
  auto page = reinterpret_cast<TablePage *>(table_heap_->buffer_pool_manager_->FetchPage(rid_.GetPageId()));
  RowId next_rid;
  if (page->GetNextTupleRid(rid_, &next_rid)) {
    rid_ = next_rid;
    row_.SetRowId(rid_);
    row_.destroy();
    page->GetTuple(&row_, table_heap_->schema_, txn_, table_heap_->lock_manager_);
    table_heap_->buffer_pool_manager_->UnpinPage(page->GetTablePageId(), false);
  } else {
    page_id_t next_page_id = page->GetNextPageId();
    table_heap_->buffer_pool_manager_->UnpinPage(page->GetTablePageId(), false);
    while (next_page_id != INVALID_PAGE_ID) {
      page = reinterpret_cast<TablePage *>(table_heap_->buffer_pool_manager_->FetchPage(next_page_id));
      if (page->GetFirstTupleRid(&next_rid)) {
        rid_ = next_rid;
        row_.SetRowId(rid_);
        row_.destroy();
        page->GetTuple(&row_, table_heap_->schema_, txn_, table_heap_->lock_manager_);
        table_heap_->buffer_pool_manager_->UnpinPage(page->GetTablePageId(), false);
        return *this;
      }
      next_page_id = page->GetNextPageId();
      table_heap_->buffer_pool_manager_->UnpinPage(page->GetTablePageId(), false);
    }
    // No more tuples found
    rid_ = RowId(INVALID_PAGE_ID, 0);
    table_heap_ = nullptr;
  }
  return *this;
}

// iter++
TableIterator TableIterator::operator++(int) {
  TableIterator temp(*this);
  ++(*this);
  return temp;
}
