#pragma once

#include "arche/command_buffer.hpp"

#include <cstdint>
#include <vector>

namespace arche {

// =============================================================================
// Thread-Local Command Buffer Array
// =============================================================================

/**
 * @class ParallelCommandBuffer
 * @brief Manages isolated thread-local CommandBuffer instances for lock-free
 * parallel mutations.
 */
class ParallelCommandBuffer {
private:
  std::vector<CommandBuffer> buffers_;

public:
  explicit ParallelCommandBuffer(uint32_t thread_count = 1)
      : buffers_(thread_count) {}

  ~ParallelCommandBuffer() = default;

  [[nodiscard]] inline uint32_t thread_count() const noexcept {
    return static_cast<uint32_t>(buffers_.size());
  }

  [[nodiscard]] inline CommandBuffer &get(uint32_t thread_id) noexcept {
    assert(thread_id < buffers_.size() && "Worker thread ID out of range");
    return buffers_[thread_id];
  }

  inline void commit(World &world) {
    for (auto &buf : buffers_) {
      buf.commit(world);
      buf.clear();
    }
  }

  inline void clear() noexcept {
    for (auto &buf : buffers_) {
      buf.clear();
    }
  }
};

// =============================================================================
// World Parallel Query & Playback Implementations
// =============================================================================

inline void World::commit(ParallelCommandBuffer &cmd_buffers) {
  cmd_buffers.commit(*this);
}

inline void World::commit(CommandBuffer &cmd_buffer) {
  cmd_buffer.commit(*this);
}

template <typename... Components, typename Func>
inline void World::query_parallel(
    ThreadPool &pool,
    ParallelCommandBuffer &cmd_buffers,
    Func &&func
) {
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

  dispatch_parallel_chunks<Components...>(
      pool, [&](uint32_t thread_id, Chunk *chunk, const auto &offsets) {
        CommandBuffer &thread_cmd = cmd_buffers.get(thread_id);
        std::apply(
            [&](auto &&...ptrs) { func(chunk->count(), thread_cmd, ptrs...); },
            unpack_ptrs(
                chunk, offsets, std::index_sequence_for<Components...>{}
            )
        );
      }
  );
}

} // namespace arche