#include "buffer/clock_replacer.h"

#include <iterator>

CLOCKReplacer::CLOCKReplacer(size_t num_pages) : capacity_(num_pages), clock_hand_(clock_list_.end()) {}

CLOCKReplacer::~CLOCKReplacer() = default;

bool CLOCKReplacer::Victim(frame_id_t *frame_id) {
  std::lock_guard<std::mutex> lock(latch_);
  if (frame_id == nullptr || clock_list_.empty()) {
    return false;
  }

  while (true) {
    if (clock_hand_ == clock_list_.end()) {
      clock_hand_ = clock_list_.begin();
    }

    auto current = clock_hand_;
    auto candidate = *current;
    if (reference_bits_[candidate]) {
      reference_bits_[candidate] = false;
      ++clock_hand_;
      continue;
    }

    *frame_id = candidate;
    ++clock_hand_;
    frame_table_.erase(candidate);
    reference_bits_.erase(candidate);
    clock_list_.erase(current);
    if (clock_list_.empty()) {
      clock_hand_ = clock_list_.end();
    } else if (clock_hand_ == clock_list_.end()) {
      clock_hand_ = clock_list_.begin();
    }
    return true;
  }
}

void CLOCKReplacer::Pin(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock(latch_);
  auto iter = frame_table_.find(frame_id);
  if (iter == frame_table_.end()) {
    return;
  }

  auto current = iter->second;
  auto next = std::next(current);
  if (current == clock_hand_) {
    clock_hand_ = next;
  }
  frame_table_.erase(iter);
  reference_bits_.erase(frame_id);
  clock_list_.erase(current);
  if (clock_list_.empty()) {
    clock_hand_ = clock_list_.end();
  } else if (clock_hand_ == clock_list_.end()) {
    clock_hand_ = clock_list_.begin();
  }
}

void CLOCKReplacer::Unpin(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock(latch_);
  if (capacity_ == 0) {
    return;
  }

  auto iter = frame_table_.find(frame_id);
  if (iter != frame_table_.end()) {
    reference_bits_[frame_id] = true;
    return;
  }

  if (clock_list_.size() >= capacity_) {
    return;
  }

  const bool was_empty = clock_list_.empty();
  clock_list_.push_back(frame_id);
  auto list_iter = std::prev(clock_list_.end());
  frame_table_[frame_id] = list_iter;
  reference_bits_[frame_id] = true;
  if (was_empty) {
    clock_hand_ = list_iter;
  }
}

size_t CLOCKReplacer::Size() {
  std::lock_guard<std::mutex> lock(latch_);
  return clock_list_.size();
}
