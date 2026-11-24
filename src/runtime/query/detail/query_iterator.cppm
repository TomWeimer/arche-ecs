module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <tuple>
#include <type_traits>
#include <utility>

export module arche:query.detail.query_iterator;

import :entity;
import :chunk;
import :archetype;
import :filters;
import :query.detail.offsets;

export namespace arche {

using namespace internal;

namespace detail::query {

/**
 * @brief Unpacks an individual row element from a column buffer.
 *
 * Adapts memory layouts according to component term category:
 * - `Has<T>`: Returns boolean presence flag.
 * - Optional pointer (`T*`): Returns indexed element pointer or `nullptr`.
 * - Required component (`T&` / `const T&`): Returns lvalue reference to column
 * slot.
 */
template <typename Comp, typename Col>
inline decltype(auto) access_query_row(Col col, uint32_t row) {
  // 1. Has<T> presence flag: col is a boolean indicating archetype membership;
  // yields boolean flag directly without memory offset indexing.
  if constexpr (is_has_v<Comp>) {
    return static_cast<bool>(col);
  }
  // 2. Optional component (T*, Opt<T>, OptMut<T>): returns offset pointer
  // (col + row) if component exists in chunk, or nullptr if absent.
  else if constexpr (std::is_pointer_v<std::decay_t<Comp>>) {
    return col != nullptr ? (col + row) : nullptr;
  }
  // 3. Required component or Entity: guaranteed present; returns direct lvalue
  // reference col[row] to the contiguous column slot.
  else {
    return col[row];
  }
}

} // namespace detail::query

// =============================================================================
// Query Element & Column Pointer Type Mappings
// =============================================================================

/**
 * @brief Maps a queried term `T` to the exact return type yielded during row
 * iteration.
 *
 * - `Entity`                 -> `Entity` (by value)
 * - `Has<Comp>`              -> `bool` (by value)
 * - Optional `Comp*`         -> `Comp*` (by pointer)
 * - Required `Comp`          -> `Comp&` or `const Comp&` (by lvalue reference)
 */
template <typename T>
using query_element_t = std::conditional_t<
    std::is_same_v<std::decay_t<T>, Entity>,
    Entity,
    std::conditional_t<
        is_has_v<T>,
        bool,
        std::conditional_t<
            std::is_pointer_v<std::decay_t<T>>,
            std::decay_t<T>,
            std::add_lvalue_reference_t<T>>>>;

/**
 * @brief Maps a queried term `T` to the typed column pointer passed to chunk
 * callbacks.
 *
 * - `Entity`                 -> `Entity*`
 * - `Has<Comp>`              -> `bool`
 * - Optional `Comp*`         -> `Comp*` (typed pointer or nullptr)
 * - Required `Comp`          -> `Comp*` or `const Comp*`
 */
template <typename T>
using column_pointer_type_t = std::conditional_t<
    std::is_same_v<std::decay_t<T>, Entity>,
    Entity *,
    std::conditional_t<
        is_has_v<T>,
        bool,
        std::conditional_t<
            std::is_pointer_v<std::decay_t<T>>,
            std::decay_t<T>,
            std::remove_reference_t<T> *>>>;

template <QueryTerm... Components> class Query;

// =============================================================================
// QueryIterator (C++20 Input / Forward Range Traversal)
// =============================================================================

/**
 * @class QueryIterator
 * @brief Forward iterator traversing entities matching a query across
 * archetypes and chunks.
 *
 * Conforms to C++20 range traversal requirements, yielding a tuple of component
 * references, pointers, entity handles, or boolean presence flags for each
 * entity row.
 */
template <QueryTerm... Components> class QueryIterator {
public:
  using iterator_category = std::forward_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = std::tuple<query_element_t<Components>...>;
  using pointer = void;
  using reference = value_type;

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
   * @brief Dereferences the iterator to unpack component references for the
   * current row.
   *
   * Uses `std::apply` to unpack chunk column pointers into `access_query_row`
   * with zero runtime overhead.
   */
  [[nodiscard]] inline value_type operator*() const {
    return std::apply(
        [this](auto... cols) {
          return value_type(
              detail::query::access_query_row<Components>(cols, row_)...
          );
        },
        pointers_
    );
  }

  /// Pre-increment: advances to the next entity row or chunk.
  inline QueryIterator &operator++() {
    ++row_;
    if (row_ >= current_chunk_count_) {
      ++chunk_idx_;
      advance_to_valid_chunk();
    }
    return *this;
  }

  /// Post-increment: advances iterator and returns previous position.
  inline QueryIterator operator++(int) {
    QueryIterator tmp = *this;
    ++(*this);
    return tmp;
  }

  /// Equality comparison for range bounds termination (operator!= is
  /// synthesized in C++20).
  [[nodiscard]] inline bool operator==(
      const QueryIterator &other
  ) const noexcept {
    return match_idx_ == other.match_idx_ && chunk_idx_ == other.chunk_idx_ &&
        row_ == other.row_;
  }

private:
  inline void load_chunk(Chunk *chunk, Archetype *arch) {
    current_chunk_count_ = chunk->count();
    pointers_ = detail::query::get_column_pointers<Components...>(chunk, arch);
  }

  inline void advance_to_valid_chunk() {
    // 1. Reset row cursor to the beginning of the next prospective chunk.
    row_ = 0;
    const auto matching = query_->matching_archetypes();

    // 2. Iterate across matching archetypes starting from the current match index.
    while (match_idx_ < matching.size()) {
      Archetype *arch = matching[match_idx_];
      const auto chunks = arch->chunks();

      // 3. Scan contiguous chunks within the current archetype table.
      while (chunk_idx_ < chunks.size()) {
        Chunk *chunk = chunks[chunk_idx_];

        // 4. If chunk contains live entities, load column base pointers and halt scan.
        if (!chunk->is_empty()) {
          load_chunk(chunk, arch);
          return;
        }
        ++chunk_idx_;
      }

      // 5. Exhausted all chunks in this archetype; advance to the next archetype table.
      ++match_idx_;
      chunk_idx_ = 0;
    }

    // 6. No more non-empty chunks in any matched archetype; mark iterator at end.
    current_chunk_count_ = 0;
  }

private:
  Query<Components...> *query_{nullptr};
  size_t match_idx_{0};
  size_t chunk_idx_{0};
  uint32_t row_{0};
  uint32_t current_chunk_count_{0};
  std::tuple<column_pointer_type_t<Components>...> pointers_{};
};

} // namespace arche
