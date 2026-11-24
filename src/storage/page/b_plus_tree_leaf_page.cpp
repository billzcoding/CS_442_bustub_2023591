//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_leaf_page.cpp
//
// Identification: src/storage/page/b_plus_tree_leaf_page.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <sstream>

#include "common/exception.h"
#include "common/rid.h"
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {

/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * @brief Init method after creating a new leaf page
 *
 * After creating a new leaf page from buffer pool, must call initialize method to set default values,
 * including set page type, set current size to zero, set page id/parent id, set
 * next page id and set max size.
 *
 * @param max_size Max size of the leaf node
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::LEAF_PAGE);
  SetSize(0);
  SetMaxSize(max_size);
  next_page_id_ = INVALID_PAGE_ID;
  num_tombstones_ = 0;
}

/**
 * @brief Helper function for fetching tombstones of a page.
 * @return The last `NumTombs` keys with pending deletes in this page in order of recency (oldest at front).
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetTombstones() const -> std::vector<KeyType> {
  std::vector<KeyType> result;
  if constexpr (LEAF_PAGE_TOMB_CNT > 0) {
    result.reserve(num_tombstones_);
    for (size_t i = 0; i < num_tombstones_; i++) {
      result.push_back(key_array_[tombstones_[i]]);
    }
  }
  return result;
}

/**
 * Helper methods to set/get next page id
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetNextPageId() const -> page_id_t { return next_page_id_; }

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetNextPageId(page_id_t next_page_id) { next_page_id_ = next_page_id; }

/*
 * Helper method to find and return the key associated with input "index" (a.k.a
 * array offset)
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAt(int index) const -> KeyType { return key_array_[index]; }

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::ValueAt(int index) const -> ValueType { return rid_array_[index]; }

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyRefAt(int index) const -> const KeyType & { return key_array_[index]; }

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::ValueRefAt(int index) const -> const ValueType & { return rid_array_[index]; }

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetNumTombstones() const -> size_t { return num_tombstones_; }

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::IsTombstoned(int index) const -> bool {
  if constexpr (LEAF_PAGE_TOMB_CNT > 0) {
    auto target = static_cast<size_t>(index);
    for (size_t i = 0; i < num_tombstones_; i++) {
      if (tombstones_[i] == target) {
        return true;
      }
    }
  }
  return false;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyIndex(const KeyType &key, const KeyComparator &comparator) const -> int {
  int left = 0;
  int right = GetSize();
  while (left < right) {
    int mid = left + (right - left) / 2;
    if (comparator(key_array_[mid], key) < 0) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }
  return left;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::Lookup(const KeyType &key, ValueType *value, const KeyComparator &comparator) const
    -> bool {
  int idx = KeyIndex(key, comparator);
  if (idx < GetSize() && comparator(key_array_[idx], key) == 0 && !IsTombstoned(idx)) {
    *value = rid_array_[idx];
    return true;
  }
  return false;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::Insert(const KeyType &key, const ValueType &value, const KeyComparator &comparator)
    -> bool {
  int size = GetSize();
  int idx = KeyIndex(key, comparator);
  if (idx < size && comparator(key_array_[idx], key) == 0) {
    if constexpr (LEAF_PAGE_TOMB_CNT > 0) {
      auto target = static_cast<size_t>(idx);
      for (size_t i = 0; i < num_tombstones_; i++) {
        if (tombstones_[i] == target) {
          for (size_t j = i + 1; j < num_tombstones_; j++) {
            tombstones_[j - 1] = tombstones_[j];
          }
          num_tombstones_--;
          rid_array_[idx] = value;
          return true;
        }
      }
    }
    return false;
  }

  for (int i = size; i > idx; i--) {
    key_array_[i] = key_array_[i - 1];
    rid_array_[i] = rid_array_[i - 1];
  }
  key_array_[idx] = key;
  rid_array_[idx] = value;
  ChangeSizeBy(1);

  if constexpr (LEAF_PAGE_TOMB_CNT > 0) {
    auto threshold = static_cast<size_t>(idx);
    for (size_t i = 0; i < num_tombstones_; i++) {
      if (tombstones_[i] >= threshold) {
        tombstones_[i]++;
      }
    }
  }
  return true;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::Remove(const KeyType &key, const KeyComparator &comparator) -> bool {
  int size = GetSize();
  int idx = KeyIndex(key, comparator);
  if (idx >= size || comparator(key_array_[idx], key) != 0 || IsTombstoned(idx)) {
    return false;
  }

  if constexpr (LEAF_PAGE_TOMB_CNT == 0) {
    for (int i = idx; i < size - 1; i++) {
      key_array_[i] = key_array_[i + 1];
      rid_array_[i] = rid_array_[i + 1];
    }
    ChangeSizeBy(-1);
    return true;
  } else {
    if (num_tombstones_ < static_cast<size_t>(LEAF_PAGE_TOMB_CNT)) {
      tombstones_[num_tombstones_++] = static_cast<size_t>(idx);
      return false;
    }
    size_t evict_idx = tombstones_[0];
    for (size_t i = 1; i < num_tombstones_; i++) {
      tombstones_[i - 1] = tombstones_[i];
    }
    tombstones_[num_tombstones_ - 1] = static_cast<size_t>(idx);

    for (int i = static_cast<int>(evict_idx); i < size - 1; i++) {
      key_array_[i] = key_array_[i + 1];
      rid_array_[i] = rid_array_[i + 1];
    }
    ChangeSizeBy(-1);

    for (size_t i = 0; i < num_tombstones_; i++) {
      if (tombstones_[i] > evict_idx) {
        tombstones_[i]--;
      }
    }
    return true;
  }
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveHalfTo(BPlusTreeLeafPage *recipient) {
  int total = GetSize();
  int left_size = total / 2;
  int right_size = total - left_size;

  for (int i = 0; i < right_size; i++) {
    recipient->key_array_[i] = key_array_[left_size + i];
    recipient->rid_array_[i] = rid_array_[left_size + i];
  }

  if constexpr (LEAF_PAGE_TOMB_CNT > 0) {
    size_t left_t = 0;
    size_t right_t = 0;
    auto split_pt = static_cast<size_t>(left_size);
    for (size_t i = 0; i < num_tombstones_; i++) {
      if (tombstones_[i] < split_pt) {
        tombstones_[left_t++] = tombstones_[i];
      } else {
        recipient->tombstones_[right_t++] = tombstones_[i] - split_pt;
      }
    }
    num_tombstones_ = left_t;
    recipient->num_tombstones_ = right_t;
  }

  SetSize(left_size);
  recipient->SetSize(right_size);
  recipient->SetNextPageId(next_page_id_);
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveAllTo(BPlusTreeLeafPage *recipient) {
  int rec_old_size = recipient->GetSize();
  int my_size = GetSize();
  for (int i = 0; i < my_size; i++) {
    recipient->key_array_[rec_old_size + i] = key_array_[i];
    recipient->rid_array_[rec_old_size + i] = rid_array_[i];
  }
  recipient->ChangeSizeBy(my_size);
  recipient->SetNextPageId(next_page_id_);

  if constexpr (LEAF_PAGE_TOMB_CNT > 0) {
    size_t all_tombs[2 * LEAF_PAGE_TOMB_CNT];
    size_t total_t = 0;
    for (size_t i = 0; i < recipient->num_tombstones_; i++) {
      all_tombs[total_t++] = recipient->tombstones_[i];
    }
    auto base = static_cast<size_t>(rec_old_size);
    for (size_t i = 0; i < num_tombstones_; i++) {
      all_tombs[total_t++] = base + tombstones_[i];
    }

    if (total_t <= static_cast<size_t>(LEAF_PAGE_TOMB_CNT)) {
      for (size_t i = 0; i < total_t; i++) {
        recipient->tombstones_[i] = all_tombs[i];
      }
      recipient->num_tombstones_ = total_t;
    } else {
      size_t evict_cnt = total_t - static_cast<size_t>(LEAF_PAGE_TOMB_CNT);
      for (size_t e = 0; e < evict_cnt; e++) {
        size_t del_idx = all_tombs[e];
        int cur_size = recipient->GetSize();
        for (int i = static_cast<int>(del_idx); i < cur_size - 1; i++) {
          recipient->key_array_[i] = recipient->key_array_[i + 1];
          recipient->rid_array_[i] = recipient->rid_array_[i + 1];
        }
        recipient->ChangeSizeBy(-1);
        for (size_t j = e + 1; j < total_t; j++) {
          if (all_tombs[j] > del_idx) {
            all_tombs[j]--;
          }
        }
      }
      for (size_t i = 0; i < static_cast<size_t>(LEAF_PAGE_TOMB_CNT); i++) {
        recipient->tombstones_[i] = all_tombs[evict_cnt + i];
      }
      recipient->num_tombstones_ = static_cast<size_t>(LEAF_PAGE_TOMB_CNT);
    }
  }

  SetSize(0);
  num_tombstones_ = 0;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveFirstToEndOf(BPlusTreeLeafPage *recipient) {
  bool was_tomb = false;
  if constexpr (LEAF_PAGE_TOMB_CNT > 0) {
    size_t write_i = 0;
    for (size_t i = 0; i < num_tombstones_; i++) {
      if (tombstones_[i] == 0) {
        was_tomb = true;
      } else {
        tombstones_[write_i++] = tombstones_[i] - 1;
      }
    }
    num_tombstones_ = write_i;
  }

  int rec_pos = recipient->GetSize();
  recipient->key_array_[rec_pos] = key_array_[0];
  recipient->rid_array_[rec_pos] = rid_array_[0];
  recipient->ChangeSizeBy(1);

  int my_size = GetSize();
  for (int i = 0; i < my_size - 1; i++) {
    key_array_[i] = key_array_[i + 1];
    rid_array_[i] = rid_array_[i + 1];
  }
  ChangeSizeBy(-1);

  if constexpr (LEAF_PAGE_TOMB_CNT > 0) {
    if (was_tomb) {
      if (recipient->num_tombstones_ < static_cast<size_t>(LEAF_PAGE_TOMB_CNT)) {
        recipient->tombstones_[recipient->num_tombstones_++] = static_cast<size_t>(rec_pos);
      } else {
        size_t evict_idx = recipient->tombstones_[0];
        for (size_t i = 1; i < recipient->num_tombstones_; i++) {
          recipient->tombstones_[i - 1] = recipient->tombstones_[i];
        }
        recipient->tombstones_[recipient->num_tombstones_ - 1] = static_cast<size_t>(rec_pos);

        int rec_size = recipient->GetSize();
        for (int i = static_cast<int>(evict_idx); i < rec_size - 1; i++) {
          recipient->key_array_[i] = recipient->key_array_[i + 1];
          recipient->rid_array_[i] = recipient->rid_array_[i + 1];
        }
        recipient->ChangeSizeBy(-1);

        for (size_t i = 0; i < recipient->num_tombstones_; i++) {
          if (recipient->tombstones_[i] > evict_idx) {
            recipient->tombstones_[i]--;
          }
        }
      }
    }
  }
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveLastToFrontOf(BPlusTreeLeafPage *recipient) {
  int my_size = GetSize();
  int last_idx = my_size - 1;
  bool was_tomb = false;
  if constexpr (LEAF_PAGE_TOMB_CNT > 0) {
    size_t write_i = 0;
    auto target = static_cast<size_t>(last_idx);
    for (size_t i = 0; i < num_tombstones_; i++) {
      if (tombstones_[i] == target) {
        was_tomb = true;
      } else {
        tombstones_[write_i++] = tombstones_[i];
      }
    }
    num_tombstones_ = write_i;
  }

  int rec_size = recipient->GetSize();
  for (int i = rec_size; i > 0; i--) {
    recipient->key_array_[i] = recipient->key_array_[i - 1];
    recipient->rid_array_[i] = recipient->rid_array_[i - 1];
  }
  recipient->key_array_[0] = key_array_[last_idx];
  recipient->rid_array_[0] = rid_array_[last_idx];
  recipient->ChangeSizeBy(1);
  ChangeSizeBy(-1);

  if constexpr (LEAF_PAGE_TOMB_CNT > 0) {
    for (size_t i = 0; i < recipient->num_tombstones_; i++) {
      recipient->tombstones_[i]++;
    }
    if (was_tomb) {
      if (recipient->num_tombstones_ < static_cast<size_t>(LEAF_PAGE_TOMB_CNT)) {
        recipient->tombstones_[recipient->num_tombstones_++] = 0;
      } else {
        size_t evict_idx = recipient->tombstones_[0];
        for (size_t i = 1; i < recipient->num_tombstones_; i++) {
          recipient->tombstones_[i - 1] = recipient->tombstones_[i];
        }
        recipient->tombstones_[recipient->num_tombstones_ - 1] = 0;

        int new_rec_size = recipient->GetSize();
        for (int i = static_cast<int>(evict_idx); i < new_rec_size - 1; i++) {
          recipient->key_array_[i] = recipient->key_array_[i + 1];
          recipient->rid_array_[i] = recipient->rid_array_[i + 1];
        }
        recipient->ChangeSizeBy(-1);

        for (size_t i = 0; i < recipient->num_tombstones_; i++) {
          if (recipient->tombstones_[i] > evict_idx) {
            recipient->tombstones_[i]--;
          }
        }
      }
    }
  }
}

template class BPlusTreeLeafPage<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>, 3>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>, 2>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>, 1>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>, -1>;

template class BPlusTreeLeafPage<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTreeLeafPage<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTreeLeafPage<GenericKey<64>, RID, GenericComparator<64>>;
}  // namespace bustub
