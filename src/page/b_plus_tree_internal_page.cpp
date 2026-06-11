#include "page/b_plus_tree_internal_page.h"

#include <algorithm>
#include <cstring>

#include "index/generic_key.h"

#define pairs_off (data_)
#define pair_size (GetKeySize() + sizeof(page_id_t))
#define key_off 0
#define val_off GetKeySize()

/**
 * TODO: Student Implement
 */
/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/
/*
 * Init method after creating a new internal page
 * Including set page type, set current size, set page id, set parent id and set
 * max page size
 */
void InternalPage::Init(page_id_t page_id, page_id_t parent_id, int key_size, int max_size) {
  SetPageType(IndexPageType::INTERNAL_PAGE);
  SetKeySize(key_size);
  SetSize(0);
  SetPageId(page_id);
  SetParentPageId(parent_id);
  SetLSN();
  if (max_size == UNDEFINED_SIZE) {
    max_size = static_cast<int>((PAGE_SIZE - INTERNAL_PAGE_HEADER_SIZE) / (key_size + sizeof(page_id_t))) - 1;
  }
  SetMaxSize(std::max(3, max_size));
}
/*
 * Helper method to get/set the key associated with input "index"(a.k.a
 * array offset)
 */
GenericKey *InternalPage::KeyAt(int index) {
  return reinterpret_cast<GenericKey *>(pairs_off + index * pair_size + key_off);
}

void InternalPage::SetKeyAt(int index, GenericKey *key) {
  memcpy(pairs_off + index * pair_size + key_off, key, GetKeySize());
}

page_id_t InternalPage::ValueAt(int index) const {
  return *reinterpret_cast<const page_id_t *>(pairs_off + index * pair_size + val_off);
}

void InternalPage::SetValueAt(int index, page_id_t value) {
  *reinterpret_cast<page_id_t *>(pairs_off + index * pair_size + val_off) = value;
}

int InternalPage::ValueIndex(const page_id_t &value) const {
  for (int i = 0; i < GetSize(); ++i) {
    if (ValueAt(i) == value)
      return i;
  }
  return -1;
}

void *InternalPage::PairPtrAt(int index) {
  return KeyAt(index);
}

void InternalPage::PairCopy(void *dest, void *src, int pair_num) {
  memcpy(dest, src, pair_num * (GetKeySize() + sizeof(page_id_t)));
}
/*****************************************************************************
 * LOOKUP
 *****************************************************************************/
/*
 * Find and return the child pointer(page_id) which points to the child page
 * that contains input "key"
 * Start the search from the second key(the first key should always be invalid)
 * 用了二分查找
 */
page_id_t InternalPage::Lookup(const GenericKey *key, const KeyManager &KM) {
  if (GetSize() == 0) {
    return INVALID_PAGE_ID;
  }
  int left = 1;
  int right = GetSize() - 1;
  int result = 0;
  while (left <= right) {
    int mid = left + (right - left) / 2;
    if (KM.CompareKeys(KeyAt(mid), key) <= 0) {
      result = mid;
      left = mid + 1;
    } else {
      right = mid - 1;
    }
  }
  return ValueAt(result);
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/*
 * Populate new root page with old_value + new_key & new_value
 * When the insertion cause overflow from leaf page all the way upto the root
 * page, you should create a new root page and populate its elements.
 * NOTE: This method is only called within InsertIntoParent()(b_plus_tree.cpp)
 */
void InternalPage::PopulateNewRoot(const page_id_t &old_value, GenericKey *new_key, const page_id_t &new_value) {
  SetSize(2);
  SetValueAt(0, old_value);
  SetKeyAt(1, new_key);
  SetValueAt(1, new_value);
}

/*
 * Insert new_key & new_value pair right after the pair with its value ==
 * old_value
 * @return:  new size after insertion
 */
int InternalPage::InsertNodeAfter(const page_id_t &old_value, GenericKey *new_key, const page_id_t &new_value) {
  int index = ValueIndex(old_value);
  ASSERT(index != -1, "Old child page is not found in internal page.");
  int insert_index = index + 1;
  if (insert_index < GetSize()) {
    memmove(PairPtrAt(insert_index + 1), PairPtrAt(insert_index), (GetSize() - insert_index) * pair_size);
  }
  SetKeyAt(insert_index, new_key);
  SetValueAt(insert_index, new_value);
  IncreaseSize(1);
  return GetSize();
}

/*****************************************************************************
 * SPLIT
 *****************************************************************************/
/*
 * Remove half of key & value pairs from this page to "recipient" page
 * buffer_pool_manager 是干嘛的？传给CopyNFrom()用于Fetch数据页
 */
void InternalPage::MoveHalfTo(InternalPage *recipient, BufferPoolManager *buffer_pool_manager) {
  int start = GetSize() / 2;
  int move_size = GetSize() - start;
  recipient->CopyNFrom(PairPtrAt(start), move_size, buffer_pool_manager);
  SetSize(start);
}

/* Copy entries into me, starting from {items} and copy {size} entries.
 * Since it is an internal page, for all entries (pages) moved, their parents page now changes to me.
 * So I need to 'adopt' them by changing their parent page id, which needs to be persisted with BufferPoolManger
 *
 */
void InternalPage::CopyNFrom(void *src, int size, BufferPoolManager *buffer_pool_manager) {
  if (size <= 0) {
    return;
  }
  int old_size = GetSize();
  PairCopy(PairPtrAt(old_size), src, size);
  IncreaseSize(size);
  for (int i = old_size; i < old_size + size; i++) {
    Page *child_page = buffer_pool_manager->FetchPage(ValueAt(i));
    ASSERT(child_page != nullptr, "Failed to fetch child page.");
    auto *child = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
    child->SetParentPageId(GetPageId());
    buffer_pool_manager->UnpinPage(child->GetPageId(), true);
  }
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/*
 * Remove the key & value pair in internal page according to input index(a.k.a
 * array offset)
 * NOTE: store key&value pair continuously after deletion
 */
void InternalPage::Remove(int index) {
  if (index < 0 || index >= GetSize()) {
    return;
  }
  if (index + 1 < GetSize()) {
    memmove(PairPtrAt(index), PairPtrAt(index + 1), (GetSize() - index - 1) * pair_size);
  }
  IncreaseSize(-1);
}

/*
 * Remove the only key & value pair in internal page and return the value
 * NOTE: only call this method within AdjustRoot()(in b_plus_tree.cpp)
 */
page_id_t InternalPage::RemoveAndReturnOnlyChild() {
  page_id_t child = ValueAt(0);
  SetSize(0);
  return child;
}

/*****************************************************************************
 * MERGE
 *****************************************************************************/
/*
 * Remove all of key & value pairs from this page to "recipient" page.
 * The middle_key is the separation key you should get from the parent. You need
 * to make sure the middle key is added to the recipient to maintain the invariant.
 * You also need to use BufferPoolManager to persist changes to the parent page id for those
 * pages that are moved to the recipient
 */
void InternalPage::MoveAllTo(InternalPage *recipient, GenericKey *middle_key, BufferPoolManager *buffer_pool_manager) {
  SetKeyAt(0, middle_key);
  recipient->CopyNFrom(PairPtrAt(0), GetSize(), buffer_pool_manager);
  SetSize(0);
}

/*****************************************************************************
 * REDISTRIBUTE
 *****************************************************************************/
/*
 * Remove the first key & value pair from this page to tail of "recipient" page.
 *
 * The middle_key is the separation key you should get from the parent. You need
 * to make sure the middle key is added to the recipient to maintain the invariant.
 * You also need to use BufferPoolManager to persist changes to the parent page id for those
 * pages that are moved to the recipient
 */
void InternalPage::MoveFirstToEndOf(InternalPage *recipient, GenericKey *middle_key,
                                    BufferPoolManager *buffer_pool_manager) {
  if (GetSize() == 0) {
    return;
  }
  recipient->CopyLastFrom(middle_key, ValueAt(0), buffer_pool_manager);
  if (GetSize() > 1) {
    GenericKey *new_middle_key = KeyAt(1);
    memmove(PairPtrAt(0), PairPtrAt(1), (GetSize() - 1) * pair_size);
    SetKeyAt(0, new_middle_key);
  }
  IncreaseSize(-1);
}

/* Append an entry at the end.
 * Since it is an internal page, the moved entry(page)'s parent needs to be updated.
 * So I need to 'adopt' it by changing its parent page id, which needs to be persisted with BufferPoolManger
 */
void InternalPage::CopyLastFrom(GenericKey *key, const page_id_t value, BufferPoolManager *buffer_pool_manager) {
  SetKeyAt(GetSize(), key);
  SetValueAt(GetSize(), value);
  IncreaseSize(1);
  Page *child_page = buffer_pool_manager->FetchPage(value);
  ASSERT(child_page != nullptr, "Failed to fetch child page.");
  auto *child = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
  child->SetParentPageId(GetPageId());
  buffer_pool_manager->UnpinPage(child->GetPageId(), true);
}

/*
 * Remove the last key & value pair from this page to head of "recipient" page.
 * You need to handle the original dummy key properly, e.g. updating recipient’s array to position the middle_key at the
 * right place.
 * You also need to use BufferPoolManager to persist changes to the parent page id for those pages that are
 * moved to the recipient
 */
void InternalPage::MoveLastToFrontOf(InternalPage *recipient, GenericKey *middle_key,
                                     BufferPoolManager *buffer_pool_manager) {
  if (GetSize() == 0) {
    return;
  }
  recipient->CopyFirstFrom(ValueAt(GetSize() - 1), buffer_pool_manager);
  recipient->SetKeyAt(1, middle_key);
  IncreaseSize(-1);
}

/* Append an entry at the beginning.
 * Since it is an internal page, the moved entry(page)'s parent needs to be updated.
 * So I need to 'adopt' it by changing its parent page id, which needs to be persisted with BufferPoolManger
 */
void InternalPage::CopyFirstFrom(const page_id_t value, BufferPoolManager *buffer_pool_manager) {
  if (GetSize() > 0) {
    memmove(PairPtrAt(1), PairPtrAt(0), GetSize() * pair_size);
  }
  SetValueAt(0, value);
  IncreaseSize(1);
  Page *child_page = buffer_pool_manager->FetchPage(value);
  ASSERT(child_page != nullptr, "Failed to fetch child page.");
  auto *child = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
  child->SetParentPageId(GetPageId());
  buffer_pool_manager->UnpinPage(child->GetPageId(), true);
}
