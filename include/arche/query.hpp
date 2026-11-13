#pragma once

#include "arche/archetype.hpp"
#include "arche/chunk.hpp"
#include "arche/component_mask.hpp"
#include "arche/entity.hpp"
#include "arche/query_filter.hpp"
#include "arche/type_registry.hpp"
#include "arche/world.hpp"

#include <cassert>
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace arche {

template <typename... Components> class QueryIterator;

// =============================================================================
// Query View & SIMD Visitor
// =============================================================================

/**
 * @class Query
 * @brief Type-safe, SIMD-aligned query interface for traversing entities
 * matching component criteria.
 *
 * @details
 * A Query evaluates component requirements against the ArchetypeGraph:
 * 1. **Inclusion Filter:** Archetypes must contain all queried component types
 *    plus any specified in `With<...>`.
 * 2. **Exclusion Filter:** Archetypes containing any component from
 *    `Without<...>` are rejected in 1-2 CPU cycles via bitmask tests.
 * 3. **Entity Introspection:** Supports `Entity` in the template pack
 *    (`Query<Entity, Position>`) or via `for_each_entity` for deferred
 * mutations.
 * 4. **Contiguous Memory Access:** Enables the CPU and compiler to
 *    auto-vectorize inner loops via chunk-level pointer callbacks
 *    (`for_each_chunk`).
 *
 * @tparam Components Component types (or Entity) to access during iteration.
 */
template <typename... Components> class Query {
public:
  template <typename... Comps> friend class QueryIterator;
  friend class World;

  /// Iterator type for range-based for loops and structured bindings.
  using Iterator = QueryIterator<Components...>;

  // --- Constructors & Destructors --------------------------------------------

  /**
   * @brief Constructs a Query against the given World with optional filter
   * tags.
   *
   * @details
   * Accepts any combination of `With<...>` and `Without<...>` filter tags in
   * any order:
   * ```cpp
   * Query<Position, Velocity> q1(world);
   * Query<Position, Velocity> q2(world, Without<TagDead>{});
   * Query<Position, Velocity> q3(world, With<Mass>{}, Without<TagDead>{});
   * ```
   *
   * @tparam Filters Variadic pack of With<...> or Without<...> filter tags.
   * @param world Central World coordinator to query against.
   */
  template <typename... Filters>
  explicit inline Query(World &world, Filters... filters) : world_(world) {
    build_masks();
    (apply_filter(filters), ...);
    // Acquire deduplicated query cache from World in O(1)
    cache_ = world_.get_or_create_query_cache(include_mask_, exclude_mask_);
  }

  // --- Iteration & Callback Visitors -----------------------------------------

  /**
   * @brief Direct chunk visitor for maximum performance and SIMD
   * auto-vectorization.
   *
   * @details
   * Passes `count` followed by contiguous column pointers (`Component*...`) to
   * the user lambda. If `Entity` is queried, it passes `Entity*` directly.
   *
   * @tparam Func Callable type accepting `(uint32_t count, Components*...)`.
   * @param func Callback executed once per active chunk with raw contiguous
   * pointers.
   */
  template <typename Func> inline void for_each_chunk(Func &&func) {
    for_each_active_chunk([&](Chunk *chunk, Archetype *arch) {
      func(
          chunk->count(),
          get_column_ptr<Components>(
              chunk, resolve_column_offset<Components>(arch)
          )...
      );
    });
  }

  /**
   * @brief High-performance chunk visitor with tail-padded vector iteration
   * bounds.
   *
   * @details
   * Exposes both `active_count` and `padded_count` (`(active_count + 15) &
   * ~15`). Because every component column is tail-padded by 16 elements, vector
   * SIMD loops (AVX2, AVX-512, NEON) can iterate up to `padded_count`
   * unconditionally with zero scalar cleanup epilogues.
   *
   * @tparam Func Callable accepting `(uint32_t active_count, uint32_t
   * padded_count, Components*...)`.
   * @param func User lambda executed once per chunk.
   */
  template <typename Func> inline void for_each_chunk_padded(Func &&func) {
    for_each_active_chunk([&](Chunk *chunk, Archetype *arch) {
      const uint32_t active = chunk->count();
      // Round up to nearest vector width using arche::math::round_up_simd
      // (defined in math_utils.hpp)
      const uint32_t padded = arche::math::round_up_simd<16>(active);
      func(
          active,
          padded,
          get_column_ptr<Components>(
              chunk, resolve_column_offset<Components>(arch)
          )...
      );
    });
  }

  /**
   * @brief Convenient per-entity visitor passing direct component references.
   *
   * @tparam Func Callable accepting `(Components&...)`.
   * @param func User lambda executed once per matched entity.
   */
  template <typename Func> inline void for_each(Func &&func) {
    for (auto it = begin(); it != end(); ++it) {
      std::apply([&](auto &&...args) { func(args...); }, *it);
    }
  }

  /**
   * @brief Entity-aware visitor passing the Entity handle alongside component
   * references.
   *
   * @details
   * Allows accessing the Entity handle for deferred mutation commands even when
   * `Entity` is not explicitly declared in the Query template parameters:
   * ```cpp
   * query.for_each_entity([&](Entity e, Position &pos, Velocity &vel) {
   *     if (pos.y < 0.0f) cmd.destroy_entity(e);
   * });
   * ```
   *
   * @tparam Func Callable accepting `(Entity, Components&...)`.
   * @param func User callback executed once per entity.
   */
  template <typename Func> inline void for_each_entity(Func &&func) {
    for_each_active_chunk([&](Chunk *chunk, const auto &match) {
      const uint32_t count = chunk->count();
      const Entity *entities = chunk->entity_slice();
      auto col_ptrs = get_column_pointers(chunk, match);

      for (uint32_t r = 0; r < count; ++r) {
        invoke_row(func, entities[r], col_ptrs, r);
      }
    });
  }

  /**
   * @brief Enables C++17 range-based for loops with structured bindings.
   *
   * @return Iterator positioned at the first matching entity or end().
   */
  [[nodiscard]] inline Iterator begin();

  /**
   * @brief Returns a past-the-end sentinel iterator.
   */
  [[nodiscard]] inline Iterator end();

  // --- Compile-Time Type Index Helpers ---------------------------------------

  /// Returns the 0-based column index of a component type within the query
  /// pack.
  template <typename T> static constexpr size_t type_index() {
    return index_in_pack<T, Components...>();
  }

private:
  // --- Internal Data Structures ----------------------------------------------

  /// Rule for runtime component mutation change-detection filters.
  struct MutationFilterRule {
    TypeID type_id{INVALID_TYPE_ID};
    uint32_t since_tick{0};
  };

  template <typename Target, typename First, typename... Rest>
  static constexpr size_t index_in_pack() {
    if constexpr (std::is_same_v<std::decay_t<Target>, std::decay_t<First>>) {
      return 0;
    } else {
      return 1 + index_in_pack<Target, Rest...>();
    }
  }

  // --- Filter Tag Dispatch Helpers -------------------------------------------

  /**
   * @brief Tag-dispatch handler for With<Ts...> filters.
   *
   * @details
   * Tag dispatch allows the constructor `Query(world, filters...)` to inspect
   * each filter tag at compile time without runtime branch checks:
   * - Registers each type in `Ts...` with the world's TypeRegistry.
   * - Sets their bits in `include_mask_` so archetypes missing any of these
   *   components are rejected during matching.
   * - Unlike queried components, types in `With<...>` are NOT unpacked into the
   *   iteration data tuple.
   *
   * @tparam Ts Component types that must exist on matching archetypes.
   */
  template <typename... Ts> inline void apply_filter(With<Ts...>) {
    register_and_set_mask<Ts...>(include_mask_);
  }

  /**
   * @brief Tag-dispatch handler for Without<Ts...> filters.
   *
   * @details
   * Tag dispatch handler for exclusion filters:
   * - Registers each forbidden type in `Ts...` with the TypeRegistry.
   * - Sets their bits in `exclude_mask_`.
   * - During archetype filtering, any candidate archetype whose bitmask
   * intersects with `exclude_mask_` is skipped in 1-2 CPU clock cycles via
   * bitwise AND.
   *
   * @tparam Ts Component types forbidden from appearing on matching archetypes.
   */
  template <typename... Ts> inline void apply_filter(Without<Ts...>) {
    register_and_set_mask<Ts...>(exclude_mask_);
  }

  /**
   * @brief Tag-dispatch handler for Mutated<T> change-detection filters.
   */
  template <typename T> inline void apply_filter(Mutated<T> f) {
    world_.registry().template register_type<std::decay_t<T>>();
    include_mask_.set(type_id<std::decay_t<T>>());
    mutation_filters_.push_back({type_id<std::decay_t<T>>(), f.since_tick});
  }

  /**
   * @brief Tag-dispatch handler for Added structural change-detection filters.
   */
  inline void apply_filter(Added f) {
    filter_added_ = true;
    added_since_tick_ = f.since_tick;
  }

  /**
   * @brief Registers component types in the World registry and sets their bits
   * in the target mask.
   *
   * @details
   * Iterates through the component type pack `Ts...` using a fold expression:
   * 1. **Entity Guard:** If `T` is `Entity`, it is skipped (`if constexpr`)
   *    because `Entity` is a handle, not a component stored in the
   * TypeRegistry.
   * 2. **Registry Sync:** Ensures the component type is registered in the
   *    World's TypeRegistry (assigning a stable TypeID and memory descriptor).
   * 3. **Bitmask Population:** Sets the bit corresponding to `type_id<T>()` in
   *    the destination mask (`include_mask_` or `exclude_mask_`).
   *
   * @tparam Ts Component types to process.
   * @param mask Target bitmask (either include_mask_ or exclude_mask_) to
   * mutate.
   */
  template <typename... Ts>
  inline void register_and_set_mask(ComponentMask &mask) {
    (
        [&] {
          // Skip Entity handles since they are stored in chunk headers, not
          // component columns
          if constexpr (!std::is_same_v<std::decay_t<Ts>, Entity>) {
            using Raw =
                typename is_write_target<std::decay_t<Ts>>::underlying_type;
            // Ensure the TypeRegistry knows about this component type
            world_.registry().template register_type<std::decay_t<Raw>>();

            // Mark the component's unique TypeID bit in the target filter mask
            mask.set(type_id<std::decay_t<Raw>>());
          }
        }(),
        ...);
  }

public:
  // --- Column & Memory Resolution Helpers ------------------------------------

  /**
   * @brief Resolves the column byte offset for a component type in the target
   * archetype.
   *
   * @details
   * In Archetype chunk memory, components are stored in separate, cache-aligned
   * Structure-of-Arrays (SoA) columns. Each column begins at a specific byte
   * offset from the chunk's base memory address:
   * - **Entity Type:** Entity IDs reside in the chunk's dedicated entity slice
   *   (`chunk->entity_slice()`), so its component column byte offset is unused
   * (returns 0).
   * - **Component Type:** Queries the Archetype layout table for the column's
   *   precomputed byte offset (`layout->chunk_byte_offset`).
   *
   * Caching this offset in `ArchetypeMatch::offsets` allows inner query loops
   * to compute `chunk->component_slice(offset)` instantly in $O(1)$ without any
   * dictionary, string, or hash lookups.
   *
   * @tparam Comp Component type (or Entity) to locate in the chunk layout.
   * @param arch Pointer to the archetype whose layout table is being inspected.
   * @return Starting byte offset of the component column inside the chunk.
   */
  template <typename Comp>
  static inline uint32_t resolve_column_offset(Archetype *arch) {
    if constexpr (std::is_same_v<std::decay_t<Comp>, Entity>) {
      // Entity IDs are stored in chunk->entity_slice(), not a component column
      return 0;
    } else if constexpr (is_write_target_v<std::decay_t<Comp>>) {

      using Raw = typename is_write_target<std::decay_t<Comp>>::underlying_type;
      const auto *layout = arch->column_layout_for(type_id<Raw>());
      assert(
          layout != nullptr &&
          "Queried component missing from matched archetype layout!"
      );
      assert(
          layout->is_double_buffered &&
          "Component requested as Write<T> must be double-buffered!"
      );
      return layout->write_byte_offset;

    } else {
      const auto *layout = arch->column_layout_for(
          type_id<std::decay_t<Comp>>()
      );
      assert(
          layout != nullptr &&
          "Queried component missing from matched archetype layout!"
      );
      return layout->chunk_byte_offset;
    }
  }

  /**
   * @brief Resolves the typed contiguous pointer for a single column inside a
   * chunk.
   */
  template <typename Comp>
  static inline auto get_column_ptr(Chunk *chunk, uint32_t offset) {
    if constexpr (std::is_same_v<std::decay_t<Comp>, Entity>) {
      return chunk->entity_slice();
    } else if constexpr (is_write_target_v<std::decay_t<Comp>>) {
      using Raw = typename is_write_target<std::decay_t<Comp>>::underlying_type;
      return Write<Raw>{
          reinterpret_cast<Raw *>(chunk->component_slice(offset))
      };
    } else {
      return reinterpret_cast<Comp *>(chunk->component_slice(offset));
    }
  }

private:
  /**
   * @brief Resolves a tuple of contiguous column pointers for all queried types
   * in a chunk.
   */
  inline auto get_column_pointers(Chunk *chunk, Archetype *arch) const {
    return std::make_tuple(
        get_column_ptr<Components>(
            chunk, resolve_column_offset<Components>(arch)
        )...
    );
  }

  // --- Chunk Traversal & Row Invocation Helpers ------------------------------

  /**
   * @brief Issues lookahead cache prefetch hints for an upcoming chunk and its
   * active component columns.
   *
   * @details
   * Prefetches into L1/L2 data cache with high temporal locality before the
   * iteration loop advances to the next chunk:
   * 1. ChunkHeader (128 bytes = two 64-byte cache lines).
   * 2. The first cache line of each queried component column slice.
   *
   * @param next_chunk Pointer to the upcoming chunk to prefetch.
   * @param arch Pointer to the archetype whose layout table is being inspected.
   */
  static inline void prefetch_next_chunk(
      const Chunk *next_chunk,
      Archetype *arch
  ) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    // 1. Prefetch ChunkHeader (128 bytes = 2 cache lines of 64 bytes)
    __builtin_prefetch(next_chunk, 0, 3);
    __builtin_prefetch(
        reinterpret_cast<const uint8_t *>(next_chunk) + 64, 0, 3
    );

    // 2. Prefetch first cache line of each queried component column
    ((__builtin_prefetch(
         reinterpret_cast<const uint8_t *>(next_chunk) +
             resolve_column_offset<Components>(arch),
         0, // 0 = read
         3  // 3 = high temporal locality (L1/L2)
     )),
     ...);
#else
    (void)next_chunk;
    (void)arch;
#endif
  }

  /**
   * @brief Evaluates runtime change-detection filters (Added, Mutated<T>)
   * against a chunk.
   *
   * @param chunk Pointer to the chunk to inspect.
   * @param arch Pointer to the archetype being inspected.
   * @return true if the chunk satisfies all change filters; false if it can be
   * skipped.
   */
  [[nodiscard]] inline bool chunk_matches_filters(
      const Chunk *chunk,
      Archetype *arch
  ) const noexcept {
    // 1. Evaluate Added structural change filter in O(1)
    if (filter_added_ && chunk->structure_version() <= added_since_tick_) {
      return false;
    }

    // 2. Evaluate Mutated<T> component change filters in O(1)
    for (const auto &rule : mutation_filters_) {
      const int16_t col = arch->column_index(rule.type_id);
      if (col >= 0 &&
          chunk->column_version(static_cast<uint16_t>(col)) <=
              rule.since_tick) {
        return false;
      }
    }

    return true;
  }

  /**
   * @brief Traverses all non-empty memory chunks across all matching
   * archetypes.
   */
  template <typename Visitor>
  inline void for_each_active_chunk(Visitor &&visitor) {
    assert(cache_ != nullptr);
    for (Archetype *arch : cache_->matching_archetypes) {
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

  /**
   * @brief Unpacks component references for a single row index and invokes the
   * callable.
   */
  template <typename Func, typename Tuple>
  static inline void invoke_row(
      Func &&func,
      Entity entity,
      const Tuple &cols,
      uint32_t row
  ) {
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      func(entity, (std::get<Is>(cols)[row])...);
    }(std::index_sequence_for<Components...>{});
  }

  // --- Internal Query Resolution Methods -------------------------------------

  inline void build_masks() {
    register_and_set_mask<Components...>(include_mask_);
  }

  [[nodiscard]] inline std::span<Chunk *const> get_chunks_for(Archetype *arch) {
    auto &chunk_map = world_.get_archetype_chunks(arch);
    return std::span<Chunk *const>(chunk_map.data(), chunk_map.size());
  }

  [[nodiscard]] inline std::span<Archetype *const>
  matching_archetypes() const noexcept {
    if (!cache_) {
      return {};
    }
    return std::span<Archetype *const>(
        cache_->matching_archetypes.data(), cache_->matching_archetypes.size()
    );
  }

private:
  World &world_;
  const QueryCache *cache_{nullptr};
  ComponentMask include_mask_{};
  ComponentMask exclude_mask_{};

  std::vector<MutationFilterRule> mutation_filters_{};
  uint32_t added_since_tick_{0};
  bool filter_added_{false};
};

} // namespace arche

// =============================================================================
// Query Iterator and World Factory Definitions
// =============================================================================

#include "arche/query_iterator.hpp"

namespace arche {

template <typename... Components>
inline typename Query<Components...>::Iterator Query<Components...>::begin() {
  if (!cache_ || cache_->matching_archetypes.empty()) {
    return end();
  }
  return Iterator(this, 0, 0, 0);
}

template <typename... Components>
inline typename Query<Components...>::Iterator Query<Components...>::end() {
  const size_t size = cache_ ? cache_->matching_archetypes.size() : 0;
  return Iterator(this, size, 0, 0);
}

template <typename... Components, typename... FilterArgs>
inline Query<Components...> World::query(FilterArgs &&...filters) {
  return Query<Components...>(*this, std::forward<FilterArgs>(filters)...);
}

} // namespace arche
