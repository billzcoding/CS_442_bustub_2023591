//2023591 Raja Bilal
#include "storage/page/page_guard.h"

namespace bustub {

ReadPageGuard::ReadPageGuard(ReadPageGuard &&that) noexcept {
  is_valid_ = that.is_valid_;
  page_id_ = that.page_id_;
  frame_ = std::move(that.frame_);
  replacer_ = std::move(that.replacer_);
  bpm_latch_ = std::move(that.bpm_latch_);
  disk_scheduler_ = std::move(that.disk_scheduler_);
  read_lock_ = std::move(that.read_lock_);

  that.is_valid_ = false;
}

auto ReadPageGuard::operator=(ReadPageGuard &&that) noexcept -> ReadPageGuard & {
  if (this != &that) {
    Drop(); // Release resources currently owned by this guard.

    is_valid_ = that.is_valid_;
    page_id_ = that.page_id_;
    frame_ = std::move(that.frame_);
    replacer_ = std::move(that.replacer_);
    bpm_latch_ = std::move(that.bpm_latch_);
    disk_scheduler_ = std::move(that.disk_scheduler_);
    read_lock_ = std::move(that.read_lock_);

    that.is_valid_ = false;
  }
  return *this;
}

void ReadPageGuard::Flush() {
  if (is_valid_ && frame_) {
    auto p = std::promise<bool>();
    auto f = p.get_future();
    std::vector<DiskRequest> req;
    req.push_back(DiskRequest{true, frame_->GetDataMut(), page_id_, std::move(p)});
    disk_scheduler_->Schedule(req);
    f.get();
    frame_->is_dirty_ = false;
  }
}

void ReadPageGuard::Drop() {
  if (is_valid_) {
    read_lock_.reset(); // Release the frame's shared latch.
    
    if (frame_) {
      std::lock_guard<std::mutex> lock(*bpm_latch_);
      if (frame_->pin_count_.fetch_sub(1) == 1) {
        replacer_->SetEvictable(frame_->frame_id_, true);
      }
    }
    is_valid_ = false;
    frame_.reset();
    replacer_.reset();
    bpm_latch_.reset();
    disk_scheduler_.reset();
  }
}

ReadPageGuard::~ReadPageGuard() { Drop(); }

WritePageGuard::WritePageGuard(WritePageGuard &&that) noexcept {
  is_valid_ = that.is_valid_;
  page_id_ = that.page_id_;
  frame_ = std::move(that.frame_);
  replacer_ = std::move(that.replacer_);
  bpm_latch_ = std::move(that.bpm_latch_);
  disk_scheduler_ = std::move(that.disk_scheduler_);
  write_lock_ = std::move(that.write_lock_);

  that.is_valid_ = false;
}

auto WritePageGuard::operator=(WritePageGuard &&that) noexcept -> WritePageGuard & {
  if (this != &that) {
    Drop();

    is_valid_ = that.is_valid_;
    page_id_ = that.page_id_;
    frame_ = std::move(that.frame_);
    replacer_ = std::move(that.replacer_);
    bpm_latch_ = std::move(that.bpm_latch_);
    disk_scheduler_ = std::move(that.disk_scheduler_);
    write_lock_ = std::move(that.write_lock_);

    that.is_valid_ = false;
  }
  return *this;
}

void WritePageGuard::Flush() {
  if (is_valid_ && frame_) {
    auto p = std::promise<bool>();
    auto f = p.get_future();
    std::vector<DiskRequest> req;
    req.push_back(DiskRequest{true, frame_->GetDataMut(), page_id_, std::move(p)});
    disk_scheduler_->Schedule(req);
    f.get();
    frame_->is_dirty_ = false;
  }
}

void WritePageGuard::Drop() {
  if (is_valid_) {
    if (frame_) {
      frame_->is_dirty_ = true; // A write guard may have modified the page.
    }
    
    write_lock_.reset(); // Release the frame's exclusive latch.
    
    if (frame_) {
      std::lock_guard<std::mutex> lock(*bpm_latch_);
      if (frame_->pin_count_.fetch_sub(1) == 1) {
        replacer_->SetEvictable(frame_->frame_id_, true);
      }
    }
    is_valid_ = false;
    frame_.reset();
    replacer_.reset();
    bpm_latch_.reset();
    disk_scheduler_.reset();
  }
}

WritePageGuard::~WritePageGuard() { Drop(); }

auto ReadPageGuard::GetPageId() const -> page_id_t { 
  return page_id_; 
}

auto ReadPageGuard::GetData() const -> const char * { 
  return frame_ ? frame_->GetData() : nullptr; 
}

auto ReadPageGuard::IsDirty() const -> bool { 
  return frame_ ? frame_->is_dirty_ : false; 
}


auto WritePageGuard::GetPageId() const -> page_id_t { 
  return page_id_; 
}

auto WritePageGuard::GetData() const -> const char * { 
  return frame_ ? frame_->GetData() : nullptr; 
}

auto WritePageGuard::GetDataMut() -> char * { 
  return frame_ ? frame_->GetDataMut() : nullptr; 
}

auto WritePageGuard::IsDirty() const -> bool { 
  return frame_ ? frame_->is_dirty_ : false; 
}

}  // End namespace bustub.
