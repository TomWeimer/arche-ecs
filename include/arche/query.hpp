#pragma once

#include "arche/archetype.hpp"
#include "arche/chunk.hpp"
#include "arche/component_mask.hpp"
#include "arche/entity.hpp"
#include "arche/query_filter.hpp"
#include "arche/type_registry.hpp"
#include "arche/world.hpp"

#include <array>
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
  explicit inline Query(World &world, Filters... /*filters*/) : world_(world) {
    build_masks();
    (apply_filter(Filters{}), ...);
    find_matching_archetypes();
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
    for_each_active_chunk([&](Chunk *chunk, const auto &match) {
      func(
          chunk->count(),
          get_column_ptr<Components>(
              chunk, match.offsets[type_index<Components>()]
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

  /// Cached metadata for an archetype that satisfies query filter criteria.
  struct ArchetypeMatch {
    Archetype *archetype{nullptr};
    std::array<uint32_t, sizeof...(Components)> offsets{};
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
            // Ensure the TypeRegistry knows about this component type
            world_.registry().template register_type<std::decay_t<Ts>>();

            // Mark the component's unique TypeID bit in the target filter mask
            mask.set(type_id<std::decay_t<Ts>>());
          }
        }(),
        ...);
  }

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
    } else {
      // Look up pre-calculated memory column layout from the archetype
      // definition
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
  static inline Comp *get_column_ptr(Chunk *chunk, uint32_t offset) {
    if constexpr (std::is_same_v<std::decay_t<Comp>, Entity>) {
      return chunk->entity_slice();
    } else {
      return reinterpret_cast<Comp *>(chunk->component_slice(offset));
    }
  }

  /**
   * @brief Resolves a tuple of contiguous column pointers for all queried types
   * in a chunk.
   */
  inline auto get_column_pointers(
      Chunk *chunk,
      const ArchetypeMatch &match
  ) const {
    return std::make_tuple(
        get_column_ptr<Components>(
            chunk, match.offsets[type_index<Components>()]
        )...
    );
  }

  // --- Chunk Traversal & Row Invocation Helpers ------------------------------

  /**
   * @brief Traverses all non-empty memory chunks across all matching
   * archetypes.
   */
  template <typename Visitor>
  inline void for_each_active_chunk(Visitor &&visitor) {
    for (const auto &match : matching_archetypes_) {
      const auto &chunks = get_chunks_for(match.archetype);
      for (Chunk *chunk : chunks) {
        if (!chunk->is_empty()) {
          visitor(chunk, match);
        }
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

  inline void find_matching_archetypes() {
    matching_archetypes_.clear();

    for (const auto &arch_ptr : world_.graph().archetypes()) {
      Archetype *arch = arch_ptr.get();
      const ComponentMask &arch_mask = arch->mask();

      // 1. Verify inclusion mask: (arch_mask & include) == include
      if (!arch_mask.contains_all(include_mask_)) {
        continue;
      }

      // 2. Verify exclusion mask: (arch_mask & exclude) == 0
      if (!exclude_mask_.empty() && arch_mask.contains_any(exclude_mask_)) {
        continue;
      }

      // 3. Precompute column byte offsets within chunks
      ArchetypeMatch match;
      match.archetype = arch;
      ((match.offsets[type_index<Components>()] = resolve_column_offset<
            Components>(arch)),
       ...);

      matching_archetypes_.push_back(match);
    }
  }

  [[nodiscard]] inline std::span<Chunk *const> get_chunks_for(Archetype *arch) {
    auto &chunk_map = world_.get_archetype_chunks(arch);
    return std::span<Chunk *const>(chunk_map.data(), chunk_map.size());
  }

private:
  World &world_;
  ComponentMask include_mask_{};
  ComponentMask exclude_mask_{};
  std::vector<ArchetypeMatch> matching_archetypes_{};
};

} // namespace arche

// =============================================================================
// Query Iterator and World Factory Definitions
// =============================================================================

#include "arche/query_iterator.hpp"

namespace arche {

template <typename... Components>
inline typename Query<Components...>::Iterator Query<Components...>::begin() {
  if (matching_archetypes_.empty()) {
    return end();
  }
  return Iterator(this, 0, 0, 0);
}

template <typename... Components>
inline typename Query<Components...>::Iterator Query<Components...>::end() {
  return Iterator(this, matching_archetypes_.size(), 0, 0);
}

template <typename... Components, typename... FilterArgs>
inline Query<Components...> World::query(FilterArgs &&...filters) {
  return Query<Components...>(*this, std::forward<FilterArgs>(filters)...);
}

} // namespace arche
