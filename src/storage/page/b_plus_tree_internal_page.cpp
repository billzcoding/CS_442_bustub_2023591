//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_internal_page.cpp
//
// Identification: src/storage/page/b_plus_tree_internal_page.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <sstream>

#include "common/exception.h"
#include "storage/page/b_plus_tree_internal_page.h"

namespace bustub {
/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * @brief Init method after creating a new internal page.
 *
 * Writes the necessary header information to a newly created page,
 * including set page type, set current size, set page id, set parent id and set max page size,
 * must be called after the creation of a new page to make a valid BPlusTreeInternalPage.
 *
 * @param max_size Maximal size of the page
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::INTERNAL_PAGE);
  SetSize(0);
  SetMaxSize(max_size);
}

/**
 * @brief Helper method to get/set the key associated with input "index"(a.k.a
 * array offset).
 *
 * @param index The index of the key to get. Index must be non-zero.
 * @return Key at index
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(int index) const -> KeyType { return key_array_[index]; }

/**
 * @brief Set key at the specified index.
 *
 * @param index The index of the key to set. Index must be non-zero.
 * @param key The new value for key
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) { key_array_[index] = key; }

/**
 * @brief Helper method to get the index of a given value.
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueIndex(const ValueType &value) const -> int {
  for (int i = 0; i < GetSize(); i++) {
    if (page_id_array_[i] == value) {
      return i;
    }
  }
  return -1;
}

/**
 * @brief Helper method to get the value associated with input "index"(a.k.a array
 * offset)
 *
 * @param index The index of the value to get.
 * @return Value at index
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(int index) const -> ValueType { return page_id_array_[index]; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetValueAt(int index, const ValueType &value) { page_id_array_[index] = value; }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::Lookup(const KeyType &key, const KeyComparator &comparator) const -> ValueType {
  int left = 1;
  int right = GetSize();
  while (left < right) {
    int mid = left + (right - left) / 2;
    if (comparator(key_array_[mid], key) <= 0) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }
  return page_id_array_[left - 1];
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Insert(const KeyType &new_key, const ValueType &new_value,
                                            const KeyComparator &comparator) {
  int size = GetSize();
  int left = 1;
  int right = size;
  while (left < right) {
    int mid = left + (right - left) / 2;
    if (comparator(key_array_[mid], new_key) <= 0) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }
  int insert_idx = left;
  for (int i = size; i > insert_idx; i--) {
    key_array_[i] = key_array_[i - 1];
    page_id_array_[i] = page_id_array_[i - 1];
  }
  key_array_[insert_idx] = new_key;
  page_id_array_[insert_idx] = new_value;
  ChangeSizeBy(1);
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::InsertAndSplit(const KeyType &new_key, const ValueType &new_value,
                                                    BPlusTreeInternalPage *recipient, const KeyComparator &comparator)
    -> KeyType {
  int size = GetSize();
  int left = 1;
  int right = size;
  while (left < right) {
    int mid = left + (right - left) / 2;
    if (comparator(key_array_[mid], new_key) <= 0) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }
  int insert_idx = left;

  auto get_key = [&](int v) -> KeyType {
    if (v < insert_idx) {
      return key_array_[v];
    }
    if (v == insert_idx) {
      return new_key;
    }
    return key_array_[v - 1];
  };
  auto get_val = [&](int v) -> ValueType {
    if (v < insert_idx) {
      return page_id_array_[v];
    }
    if (v == insert_idx) {
      return new_value;
    }
    return page_id_array_[v - 1];
  };

  int total = size + 1;
  int left_size = total / 2;
  int right_size = total - left_size;

  KeyType promoted_key = get_key(left_size);

  recipient->page_id_array_[0] = get_val(left_size);
  for (int i = 1; i < right_size; i++) {
    recipient->key_array_[i] = get_key(left_size + i);
    recipient->page_id_array_[i] = get_val(left_size + i);
  }
  recipient->SetSize(right_size);

  if (insert_idx < left_size) {
    for (int i = left_size - 1; i > insert_idx; i--) {
      key_array_[i] = key_array_[i - 1];
      page_id_array_[i] = page_id_array_[i - 1];
    }
    key_array_[insert_idx] = new_key;
    page_id_array_[insert_idx] = new_value;
  }
  SetSize(left_size);

  return promoted_key;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::RemoveAt(int index) {
  int size = GetSize();
  for (int i = index; i < size - 1; i++) {
    key_array_[i] = key_array_[i + 1];
    page_id_array_[i] = page_id_array_[i + 1];
  }
  ChangeSizeBy(-1);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveAllTo(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  int rec_size = recipient->GetSize();
  int my_size = GetSize();
  recipient->key_array_[rec_size] = middle_key;
  recipient->page_id_array_[rec_size] = page_id_array_[0];
  for (int i = 1; i < my_size; i++) {
    recipient->key_array_[rec_size + i] = key_array_[i];
    recipient->page_id_array_[rec_size + i] = page_id_array_[i];
  }
  recipient->ChangeSizeBy(my_size);
  SetSize(0);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveFirstToEndOf(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  int rec_size = recipient->GetSize();
  recipient->key_array_[rec_size] = middle_key;
  recipient->page_id_array_[rec_size] = page_id_array_[0];
  recipient->ChangeSizeBy(1);

  int my_size = GetSize();
  page_id_array_[0] = page_id_array_[1];
  for (int i = 1; i < my_size - 1; i++) {
    key_array_[i] = key_array_[i + 1];
    page_id_array_[i] = page_id_array_[i + 1];
  }
  ChangeSizeBy(-1);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveLastToFrontOf(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  int rec_size = recipient->GetSize();
  for (int i = rec_size; i > 1; i--) {
    recipient->key_array_[i] = recipient->key_array_[i - 1];
    recipient->page_id_array_[i] = recipient->page_id_array_[i - 1];
  }
  recipient->page_id_array_[1] = recipient->page_id_array_[0];
  recipient->key_array_[1] = middle_key;
  recipient->page_id_array_[0] = page_id_array_[GetSize() - 1];
  recipient->ChangeSizeBy(1);
  ChangeSizeBy(-1);
}

// valuetype for internalNode should be page id_t
template class BPlusTreeInternalPage<GenericKey<4>, page_id_t, GenericComparator<4>>;
template class BPlusTreeInternalPage<GenericKey<8>, page_id_t, GenericComparator<8>>;
template class BPlusTreeInternalPage<GenericKey<16>, page_id_t, GenericComparator<16>>;
template class BPlusTreeInternalPage<GenericKey<32>, page_id_t, GenericComparator<32>>;
template class BPlusTreeInternalPage<GenericKey<64>, page_id_t, GenericComparator<64>>;
}  // namespace bustub
