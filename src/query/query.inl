#pragma once

#include "arche/command.hpp"
#include "concurrency/thread_pool.hpp"
#include "query/query_iterator.hpp"
#include "query/query_rules.hpp"

#include <array>
#include <atomic>

namespace arche {

// --- Constructor -----------------------------------------------------------

template <typename... Components>
template <typename... Filters>
inline Query<Components...>::Query(const QueryCache &cache, Filters... filters)
    : cache_(&cache) {
  build_masks();
  (apply_filter(filters), ...);
  if (!or_clauses_.empty()) {
    filter_custom_archetypes();
  }
}

// --- Iteration & Callback Visitors -----------------------------------------

template <typename... Components>
template <typename Func>
inline void Query<Components...>::for_each_chunk(Func &&func) {
  for (Archetype *arch : matching_archetypes()) {
    const auto &chunks = get_chunks_for(arch);
    const size_t num_chunks = chunks.size();
    if (num_chunks == 0) {
      continue;
    }

    const auto offsets = std::make_tuple(
        resolve_column_offset<Components>(arch)...
    );

    for (size_t i = 0; i < num_chunks; ++i) {
      Chunk *chunk = chunks[i];
      if (chunk->is_empty() || !chunk_matches_filters(chunk, arch)) {
        continue;
      }

      // Lookahead prefetching with pre-resolved byte offsets
      if (i + 1 < num_chunks) {
        prefetch_next_chunk_with_offsets(chunks[i + 1], offsets);
      }

      [&]<size_t... Is>(std::index_sequence<Is...>) {
        func(
            chunk->count(),
            get_column_ptr<Components>(chunk, std::get<Is>(offsets))...
        );
      }(std::index_sequence_for<Components...>{});
    }
  }
}

template <typename... Components>
template <typename Func>
inline void Query<Components...>::for_each_chunk_padded(Func &&func) {
  for (Archetype *arch : matching_archetypes()) {
    const auto &chunks = get_chunks_for(arch);
    const size_t num_chunks = chunks.size();
    if (num_chunks == 0) {
      continue;
    }

    const auto offsets = std::make_tuple(
        resolve_column_offset<Components>(arch)...
    );

    for (size_t i = 0; i < num_chunks; ++i) {
      Chunk *chunk = chunks[i];
      if (chunk->is_empty() || !chunk_matches_filters(chunk, arch)) {
        continue;
      }

      if (i + 1 < num_chunks) {
        prefetch_next_chunk_with_offsets(chunks[i + 1], offsets);
      }

      const uint32_t active = chunk->count();
      const uint32_t padded = arche::math::round_up_simd<16>(active);

      [&]<size_t... Is>(std::index_sequence<Is...>) {
        func(
            active,
            padded,
            get_column_ptr<Components>(chunk, std::get<Is>(offsets))...
        );
      }(std::index_sequence_for<Components...>{});
    }
  }
}

template <typename... Components>
template <typename Func>
inline void Query<Components...>::for_each(Func &&func) {
  for (auto it = begin(); it != end(); ++it) {
    std::apply([&](auto &&...args) { func(args...); }, *it);
  }
}

template <typename... Components>
template <typename Func>
inline void Query<Components...>::for_each_entity(Func &&func) {
  for_each_active_chunk([&](Chunk *chunk, const auto &match) {
    const uint32_t count = chunk->count();
    const Entity *entities = chunk->entity_slice();
    auto col_ptrs = get_column_pointers(chunk, match);

    for (uint32_t r = 0; r < count; ++r) {
      invoke_row(func, entities[r], col_ptrs, r);
    }
  });
}

template <typename... Components>
template <typename Func>
inline void Query<Components...>::each_parallel(ThreadPool &pool, Func &&func) {
  struct ChunkTask {
    Chunk *chunk{nullptr};
    std::array<uint32_t, sizeof...(Components)> offsets;
  };

  std::vector<ChunkTask> tasks;
  for_each_active_chunk([&](Chunk *chunk, Archetype *arch) {
    tasks.push_back({chunk, {resolve_column_offset<Components>(arch)...}});
  });

  if (tasks.empty()) {
    return;
  }

  const size_t total_tasks = tasks.size();
  std::atomic<size_t> next_task{0};

  pool.parallel_for([&](uint32_t /*thread_id*/) {
    while (true) {
      const size_t idx = next_task.fetch_add(1, std::memory_order_relaxed);
      if (idx >= total_tasks) {
        break;
      }
      Chunk *chunk = tasks[idx].chunk;
      const auto &offsets = tasks[idx].offsets;
      [&]<size_t... Is>(std::index_sequence<Is...>) {
        func(chunk->count(), get_column_ptr<Components>(chunk, offsets[Is])...);
      }(std::index_sequence_for<Components...>{});
    }
  });
}

template <typename... Components>
template <typename Func>
inline void Query<Components...>::each_parallel(
    ThreadPool &pool,
    ParallelCommandBuffer &cmd_buffers,
    Func &&func
) {
  struct ChunkTask {
    Chunk *chunk{nullptr};
    std::array<uint32_t, sizeof...(Components)> offsets;
  };

  std::vector<ChunkTask> tasks;
  for_each_active_chunk([&](Chunk *chunk, Archetype *arch) {
    tasks.push_back({chunk, {resolve_column_offset<Components>(arch)...}});
  });

  if (tasks.empty()) {
    return;
  }

  const size_t total_tasks = tasks.size();
  std::atomic<size_t> next_task{0};

  pool.parallel_for([&](uint32_t thread_id) {
    CommandBuffer &thread_cmd = cmd_buffers.get(thread_id);
    while (true) {
      const size_t idx = next_task.fetch_add(1, std::memory_order_relaxed);
      if (idx >= total_tasks) {
        break;
      }
      Chunk *chunk = tasks[idx].chunk;
      const auto &offsets = tasks[idx].offsets;
      [&]<size_t... Is>(std::index_sequence<Is...>) {
        func(
            chunk->count(),
            thread_cmd,
            get_column_ptr<Components>(chunk, offsets[Is])...
        );
      }(std::index_sequence_for<Components...>{});
    }
  });
}

template <typename... Components>
[[nodiscard]] inline auto Query<Components...>::begin() -> Iterator {
  const auto matching = matching_archetypes();
  if (matching.empty()) {
    return end();
  }
  return Iterator(this, 0, 0, 0);
}

template <typename... Components>
[[nodiscard]] inline auto Query<Components...>::end() -> Iterator {
  const auto matching = matching_archetypes();
  return Iterator(this, matching.size(), 0, 0);
}

// --- Filter Tag Dispatch ---------------------------------------------------

template <typename... Components>
template <typename... Ts>
inline void Query<Components...>::apply_filter(With<Ts...>) {
  register_and_set_mask<Ts...>(include_mask_);
}

template <typename... Components>
template <typename... Ts>
inline void Query<Components...>::apply_filter(Without<Ts...>) {
  register_and_set_mask<Ts...>(exclude_mask_);
}

template <typename... Components>
template <typename... SubFilters>
inline void Query<Components...>::apply_filter(Or<SubFilters...>) {
  OrClause clause;
  auto add_branch = [&](auto branch_tag) {
    using B = decltype(branch_tag);
    OrBranch branch;
    if constexpr (is_with_v<B>) {
      [&]<typename... Ts>(With<Ts...>) {
        register_and_set_mask<Ts...>(branch.include);
      }(branch_tag);
    } else if constexpr (is_without_v<B>) {
      [&]<typename... Ts>(Without<Ts...>) {
        register_and_set_mask<Ts...>(branch.exclude);
      }(branch_tag);
    } else {
      register_and_set_mask<B>(branch.include);
    }
    clause.branches.push_back(std::move(branch));
  };
  (add_branch(SubFilters{}), ...);
  or_clauses_.push_back(std::move(clause));
}

template <typename... Components>
template <typename T>
inline void Query<Components...>::apply_filter(Mutated<T> f) {
  include_mask_.set(type_id<std::decay_t<T>>());
  mutation_filters_.push_back({type_id<std::decay_t<T>>(), f.since_version});
}

template <typename... Components>
inline void Query<Components...>::apply_filter(Added f) {
  filter_added_ = true;
  added_since_version_ = f.since_version;
}

template <typename... Components>
template <typename... Ts>
inline void Query<Components...>::register_and_set_mask(ComponentMask &mask) {
  (
      [&] {
        using DecayTs = std::decay_t<Ts>;
        if constexpr (ComponentType<DecayTs>) {
          mask.set(type_id<DecayTs>());
        }
      }(),
      ...);
}
// --- Column & Memory Resolution Helpers ------------------------------------

template <typename... Components>
template <typename Comp>
inline uint32_t Query<Components...>::resolve_column_offset(Archetype *arch) {
  using DecayComp = std::decay_t<Comp>;
  if constexpr (std::is_same_v<DecayComp, Entity>) {
    // Entity IDs are stored in chunk->entity_slice(), not a component column
    return 0;
  } else if constexpr (is_has_v<DecayComp>) {
    using Inner = typename is_has<DecayComp>::type;
    return arch->has_component(type_id<std::decay_t<Inner>>()) ? 1 : 0;
  } else if constexpr (std::is_pointer_v<DecayComp>) {
    using Raw = std::remove_pointer_t<DecayComp>;
    const auto *layout = arch->column_layout_for(type_id<std::decay_t<Raw>>());
    if (layout == nullptr) {
      return UINT32_MAX;
    }
    return layout->chunk_byte_offset;
  } else {
    const auto *layout = arch->column_layout_for(type_id<DecayComp>());
    assert(
        layout != nullptr &&
        "Queried component missing from matched archetype layout!"
    );
    return layout->chunk_byte_offset;
  }
}

template <typename... Components>
template <typename Comp>
inline auto Query<Components...>::get_column_ptr(
    Chunk *chunk,
    uint32_t offset
) {
  using DecayComp = std::decay_t<Comp>;
  if constexpr (std::is_same_v<DecayComp, Entity>) {
    return chunk->entity_slice();
  } else if constexpr (is_has_v<DecayComp>) {
    return offset != 0;
  } else if constexpr (std::is_pointer_v<DecayComp>) {
    if (offset == UINT32_MAX) {
      return static_cast<Comp>(nullptr);
    }
    return reinterpret_cast<Comp>(chunk->component_slice(offset));
  } else {
    return reinterpret_cast<Comp *>(chunk->component_slice(offset));
  }
}

template <typename... Components>
inline auto Query<Components...>::get_column_pointers(
    Chunk *chunk,
    Archetype *arch
) const {
  return std::make_tuple(
      get_column_ptr<Components>(
          chunk, resolve_column_offset<Components>(arch)
      )...
  );
}

// --- Chunk Traversal & Lookahead Prefetching -------------------------------

template <typename... Components>
template <typename Tuple>
inline void Query<Components...>::prefetch_next_chunk_with_offsets(
    const Chunk *next_chunk,
    const Tuple &offsets
) noexcept {
  if (next_chunk == nullptr) {
    return;
  }
  // 1. Prefetch ChunkHeader (128 bytes = 2 cache lines of 64 bytes)
  const auto *base = reinterpret_cast<const uint8_t *>(next_chunk);
  Hardware::prefetch_read(base);
  Hardware::prefetch_read(base + 64);

  // 2. Prefetch first cache line using precalculated offsets
  [&]<size_t... Is>(std::index_sequence<Is...>) {
    (([&] {
       if constexpr (!is_has_v<std::decay_t<Components>>) {
         const uint32_t off = std::get<Is>(offsets);
         if (off != UINT32_MAX) {
           Hardware::prefetch_read(base + off);
         }
       }
     }()),
     ...);
  }(std::index_sequence_for<Components...>{});
}

template <typename... Components>
inline void Query<Components...>::prefetch_next_chunk(
    const Chunk *next_chunk,
    Archetype *arch
) noexcept {
  prefetch_next_chunk_with_offsets(
      next_chunk, std::make_tuple(resolve_column_offset<Components>(arch)...)
  );
}

template <typename... Components>
inline bool Query<Components...>::chunk_matches_filters(
    const Chunk *chunk,
    Archetype *arch
) const noexcept {
  // 1. Evaluate Added structural change filter in O(1)
  if (filter_added_ && chunk->structure_version() <= added_since_version_) {
    return false;
  }

  // 2. Evaluate Mutated<T> component change filters in O(1)
  for (const auto &rule : mutation_filters_) {
    const int16_t col = arch->column_index(rule.type_id);
    if (col >= 0 &&
        chunk->column_version(static_cast<uint16_t>(col)) <=
            rule.since_version) {
      return false;
    }
  }

  return true;
}

template <typename... Components>
template <typename Visitor>
inline void Query<Components...>::for_each_active_chunk(Visitor &&visitor) {
  for (Archetype *arch : matching_archetypes()) {
    const auto &chunks = get_chunks_for(arch);
    const size_t num_chunks = chunks.size();

    for (size_t i = 0; i < num_chunks; ++i) {
      Chunk *chunk = chunks[i];
      if (chunk->is_empty() || !chunk_matches_filters(chunk, arch)) {
        continue;
      }

      // Lookahead prefetching: warm L1/L2 cache lines for the upcoming chunk
      if (i + 1 < num_chunks) {
        prefetch_next_chunk(chunks[i + 1], arch);
      }

      visitor(chunk, arch);
    }
  }
}

template <typename... Components>
template <typename Func, typename Tuple>
inline void Query<Components...>::invoke_row(
    Func &&func,
    Entity entity,
    const Tuple &cols,
    uint32_t row
) {
  [&]<size_t... Is>(std::index_sequence<Is...>) {
    func(entity, access_query_row<Components>(std::get<Is>(cols), row)...);
  }(std::index_sequence_for<Components...>{});
}

// --- Internal Query Resolution Methods -------------------------------------

template <typename... Components>
inline void Query<Components...>::build_masks() {
  register_and_set_mask<Components...>(include_mask_);
}

template <typename... Components>
inline void Query<Components...>::filter_custom_archetypes() {
  assert(cache_ != nullptr);
  for (Archetype *arch : cache_->matching_archetypes) {
    if (matches_or_clauses(arch)) {
      custom_matching_archetypes_.push_back(arch);
    }
  }
}

template <typename... Components>
inline bool Query<Components...>::matches_or_clauses(
    Archetype *arch
) const noexcept {
  const ComponentMask &mask = arch->mask();
  for (const auto &clause : or_clauses_) {
    bool clause_satisfied = false;
    for (const auto &branch : clause.branches) {
      if (mask.contains_all(branch.include)) {
        if (branch.exclude.empty() || !mask.contains_any(branch.exclude)) {
          clause_satisfied = true;
          break;
        }
      }
    }
    if (!clause_satisfied) {
      return false;
    }
  }
  return true;
}

template <typename... Components>
inline std::span<Chunk *const> Query<Components...>::get_chunks_for(
    Archetype *arch
) {
  return arch->chunks();
}

template <typename... Components>
inline std::span<Archetype *const> Query<
    Components...>::matching_archetypes() const noexcept {
  if (!or_clauses_.empty()) {
    return std::span<Archetype *const>(
        custom_matching_archetypes_.data(), custom_matching_archetypes_.size()
    );
  }
  if (!cache_) {
    return {};
  }
  return std::span<Archetype *const>(
      cache_->matching_archetypes.data(), cache_->matching_archetypes.size()
  );
}

template <typename... Components>
template <typename... FilterArgs>
inline std::pair<ComponentMask, ComponentMask> Query<
    Components...>::compute_query_masks(const FilterArgs &...filters) {
  ComponentMask include{};
  ComponentMask exclude{};
  register_and_set_mask<Components...>(include);
  auto extract = [&](const auto &f) {
    using F = std::decay_t<decltype(f)>;
    if constexpr (is_with_v<F>) {
      [&]<typename... Ts>(With<Ts...>) {
        register_and_set_mask<Ts...>(include);
      }(f);
    } else if constexpr (is_without_v<F>) {
      [&]<typename... Ts>(Without<Ts...>) {
        register_and_set_mask<Ts...>(exclude);
      }(f);
    } else if constexpr (is_mutated_v<F>) {
      register_and_set_mask<typename F::component_type>(include);
    }
  };
  (extract(filters), ...);
  return {include, exclude};
}

} // namespace arche
