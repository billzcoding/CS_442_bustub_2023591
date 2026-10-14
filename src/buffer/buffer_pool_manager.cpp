//2023591 Raja Bilal

#include "buffer/buffer_pool_manager.h"
#include "common/exception.h"
#include "common/macros.h"

namespace bustub {

  // FrameHeader stores per-frame state and provides access to the frame data.
FrameHeader::FrameHeader(frame_id_t frameid) 
    : frame_id_(frameid), pin_count_(0), is_dirty_(false), data_(BUSTUB_PAGE_SIZE, 0) {}

auto FrameHeader::GetData() const -> const char * { return data_.data(); }
auto FrameHeader::GetDataMut() -> char * { return data_.data(); }

void FrameHeader::Reset() {
  std::fill(data_.begin(), data_.end(), 0);
  pin_count_ = 0;
  is_dirty_ = false;
  page_id_ = INVALID_PAGE_ID;
  is_io_in_progress_ = false;
}

// BufferPoolManager coordinates page frames, disk I/O, and eviction across concurrent callers.
BufferPoolManager::BufferPoolManager(size_t numframes, DiskManager *diskmanager, LogManager *logmanager)
    : num_frames_(numframes), next_page_id_(0), bpm_latch_(std::make_shared<std::mutex>()),
      disk_scheduler_(std::make_shared<DiskScheduler>(diskmanager)), log_manager_(logmanager) {
  
  replacer_ = std::make_shared<ArcReplacer>(numframes);
  for (size_t i = 0; i < numframes; i++) {
    frames_.push_back(std::make_shared<FrameHeader>(i));
    free_frames_.push_back(i);
  }
}

BufferPoolManager::~BufferPoolManager() = default;

auto BufferPoolManager::Size() const -> size_t { return num_frames_; }

auto BufferPoolManager::FindAvailableFrame(frame_id_t *outframeid) -> bool {
  if (!free_frames_.empty()) {
    *outframeid = free_frames_.front();
    free_frames_.pop_front();
    return true;
  }
  auto evictopt = replacer_->Evict();
  if (evictopt) {
    *outframeid = evictopt.value();
    return true;
  }
  return false;
}

auto BufferPoolManager::NewPage() -> page_id_t {
  std::unique_lock<std::mutex> lock(*bpm_latch_);
  frame_id_t frameid;

  // Return an invalid page ID when no frame can be allocated.
  if (!FindAvailableFrame(&frameid)) {
    return INVALID_PAGE_ID;
  }
  
  auto frame = frames_[frameid];

  if (frame->page_id_ != INVALID_PAGE_ID) {
    page_table_.erase(frame->page_id_);
    if (frame->is_dirty_) {
      frame->is_io_in_progress_ = true;
      auto p = std::promise<bool>();
      auto f = p.get_future();
      std::vector<DiskRequest> req;
      req.push_back(DiskRequest{true, frame->GetDataMut(), frame->page_id_, std::move(p)});

      lock.unlock();
      disk_scheduler_->Schedule(req);
      f.get(); // Wait for disk I/O without holding the buffer-pool latch.
      lock.lock();
    }
  }

  page_id_t newpageid = next_page_id_.fetch_add(1);
  frame->Reset();
  frame->page_id_ = newpageid;
  frame->pin_count_ = 0; // Newly allocated pages begin unpinned.

  page_table_[newpageid] = frameid;
  replacer_->RecordAccess(frameid, newpageid, AccessType::Unknown);
  replacer_->SetEvictable(frameid, true); 

  cv_.notify_all();
  return newpageid;
}

auto BufferPoolManager::DeletePage(page_id_t pageid) -> bool {
  std::lock_guard<std::mutex> lock(*bpm_latch_);
  if (page_table_.find(pageid) == page_table_.end()) return true;

  frame_id_t frameid = page_table_[pageid];
  auto frame = frames_[frameid];

  if (frame->pin_count_ > 0 || frame->is_io_in_progress_) return false;

  page_table_.erase(pageid);
  replacer_->Remove(frameid);
  free_frames_.push_back(frameid);

  frame->Reset();
  disk_scheduler_->DeallocatePage(pageid);
  cv_.notify_all();
  return true;
}

auto BufferPoolManager::CheckedReadPage(page_id_t pageid, AccessType accesstype) -> std::optional<ReadPageGuard> {
  std::unique_lock<std::mutex> lock(*bpm_latch_);

  cv_.wait(lock, [&]() {
    if (page_table_.count(pageid)) return !frames_[page_table_[pageid]]->is_io_in_progress_;
    return true;
  });

  if (page_table_.count(pageid)) {
    frame_id_t frameid = page_table_[pageid];
    frames_[frameid]->pin_count_.fetch_add(1);
    replacer_->RecordAccess(frameid, pageid, accesstype);
    replacer_->SetEvictable(frameid, false);

    ReadPageGuard guard;
    guard.page_id_ = pageid;
    guard.frame_ = frames_[frameid];
    guard.replacer_ = replacer_;
    guard.bpm_latch_ = bpm_latch_;
    guard.disk_scheduler_ = disk_scheduler_;

    lock.unlock(); 
    guard.read_lock_.emplace(frames_[frameid]->rwlatch_);
    guard.is_valid_ = true;
    return guard;
  }

  frame_id_t frameid;
  if (!FindAvailableFrame(&frameid)) {
    return std::nullopt; // No frame is available.
  }
  
  auto frame = frames_[frameid];

  if (frame->page_id_ != INVALID_PAGE_ID) {
    page_table_.erase(frame->page_id_);
    if (frame->is_dirty_) {
      frame->is_io_in_progress_ = true;
      auto p = std::promise<bool>();
      auto f = p.get_future();
      std::vector<DiskRequest> req;
      req.push_back(DiskRequest{true, frame->GetDataMut(), frame->page_id_, std::move(p)});

      lock.unlock();
      disk_scheduler_->Schedule(req);
      f.get();
      lock.lock();
    }
  }

  frame->Reset();
  frame->page_id_ = pageid;
  frame->is_io_in_progress_ = true;
  frame->pin_count_ = 1;
  page_table_[pageid] = frameid;

  auto p = std::promise<bool>();
  auto f = p.get_future();
  std::vector<DiskRequest> req;
  req.push_back(DiskRequest{false, frame->GetDataMut(), pageid, std::move(p)});

  lock.unlock();
  disk_scheduler_->Schedule(req);
  f.get();
  lock.lock();

  frame->is_io_in_progress_ = false;
  replacer_->RecordAccess(frameid, pageid, accesstype);
  replacer_->SetEvictable(frameid, false);
  cv_.notify_all();

  ReadPageGuard guard;
  guard.page_id_ = pageid;
  guard.frame_ = frame;
  guard.replacer_ = replacer_;
  guard.bpm_latch_ = bpm_latch_;
  guard.disk_scheduler_ = disk_scheduler_;

  lock.unlock(); 
  guard.read_lock_.emplace(frame->rwlatch_);
  guard.is_valid_ = true;
  return guard;
}

auto BufferPoolManager::CheckedWritePage(page_id_t pageid, AccessType accesstype) -> std::optional<WritePageGuard> {
  std::unique_lock<std::mutex> lock(*bpm_latch_);

  cv_.wait(lock, [&]() {
    if (page_table_.count(pageid)) return !frames_[page_table_[pageid]]->is_io_in_progress_;
    return true;
  });

  if (page_table_.count(pageid)) {
    frame_id_t frameid = page_table_[pageid];
    frames_[frameid]->pin_count_.fetch_add(1);
    replacer_->RecordAccess(frameid, pageid, accesstype);
    replacer_->SetEvictable(frameid, false);

    WritePageGuard guard;
    guard.page_id_ = pageid;
    guard.frame_ = frames_[frameid];
    guard.replacer_ = replacer_;
    guard.bpm_latch_ = bpm_latch_;
    guard.disk_scheduler_ = disk_scheduler_;

    lock.unlock(); 
    guard.write_lock_.emplace(frames_[frameid]->rwlatch_);
    guard.is_valid_ = true;
    return guard;
  }

  frame_id_t frameid;
  if (!FindAvailableFrame(&frameid)) {
    return std::nullopt; // No frame is available.
  }
  
  auto frame = frames_[frameid];

  if (frame->page_id_ != INVALID_PAGE_ID) {
    page_table_.erase(frame->page_id_);
    if (frame->is_dirty_) {
      frame->is_io_in_progress_ = true;
      auto p = std::promise<bool>();
      auto f = p.get_future();
      std::vector<DiskRequest> req;
      req.push_back(DiskRequest{true, frame->GetDataMut(), frame->page_id_, std::move(p)});

      lock.unlock();
      disk_scheduler_->Schedule(req);
      f.get();
      lock.lock();
    }
  }

  frame->Reset();
  frame->page_id_ = pageid;
  frame->is_io_in_progress_ = true;
  frame->pin_count_ = 1;
  page_table_[pageid] = frameid;

  auto p = std::promise<bool>();
  auto f = p.get_future();
  std::vector<DiskRequest> req;
  req.push_back(DiskRequest{false, frame->GetDataMut(), pageid, std::move(p)});

  lock.unlock();
  disk_scheduler_->Schedule(req);
  f.get();
  lock.lock();

  frame->is_io_in_progress_ = false;
  replacer_->RecordAccess(frameid, pageid, accesstype);
  replacer_->SetEvictable(frameid, false);
  cv_.notify_all();

  WritePageGuard guard;
  guard.page_id_ = pageid;
  guard.frame_ = frame;
  guard.replacer_ = replacer_;
  guard.bpm_latch_ = bpm_latch_;
  guard.disk_scheduler_ = disk_scheduler_;

  lock.unlock(); // Release the buffer-pool latch before acquiring the page latch.
  guard.write_lock_.emplace(frame->rwlatch_);
  guard.is_valid_ = true;
  return guard;
}

auto BufferPoolManager::ReadPage(page_id_t pageid, AccessType accesstype) -> ReadPageGuard {
  auto guardopt = CheckedReadPage(pageid, accesstype);
  BUSTUB_ASSERT(guardopt.has_value(), "ReadPage failed to fetch page");
  return std::move(guardopt.value());
}

auto BufferPoolManager::WritePage(page_id_t pageid, AccessType accesstype) -> WritePageGuard {
  auto guardopt = CheckedWritePage(pageid, accesstype);
  BUSTUB_ASSERT(guardopt.has_value(), "WritePage failed to fetch page");
  return std::move(guardopt.value());
}

auto BufferPoolManager::FlushPageUnsafe(page_id_t pageid) -> bool {
  if (page_table_.find(pageid) == page_table_.end()) return false;
  auto frame = frames_[page_table_[pageid]];

  if (frame->is_dirty_) {
    auto p = std::promise<bool>();
    auto f = p.get_future();
    std::vector<DiskRequest> req;
    req.push_back(DiskRequest{true, frame->GetDataMut(), pageid, std::move(p)});
    disk_scheduler_->Schedule(req);
    f.get();
    frame->is_dirty_ = false;
  }
  return true;
}

auto BufferPoolManager::FlushPage(page_id_t pageid) -> bool {
  std::lock_guard<std::mutex> lock(*bpm_latch_);
  return FlushPageUnsafe(pageid);
}

void BufferPoolManager::FlushAllPagesUnsafe() {
  for (const auto &[pageid, frameid] : page_table_) {
    FlushPageUnsafe(pageid);
  }
}

void BufferPoolManager::FlushAllPages() {
  std::lock_guard<std::mutex> lock(*bpm_latch_);
  FlushAllPagesUnsafe();
}

auto BufferPoolManager::GetPinCount(page_id_t pageid) -> std::optional<size_t> {
  std::lock_guard<std::mutex> lock(*bpm_latch_);
  if (page_table_.find(pageid) != page_table_.end()) {
    return frames_[page_table_[pageid]]->pin_count_.load();
  }
  return std::nullopt;
}

}  // End namespace bustub.