//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_iterator.cpp
//
// Identification: src/storage/index/index_iterator.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

/**
 * index_iterator.cpp
 */
#include <cassert>

#include "storage/index/index_iterator.h"

namespace bustub {

/**
 * @note you can change the destructor/constructor method here
 * set your own input parameters
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::IndexIterator() = default;

FULL_INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::IndexIterator(std::shared_ptr<TracedBufferPoolManager> bpm, ReadPageGuard &&guard, int index)
    : bpm_(std::move(bpm)), guard_(std::move(guard)), page_id_(guard_->GetPageId()), index_(index) {
  AdvanceToValid();
}

FULL_INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::~IndexIterator() = default;  // NOLINT

FULL_INDEX_TEMPLATE_ARGUMENTS
void INDEXITERATOR_TYPE::AdvanceToValid() {
  while (guard_.has_value()) {
    const auto *leaf = guard_->template As<LeafPage>();
    while (index_ < leaf->GetSize() && leaf->IsTombstoned(index_)) {
      index_++;
    }
    if (index_ < leaf->GetSize()) {
      return;
    }
    page_id_t next_id = leaf->GetNextPageId();
    if (next_id == INVALID_PAGE_ID) {
      guard_ = std::nullopt;
      page_id_ = INVALID_PAGE_ID;
      index_ = 0;
      return;
    }
    auto next_guard = bpm_->ReadPage(next_id);
    guard_ = std::move(next_guard);
    page_id_ = next_id;
    index_ = 0;
  }
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::IsEnd() -> bool { return page_id_ == INVALID_PAGE_ID; }

FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator*() -> std::pair<const KeyType &, const ValueType &> {
  const auto *leaf = guard_->template As<LeafPage>();
  return {leaf->KeyRefAt(index_), leaf->ValueRefAt(index_)};
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator++() -> INDEXITERATOR_TYPE & {
  if (!IsEnd()) {
    index_++;
    AdvanceToValid();
  }
  return *this;
}

template class IndexIterator<GenericKey<4>, RID, GenericComparator<4>>;

template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, 3>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, 2>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, 1>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, -1>;

template class IndexIterator<GenericKey<16>, RID, GenericComparator<16>>;

template class IndexIterator<GenericKey<32>, RID, GenericComparator<32>>;

template class IndexIterator<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
