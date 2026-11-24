module;

#include <atomic>
#include <barrier>
#include <cstdint>
#include <functional>
#include <thread>
#include <utility>
#include <vector>

export module arche:concurrency.detail.thread_pool_impl;

import :thread_pool;

export namespace arche {

// =============================================================================
// ThreadPool Implementation
// =============================================================================

ThreadPool::ThreadPool(uint32_t thread_count)
    : worker_count_(thread_count > 0 ? thread_count : Hardware::core_count()),
      start_barrier_(worker_count_ + 1),
      finish_barrier_(worker_count_ + 1) {
  workers_.reserve(worker_count_);
  for (uint32_t i = 0; i < worker_count_; ++i) {
    workers_.emplace_back([this, i]() {
      while (true) {
        start_barrier_.arrive_and_wait();
        if (stop_.load(std::memory_order_relaxed)) {
          break;
        }
        if (current_task_) {
          current_task_(i);
        }
        finish_barrier_.arrive_and_wait();
      }
    });
  }
}

ThreadPool::~ThreadPool() {
  stop_.store(true, std::memory_order_relaxed);
  start_barrier_.arrive_and_wait();
  for (auto &w : workers_) {
    if (w.joinable()) {
      w.join();
    }
  }
}

[[nodiscard]] uint32_t ThreadPool::worker_count() const noexcept {
  return worker_count_;
}

} // namespace arche
