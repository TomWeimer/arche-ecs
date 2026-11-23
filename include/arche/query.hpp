#pragma once

#include "arche/types/entity.hpp"
#include "archetype/archetype.hpp"
#include "chunk/chunk.hpp"
#include "query/filter.hpp"
#include "query/query_cache.hpp"
#include "query/query_rules.hpp"

#include <cassert>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace arche {

class ThreadPool;
class ParallelCommandBuffer;

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
  explicit inline Query(const QueryCache &cache, Filters... filters);

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
  template <typename Func> inline void for_each_chunk(Func &&func);

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
  template <typename Func> inline void for_each_chunk_padded(Func &&func);

  /**
   * @brief Convenient per-entity visitor passing direct component references.
   *
   * @tparam Func Callable accepting `(Components&...)`.
   * @param func User lambda executed once per matched entity.
   */
  template <typename Func> inline void for_each(Func &&func);

  /**
   * @brief Entity-aware visitor passing the Entity handle alongside component
   * references.
   *
   * @details
   * Allows accessing the Entity handle for deferred mutation commands even when
   * `Entity` is not explicitly declared in the Query template parameters:
   * ```cpp
   * query.for_each_entity([&](Entity e, Position &pos, Velocity &vel) {
   *     if (pos.y < 0.0f) cmd.despawn(e);
   * });
   * ```
   *
   * @tparam Func Callable accepting `(Entity, Components&...)`.
   * @param func User callback executed once per entity.
   */
  template <typename Func> inline void for_each_entity(Func &&func);

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

  template <typename Func>
  inline void each_parallel(ThreadPool &pool, Func &&func);

  template <typename Func>
  inline void each_parallel(
      ThreadPool &pool,
      ParallelCommandBuffer &cmd_buffers,
      Func &&func
  );

  // --- Compile-Time Type Index Helpers ---------------------------------------

  template <typename... FilterArgs>
  static inline std::pair<ComponentMask, ComponentMask> compute_query_masks(
      const FilterArgs &...filters
  );

  template <typename... Ts>
  static inline void register_and_set_mask(ComponentMask &mask);

private:
  // --- Internal Data Structures ----------------------------------------------

  /// Rule for runtime component mutation change-detection filters.
  struct MutationFilterRule {
    TypeID type_id{INVALID_TYPE_ID};
    uint32_t since_version{0};
  };

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
  template <typename... Ts> inline void apply_filter(With<Ts...>);

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
  template <typename... Ts> inline void apply_filter(Without<Ts...>);

  struct OrBranch {
    ComponentMask include{};
    ComponentMask exclude{};
  };

  struct OrClause {
    std::vector<OrBranch> branches{};
  };

  template <typename... SubFilters> inline void apply_filter(Or<SubFilters...>);

  /**
   * @brief Tag-dispatch handler for Mutated<T> change-detection filters.
   */
  template <typename T> inline void apply_filter(Mutated<T> f);

  /**
   * @brief Tag-dispatch handler for Added structural change-detection filters.
   */
  inline void apply_filter(Added f);

  /**
   * @brief Registers component types in the World registry and sets their bits
   * in the target mask.
   *
   * @details
   * Iterates through the component type pack `Ts...` using a fold expression:
   * 1. **Entity Guard:** If `T` is `Entity`, it is skipped (`if constexpr`)
   *    because `Entity` is a handle, not a component stored in the
   * TypeRegistry.
   * 2. **Has<T> Parameter:** If `T` is `Has<Inner>`, `Inner` is registered
   *    in TypeRegistry, but the mask bit is NOT set (optional presence).
   * 3. **Pointer Type:** If `T` is a pointer (e.g. `const T*` or `T*`), it is
   *    registered in TypeRegistry, but the mask bit is NOT set (optional
   * component).
   * 4. **Mandatory Component:** Registered and marked in the target filter
   * mask.
   *
   * @tparam Ts Component types to process.
   * @param mask Target bitmask (either include_mask_ or exclude_mask_) to
   * mutate.
   */

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
  static inline uint32_t resolve_column_offset(Archetype *arch);

  /**
   * @brief Resolves the typed contiguous pointer for a single column inside a
   * chunk.
   */
  template <typename Comp>
  static inline auto get_column_ptr(Chunk *chunk, uint32_t offset);

private:
  /**
   * @brief Resolves a tuple of contiguous column pointers for all queried types
   * in a chunk.
   */
  inline auto get_column_pointers(Chunk *chunk, Archetype *arch) const;

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
  ) noexcept;

  /**
   * @brief Issues lookahead cache prefetch hints using pre-calculated column
   * byte offsets.
   */
  template <typename Tuple>
  static inline void prefetch_next_chunk_with_offsets(
      const Chunk *next_chunk,
      const Tuple &offsets
  ) noexcept;

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
  ) const noexcept;

  /**
   * @brief Traverses all non-empty memory chunks across all matching
   * archetypes.
   */
  template <typename Visitor>
  inline void for_each_active_chunk(Visitor &&visitor);

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
  );

  // --- Internal Query Resolution Methods -------------------------------------

  inline void build_masks();

  inline void filter_custom_archetypes();

  [[nodiscard]] inline bool matches_or_clauses(Archetype *arch) const noexcept;

  [[nodiscard]] inline std::span<Chunk *const> get_chunks_for(Archetype *arch);

  [[nodiscard]] inline std::span<Archetype *const>
  matching_archetypes() const noexcept;

private:
  const QueryCache *cache_{nullptr};
  ComponentMask include_mask_{};
  ComponentMask exclude_mask_{};

  std::vector<OrClause> or_clauses_{};
  std::vector<Archetype *> custom_matching_archetypes_{};
  std::vector<MutationFilterRule> mutation_filters_{};
  uint32_t added_since_version_{0};
  bool filter_added_{false};
};

} // namespace arche

#include "query/query.inl"
#include "query/query_iterator.hpp"
