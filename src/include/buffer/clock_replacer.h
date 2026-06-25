#ifndef MINISQL_CLOCK_REPLACER_H
#define MINISQL_CLOCK_REPLACER_H

#include <list>
#include <mutex>
#include <unordered_map>

#include "buffer/replacer.h"
#include "common/config.h"

/**
 * CLOCKReplacer implements the clock replacement policy.
 */
class CLOCKReplacer : public Replacer {
 public:
  /**
   * Create a new CLOCKReplacer.
   * @param num_pages the maximum number of pages the CLOCKReplacer will be required to store
   */
  explicit CLOCKReplacer(size_t num_pages);

  /**
   * Destroys the CLOCKReplacer.
   */
  ~CLOCKReplacer() override;

  bool Victim(frame_id_t *frame_id) override;

  void Pin(frame_id_t frame_id) override;

  void Unpin(frame_id_t frame_id) override;

  size_t Size() override;

 private:
  using ClockList = std::list<frame_id_t>;

  size_t capacity_;
  ClockList clock_list_;
  ClockList::iterator clock_hand_;
  std::unordered_map<frame_id_t, ClockList::iterator> frame_table_;
  std::unordered_map<frame_id_t, bool> reference_bits_;
  std::mutex latch_;
};

#endif  // MINISQL_CLOCK_REPLACER_H
