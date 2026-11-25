module;

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <utility>
#include <vector>

export module arche:concurrency.detail.thread_pool_impl;

import :thread_pool;
import :task;
import :work_stealing_queue;
import :hardware;

namespace arche::internal::detail::concurrency {

inline thread_local uint32_t tls_thread_id = UINT32_MAX;

inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
  __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
  asm volatile("yield" ::: "memory");
#else
  std::this_thread::yield();
#endif
}

} // namespace arche::internal::detail::concurrency

export namespace arche::internal {

// =============================================================================
// ThreadPool (Work-Stealing Task Graph) Implementation
// =============================================================================

ThreadPool::ThreadPool(uint32_t thread_count)
    : worker_count_(
          thread_count > 0 ? thread_count : Hardware::core_count()
      ) {
  if (worker_count_ == 0) {
    worker_count_ = 1;
  }

  queues_.reserve(worker_count_);
  for (uint32_t i = 0; i < worker_count_; ++i) {
    queues_.push_back(std::make_unique<WorkStealingQueue>());
  }

  workers_.reserve(worker_count_);
  for (uint32_t i = 0; i < worker_count_; ++i) {
    workers_.emplace_back([this, i]() { worker_loop(i); });
  }
}

ThreadPool::~ThreadPool() {
  stop_.store(true, std::memory_order_relaxed);
  notify_epoch_.fetch_add(1, std::memory_order_release);
  notify_epoch_.notify_all();

  for (auto &w : workers_) {
    if (w.joinable()) {
      w.join();
    }
  }
}

[[nodiscard]] uint32_t ThreadPool::worker_count() const noexcept {
  return worker_count_;
}

void ThreadPool::schedule(Task task, uint32_t thread_hint) noexcept {
  if (worker_count_ == 0 || !task) {
    return;
  }

  uint32_t target = thread_hint;
  if (target >= worker_count_) {
    static std::atomic<uint32_t> rr_counter{0};
    target = rr_counter.fetch_add(1, std::memory_order_relaxed) % worker_count_;
  }

  if (!queues_[target]->push(task)) {
    // If target queue is full, spill to alternate worker deques
    bool pushed = false;
    for (uint32_t i = 0; i < worker_count_; ++i) {
      uint32_t alt = (target + 1 + i) % worker_count_;
      if (queues_[alt]->push(task)) {
        pushed = true;
        break;
      }
    }
    if (!pushed) {
      // Ring buffers saturated: execute synchronously to maintain forward progress
      task.run(target);
      return;
    }
  }

  notify_epoch_.fetch_add(1, std::memory_order_release);
  notify_epoch_.notify_one();
}

void ThreadPool::schedule(
    std::span<const Task> tasks,
    TaskGroup &group
) noexcept {
  if (tasks.empty()) {
    return;
  }
  group.pending.fetch_add(
      static_cast<uint32_t>(tasks.size()), std::memory_order_relaxed
  );

  for (size_t i = 0; i < tasks.size(); ++i) {
    uint32_t hint = static_cast<uint32_t>(i % worker_count_);
    schedule(tasks[i], hint);
  }
}

void ThreadPool::wait(TaskGroup &group) noexcept {
  const uint32_t caller_tid =
      (detail::concurrency::tls_thread_id != UINT32_MAX)
          ? detail::concurrency::tls_thread_id
          : worker_count_;

  uint32_t spin_count = 0;
  while (group.pending.load(std::memory_order_acquire) > 0) {
    // Actively pop or steal and execute tasks to accelerate completion
    if (try_steal_any(caller_tid)) {
      spin_count = 0;
      continue;
    }

    if (spin_count < 64) {
      detail::concurrency::cpu_relax();
      ++spin_count;
    } else if (spin_count < 128) {
      std::this_thread::yield();
      ++spin_count;
    } else {
      std::this_thread::yield();
    }
  }
}

bool ThreadPool::try_steal_any(uint32_t thread_id) noexcept {
  if (thread_id < worker_count_) {
    if (auto task = queues_[thread_id]->pop()) {
      task->run(thread_id);
      return true;
    }
  }

  static thread_local uint32_t victim_seed = 0;
  for (uint32_t i = 0; i < worker_count_; ++i) {
    uint32_t victim = (victim_seed++) % worker_count_;
    if (victim == thread_id) {
      continue;
    }
    if (auto task = queues_[victim]->steal()) {
      task->run(thread_id);
      return true;
    }
  }
  return false;
}

void ThreadPool::worker_loop(uint32_t thread_id) noexcept {
  detail::concurrency::tls_thread_id = thread_id;
  uint32_t victim_seed = thread_id + 1;
  uint32_t spin_count = 0;

  while (!stop_.load(std::memory_order_relaxed)) {
    // 1. Try to pop from local deque in LIFO order (cache-hot)
    if (auto task = queues_[thread_id]->pop()) {
      task->run(thread_id);
      spin_count = 0;
      continue;
    }

    // 2. Try to steal from victim worker deques in FIFO order
    bool stolen = false;
    for (uint32_t i = 0; i < worker_count_; ++i) {
      uint32_t victim = (victim_seed++) % worker_count_;
      if (victim == thread_id) {
        continue;
      }
      if (auto task = queues_[victim]->steal()) {
        task->run(thread_id);
        stolen = true;
        spin_count = 0;
        break;
      }
    }
    if (stolen) {
      continue;
    }

    // 3. No work found across all queues: Backoff
    if (spin_count < 64) {
      detail::concurrency::cpu_relax();
      ++spin_count;
    } else if (spin_count < 128) {
      std::this_thread::yield();
      ++spin_count;
    } else {
      // Sleep until epoch is bumped by new task submissions
      uint32_t epoch = notify_epoch_.load(std::memory_order_relaxed);
      if (epoch == notify_epoch_.load(std::memory_order_relaxed) &&
          !stop_.load(std::memory_order_relaxed)) {
        notify_epoch_.wait(epoch);
      }
      spin_count = 0;
    }
  }
}

} // namespace arche::internal
