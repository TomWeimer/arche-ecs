module;

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <thread>
#include <type_traits>
#include <vector>

export module arche:thread_pool;

import :hardware;
import :task;
import :work_stealing_queue;

export namespace arche::internal {

/**
 * @struct TaskGroup
 * @brief Zero-overhead atomic countdown completion latch for tracking task batch completion.
 */
struct TaskGroup {
  std::atomic<uint32_t> pending{0};
};

/**
 * @class ThreadPool
 * @brief High-performance work-stealing thread pool with per-thread Chase-Lev lock-free deques.
 *
 * Implements Phase 2 elastic task scheduling:
 * - Each worker thread owns a WorkStealingQueue (LIFO local pop, FIFO stealer steal).
 * - Dynamic work-stealing balances asymmetric P/E core workloads and non-uniform chunk topologies.
 * - Supports task groups and backward-compatible parallel_for without barrier deadlocks.
 */
class ThreadPool {
private:
  uint32_t worker_count_{0};
  std::vector<std::thread> workers_;
  std::vector<std::unique_ptr<WorkStealingQueue>> queues_;
  std::atomic<bool> stop_{false};
  alignas(64) std::atomic<uint32_t> notify_epoch_{0};

  void worker_loop(uint32_t thread_id) noexcept;
  bool try_steal_any(uint32_t thread_id) noexcept;

public:
  /**
   * @brief Constructs a work-stealing thread pool with the specified number of worker threads.
   *
   * @param thread_count Number of persistent worker threads to spawn. Defaults to hardware core count.
   */
  explicit ThreadPool(uint32_t thread_count = Hardware::core_count());

  /**
   * @brief Destructor. Signals stop token and safely joins all worker threads.
   */
  ~ThreadPool();

  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;
  ThreadPool(ThreadPool &&) = delete;
  ThreadPool &operator=(ThreadPool &&) = delete;

  /**
   * @brief Returns the number of dedicated worker threads in this pool.
   */
  [[nodiscard]] uint32_t worker_count() const noexcept;

  /**
   * @brief Enqueues a task to the pool, optionally specifying an initial worker thread hint.
   *
   * @param task POD task descriptor to execute.
   * @param thread_hint Target worker thread index (defaults to round-robin).
   */
  void schedule(Task task, uint32_t thread_hint = UINT32_MAX) noexcept;

  /**
   * @brief Enqueues a batch of tasks associated with a TaskGroup completion latch.
   *
   * @param tasks Span of POD tasks to schedule.
   * @param group TaskGroup latch to track completion.
   */
  void schedule(std::span<const Task> tasks, TaskGroup &group) noexcept;

  /**
   * @brief Waits for all tasks in a TaskGroup to complete.
   *
   * The calling thread actively participates in stealing and executing tasks while waiting.
   *
   * @param group TaskGroup latch to wait on.
   */
  void wait(TaskGroup &group) noexcept;

  /**
   * @brief Dispatches a callable across all worker threads and waits for completion.
   *
   * Re-implemented on top of lock-free work-stealing tasks without rigid std::barrier constraints,
   * completely eliminating nested dispatch deadlock hazards.
   *
   * @tparam Invocable Invocable type accepting `uint32_t thread_id`: `void(uint32_t)`.
   * @param func Callable executed concurrently across worker threads.
   */
  template <typename Invocable>
  void parallel_for(Invocable &&func) {
    if (worker_count_ <= 1) {
      func(0);
      return;
    }

    struct alignas(64) SliceTaskContext {
      struct Slice {
        SliceTaskContext *parent{nullptr};
        uint32_t slice_id{0};
      };

      std::remove_reference_t<Invocable> *fn{nullptr};
      TaskGroup group;
      Slice slices[128];
    };

    SliceTaskContext ctx;
    ctx.fn = &func;
    ctx.group.pending.store(worker_count_, std::memory_order_relaxed);

    auto task_fn = [](void *data, uint32_t) {
      auto *s = static_cast<typename SliceTaskContext::Slice *>(data);
      (*s->parent->fn)(s->slice_id);
      s->parent->group.pending.fetch_sub(1, std::memory_order_release);
    };

    const uint32_t limit = worker_count_ <= 128 ? worker_count_ : 128;
    for (uint32_t i = 0; i < limit; ++i) {
      ctx.slices[i] = {.parent = &ctx, .slice_id = i};
      schedule(Task{.execute = task_fn, .data = &ctx.slices[i]}, i);
    }

    wait(ctx.group);
  }
};

} // namespace arche::internal
