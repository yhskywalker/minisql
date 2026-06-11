#include "index/index_iterator.h"

#include "index/basic_comparator.h"
#include "index/generic_key.h"

IndexIterator::IndexIterator() = default;

IndexIterator::IndexIterator(page_id_t page_id, BufferPoolManager *bpm, int index)
    : current_page_id(page_id), item_index(index), buffer_pool_manager(bpm) {
  if (current_page_id != INVALID_PAGE_ID && buffer_pool_manager != nullptr) {
    page = reinterpret_cast<LeafPage *>(buffer_pool_manager->FetchPage(current_page_id)->GetData());
  }
}

IndexIterator::IndexIterator(const IndexIterator &other)
    : current_page_id(other.current_page_id), item_index(other.item_index), buffer_pool_manager(other.buffer_pool_manager) {
  if (current_page_id != INVALID_PAGE_ID && buffer_pool_manager != nullptr) {
    page = reinterpret_cast<LeafPage *>(buffer_pool_manager->FetchPage(current_page_id)->GetData());
  }
}

IndexIterator::IndexIterator(IndexIterator &&other) noexcept
    : current_page_id(other.current_page_id),
      page(other.page),
      item_index(other.item_index),
      buffer_pool_manager(other.buffer_pool_manager) {
  other.current_page_id = INVALID_PAGE_ID;
  other.page = nullptr;
  other.item_index = 0;
  other.buffer_pool_manager = nullptr;
}

IndexIterator::~IndexIterator() {
  if (current_page_id != INVALID_PAGE_ID && buffer_pool_manager != nullptr) {
    buffer_pool_manager->UnpinPage(current_page_id, false);
  }
}

IndexIterator &IndexIterator::operator=(const IndexIterator &other) {
  if (this != &other) {
    if (current_page_id != INVALID_PAGE_ID && buffer_pool_manager != nullptr) {
      buffer_pool_manager->UnpinPage(current_page_id, false);
    }
    current_page_id = other.current_page_id;
    item_index = other.item_index;
    buffer_pool_manager = other.buffer_pool_manager;
    page = nullptr;
    if (current_page_id != INVALID_PAGE_ID && buffer_pool_manager != nullptr) {
      page = reinterpret_cast<LeafPage *>(buffer_pool_manager->FetchPage(current_page_id)->GetData());
    }
  }
  return *this;
}

IndexIterator &IndexIterator::operator=(IndexIterator &&other) noexcept {
  if (this != &other) {
    if (current_page_id != INVALID_PAGE_ID && buffer_pool_manager != nullptr) {
      buffer_pool_manager->UnpinPage(current_page_id, false);
    }
    current_page_id = other.current_page_id;
    page = other.page;
    item_index = other.item_index;
    buffer_pool_manager = other.buffer_pool_manager;
    other.current_page_id = INVALID_PAGE_ID;
    other.page = nullptr;
    other.item_index = 0;
    other.buffer_pool_manager = nullptr;
  }
  return *this;
}

/**
 * TODO: Student Implement
 */
std::pair<GenericKey *, RowId> IndexIterator::operator*() {
  ASSERT(page != nullptr && item_index >= 0 && item_index < page->GetSize(), "Invalid index iterator dereference.");
  return page->GetItem(item_index);
}

/**
 * TODO: Student Implement
 */
IndexIterator &IndexIterator::operator++() {
  if (current_page_id == INVALID_PAGE_ID || page == nullptr) {
    return *this;
  }
  item_index++;
  if (item_index < page->GetSize()) {
    return *this;
  }

  page_id_t next_page_id = page->GetNextPageId();
  buffer_pool_manager->UnpinPage(current_page_id, false);
  current_page_id = INVALID_PAGE_ID;
  page = nullptr;
  item_index = 0;

  while (next_page_id != INVALID_PAGE_ID) {
    auto *next_page = reinterpret_cast<LeafPage *>(buffer_pool_manager->FetchPage(next_page_id)->GetData());
    if (next_page->GetSize() > 0) {
      current_page_id = next_page_id;
      page = next_page;
      return *this;
    }
    page_id_t following_page_id = next_page->GetNextPageId();
    buffer_pool_manager->UnpinPage(next_page_id, false);
    next_page_id = following_page_id;
  }
  return *this;
}

bool IndexIterator::operator==(const IndexIterator &itr) const {
  return current_page_id == itr.current_page_id && item_index == itr.item_index;
}

bool IndexIterator::operator!=(const IndexIterator &itr) const {
  return !(*this == itr);
}
