module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

export module arche:archetype_graph;

export import :archetype;
export import :component_mask;
export import :component_id;
export import :query_cache;

export namespace arche::internal {

// =============================================================================
// ArchetypeGraph: Directional Graph & Archetype Factory
// =============================================================================

/**
 * @class ArchetypeGraph
 * @brief Directional transition graph and registry managing all live Archetype tables.
 *
 * `ArchetypeGraph` serves as the structural backbone of Arche ECS. It maintains the registry
 * of unique archetype tables, maps component bitmasks to archetype nodes, and dynamically
 * constructs and traverses directional transition edges (`traverse_add`, `traverse_remove`)
 * during entity component additions and removals.
 *
 * ### Key Responsibilities:
 * - **Node Factory:** Creates unique Archetype tables on demand for any combination of components.
 * - **Graph Navigation:** Traverses and memoizes transition edges ($O(1)$ amortized component mutation).
 * - **Query Cache Reaction:** Informs registered `QueryCache` instances when new matching archetypes
 *   are introduced into the graph.
 */
class ArchetypeGraph {
private:
  const ComponentRegistry &registry_;
  uint32_t chunk_size_{65536};
  Archetype *root_{nullptr};
  std::vector<std::unique_ptr<Archetype>> archetypes_;
  std::unordered_map<ComponentMask, Archetype *, ComponentMaskHash>
      mask_to_archetype_;
  std::vector<QueryCache *> registered_caches_{};

public:
  // --- Lifecycle & Construction ----------------------------------------------

  /**
   * @brief Constructs an ArchetypeGraph, initializing the empty root archetype node.
   * @param registry Component metadata registry.
   * @param chunk_size Memory page size in bytes for chunk allocations (default: 64 KB).
   */
  explicit ArchetypeGraph(
      const ComponentRegistry &registry,
      uint32_t chunk_size = 65536
  );

  ~ArchetypeGraph() = default;

  ArchetypeGraph(const ArchetypeGraph &) = delete;
  ArchetypeGraph &operator=(const ArchetypeGraph &) = delete;
  ArchetypeGraph(ArchetypeGraph &&) noexcept = default;
  ArchetypeGraph &operator=(ArchetypeGraph &&) noexcept = delete;

  // --- Graph Node Introspection ----------------------------------------------

  /// Returns a pointer to the empty root archetype (0 components).
  [[nodiscard]] Archetype *root() const noexcept;

  /**
   * @brief Looks up an existing archetype by its exact component bitmask.
   * @param mask Component bitmask.
   * @return Pointer to matching archetype, or nullptr if not yet instantiated.
   */
  [[nodiscard]] Archetype *get_archetype(
      const ComponentMask &mask
  ) const noexcept;

  /// Returns a span over all unique archetypes currently managed by the graph.
  [[nodiscard]] std::span<const std::unique_ptr<Archetype>>
  archetypes() const noexcept;

  /// Returns the total number of unique archetype tables created so far.
  [[nodiscard]] size_t archetype_count() const noexcept;

  // --- Dynamic Archetype Resolution & Creation -------------------------------

  /**
   * @brief Retrieves an archetype matching the mask, constructing it if not found.
   * @param mask Component bitmask describing the archetype composition.
   * @return Active archetype pointer.
   */
  Archetype *get_or_create_archetype(const ComponentMask &mask);

  /**
   * @brief Retrieves an archetype for a span of constituent TypeIDs.
   * @param types Span of component TypeIDs.
   * @return Active archetype pointer.
   */
  Archetype *get_or_create(std::span<const ComponentID> types);

  // --- Graph Transition Navigation -------------------------------------------

  /**
   * @brief Navigates the directional transition edge for adding a component type.
   *
   * Traverses the cached add edge if available; otherwise computes the target
   * bitmask, resolves the target archetype node, and memoizes the bidirectional edge.
   *
   * @param from Source archetype node.
   * @param type Component ComponentID to attach.
   * @return Destination archetype node.
   */
  Archetype *traverse_add(Archetype *from, ComponentID type);

  /**
   * @brief Navigates the directional transition edge for removing a component type.
   *
   * Traverses the cached remove edge if available; otherwise computes the target
   * bitmask, resolves the target archetype node, and memoizes the bidirectional edge.
   *
   * @param from Source archetype node.
   * @param type Component ComponentID to detach.
   * @return Destination archetype node.
   */
  Archetype *traverse_remove(Archetype *from, ComponentID type);

  // --- Query Cache Registration ----------------------------------------------

  /**
   * @brief Registers a QueryCache for reactive archetype matching updates.
   * @param cache Query cache to register and backfill with existing matching archetypes.
   */
  void register_query_cache(QueryCache &cache);
};

} // namespace arche
