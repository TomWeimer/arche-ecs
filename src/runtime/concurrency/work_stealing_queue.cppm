module;

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>

export module arche:work_stealing_queue;

import :task;

export namespace arche::internal {

/**
 * @class WorkStealingQueue
 * @brief Lock-free, cache-aligned Chase-Lev double-ended queue.
 *
 * Used per worker thread in the Phase 2 Work-Stealing Task Graph:
 * - Owner thread pushes and pops from the bottom in LIFO order (optimal cache locality).
 * - Stealer threads steal from the top in FIFO order via atomic CAS.
 * - Enforces 64-byte CPU cache line alignment on atomic indices to eliminate false sharing.
 */
class WorkStealingQueue {
public:
  static constexpr int64_t CAPACITY = 4096;
  static constexpr int64_t MASK = CAPACITY - 1;

private:
  alignas(64) std::atomic<int64_t> top_{0};
  alignas(64) std::atomic<int64_t> bottom_{0};
  alignas(64) std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
  alignas(64) Task buffer_[CAPACITY];

public:
  WorkStealingQueue() noexcept = default;
  ~WorkStealingQueue() = default;

  WorkStealingQueue(const WorkStealingQueue &) = delete;
  WorkStealingQueue &operator=(const WorkStealingQueue &) = delete;
  WorkStealingQueue(WorkStealingQueue &&) = delete;
  WorkStealingQueue &operator=(WorkStealingQueue &&) = delete;

  /**
   * @brief Pushes a task to the bottom of the queue.
   * Safe for multi-producer push via lightweight uncontended spinlock on bottom.
   *
   * @param task POD task descriptor to enqueue.
   * @return true if successfully pushed; false if the ring buffer is full.
   */
  bool push(Task task) noexcept {
    while (lock_.test_and_set(std::memory_order_acquire)) {
#if defined(__x86_64__) || defined(_M_X64)
      __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
      asm volatile("yield" ::: "memory");
#endif
    }

    const int64_t b = bottom_.load(std::memory_order_relaxed);
    const int64_t t = top_.load(std::memory_order_acquire);

    if (b - t >= CAPACITY) {
      lock_.clear(std::memory_order_release);
      return false;
    }

    buffer_[b & MASK] = task;
    bottom_.store(b + 1, std::memory_order_release);
    lock_.clear(std::memory_order_release);
    return true;
  }

  /**
   * @brief Pops a task from the bottom of the queue in LIFO order.
   *
   * @return Enqueued Task, or std::nullopt if the queue is empty or contested.
   */
  std::optional<Task> pop() noexcept {
    while (lock_.test_and_set(std::memory_order_acquire)) {
#if defined(__x86_64__) || defined(_M_X64)
      __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
      asm volatile("yield" ::: "memory");
#endif
    }

    const int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
    bottom_.store(b, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    int64_t t = top_.load(std::memory_order_relaxed);

    if (t <= b) {
      Task task = buffer_[b & MASK];
      if (t == b) {
        // Last element race against concurrent stealers
        if (!top_.compare_exchange_strong(
                t,
                t + 1,
                std::memory_order_seq_cst,
                std::memory_order_relaxed
            )) {
          bottom_.store(b + 1, std::memory_order_relaxed);
          lock_.clear(std::memory_order_release);
          return std::nullopt;
        }
        bottom_.store(b + 1, std::memory_order_relaxed);
      }
      lock_.clear(std::memory_order_release);
      return task;
    } else {
      bottom_.store(b + 1, std::memory_order_relaxed);
      lock_.clear(std::memory_order_release);
      return std::nullopt;
    }
  }

  /**
   * @brief Steals a task from the top of the queue in FIFO order. Executed by stealer threads.
   *
   * @return Stolen Task, or std::nullopt if queue is empty or contested.
   */
  std::optional<Task> steal() noexcept {
    int64_t t = top_.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    const int64_t b = bottom_.load(std::memory_order_acquire);

    if (t < b) {
      Task task = buffer_[t & MASK];
      if (top_.compare_exchange_strong(
              t,
              t + 1,
              std::memory_order_seq_cst,
              std::memory_order_relaxed
          )) {
        return task;
      }
    }
    return std::nullopt;
  }

  /**
   * @brief Returns approximate task count currently in queue.
   */
  [[nodiscard]] size_t size() const noexcept {
    const int64_t b = bottom_.load(std::memory_order_relaxed);
    const int64_t t = top_.load(std::memory_order_relaxed);
    return b >= t ? static_cast<size_t>(b - t) : 0;
  }

  /**
   * @brief Checks if queue is currently empty.
   */
  [[nodiscard]] bool empty() const noexcept {
    return size() == 0;
  }
};

} // namespace arche::internal
