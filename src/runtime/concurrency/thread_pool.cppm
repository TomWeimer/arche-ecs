module;

#include <atomic>
#include <barrier>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

export module arche:thread_pool;

import :hardware;

export namespace arche::internal {

// =============================================================================
// ThreadPool (Lock-Free Barrier Worker Pool)
// =============================================================================

/**
 * @class ThreadPool
 * @brief High-performance barrier-synchronized thread pool for intra-query and batch parallelism.
 *
 * Employs dual `std::barrier` synchronizations to eliminate mutex contention,
 * lock-free task dispatching, and zero allocation per dispatch cycle.
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
  /**
   * @brief Constructs a thread pool with the specified number of worker threads.
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
   * @brief Dispatches a callable across all worker threads and waits for completion.
   *
   * @tparam Task Invocable type accepting `uint32_t thread_id`: `void(uint32_t)`.
   * @param task Callable executed concurrently by all worker threads.
   */
  template <typename Task>
  void parallel_for(Task &&task) {
    current_task_ = task;
    start_barrier_.arrive_and_wait();
    finish_barrier_.arrive_and_wait();
    current_task_ = nullptr;
  }
};

} // namespace arche
