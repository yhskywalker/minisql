#include "page/bitmap_page.h"

#include "glog/logging.h"

/**
 * TODO: Student Implement
 */
template <size_t PageSize>
bool BitmapPage<PageSize>::AllocatePage(uint32_t &page_offset) {
  if (page_allocated_ >= GetMaxSupportedSize()) {
    return false;
  }

  uint32_t start = next_free_page_;
  if (start >= GetMaxSupportedSize()) {
    start = 0;
  }
  for (uint32_t i = 0; i < GetMaxSupportedSize(); i++) {
    uint32_t offset = (start + i) % GetMaxSupportedSize();
    uint32_t byte_index = offset / 8;
    uint8_t bit_index = offset % 8;
    if (IsPageFreeLow(byte_index, bit_index)) {
      bytes[byte_index] |= static_cast<unsigned char>(1U << bit_index);
      page_allocated_++;
      page_offset = offset;

      next_free_page_ = GetMaxSupportedSize();
      if (page_allocated_ < GetMaxSupportedSize()) {
        for (uint32_t j = 1; j <= GetMaxSupportedSize(); j++) {
          uint32_t candidate = (offset + j) % GetMaxSupportedSize();
          if (IsPageFree(candidate)) {
            next_free_page_ = candidate;
            break;
          }
        }
      }
      return true;
    }
  }
  return false;
}

/**
 * TODO: Student Implement
 */
template <size_t PageSize>
bool BitmapPage<PageSize>::DeAllocatePage(uint32_t page_offset) {
  if (page_offset >= GetMaxSupportedSize() || IsPageFree(page_offset)) {
    return false;
  }
  uint32_t byte_index = page_offset / 8;
  uint8_t bit_index = page_offset % 8;
  bytes[byte_index] &= static_cast<unsigned char>(~(1U << bit_index));
  page_allocated_--;
  if (next_free_page_ >= GetMaxSupportedSize() || page_offset < next_free_page_) {
    next_free_page_ = page_offset;
  }
  return true;
}

/**
 * TODO: Student Implement
 */
template <size_t PageSize>
bool BitmapPage<PageSize>::IsPageFree(uint32_t page_offset) const {
  if (page_offset >= GetMaxSupportedSize()) {
    return false;
  }
  return IsPageFreeLow(page_offset / 8, page_offset % 8);
}

template <size_t PageSize>
bool BitmapPage<PageSize>::IsPageFreeLow(uint32_t byte_index, uint8_t bit_index) const {
  return (bytes[byte_index] & static_cast<unsigned char>(1U << bit_index)) == 0;
}

template class BitmapPage<64>;

template class BitmapPage<128>;

template class BitmapPage<256>;

template class BitmapPage<512>;

template class BitmapPage<1024>;

template class BitmapPage<2048>;

template class BitmapPage<4096>;
