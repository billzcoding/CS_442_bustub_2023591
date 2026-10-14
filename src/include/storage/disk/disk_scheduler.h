//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// disk_scheduler.h
//
// Identification: src/include/storage/disk/disk_scheduler.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <future>  // NOLINT
#include <optional>
#include <thread>  // NOLINT
#include <vector>
#include <memory>  // Added for std::shared_ptr

#include "common/channel.h"
#include "storage/disk/disk_manager.h"

namespace bustub {

/**
 * @brief Represents a Write or Read request for the DiskManager to execute.
 */
struct DiskRequest {
  /** Flag indicating whether the request is a write or a read. */
  bool is_write_;

  /**
   *  Pointer to the start of the memory location where a page is either:
   *   1. being read into from disk (on a read).
   *   2. being written out to disk (on a write).
   */
  char *data_;

  /** ID of the page being read from / written to disk. */
  page_id_t page_id_;

  /** Callback used to signal to the request issuer when the request has been completed. */
  std::promise<bool> callback_;
};

/**
 * @brief The DiskScheduler schedules disk read and write operations.
 *
 * A request is scheduled by calling DiskScheduler::Schedule() with an appropriate DiskRequest object. The scheduler
 * maintains background worker threads that process the scheduled requests using the disk manager. 
 */
class DiskScheduler {
 public:
  explicit DiskScheduler(DiskManager *disk_manager);
  ~DiskScheduler();

  void Schedule(std::vector<DiskRequest> &requests);

  // Keep this signature intact for the autograder, even if we use WorkerRoutine() internally.
  void StartWorkerThread();

  using DiskSchedulerPromise = std::promise<bool>;

  /**
   * @brief Create a Promise object.
   *
   * @return std::promise<bool>
   */
  auto CreatePromise() -> DiskSchedulerPromise { return {}; };

  /**
   * @brief Deallocates a page on disk.
   *
   * Note: You should look at the documentation for `DeletePage` in `BufferPoolManager` before using this method.
   *
   * @param page_id The page ID of the page to deallocate from disk.
   */
  void DeallocatePage(page_id_t page_id) { disk_manager_->DeletePage(page_id); }

 private:
  /** Number of parallel I/O workers. Tune this based on your system. */
  static constexpr size_t NUM_WORKERS = 4;

  /** Pointer to the disk manager. */
  DiskManager *disk_manager_ __attribute__((__unused__));
  
  /** 
   * A sharded array of shared queues to concurrently schedule and process requests.
   * We hash the page_id to route requests to a specific channel. This ensures 
   * Read-After-Write consistency for the same page, while allowing parallel I/O for different pages.
   */
  std::vector<std::shared_ptr<Channel<std::optional<DiskRequest>>>> request_queues_;
  
  /** The pool of background threads responsible for issuing scheduled requests to the disk manager. */
  std::vector<std::thread> background_threads_;

  /** 
   * Helper function executed by each worker thread to process its dedicated queue. 
   */
  void WorkerRoutine(size_t worker_id);
};
}  // namespace bustub