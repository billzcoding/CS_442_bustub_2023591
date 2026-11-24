//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_iterator.h
//
// Identification: src/include/storage/index/index_iterator.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

/**
 * index_iterator.h
 * For range scan of b+ tree
 */
#pragma once
#include <memory>
#include <optional>
#include <utility>
#include "buffer/traced_buffer_pool_manager.h"
#include "common/config.h"
#include "common/macros.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/page_guard.h"

namespace bustub {

#define INDEXITERATOR_TYPE IndexIterator<KeyType, ValueType, KeyComparator, NumTombs>
#define SHORT_INDEXITERATOR_TYPE IndexIterator<KeyType, ValueType, KeyComparator>

FULL_INDEX_TEMPLATE_ARGUMENTS_DEFN
class IndexIterator {
  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator, NumTombs>;

 public:
  // you may define your own constructor based on your member variables
  IndexIterator();
  IndexIterator(std::shared_ptr<TracedBufferPoolManager> bpm, ReadPageGuard &&guard, int index);
  IndexIterator(IndexIterator &&other) noexcept = default;
  auto operator=(IndexIterator &&other) noexcept -> IndexIterator & = default;
  ~IndexIterator();  // NOLINT

  auto IsEnd() -> bool;

  auto operator*() -> std::pair<const KeyType &, const ValueType &>;

  auto operator++() -> IndexIterator &;

  auto operator==(const IndexIterator &itr) const -> bool { return page_id_ == itr.page_id_ && index_ == itr.index_; }

  auto operator!=(const IndexIterator &itr) const -> bool { return !(*this == itr); }

 private:
  void AdvanceToValid();

  std::shared_ptr<TracedBufferPoolManager> bpm_{nullptr};
  std::optional<ReadPageGuard> guard_{std::nullopt};
  page_id_t page_id_{INVALID_PAGE_ID};
  int index_{0};
};

}  // namespace bustub
