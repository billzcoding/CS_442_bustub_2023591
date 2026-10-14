//2023591 Raja Bilal
#include "buffer/arc_replacer.h"
#include <algorithm>

namespace bustub {

ArcReplacer::ArcReplacer(size_t numframes) : replacer_size_(numframes) {}

auto ArcReplacer::Size() -> size_t {
  std::lock_guard<std::mutex> lock(latch_);
  return curr_size_;
}

void ArcReplacer::SetEvictable(frame_id_t frameid, bool setevictable) {
  std::lock_guard<std::mutex> lock(latch_);
  if (alive_map_.count(frameid)) {
    if (alive_map_[frameid]->evictable_ != setevictable) {
      alive_map_[frameid]->evictable_ = setevictable;
      curr_size_ += setevictable ? 1 : -1;
    }
  }
}

void ArcReplacer::RecordAccess(frame_id_t frameid, page_id_t pageid, AccessType accesstype) {
  std::lock_guard<std::mutex> lock(latch_);

  // Move an active cache hit to the front of the MFU list.
  if (alive_map_.count(frameid)) {
    auto status = alive_map_[frameid];
    if (status->arc_status_ == ArcStatus::MRU) {
      mru_.remove(frameid);
    } else if (status->arc_status_ == ArcStatus::MFU) {
      mfu_.remove(frameid);
    }
    mfu_.push_front(frameid);
    status->arc_status_ = ArcStatus::MFU;
    return;
  }

  // An MRU ghost hit increases the target size for the MRU list.
  if (ghost_map_.count(pageid) && ghost_map_[pageid]->arc_status_ == ArcStatus::MRU_GHOST) {
    size_t mrug = std::max<size_t>(1, mru_ghost_.size());
    size_t delta = (mru_ghost_.size() >= mfu_ghost_.size()) ? 1 : mfu_ghost_.size() / mrug;
    mru_target_size_ = std::min(replacer_size_, mru_target_size_ + delta);

    mru_ghost_.remove(pageid);
    ghost_map_.erase(pageid);

    mfu_.push_front(frameid);
    alive_map_[frameid] = std::make_shared<FrameStatus>(pageid, frameid, false, ArcStatus::MFU);
    return;
  }

  // An MFU ghost hit decreases the target size for the MRU list.
  if (ghost_map_.count(pageid) && ghost_map_[pageid]->arc_status_ == ArcStatus::MFU_GHOST) {
    size_t mfug = std::max<size_t>(1, mfu_ghost_.size());
    size_t delta = (mfu_ghost_.size() >= mru_ghost_.size()) ? 1 : mru_ghost_.size() / mfug;
    mru_target_size_ = (mru_target_size_ >= delta) ? mru_target_size_ - delta : 0;

    mfu_ghost_.remove(pageid);
    ghost_map_.erase(pageid);

    mfu_.push_front(frameid);
    alive_map_[frameid] = std::make_shared<FrameStatus>(pageid, frameid, false, ArcStatus::MFU);
    return;
  }

  // On a miss, make room in the ghost lists before adding the frame to MRU.
  if (mru_.size() + mru_ghost_.size() == replacer_size_) {
    if (!mru_ghost_.empty()) {
      page_id_t evictpage = mru_ghost_.back();
      mru_ghost_.pop_back();
      ghost_map_.erase(evictpage);
    }
  } else if (mru_.size() + mru_ghost_.size() < replacer_size_) {
    if (mru_.size() + mru_ghost_.size() + mfu_.size() + mfu_ghost_.size() >= 2 * replacer_size_) {
      if (!mfu_ghost_.empty()) {
        page_id_t evictpage = mfu_ghost_.back();
        mfu_ghost_.pop_back();
        ghost_map_.erase(evictpage);
      }
    }
  }

  mru_.push_front(frameid);
  alive_map_[frameid] = std::make_shared<FrameStatus>(pageid, frameid, false, ArcStatus::MRU);
}

auto ArcReplacer::Evict() -> std::optional<frame_id_t> {
  std::lock_guard<std::mutex> lock(latch_);
  if (curr_size_ == 0) return std::nullopt;

  std::optional<frame_id_t> target = std::nullopt;

  auto tryevict = [&](std::list<frame_id_t> &list, std::list<page_id_t> &ghostlist, ArcStatus ghoststatus) -> bool {
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
      if (alive_map_[*it]->evictable_) {
        target = *it;
        page_id_t pid = alive_map_[*target]->page_id_;

        ghostlist.push_front(pid);
        ghost_map_[pid] = std::make_shared<FrameStatus>(pid, *target, false, ghoststatus);

        alive_map_.erase(*target);
        list.erase(std::next(it).base());
        curr_size_--;
        return true;
      }
    }
    return false;
  };

  if (mru_.size() >= mru_target_size_ && !mru_.empty()) {
    if (!tryevict(mru_, mru_ghost_, ArcStatus::MRU_GHOST)) {
      tryevict(mfu_, mfu_ghost_, ArcStatus::MFU_GHOST);
    }
  } else {
    if (!tryevict(mfu_, mfu_ghost_, ArcStatus::MFU_GHOST)) {
      tryevict(mru_, mru_ghost_, ArcStatus::MRU_GHOST);
    }
  }

  return target;
}

void ArcReplacer::Remove(frame_id_t frameid) {
  std::lock_guard<std::mutex> lock(latch_);
  if (alive_map_.count(frameid)) {
    if (!alive_map_[frameid]->evictable_) {
      return;
    }
    if (alive_map_[frameid]->arc_status_ == ArcStatus::MRU) {
      mru_.remove(frameid);
    } else {
      mfu_.remove(frameid);
    }
    alive_map_.erase(frameid);
    curr_size_--;
  }
}

}  // End namespace bustub.