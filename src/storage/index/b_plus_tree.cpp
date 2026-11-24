//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree.cpp
//
// Identification: src/storage/index/b_plus_tree.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/index/b_plus_tree.h"
#include "buffer/traced_buffer_pool_manager.h"
#include "storage/index/b_plus_tree_debug.h"

namespace bustub {

FULL_INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(std::string name, page_id_t header_page_id, BufferPoolManager *buffer_pool_manager,
                          const KeyComparator &comparator, int leaf_max_size, int internal_max_size)
    : bpm_(std::make_shared<TracedBufferPoolManager>(buffer_pool_manager)),
      index_name_(std::move(name)),
      comparator_(std::move(comparator)),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size),
      header_page_id_(header_page_id) {
  WritePageGuard guard = bpm_->WritePage(header_page_id_);
  auto root_page = guard.AsMut<BPlusTreeHeaderPage>();
  root_page->root_page_id_ = INVALID_PAGE_ID;
}

/**
 * @brief Helper function to decide whether current b+tree is empty
 * @return Returns true if this B+ tree has no keys and values.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const -> bool {
  auto guard = bpm_->ReadPage(header_page_id_);
  const auto *header = guard.template As<BPlusTreeHeaderPage>();
  return header->root_page_id_ == INVALID_PAGE_ID;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::ReleaseAncestors(Context &ctx) {
  ctx.header_page_ = std::nullopt;
  while (!ctx.write_set_.empty()) {
    ctx.write_set_.front().Drop();
    ctx.write_set_.pop_front();
  }
}

/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/**
 * @brief Return the only value that associated with input key
 *
 * This method is used for point query
 *
 * @param key input key
 * @param[out] result vector that stores the only value that associated with input key, if the value exists
 * @return : true means key exists
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool {
  auto header_guard = bpm_->ReadPage(header_page_id_);
  page_id_t curr_id = header_guard.template As<BPlusTreeHeaderPage>()->root_page_id_;
  if (curr_id == INVALID_PAGE_ID) {
    return false;
  }

  auto curr_guard = bpm_->ReadPage(curr_id);
  header_guard.Drop();

  while (!curr_guard.template As<BPlusTreePage>()->IsLeafPage()) {
    const auto *internal = curr_guard.template As<InternalPage>();
    page_id_t next_id = internal->Lookup(key, comparator_);
    auto next_guard = bpm_->ReadPage(next_id);
    curr_guard = std::move(next_guard);
  }

  const auto *leaf = curr_guard.template As<LeafPage>();
  ValueType val;
  if (leaf->Lookup(key, &val, comparator_)) {
    result->push_back(val);
    return true;
  }
  return false;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertIntoParent(Context &ctx, page_id_t old_page_id, const KeyType &key, page_id_t new_page_id) {
  page_id_t left_id = old_page_id;
  KeyType split_key = key;
  page_id_t right_id = new_page_id;

  while (true) {
    if (ctx.write_set_.empty()) {
      page_id_t new_root_id = bpm_->NewPage();
      auto new_root_guard = bpm_->WritePage(new_root_id);
      auto *new_root = new_root_guard.template AsMut<InternalPage>();
      new_root->Init(internal_max_size_);
      new_root->SetValueAt(0, left_id);
      new_root->SetKeyAt(1, split_key);
      new_root->SetValueAt(1, right_id);
      new_root->SetSize(2);
      ctx.header_page_->template AsMut<BPlusTreeHeaderPage>()->root_page_id_ = new_root_id;
      return;
    }

    auto parent_guard = std::move(ctx.write_set_.back());
    ctx.write_set_.pop_back();
    auto *parent = parent_guard.template AsMut<InternalPage>();

    if (parent->GetSize() < parent->GetMaxSize()) {
      parent->Insert(split_key, right_id, comparator_);
      return;
    }

    page_id_t new_internal_id = bpm_->NewPage();
    auto new_internal_guard = bpm_->WritePage(new_internal_id);
    auto *new_internal = new_internal_guard.template AsMut<InternalPage>();
    new_internal->Init(internal_max_size_);

    KeyType promoted_key = parent->InsertAndSplit(split_key, right_id, new_internal, comparator_);
    left_id = parent_guard.GetPageId();
    split_key = promoted_key;
    right_id = new_internal_id;
  }
}

/**
 * @brief Insert constant key & value pair into b+ tree
 *
 * if current tree is empty, start new tree, update root page id and insert
 * entry; otherwise, insert into leaf page.
 *
 * @param key the key to insert
 * @param value the value associated with key
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false; otherwise, return true.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value) -> bool {
  // Optimistic pass: read-latch down to leaf parent, write-latch leaf only.
  {
    auto header_guard = bpm_->ReadPage(header_page_id_);
    page_id_t root_id = header_guard.template As<BPlusTreeHeaderPage>()->root_page_id_;
    if (root_id != INVALID_PAGE_ID) {
      auto curr_guard = bpm_->ReadPage(root_id);
      header_guard.Drop();
      if (!curr_guard.template As<BPlusTreePage>()->IsLeafPage()) {
        while (true) {
          page_id_t child_id = curr_guard.template As<InternalPage>()->Lookup(key, comparator_);
          auto child_read = bpm_->ReadPage(child_id);
          if (child_read.template As<BPlusTreePage>()->IsLeafPage()) {
            // Keep parent read-latched while switching child from read to write latch.
            child_read.Drop();
            auto leaf_guard = bpm_->WritePage(child_id);
            curr_guard.Drop();

            auto *leaf = leaf_guard.template AsMut<LeafPage>();
            int idx = leaf->KeyIndex(key, comparator_);
            if (idx < leaf->GetSize() && comparator_(leaf->KeyAt(idx), key) == 0) {
              return leaf->Insert(key, value, comparator_);
            }
            if (leaf->GetSize() + 1 < leaf->GetMaxSize()) {
              return leaf->Insert(key, value, comparator_);
            }
            break;
          }
          curr_guard = std::move(child_read);
        }
      }
    }
  }

  // Pessimistic pass: acquire write latches with latch crabbing.
  Context ctx;
  ctx.header_page_ = bpm_->WritePage(header_page_id_);
  ctx.root_page_id_ = ctx.header_page_->template As<BPlusTreeHeaderPage>()->root_page_id_;

  if (ctx.root_page_id_ == INVALID_PAGE_ID) {
    page_id_t new_root_id = bpm_->NewPage();
    auto root_guard = bpm_->WritePage(new_root_id);
    auto *root_leaf = root_guard.template AsMut<LeafPage>();
    root_leaf->Init(leaf_max_size_);
    root_leaf->Insert(key, value, comparator_);
    ctx.header_page_->template AsMut<BPlusTreeHeaderPage>()->root_page_id_ = new_root_id;
    ctx.header_page_ = std::nullopt;
    return true;
  }

  auto curr_guard = bpm_->WritePage(ctx.root_page_id_);
  while (!curr_guard.template As<BPlusTreePage>()->IsLeafPage()) {
    const auto *internal = curr_guard.template As<InternalPage>();
    if (internal->GetSize() < internal->GetMaxSize()) {
      ReleaseAncestors(ctx);
    }
    page_id_t next_id = internal->Lookup(key, comparator_);
    ctx.write_set_.push_back(std::move(curr_guard));
    curr_guard = bpm_->WritePage(next_id);
  }

  auto *leaf = curr_guard.template AsMut<LeafPage>();
  if (leaf->GetSize() + 1 < leaf->GetMaxSize()) {
    ReleaseAncestors(ctx);
  }

  if (!leaf->Insert(key, value, comparator_)) {
    ReleaseAncestors(ctx);
    return false;
  }

  if (leaf->GetSize() < leaf->GetMaxSize()) {
    ReleaseAncestors(ctx);
    return true;
  }

  // Split leaf node.
  page_id_t new_leaf_id = bpm_->NewPage();
  auto new_leaf_guard = bpm_->WritePage(new_leaf_id);
  auto *new_leaf = new_leaf_guard.template AsMut<LeafPage>();
  new_leaf->Init(leaf_max_size_);
  leaf->MoveHalfTo(new_leaf);
  leaf->SetNextPageId(new_leaf_id);
  KeyType split_key = new_leaf->KeyAt(0);
  new_leaf_guard.Drop();

  page_id_t old_leaf_id = curr_guard.GetPageId();
  InsertIntoParent(ctx, old_leaf_id, split_key, new_leaf_id);
  ReleaseAncestors(ctx);
  return true;
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::HandleInternalUnderflow(Context &ctx, WritePageGuard node_guard) {
  while (true) {
    auto *node = node_guard.template AsMut<InternalPage>();
    if (ctx.write_set_.empty()) {
      if (node->GetSize() == 1) {
        page_id_t new_root_id = node->ValueAt(0);
        page_id_t old_root_id = node_guard.GetPageId();
        ctx.header_page_->template AsMut<BPlusTreeHeaderPage>()->root_page_id_ = new_root_id;
        node_guard.Drop();
        bpm_->DeletePage(old_root_id);
      }
      return;
    }

    if (node->GetSize() >= node->GetMinSize()) {
      return;
    }

    auto &parent_guard = ctx.write_set_.back();
    auto *parent = parent_guard.template AsMut<InternalPage>();
    page_id_t node_id = node_guard.GetPageId();
    int idx = parent->ValueIndex(node_id);

    if (idx > 0) {
      page_id_t left_id = parent->ValueAt(idx - 1);
      node_guard.Drop();
      auto left_guard = bpm_->WritePage(left_id);
      auto right_guard = bpm_->WritePage(node_id);
      auto *left = left_guard.template AsMut<InternalPage>();
      auto *right = right_guard.template AsMut<InternalPage>();

      if (left->GetSize() > left->GetMinSize()) {
        KeyType new_sep = left->KeyAt(left->GetSize() - 1);
        left->MoveLastToFrontOf(right, parent->KeyAt(idx));
        parent->SetKeyAt(idx, new_sep);
        return;
      }

      right->MoveAllTo(left, parent->KeyAt(idx));
      parent->RemoveAt(idx);
      right_guard.Drop();
      bpm_->DeletePage(node_id);
      left_guard.Drop();
    } else {
      page_id_t right_id = parent->ValueAt(1);
      auto right_guard = bpm_->WritePage(right_id);
      auto *left = node;
      auto *right = right_guard.template AsMut<InternalPage>();

      if (right->GetSize() > right->GetMinSize()) {
        KeyType new_sep = right->KeyAt(1);
        right->MoveFirstToEndOf(left, parent->KeyAt(1));
        parent->SetKeyAt(1, new_sep);
        return;
      }

      right->MoveAllTo(left, parent->KeyAt(1));
      parent->RemoveAt(1);
      right_guard.Drop();
      bpm_->DeletePage(right_id);
      node_guard.Drop();
    }

    node_guard = std::move(ctx.write_set_.back());
    ctx.write_set_.pop_back();
  }
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::HandleLeafUnderflow(Context &ctx, WritePageGuard leaf_guard) {
  auto &parent_guard = ctx.write_set_.back();
  auto *parent = parent_guard.template AsMut<InternalPage>();
  page_id_t leaf_id = leaf_guard.GetPageId();
  int idx = parent->ValueIndex(leaf_id);
  bool merged = false;

  if (idx > 0) {
    page_id_t left_id = parent->ValueAt(idx - 1);
    // Strict left-to-right latch ordering to prevent deadlock with left-to-right iterators.
    leaf_guard.Drop();
    auto left_guard = bpm_->WritePage(left_id);
    auto right_guard = bpm_->WritePage(leaf_id);
    auto *left = left_guard.template AsMut<LeafPage>();
    auto *right = right_guard.template AsMut<LeafPage>();

    while (right->GetSize() < right->GetMinSize()) {
      if (left->GetSize() > left->GetMinSize()) {
        left->MoveLastToFrontOf(right);
        parent->SetKeyAt(idx, right->KeyAt(0));
      } else {
        right->MoveAllTo(left);
        parent->RemoveAt(idx);
        right_guard.Drop();
        bpm_->DeletePage(leaf_id);
        left_guard.Drop();
        merged = true;
        break;
      }
    }
  } else {
    page_id_t right_id = parent->ValueAt(1);
    auto right_guard = bpm_->WritePage(right_id);
    auto *left = leaf_guard.template AsMut<LeafPage>();
    auto *right = right_guard.template AsMut<LeafPage>();

    while (left->GetSize() < left->GetMinSize()) {
      if (right->GetSize() > right->GetMinSize()) {
        right->MoveFirstToEndOf(left);
        parent->SetKeyAt(1, right->KeyAt(0));
      } else {
        right->MoveAllTo(left);
        parent->RemoveAt(1);
        right_guard.Drop();
        bpm_->DeletePage(right_id);
        leaf_guard.Drop();
        merged = true;
        break;
      }
    }
  }

  if (merged) {
    auto next_guard = std::move(ctx.write_set_.back());
    ctx.write_set_.pop_back();
    HandleInternalUnderflow(ctx, std::move(next_guard));
  }
}

/**
 * @brief Delete key & value pair associated with input key
 * If current tree is empty, return immediately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 *
 * @param key input key
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key) {
  // Optimistic pass: read-latch down to leaf parent, write-latch leaf only.
  {
    auto header_guard = bpm_->ReadPage(header_page_id_);
    page_id_t root_id = header_guard.template As<BPlusTreeHeaderPage>()->root_page_id_;
    if (root_id == INVALID_PAGE_ID) {
      return;
    }
    auto curr_guard = bpm_->ReadPage(root_id);
    header_guard.Drop();
    if (!curr_guard.template As<BPlusTreePage>()->IsLeafPage()) {
      while (true) {
        page_id_t child_id = curr_guard.template As<InternalPage>()->Lookup(key, comparator_);
        auto child_read = bpm_->ReadPage(child_id);
        if (child_read.template As<BPlusTreePage>()->IsLeafPage()) {
          child_read.Drop();
          auto leaf_guard = bpm_->WritePage(child_id);
          curr_guard.Drop();

          auto *leaf = leaf_guard.template AsMut<LeafPage>();
          int idx = leaf->KeyIndex(key, comparator_);
          if (idx >= leaf->GetSize() || comparator_(leaf->KeyAt(idx), key) != 0 || leaf->IsTombstoned(idx)) {
            return;
          }
          if (leaf->GetSize() > leaf->GetMinSize() ||
              leaf->GetNumTombstones() < static_cast<size_t>(LEAF_PAGE_TOMB_CNT)) {
            leaf->Remove(key, comparator_);
            return;
          }
          break;
        }
        curr_guard = std::move(child_read);
      }
    }
  }

  // Pessimistic pass: acquire write latches with latch crabbing.
  Context ctx;
  ctx.header_page_ = bpm_->WritePage(header_page_id_);
  ctx.root_page_id_ = ctx.header_page_->template As<BPlusTreeHeaderPage>()->root_page_id_;

  if (ctx.root_page_id_ == INVALID_PAGE_ID) {
    return;
  }

  auto curr_guard = bpm_->WritePage(ctx.root_page_id_);
  while (!curr_guard.template As<BPlusTreePage>()->IsLeafPage()) {
    const auto *internal = curr_guard.template As<InternalPage>();
    bool is_root = ctx.IsRootPage(curr_guard.GetPageId());
    bool safe = is_root ? (internal->GetSize() > 2) : (internal->GetSize() > internal->GetMinSize());
    if (safe) {
      ReleaseAncestors(ctx);
    }
    page_id_t next_id = internal->Lookup(key, comparator_);
    ctx.write_set_.push_back(std::move(curr_guard));
    curr_guard = bpm_->WritePage(next_id);
  }

  auto *leaf = curr_guard.template AsMut<LeafPage>();
  bool is_root = ctx.IsRootPage(curr_guard.GetPageId());
  bool safe = is_root ? (leaf->GetSize() > 1 || LEAF_PAGE_TOMB_CNT > 0)
                      : (leaf->GetSize() > leaf->GetMinSize() ||
                         leaf->GetNumTombstones() < static_cast<size_t>(LEAF_PAGE_TOMB_CNT));
  if (safe) {
    ReleaseAncestors(ctx);
  }

  bool physically_removed = leaf->Remove(key, comparator_);
  if (!physically_removed) {
    ReleaseAncestors(ctx);
    return;
  }

  if (is_root) {
    if (leaf->GetSize() == 0) {
      page_id_t old_root_id = curr_guard.GetPageId();
      ctx.header_page_->template AsMut<BPlusTreeHeaderPage>()->root_page_id_ = INVALID_PAGE_ID;
      curr_guard.Drop();
      bpm_->DeletePage(old_root_id);
    }
    ReleaseAncestors(ctx);
    return;
  }

  if (leaf->GetSize() >= leaf->GetMinSize()) {
    ReleaseAncestors(ctx);
    return;
  }

  HandleLeafUnderflow(ctx, std::move(curr_guard));
  ReleaseAncestors(ctx);
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/
/**
 * @brief Input parameter is void, find the leftmost leaf page first, then construct
 * index iterator
 *
 * You may want to implement this while implementing Task #3.
 *
 * @return : index iterator
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE {
  auto header_guard = bpm_->ReadPage(header_page_id_);
  page_id_t curr_id = header_guard.template As<BPlusTreeHeaderPage>()->root_page_id_;
  if (curr_id == INVALID_PAGE_ID) {
    return End();
  }

  auto curr_guard = bpm_->ReadPage(curr_id);
  header_guard.Drop();

  while (!curr_guard.template As<BPlusTreePage>()->IsLeafPage()) {
    const auto *internal = curr_guard.template As<InternalPage>();
    page_id_t next_id = internal->ValueAt(0);
    auto next_guard = bpm_->ReadPage(next_id);
    curr_guard = std::move(next_guard);
  }

  return INDEXITERATOR_TYPE(bpm_, std::move(curr_guard), 0);
}

/**
 * @brief Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE {
  auto header_guard = bpm_->ReadPage(header_page_id_);
  page_id_t curr_id = header_guard.template As<BPlusTreeHeaderPage>()->root_page_id_;
  if (curr_id == INVALID_PAGE_ID) {
    return End();
  }

  auto curr_guard = bpm_->ReadPage(curr_id);
  header_guard.Drop();

  while (!curr_guard.template As<BPlusTreePage>()->IsLeafPage()) {
    const auto *internal = curr_guard.template As<InternalPage>();
    page_id_t next_id = internal->Lookup(key, comparator_);
    auto next_guard = bpm_->ReadPage(next_id);
    curr_guard = std::move(next_guard);
  }

  const auto *leaf = curr_guard.template As<LeafPage>();
  int idx = leaf->KeyIndex(key, comparator_);
  return INDEXITERATOR_TYPE(bpm_, std::move(curr_guard), idx);
}

/**
 * @brief Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE { return INDEXITERATOR_TYPE(); }

/**
 * @return Page id of the root of this tree
 *
 * You may want to implement this while implementing Task #3.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRootPageId() -> page_id_t {
  auto guard = bpm_->ReadPage(header_page_id_);
  return guard.template As<BPlusTreeHeaderPage>()->root_page_id_;
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 3>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 2>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 1>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, -1>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
