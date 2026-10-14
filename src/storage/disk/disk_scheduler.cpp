//2023591 Raja Bilal

#include "storage/disk/disk_scheduler.h"
#include "common/exception.h"

namespace bustub {

DiskScheduler::DiskScheduler(DiskManager *diskmanager) : disk_manager_(diskmanager) {
  // Create every request queue before starting workers so each worker can safely access its queue.
  for (size_t i = 0; i < NUM_WORKERS; ++i) {
    request_queues_.push_back(std::make_shared<Channel<std::optional<DiskRequest>>>());
  }

  // Start workers only after all request queues are ready.
  for (size_t i = 0; i < NUM_WORKERS; ++i) {
    background_threads_.emplace_back(&DiskScheduler::WorkerRoutine, this, i);
  }
}

DiskScheduler::~DiskScheduler() {
  // Send a shutdown signal to each worker queue.
  for (size_t i = 0; i < NUM_WORKERS; ++i) {
    request_queues_[i]->Put(std::nullopt);
  }
  // Wait for all worker threads to exit.
  for (auto &thread : background_threads_) {
    if (thread.joinable()) { // Join only threads that are still joinable.
      thread.join();
    }
  }
}

void DiskScheduler::Schedule(std::vector<DiskRequest> &requests) {
  for (auto &req : requests) {
    // Route the same page to the same worker to preserve request order.
    size_t workeridx = std::hash<page_id_t>{}(req.page_id_) % NUM_WORKERS;
    // Queue the request; different pages can be processed in parallel.
    request_queues_[workeridx]->Put(std::move(req));
  }
}

void DiskScheduler::StartWorkerThread() {
  // Retained for autograder compatibility; workers are started by the constructor.
}

void DiskScheduler::WorkerRoutine(size_t workerid) {
  std::optional<DiskRequest> req;
  while ((req = request_queues_[workerid]->Get()) != std::nullopt) {
    if (req->is_write_) {
      disk_manager_->WritePage(req->page_id_, req->data_);
    } else {
      disk_manager_->ReadPage(req->page_id_, req->data_);
    }
    // Notify the caller that the disk operation has completed.
    req->callback_.set_value(true);
  }
}

}  // End namespace bustub.