#pragma once

#include <atomic>
#include <barrier>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace arche {

// =============================================================================
// Lightweight Lock-Free Worker Thread Pool
// =============================================================================

/**
 * @class ThreadPool
 * @brief Zero-dependency C++20 thread pool designed for chunk work-stealing.
 */
class ThreadPool {
private:
  std::vector<std::thread> workers_;
  std::function<void(uint32_t)> current_task_{nullptr};
  std::atomic<bool> stop_{false};
  uint32_t worker_count_{0};

  std::barrier<> start_barrier_;
  std::barrier<> finish_barrier_;

public:
  explicit ThreadPool(
      uint32_t thread_count = std::thread::hardware_concurrency()
  )
      : worker_count_(thread_count > 0 ? thread_count : 1),
        start_barrier_(worker_count_ + 1), finish_barrier_(worker_count_ + 1) {
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

  ~ThreadPool() {
    stop_.store(true, std::memory_order_relaxed);
    start_barrier_.arrive_and_wait();
    for (auto &w : workers_) {
      if (w.joinable()) {
        w.join();
      }
    }
  }

  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;

  [[nodiscard]] inline uint32_t worker_count() const noexcept {
    return worker_count_;
  }

  template <typename Task> inline void parallel_for(Task &&task) {
    current_task_ = task;
    start_barrier_.arrive_and_wait();
    finish_barrier_.arrive_and_wait();
    current_task_ = nullptr;
  }
};

} // namespace arche