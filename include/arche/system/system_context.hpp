#pragma once

#include "arche/command/command_buffer.hpp"
#include "arche/concurrency/thread_pool.hpp"
#include "arche/query/query.hpp"
#include "arche/world.hpp"

#include <cstdint>
#include <tuple>
#include <utility>

namespace arche {

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
  auto unpack_ptrs =
      []<size_t... Is>(
          Chunk *chunk, const auto &offsets, std::index_sequence<Is...>
      ) {
        return std::make_tuple(
            Query<Components...>::template get_column_ptr<Components>(
                chunk, offsets[Is]
            )...
        );
      };

  if (pool != nullptr && parallel_cmd != nullptr) {
    ChunkDispatcher::dispatch_parallel_chunks<Components...>(
        world, *pool, [&](uint32_t thread_id, Chunk *chunk, const auto &offsets) {
          CommandBuffer &thread_cmd = parallel_cmd->get(thread_id);
          std::apply(
              [&](auto &&...ptrs) {
                func(chunk->count(), thread_cmd, ptrs...);
              },
              unpack_ptrs(
                  chunk, offsets, std::index_sequence_for<Components...>{}
              )
          );
        }
    );
  } else {
    // Single-threaded fallback
    auto q = world.query<Components...>();
    q.for_each_active_chunk([&](Chunk *chunk, Archetype *arch) {
      auto offsets = std::array<uint32_t, sizeof...(Components)>{
          Query<Components...>::template resolve_column_offset<Components>(
              arch
          )...
      };
      std::apply(
          [&](auto &&...ptrs) { func(chunk->count(), this->cmd, ptrs...); },
          unpack_ptrs(chunk, offsets, std::index_sequence_for<Components...>{})
      );
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
