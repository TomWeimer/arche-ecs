module;

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

export module arche:query.detail.impl;

import :query;
import :task;
import :hardware;

export namespace arche {

using namespace internal;

namespace detail::query {

/**
 * @brief Evaluates whether a chunk should be skipped during query iteration.
 *
 * A chunk is skipped if it contains no active entities (empty) or fails
 * temporal change-detection filters (`Mutated<T>` or `Added`).
 */
[[nodiscard]] inline bool should_skip_chunk(
    Chunk *chunk,
    Archetype *arch,
    const MutationFilterList &mutation_filters,
    uint32_t added_since_version,
    bool has_added_filter
) noexcept {
  return chunk->is_empty() ||
      !chunk_matches_filters(
             chunk,
             arch,
             mutation_filters,
             added_since_version,
             has_added_filter
      );
}

} // namespace detail::query

// =============================================================================
// Query Constructors
// =============================================================================

template <QueryTerm... Components>
template <QueryFilter... Filters>
inline Query<Components...>::Query(
    const QueryCache &cache,
    QueryExecutionContext exec_ctx,
    Filters... filters
)
    : cache_(&cache), pool_(exec_ctx.pool),
      parallel_cmd_(exec_ctx.parallel_cmd) {
  // Extract instance-specific temporal change detection state (Mutated<T> and
  // Added) directly into this Query's inline stack state without touching
  // static masks.
  (detail::query::apply_temporal_filter(
       mutation_filters_,
       added_since_version_,
       has_added_filter_,
       filters,
       exec_ctx.previous_version
   ),
   ...);
}

template <QueryTerm... Components>
template <QueryFilter... Filters>
inline Query<Components...>::Query(const QueryCache &cache, Filters... filters)
    : Query(cache, QueryExecutionContext{}, filters...) {}

// =============================================================================
// Query Introspection & Matching Archetypes
// =============================================================================

template <QueryTerm... Components>
inline std::span<Archetype *const> Query<
    Components...>::matching_archetypes() const noexcept {
  if (!cache_) {
    return {};
  }
  return std::span<Archetype *const>(
      cache_->matching_archetypes.data(), cache_->matching_archetypes.size()
  );
}

template <QueryTerm... Components>
inline bool Query<Components...>::empty() const noexcept {
  for (Archetype *arch : matching_archetypes()) {
    if (arch->entity_count() > 0) {
      return false;
    }
  }
  return true;
}

// =============================================================================
// Range Iterator (begin / end)
// =============================================================================

template <QueryTerm... Components>
[[nodiscard]] inline auto Query<Components...>::begin() -> Iterator {
  const auto matching = matching_archetypes();
  if (matching.empty()) {
    return end();
  }
  return Iterator(this, 0, 0, 0);
}

template <QueryTerm... Components>
[[nodiscard]] inline auto Query<Components...>::end() -> Iterator {
  const auto matching = matching_archetypes();
  return Iterator(this, matching.size(), 0, 0);
}

// =============================================================================
// Contiguous Chunk Batch Traversal (each_chunk)
// =============================================================================

template <QueryTerm... Components>
template <ChunkCallback<Components...> Func>
inline void Query<Components...>::each_chunk(Func &&func) {
  for (Archetype *arch : matching_archetypes()) {
    const auto &chunks = arch->chunks();
    const size_t num_chunks = chunks.size();
    if (num_chunks == 0) {
      continue;
    }

    // 1. Resolve column byte offsets for this archetype once per chunk batch.
    const auto offsets = std::make_tuple(
        detail::query::resolve_column_offset<Components>(arch)...
    );

    for (size_t i = 0; i < num_chunks; ++i) {
      Chunk *chunk = chunks[i];

      // 2. Filter out empty chunks and chunks failing temporal change detection
      // filters.
      if (detail::query::should_skip_chunk(
              chunk,
              arch,
              mutation_filters_,
              added_since_version_,
              has_added_filter_
          )) {
        continue;
      }

      // 3. Issue CPU hardware prefetch instructions for next chunk header and
      // columns.
      if (i + 1 < num_chunks) {
        detail::query::prefetch_next_chunk_with_offsets(chunks[i + 1], offsets);
      }

      // 4. Unpack typed column pointers and invoke user callback via
      // std::apply.
      std::apply(
          [&](auto... offs) {
            func(
                chunk->count(),
                detail::query::get_column_ptr<Components>(chunk, offs)...
            );
          },
          offsets
      );
    }
  }
}

// =============================================================================
// SIMD Tail-Padded Batch Traversal (each_chunk_padded)
// =============================================================================

template <QueryTerm... Components>
template <PaddedChunkCallback<Components...> Func>
inline void Query<Components...>::each_chunk_padded(Func &&func) {
  for (Archetype *arch : matching_archetypes()) {
    const auto &chunks = arch->chunks();
    const size_t num_chunks = chunks.size();
    if (num_chunks == 0) {
      continue;
    }

    // 1. Resolve column byte offsets for this archetype once per chunk batch.
    const auto offsets = std::make_tuple(
        detail::query::resolve_column_offset<Components>(arch)...
    );

    for (size_t i = 0; i < num_chunks; ++i) {
      Chunk *chunk = chunks[i];

      // 2. Filter out empty chunks and chunks failing temporal change detection
      // filters.
      if (detail::query::should_skip_chunk(
              chunk,
              arch,
              mutation_filters_,
              added_since_version_,
              has_added_filter_
          )) {
        continue;
      }

      // 3. Issue CPU hardware prefetch instructions for next chunk header and
      // columns.
      if (i + 1 < num_chunks) {
        detail::query::prefetch_next_chunk_with_offsets(chunks[i + 1], offsets);
      }

      // 4. Calculate active and 16-element SIMD tail-padded entity counts.
      const uint32_t active = chunk->count();
      const uint32_t padded = arche::math::round_up_simd(active);

      // 5. Unpack typed column pointers and invoke user callback via
      // std::apply.
      std::apply(
          [&](auto... offs) {
            func(
                active,
                padded,
                detail::query::get_column_ptr<Components>(chunk, offs)...
            );
          },
          offsets
      );
    }
  }
}

// =============================================================================
// Multi-Threaded Parallel Traversal (Fused each_parallel)
// =============================================================================

template <QueryTerm... Components>
template <ParallelChunkCallback<Components...> Func>
inline void Query<Components...>::each_parallel(Func &&func) {
  // 1. Detect whether callback requests CommandBuffer& for deferred mutations.
  constexpr bool wants_cmd = std::is_invocable_v<
      Func,
      uint32_t,
      CommandBuffer &,
      column_pointer_type_t<Components>...>;

  // 2. Verify concurrency execution prerequisites.
  assert(
      pool_ != nullptr && "Query::each_parallel requires an active ThreadPool"
  );
  if constexpr (wants_cmd) {
    assert(
        parallel_cmd_ != nullptr &&
        "Query::each_parallel with CommandBuffer& requires a "
        "ParallelCommandBuffer"
    );
  }

  const auto archetypes = matching_archetypes();
  if (archetypes.empty()) {
    return;
  }

  struct ChunkTask {
    Chunk *chunk{nullptr};
    std::array<uint32_t, sizeof...(Components)> offsets;
  };

  // 3. Estimate total chunks to reserve vector storage without reallocations.
  size_t estimated_chunks = 0;
  for (Archetype *arch : archetypes) {
    estimated_chunks += arch->chunks().size();
  }

  std::vector<ChunkTask> tasks;
  tasks.reserve(estimated_chunks);

  // 4. Collect non-empty matched chunks across archetypes into the task queue.
  for (Archetype *arch : archetypes) {
    const auto &chunks = arch->chunks();
    const size_t num_chunks = chunks.size();
    if (num_chunks == 0) {
      continue;
    }

    const auto offsets = std::array<uint32_t, sizeof...(Components)>{
        detail::query::resolve_column_offset<Components>(arch)...
    };

    for (size_t i = 0; i < num_chunks; ++i) {
      Chunk *chunk = chunks[i];
      if (detail::query::should_skip_chunk(
              chunk,
              arch,
              mutation_filters_,
              added_since_version_,
              has_added_filter_
          )) {
        continue;
      }
      tasks.push_back({chunk, offsets});
    }
  }

  if (tasks.empty()) {
    return;
  }

  const size_t total_tasks = tasks.size();
  const uint32_t workers = pool_->worker_count();

  // If single worker or only 1 chunk task, execute synchronously on caller thread
  if (workers <= 1 || total_tasks == 1) {
    for (size_t i = 0; i < total_tasks; ++i) {
      Chunk *chunk = tasks[i].chunk;
      const auto &offsets = tasks[i].offsets;
      std::apply(
          [&](auto... offs) {
            if constexpr (wants_cmd) {
              CommandBuffer &cmd = parallel_cmd_->get(0);
              func(
                  chunk->count(),
                  cmd,
                  detail::query::get_column_ptr<Components>(chunk, offs)...
              );
            } else {
              func(
                  chunk->count(),
                  detail::query::get_column_ptr<Components>(chunk, offs)...
              );
            }
          },
          offsets
      );
    }
    return;
  }

  // Phase 2: Sliced chunk ranges scheduled into the work-stealing pool via TaskGroup.
  // Up to 128 slices distributed across worker queues to enable dynamic stealing without queue thrashing.
  struct ChunkSliceContext {
    struct Slice {
      ChunkSliceContext *parent{nullptr};
      size_t start{0};
      size_t end{0};
    };

    TaskGroup group;
    std::remove_reference_t<Func> *fn{nullptr};
    ParallelCommandBuffer *parallel_cmd{nullptr};
    std::span<const ChunkTask> tasks;
    Slice slices[128];
  };

  const size_t max_slices = std::min<size_t>(
      128, std::max<size_t>(static_cast<size_t>(workers), (total_tasks + 7) / 8)
  );
  const size_t chunks_per_slice = (total_tasks + max_slices - 1) / max_slices;

  ChunkSliceContext ctx;
  ctx.fn = &func;
  ctx.parallel_cmd = parallel_cmd_;
  ctx.tasks = tasks;

  size_t actual_slices = 0;
  for (size_t s = 0; s < max_slices; ++s) {
    const size_t start = s * chunks_per_slice;
    if (start >= total_tasks) {
      break;
    }
    const size_t end = std::min(total_tasks, start + chunks_per_slice);
    ctx.slices[actual_slices] = {
        .parent = &ctx,
        .start = start,
        .end = end
    };
    ++actual_slices;
  }

  ctx.group.pending.store(
      static_cast<uint32_t>(actual_slices), std::memory_order_relaxed
  );

  auto task_fn = [](void *data, uint32_t thread_id) {
    auto *slice = static_cast<typename ChunkSliceContext::Slice *>(data);
    auto *c = slice->parent;

    for (size_t i = slice->start; i < slice->end; ++i) {
      Chunk *chunk = c->tasks[i].chunk;
      const auto &offsets = c->tasks[i].offsets;

      std::apply(
          [&](auto... offs) {
            if constexpr (wants_cmd) {
              CommandBuffer &cmd = c->parallel_cmd->get(thread_id);
              (*c->fn)(
                  chunk->count(),
                  cmd,
                  detail::query::get_column_ptr<Components>(chunk, offs)...
              );
            } else {
              (*c->fn)(
                  chunk->count(),
                  detail::query::get_column_ptr<Components>(chunk, offs)...
              );
            }
          },
          offsets
      );
    }
    c->group.pending.fetch_sub(1, std::memory_order_release);
  };

  for (size_t s = 0; s < actual_slices; ++s) {
    pool_->schedule(
        Task{.execute = task_fn, .data = &ctx.slices[s]},
        static_cast<uint32_t>(s % workers)
    );
  }

  pool_->wait(ctx.group);
}

} // namespace arche
