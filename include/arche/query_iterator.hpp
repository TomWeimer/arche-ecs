#pragma once

#include "arche/chunk.hpp"
#include "arche/entity.hpp"

#include <cassert>
#include <cstdint>
#include <tuple>
#include <utility>

namespace arche {

template <typename... Components> class Query;

// =============================================================================
// Query Iterator Implementation
// =============================================================================

/**
 * @class QueryIterator
 * @brief Bidirectional iterator supporting range-based for loops and
 * structured bindings:
 * `for (auto [pos, vel] : query)` or `for (auto [e, pos] : query)`.
 *
 * @details
 * Operates across archetype chunks without intermediate allocations:
 * 1. Resolves contiguous column pointers when entering each chunk.
 * 2. Advances across rows in $O(1)$ time.
 * 3. Skips empty chunks and jumps across archetype boundaries automatically.
 *
 * @tparam Components Component types (or Entity) yielded during iteration.
 */
template <typename... Components> class QueryIterator {
public:
  using value_type = std::tuple<Components &...>;

  inline QueryIterator(
      Query<Components...> *query,
      size_t match_idx,
      size_t chunk_idx,
      uint32_t row
  )
      : query_(query), match_idx_(match_idx), chunk_idx_(chunk_idx), row_(row) {
    advance_to_valid_chunk();
  }

  /**
   * @brief Dereferences the iterator, yielding a tuple of component
   * references (and Entity if queried) for the current entity.
   *
   * @details
   * Uses an immediately invoked templated lambda with
   * `std::index_sequence<Is...>` to expand component columns by positional
   * index:
   * 1. **Index-Based Access (`std::get<Is>`):** Accessing `pointers_` by
   * numerical index avoids compiler ambiguity if the query pack contains
   * duplicate types or `Entity`.
   * 2. **Zero-Copy Reference Forwarding (`std::forward_as_tuple`):** Binds
   * lvalue references (`Component&`) directly to the chunk row elements without
   * any intermediate struct copies or heap allocations.
   * 3. **Structured Binding Support:** Enables direct C++17 unpacking:
   *    `for (auto [pos, vel] : query)` or `for (auto [e, pos] : query)`.
   */
  [[nodiscard]] inline value_type operator*() const {
    return [&]<size_t... Is>(std::index_sequence<Is...>) {
      // Index each column pointer at the current row index and forward as
      // lvalue references
      return std::forward_as_tuple((std::get<Is>(pointers_)[row_])...);
    }(std::index_sequence_for<Components...>{});
  }

  /**
   * @brief Advances to the next entity, jumping across chunks and archetypes
   * as needed.
   */
  inline QueryIterator &operator++() {
    ++row_;
    if (row_ >= current_chunk_count_) {
      ++chunk_idx_;
      advance_to_valid_chunk();
    }
    return *this;
  }

  [[nodiscard]] inline bool operator==(
      const QueryIterator &other
  ) const noexcept {
    return match_idx_ == other.match_idx_ && chunk_idx_ == other.chunk_idx_ &&
        row_ == other.row_;
  }

  [[nodiscard]] inline bool operator!=(
      const QueryIterator &other
  ) const noexcept {
    return !(*this == other);
  }

private:
  /// Pre-caches contiguous column pointers (and entity slice) for the current
  /// chunk.
  inline void load_chunk(Chunk *chunk, Archetype *arch) {
    // 1. Cache the number of live entities in this chunk to determine loop
    // bounds
    current_chunk_count_ = chunk->count();

    // 2. Resolve contiguous column pointers using Query helper
    pointers_ = query_->get_column_pointers(chunk, arch);
  }

  /// Advances match_idx_ and chunk_idx_ until finding a non-empty chunk or
  /// reaching the end.
  inline void advance_to_valid_chunk() {
    // 1. Reset row counter to the start of the next chunk
    row_ = 0;

    // 2. Two-level traversal: Outer loop iterates through matching archetypes
    const auto matching = query_->matching_archetypes();

    while (match_idx_ < matching.size()) {
      Archetype *arch = matching[match_idx_];
      auto chunks = query_->get_chunks_for(arch);

      // Inner loop iterates through memory chunks assigned to this archetype
      while (chunk_idx_ < chunks.size()) {
        Chunk *chunk = chunks[chunk_idx_];
        if (!chunk->is_empty()) {
          // Found a non-empty chunk with active entities; load its column
          // pointers and stop
          load_chunk(chunk, arch);
          return;
        }
        // Skip empty chunk (e.g. after entities have been destroyed)
        ++chunk_idx_;
      }

      // All chunks in this archetype exhausted; advance to next archetype and
      // reset chunk index
      ++match_idx_;
      chunk_idx_ = 0;
    }

    // 3. Reached end of all matching archetypes and chunks; mark iterator
    // exhausted
    current_chunk_count_ = 0;
  }

private:
  Query<Components...> *query_{nullptr};
  size_t match_idx_{0};
  size_t chunk_idx_{0};
  uint32_t row_{0};
  uint32_t current_chunk_count_{0};
  std::tuple<Components *...> pointers_{};
};

} // namespace arche
