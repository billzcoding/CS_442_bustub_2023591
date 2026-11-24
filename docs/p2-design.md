# Project 2 Design Document

**Name:** Raja Bilal Khurram

**Andrew ID / Email:** 2023591

## Tombstones

Each `BPlusTreeLeafPage` maintains a fixed-size tombstone buffer `tombstones_[LEAF_PAGE_TOMB_CNT]` together with `num_tombstones_`, storing physical array indices into `key_array_` and `rid_array_` ordered from oldest deletion at index `0` to most recent deletion at `num_tombstones_ - 1`. All tombstone paths are guarded with `if constexpr (LEAF_PAGE_TOMB_CNT > 0)` so zero-tombstone trees incur no overhead.

On `Remove`, if the key is absent or already tombstoned, the operation is a no-op. Otherwise, when `num_tombstones_ < LEAF_PAGE_TOMB_CNT`, the key's index is appended to `tombstones_` without altering `size_`. Only when the buffer is full (`num_tombstones_ == LEAF_PAGE_TOMB_CNT`) is the oldest tombstoned entry (`tombstones_[0]`) physically removed from `key_array_`/`rid_array_`, shifting later entries left and decrementing every remaining tombstone index greater than the evicted index, before appending the newly deleted index. On `Insert`, reinserting a tombstoned key removes its index from `tombstones_` and overwrites its RID in place without changing `size_`; inserting a new key shifts subsequent entries right and increments affected tombstone indices. During splits, tombstones are partitioned across the left and right leaves while preserving relative recency order. During redistribution or coalescing, incoming tombstones from the donor leaf are treated as more recent than those in the recipient leaf, so the recipient's oldest tombstones are physically evicted first whenever the buffer capacity is exceeded.

## B+Tree Operations (Insertion, Deletion, and Point Search)

Point search (`GetValue`) uses binary search in internal pages (`Lookup`) and leaf pages (`KeyIndex`) and rejects tombstoned entries.

For `Insert`, leaves split when `size_` reaches `max_size_` after inserting a new key (`left_size = total / 2`, `right_size = total - left_size`), keeping all array accesses strictly within `LEAF_PAGE_SLOT_CNT`. Internal pages split via `InsertAndSplit` when `size_ == max_size_` before insertion, partitioning the virtual `max_size_ + 1` entries into `(max_size_ + 1) / 2` and the remainder and promoting the middle key. When the root splits, a new two-child internal root is allocated and `header_page->root_page_id_` is updated.

For `Remove`, minimum sizes are `max_size_ / 2` for leaves and `(max_size_ + 1) / 2` for internal pages. When a non-root node underflows after a physical deletion, we choose the left sibling if one exists (`idx > 0`) and the right sibling otherwise (`idx == 0`). If the sibling has strictly more than `GetMinSize()` entries, we redistribute (borrowing in a loop for leaves in case borrowing a tombstoned entry triggers a tombstone eviction that keeps `size_` unchanged); otherwise we coalesce by always merging the right node into the left node, deleting the right page, and propagating removal up to the parent. If a root leaf becomes empty (`size_ == 0`), it is deleted and `root_page_id_` becomes `INVALID_PAGE_ID`. If a root internal node shrinks to a single child, that child becomes the new root and the old root page is deleted.

## Index Iterator

`IndexIterator` holds a `std::shared_ptr<TracedBufferPoolManager>`, an `std::optional<ReadPageGuard>` on the current leaf, `page_id_`, and `index_`. `operator*()` returns `std::pair<const KeyType &, const ValueType &>` referencing the pinned leaf data directly. On construction and `operator++()`, `AdvanceToValid()` skips any tombstoned indices and, when reaching the end of a leaf, follows `next_page_id_` using left-to-right read latch coupling: it acquires `ReadPage(next_id)` before move-assigning over the previous `ReadPageGuard` (which releases the prior leaf). Because `Remove` also acquires sibling write latches left-to-right and always merges right into left, leaf-level scans cannot deadlock with concurrent deletions or follow a deleted `next_page_id_`.

## Concurrency Control

All traversals acquire latches strictly top-down from the header page to the leaves and release ancestor latches top-down (`header_page_` first, then `write_set_` front-to-back). `GetValue`, `Begin`, and `Begin(key)` use read latch crabbing.

`Insert` and `Remove` use optimistic latch crabbing first on multi-level trees: they traverse down to the leaf's parent with `ReadPage`, probe the child with `ReadPage` to verify it is a leaf, drop the child read guard while still holding the parent's read guard (preventing any split/merge of the child), acquire `WritePage` on the leaf, and drop the parent's read guard. If the leaf is safe (won't split on insert, or `size_ > min_size` / `num_tombstones_ < LEAF_PAGE_TOMB_CNT` on remove, or the operation is a duplicate/no-op/resurrection), the operation finishes with a single write latch. Otherwise it falls back to pessimistic write latch crabbing from the header page, releasing all held ancestors immediately upon latching a safe node (`size < max_size` for internal insert; `size + 1 < max_size` for leaf insert; `size > min_size` or `size > 2` for internal root on remove; `size > min_size` or spare tombstone slots for leaf remove). When sibling latching is needed during underflow with a left sibling, the right child's write latch is dropped and left-then-right write latches are acquired under the held parent write latch to maintain global left-to-right lock order.

## Leaderboard Optimizations

We did not add leaderboard-specific data structures or custom tombstone encodings beyond keeping `LEAF_PAGE_DEFAULT_TOMB_CNT` at `0` (maximizing leaf fanout and eliminating tombstone checks via `if constexpr`), using binary search in all internal and leaf page lookups/insertions, and implementing optimistic latch crabbing and race-free buffer pool eviction/flushing.

## AI Usage

AI tools were used to assist with understanding the Project 2 specification, designing the B+Tree page, iterator, tombstone, and concurrency-control implementation, auditing and fixing BufferPoolManager concurrency races, and running/verifying the test suite.
