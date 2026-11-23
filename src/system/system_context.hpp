#pragma once

#include "arche/command.hpp"
#include "arche/query.hpp"

#include <cstdint>
#include <tuple>
#include <utility>

namespace arche {

class World;
class ThreadPool;

// =============================================================================
// System Execution Context
// =============================================================================

/**
 * @struct SystemContext
 * @brief Execution context delivered to each system callback.
 *
 * @details
 * Provides systems with direct access to:
 * - `world`: The simulation World for querying archetypes and reading resources.
 * - `cmd`: A thread-local `CommandBuffer` for deferring structural mutations safely.
 * - `pool`: Pointer to worker `ThreadPool` for Intra-System parallel work stealing.
 */
struct SystemContext {
  /// Reference to the central simulation World.
  World &world;

  /// Thread-local CommandBuffer for recording deferred structural commands.
  CommandBuffer &cmd;

  /// Optional pointer to shared ThreadPool (provided to Wide systems).
  ThreadPool *pool{nullptr};

  /// Optional pointer to thread-isolated ParallelCommandBuffer (provided to Wide systems).
  ParallelCommandBuffer *parallel_cmd{nullptr};

  SystemContext(World &w, CommandBuffer &c) noexcept
      : world(w), cmd(c), pool(nullptr), parallel_cmd(nullptr) {}

  SystemContext(
      World &w,
      CommandBuffer &c,
      ThreadPool &p,
      ParallelCommandBuffer &pc
  ) noexcept
      : world(w), cmd(c), pool(&p), parallel_cmd(&pc) {}

  /**
   * @brief Executes chunk-level parallel work stealing across all worker threads.
   */
  template <typename... Components, typename Func>
  inline void query_parallel(Func &&func);

  /**
   * @brief Dispatches arbitrary indexed loops across worker threads.
   */
  template <typename Func>
  inline void parallel_for(Func &&func);

  /// @brief Returns current monotonic sequence version.
  [[nodiscard]] inline uint32_t version() const noexcept;

  /// @brief Read-only accessor for current frame sequence version (tick).
  [[nodiscard]] inline uint32_t tick() const noexcept { return version(); }

  /// @brief Read-only accessor for current frame sequence version (tick).
  [[nodiscard]] inline uint32_t current_tick() const noexcept {
    return version();
  }
};

// =============================================================================
// Inline Method Implementations
// =============================================================================

template <typename... Components, typename Func>
inline void SystemContext::query_parallel(Func &&func) {
  auto q = world.query<Components...>();
  if (pool != nullptr && parallel_cmd != nullptr) {
    q.each_parallel(*pool, *parallel_cmd, std::forward<Func>(func));
  } else {
    // Single-threaded fallback
    q.for_each_chunk([&](uint32_t count, auto &&...ptrs) {
      func(count, this->cmd, ptrs...);
    });
  }
}

template <typename Func>
inline void SystemContext::parallel_for(Func &&func) {
  if (pool != nullptr) {
    pool->parallel_for(std::forward<Func>(func));
  } else {
    func(0);
  }
}

inline uint32_t SystemContext::version() const noexcept {
  return world.version();
}

} // namespace arche
