#pragma once

#include "arche/archetype.hpp"
#include "arche/component_mask.hpp"
#include "arche/detail/query_cache.hpp"
#include "arche/type_registry.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace arche {

// =============================================================================
// Archetype Graph (Transition DAG)
// =============================================================================

/**
 * @class ArchetypeGraph
 * @brief Manages the Directed Acyclic Graph (DAG) of all instantiated
 * archetypes.
 *
 * @details
 * ### What is the Archetype Graph?
 * In `arche-ecs`, every unique combination of components forms an `Archetype`
 * node. As entities gain or lose components at runtime, they move between
 * archetypes. The `ArchetypeGraph` organizes these nodes into an interconnected
 * DAG where directed edges represent component additions (`+T`) and removals
 * (`-T`):
 *
 * ```text
 *                   [Root (Empty)]
 *                    /          \'
 *               +Pos/            \+Vel
 *                  ▼              ▼
 *             [Archetype]     [Archetype]
 *             (Position)       (Velocity)
 *                  \              /
 *               +Vel\            /+Pos
 *                    ▼          ▼
 *                    [Archetype]
 *                (Position, Velocity)
 * ```
 *
 * ### Key Architectural Guarantees:
 * 1. **O(1) Cached Transition Edges (Pointer-Hop):**
 *    - **Fast Path:** Traversal inspects pre-cached edge pointers
 * (`from->get_add_edge(type)`). Resolves in a single memory dereference without
 * hashing or sorting.
 *    - **Slow Path:** First-time transition computes the target mask, creates
 * or locates the canonical archetype, and caches bidirectional edges (`from <->
 * target`).
 * 2. **Canonical Deduplication (Diamond Convergence):**
 *    - Types are strictly stored in ascending sorted order: `[TypeID_0,
 * TypeID_1, ...]`.
 *    - Adding `Position` then `Velocity`, or `Velocity` then `Position`,
 * evaluates to the exact same `ComponentMask` and returns the identical
 * `Archetype*` instance.
 * 3. **Stable Pointers:**
 *    - Nodes are owned as `std::unique_ptr<Archetype>` within a continuous
 * vector.
 *    - Memory addresses remain permanent across the application lifetime,
 * guaranteeing that cached edge pointers never become dangling or invalidated.
 */
class ArchetypeGraph {

private:
  // --- Member Data -----------------------------------------------------------

  /// Reference to central type registry.
  const TypeRegistry &registry_;

  /// Chunk buffer size in bytes (64 KB).
  uint32_t chunk_size_{65536};

  /// Pointer to root archetype (0 components).
  Archetype *root_{nullptr};

  /// Primary storage owning all graph nodes.
  std::vector<std::unique_ptr<Archetype>> archetypes_;

  /// Fast lookup map: signature mask -> archetype.
  std::unordered_map<ComponentMask, Archetype *, ComponentMaskHash>
      mask_to_archetype_;

  /// Pointers to all active query caches registered for reactive updates.
  std::vector<QueryCache *> registered_caches_{};

public:
  // --- Constructors & Destructors --------------------------------------------

  /**
   * @brief Initializes the graph and constructs the canonical root archetype (0
   * components).
   *
   * @param registry Central component type registry providing memory
   * descriptors.
   * @param chunk_size Memory chunk page size in bytes (defaults to 64 KB).
   */
  explicit ArchetypeGraph(
      const TypeRegistry &registry,
      uint32_t chunk_size = 65536
  )
      : registry_(registry), chunk_size_(chunk_size) {
    // Create root archetype (Empty: 0 components)
    ComponentMask empty_mask;
    root_ = create_archetype(empty_mask, {});
  }

  ~ArchetypeGraph() = default;

  // Non-copyable, non-movable: owns permanent graph nodes with address
  // stability
  ArchetypeGraph(const ArchetypeGraph &) = delete;
  ArchetypeGraph &operator=(const ArchetypeGraph &) = delete;
  ArchetypeGraph(ArchetypeGraph &&) = delete;
  ArchetypeGraph &operator=(ArchetypeGraph &&) = delete;

  // --- Graph Topology & Root Inspection
  // ---------------------------------------

  /**
   * @brief Returns the root archetype representing entities with zero
   * components.
   */
  [[nodiscard]] Archetype *root() noexcept { return root_; }

  /**
   * @brief Returns the const root archetype representing entities with zero
   * components.
   */
  [[nodiscard]] const Archetype *root() const noexcept { return root_; }

  /**
   * @brief Returns the total number of distinct instantiated archetypes in the
   * graph.
   */
  [[nodiscard]] size_t archetype_count() const noexcept {
    return archetypes_.size();
  }

  /**
   * @brief Returns a read-only view of all archetype node instances.
   */
  [[nodiscard]] const std::vector<std::unique_ptr<Archetype>> &
  archetypes() const noexcept {
    return archetypes_;
  }

  // --- DAG Transition Traversals (Add / Remove) ------------------------------

  /**
   * @brief Traverses or instantiates the addition edge: `from + TypeID`.
   *
   * @details
   * - **Fast Path (O(1)):** Returns cached pointer from
   * `from->get_add_edge(type)`.
   * - **Slow Path:** Computes new `ComponentMask`, resolves/creates the target
   *   archetype with canonically sorted TypeIDs, and sets bidirectional edges:
   *   `from->set_add_edge(type, target)` and `target->set_remove_edge(type,
   * from)`.
   *
   * @param from Originating archetype node.
   * @param type Component TypeID to add.
   * @return Pointer to target Archetype node containing `from + type`.
   */
  Archetype *traverse_add(Archetype *from, TypeID type) {
    assert(from != nullptr);
    assert(
        !from->has_component(type) &&
        "Cannot add component already present in archetype"
    );

    // 1. Fast Path: Check cached edge
    if (Archetype *cached = from->get_add_edge(type)) {
      return cached;
    }

    // 2. Slow Path: Resolve or construct target node
    Archetype *target = find_or_create_add_target(from, type);

    // 3. Cache bidirectional transition edges for subsequent O(1) pointer-hops
    wire_transition_edge(from, target, type);

    return target;
  }

  /**
   * @brief Traverses or instantiates the removal edge: `from - TypeID`.
   *
   * @details
   * - **Fast Path (O(1)):** Returns cached pointer from
   * `from->get_remove_edge(type)`.
   * - **Slow Path:** Computes new `ComponentMask`, resolves/creates the target
   *   archetype with canonically sorted TypeIDs, and sets bidirectional edges:
   *   `from->set_remove_edge(type, target)` and `target->set_add_edge(type,
   * from)`.
   *
   * @param from Originating archetype node.
   * @param type Component TypeID to remove.
   * @return Pointer to target Archetype node containing `from - type`.
   */
  Archetype *traverse_remove(Archetype *from, TypeID type) {
    assert(from != nullptr);
    assert(
        from->has_component(type) &&
        "Cannot remove component absent from archetype"
    );

    // 1. Fast Path: Check cached edge
    if (Archetype *cached = from->get_remove_edge(type)) {
      return cached;
    }

    // 2. Slow Path: Resolve or construct target node
    Archetype *target = find_or_create_remove_target(from, type);

    // 3. Cache bidirectional transition edges for subsequent O(1) pointer-hops
    wire_transition_edge(target, from, type);

    return target;
  }

  // --- Archetype Lookup & Instantiation --------------------------------------

  /**
   * @brief Finds an existing archetype matching the exact component mask
   * signature.
   *
   * @param mask 128-bit component signature mask.
   * @return Pointer to existing Archetype, or nullptr if none exists.
   */
  [[nodiscard]] Archetype *find_archetype(
      const ComponentMask &mask
  ) const noexcept {
    auto it = mask_to_archetype_.find(mask);
    if (it != mask_to_archetype_.end()) {
      return it->second;
    }
    return nullptr;
  }

  /**
   * @brief Direct lookup or creation of an archetype from a collection of
   * component types.
   *
   * @details
   * - Builds the 128-bit `ComponentMask`.
   * - Sorts and deduplicates component TypeIDs canonically.
   * - Returns existing instance if already instantiated, or constructs a new
   * node.
   *
   * @param types Unsorted or arbitrary collection of component TypeIDs.
   * @return Pointer to canonical Archetype instance.
   */
  Archetype *get_or_create(std::span<const TypeID> types) {
    const ComponentMask mask = compute_mask(types);
    if (Archetype *existing = find_archetype(mask)) {
      return existing;
    }

    std::vector<TypeID> sorted_types = canonicalize_types(types);
    return create_archetype(mask, std::move(sorted_types));
  }

  // --- Setter ----------------------------------------------------------------

  /**
   * @brief Registers a QueryCache for reactive updates when new archetypes are
   * created.
   * @param cache Non-null pointer to QueryCache.
   */
  inline void register_query_cache(QueryCache *cache) noexcept {
    assert(cache != nullptr);
    registered_caches_.push_back(cache);
  }

private:
  // --- Internal Transition & Canonicalization Helpers ------------------------

  /**
   * @brief Computes a ComponentMask by activating bits for all given TypeIDs.
   */
  [[nodiscard]] inline static ComponentMask compute_mask(
      std::span<const TypeID> types
  ) noexcept {
    ComponentMask mask;
    for (TypeID t : types) {
      mask.set(t);
    }
    return mask;
  }

  /**
   * @brief Produces a sorted and deduplicated canonical TypeID sequence.
   */
  [[nodiscard]] inline static std::vector<TypeID> canonicalize_types(
      std::span<const TypeID> types
  ) {
    std::vector<TypeID> sorted_types(types.begin(), types.end());
    std::sort(sorted_types.begin(), sorted_types.end());
    sorted_types.erase(
        std::unique(sorted_types.begin(), sorted_types.end()),
        sorted_types.end()
    );
    return sorted_types;
  }

  /**
   * @brief Constructs a new canonically sorted TypeID sequence containing an
   * added type.
   */
  [[nodiscard]] inline static std::vector<TypeID> build_types_with_addition(
      std::span<const TypeID> existing,
      TypeID type
  ) {
    std::vector<TypeID> new_types;
    new_types.reserve(existing.size() + 1);
    for (TypeID t : existing) {
      new_types.push_back(t);
    }
    new_types.push_back(type);
    std::sort(new_types.begin(), new_types.end());
    return new_types;
  }

  /**
   * @brief Constructs a new canonically sorted TypeID sequence omitting a
   * removed type.
   */
  [[nodiscard]] inline static std::vector<TypeID> build_types_with_removal(
      std::span<const TypeID> existing,
      TypeID type
  ) {
    std::vector<TypeID> new_types;
    new_types.reserve(existing.size() - 1);
    for (TypeID t : existing) {
      if (t != type) {
        new_types.push_back(t);
      }
    }
    return new_types;
  }

  /**
   * @brief Connects bidirectional transition edges between two adjacent
   * archetype nodes.
   *
   * @param without_type Archetype node lacking the component.
   * @param with_type Archetype node containing the component.
   * @param type Component TypeID distinguishing the two archetypes.
   */
  inline static void wire_transition_edge(
      Archetype *without_type,
      Archetype *with_type,
      TypeID type
  ) noexcept {
    without_type->set_add_edge(type, with_type);
    with_type->set_remove_edge(type, without_type);
  }

  /**
   * @brief Resolves or constructs the target archetype for an addition
   * transition.
   */
  [[nodiscard]] inline Archetype *find_or_create_add_target(
      Archetype *from,
      TypeID type
  ) {
    ComponentMask target_mask = from->mask();
    target_mask.set(type);

    if (Archetype *existing = find_archetype(target_mask)) {
      return existing;
    }

    std::vector<TypeID> new_types = build_types_with_addition(
        from->types(), type
    );
    return create_archetype(target_mask, std::move(new_types));
  }

  /**
   * @brief Resolves or constructs the target archetype for a removal
   * transition.
   */
  [[nodiscard]] inline Archetype *find_or_create_remove_target(
      Archetype *from,
      TypeID type
  ) {
    ComponentMask target_mask = from->mask();
    target_mask.reset(type);

    if (Archetype *existing = find_archetype(target_mask)) {
      return existing;
    }

    std::vector<TypeID> new_types = build_types_with_removal(
        from->types(), type
    );
    return create_archetype(target_mask, std::move(new_types));
  }

private:
  // --- Internal Graph Factory Helpers ----------------------------------------

  /**
   * @brief Constructs and registers a new Archetype node in the graph.
   */
  inline Archetype *create_archetype(
      const ComponentMask &mask,
      std::vector<TypeID> sorted_types
  ) {
    const uint32_t id = static_cast<uint32_t>(archetypes_.size());
    auto arch = std::make_unique<Archetype>(
        id, mask, std::move(sorted_types), registry_, chunk_size_
    );
    Archetype *ptr = arch.get();

    mask_to_archetype_[mask] = ptr;
    archetypes_.push_back(std::move(arch));

    // Reactively update all registered query caches with zero hooks using
    // matches() helper
    for (QueryCache *cache : registered_caches_) {
      if (ptr->matches(cache->include_mask, cache->exclude_mask)) {
        cache->matching_archetypes.push_back(ptr);
      }
    }

    return ptr;
  }
};

} // namespace arche
