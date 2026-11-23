module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

export module arche:query.detail.query_iterator;

import :entity;
import :chunk;
import :archetype;
import :query.detail.filter;

export namespace arche {

template <typename T> struct is_has : std::false_type {};
template <typename T> struct is_has<Has<T>> : std::true_type {
  using type = T;
};

template <typename T>
using query_element_t = std::conditional_t<
    std::is_same_v<std::decay_t<T>, Entity>,
    Entity,
    std::conditional_t<
        is_has_v<std::decay_t<T>>,
        bool,
        std::conditional_t<
            std::is_pointer_v<std::decay_t<T>>,
            std::decay_t<T>,
            std::add_lvalue_reference_t<T>>>>;

template <typename T>
using column_pointer_type_t = std::conditional_t<
    std::is_same_v<std::decay_t<T>, Entity>,
    Entity *,
    std::conditional_t<
        is_has_v<std::decay_t<T>>,
        bool,
        std::conditional_t<
            std::is_pointer_v<std::decay_t<T>>,
            std::remove_pointer_t<std::decay_t<T>> *,
            std::conditional_t<
                std::is_const_v<std::remove_reference_t<T>>,
                const std::decay_t<T> *,
                std::decay_t<T> *>>>>;

template <typename... Components> class Query;

template <typename... Components> class QueryIterator {
public:
  using value_type = std::tuple<query_element_t<Components>...>;

  inline QueryIterator(
      Query<Components...> *query,
      size_t match_idx,
      size_t chunk_idx,
      uint32_t row
  )
      : query_(query), match_idx_(match_idx), chunk_idx_(chunk_idx), row_(row) {
    advance_to_valid_chunk();
  }

  [[nodiscard]] inline value_type operator*() const {
    return [&]<size_t... Is>(std::index_sequence<Is...>) {
      return value_type(
          access_query_row<Components>(std::get<Is>(pointers_), row_)...
      );
    }(std::index_sequence_for<Components...>{});
  }

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
  inline void load_chunk(Chunk *chunk, Archetype *arch) {
    current_chunk_count_ = chunk->count();
    pointers_ = query_->get_column_pointers(chunk, arch);
  }

  inline void advance_to_valid_chunk() {
    row_ = 0;
    const auto matching = query_->matching_archetypes();

    while (match_idx_ < matching.size()) {
      Archetype *arch = matching[match_idx_];
      auto chunks = query_->get_chunks_for(arch);

      while (chunk_idx_ < chunks.size()) {
        Chunk *chunk = chunks[chunk_idx_];
        if (!chunk->is_empty()) {
          load_chunk(chunk, arch);
          return;
        }
        ++chunk_idx_;
      }

      ++match_idx_;
      chunk_idx_ = 0;
    }

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
