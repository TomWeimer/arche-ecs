module;

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

export module arche:query;

export import :entity;
export import :archetype;
export import :chunk;
export import :query_cache;
export import :thread_pool;
export import :command;
export import :query.detail.filter;
export import :query.detail.query_rules;
export import :query.detail.query_iterator;

export namespace arche {

// =============================================================================
// Query View & SIMD Visitor
// =============================================================================

/**
 * @class Query
 * @brief Type-safe, SIMD-aligned query interface for traversing entities
 * matching component criteria.
 */
template <typename... Components> class Query {
public:
  template <typename... Comps> friend class QueryIterator;

  /// Iterator type for range-based for loops and structured bindings.
  using Iterator = QueryIterator<Components...>;

  // --- Constructors & Destructors --------------------------------------------

  template <typename... Filters>
  explicit Query(const QueryCache &cache, Filters... filters);

  // --- Iteration & Callback Visitors -----------------------------------------

  template <typename Func> void for_each_chunk(Func &&func);

  template <typename Func> void for_each_chunk_padded(Func &&func);

  template <typename Func> void for_each(Func &&func);

  template <typename Func> void for_each_entity(Func &&func);

  template <typename Func> void each(Func &&func) {
    for_each(std::forward<Func>(func));
  }

  [[nodiscard]] Iterator begin();

  [[nodiscard]] Iterator end();

  template <typename Func> void each_parallel(ThreadPool &pool, Func &&func);

  template <typename Func>
  void each_parallel(
      ThreadPool &pool,
      ParallelCommandBuffer &cmd_buffers,
      Func &&func
  );

  // --- Compile-Time Type Index Helpers ---------------------------------------

  template <typename... FilterArgs>
  static std::pair<ComponentMask, ComponentMask> compute_query_masks(
      const FilterArgs &...filters
  );

  template <typename... Ts>
  static void register_and_set_mask(ComponentMask &mask);

private:
  // --- Internal Data Structures ----------------------------------------------

  struct MutationFilterRule {
    TypeID type_id{INVALID_TYPE_ID};
    uint32_t since_version{0};
  };

  // --- Filter Tag Dispatch Helpers -------------------------------------------

  template <typename... Ts> void apply_filter(With<Ts...>);

  template <typename... Ts> void apply_filter(Without<Ts...>);

  struct OrBranch {
    ComponentMask include{};
    ComponentMask exclude{};
  };

  struct OrClause {
    std::vector<OrBranch> branches{};
  };

  template <typename... SubFilters> void apply_filter(Or<SubFilters...>);

  template <typename T> void apply_filter(Mutated<T> f);

  void apply_filter(Added f);

public:
  // --- Column & Memory Resolution Helpers ------------------------------------

  template <typename Comp>
  static uint32_t resolve_column_offset(Archetype *arch);

  template <typename Comp>
  static auto get_column_ptr(Chunk *chunk, uint32_t offset);

private:
  auto get_column_pointers(Chunk *chunk, Archetype *arch) const;

  // --- Chunk Traversal & Row Invocation Helpers ------------------------------

  static void prefetch_next_chunk(
      const Chunk *next_chunk,
      Archetype *arch
  ) noexcept;

  template <typename Tuple>
  static void prefetch_next_chunk_with_offsets(
      const Chunk *next_chunk,
      const Tuple &offsets
  ) noexcept;

  [[nodiscard]] bool chunk_matches_filters(
      const Chunk *chunk,
      Archetype *arch
  ) const noexcept;

  template <typename Visitor> void for_each_active_chunk(Visitor &&visitor);

  template <typename Func, typename Tuple>
  static void invoke_row(
      Func &&func,
      Entity entity,
      const Tuple &cols,
      uint32_t row
  );

  // --- Internal Query Resolution Methods -------------------------------------

  void build_masks();

  void filter_custom_archetypes();

  [[nodiscard]] bool matches_or_clauses(Archetype *arch) const noexcept;

  [[nodiscard]] std::span<Chunk *const> get_chunks_for(Archetype *arch);

public:
  [[nodiscard]] std::span<Archetype *const>
  matching_archetypes() const noexcept;

  [[nodiscard]] inline uint32_t count() const noexcept {
    uint32_t total = 0;
    for (Archetype *arch : matching_archetypes()) {
      total += arch->entity_count();
    }
    return total;
  }

  [[nodiscard]] inline bool empty() const noexcept { return count() == 0; }

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
