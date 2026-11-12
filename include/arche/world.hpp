#pragma once

#include "arche/archetype_graph.hpp"
#include "arche/chunk.hpp"
#include "arche/chunk_pool.hpp"
#include "arche/entity.hpp"
#include "arche/slot_map.hpp"
#include "arche/type_registry.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

namespace arche {

class CommandBuffer;
template <typename... Components> class Query;

// =============================================================================
// World: Central ECS Coordinator
// =============================================================================

/**
 * @class World
 * @brief The main ECS container that manages entities, components, and memory.
 *
 * @details
 * The World coordinates the main parts of the ECS library:
 * 1. **Entity IDs:** Managed by the SlotMap to ensure IDs are recycled safely.
 * 2. **Component Storage:** Stored in fixed-size Chunks grouped by Archetype.
 * 3. **Archetype Graph:** Tracks transitions when adding or removing
 * components.
 * 4. **Memory Pool:** Recycles Chunk memory buffers to avoid system allocation
 *    calls during gameplay or simulations.
 */
class World {
private:
  // --- Member Data -----------------------------------------------------------

  /// Memory size in bytes for each chunk buffer (defaults to 64 KB, aligned to
  /// hardware cache line).
  uint32_t chunk_size_{65536};

  /// Assigns unique IDs and stores memory layout/lifecycle functions for
  /// component types.
  TypeRegistry registry_;

  /// Tracks living entity IDs, generation numbers, and their exact storage
  /// location (archetype, chunk, row).
  SlotMap slot_map_;

  /// Reusable memory pool that stores and recycles chunk memory blocks to avoid
  /// runtime allocations.
  ChunkPool chunk_pool_;

  /// Graph that caches and organizes component combinations (archetypes) and
  /// transitions between them.
  ArchetypeGraph graph_;

  /// Storage chunks grouped by archetype index: archetype_chunks_[archetype_id]
  /// = list of active chunks.
  std::vector<std::vector<Chunk *>> archetype_chunks_;

public:
  // --- Constructors & Destructors --------------------------------------------

  /// Creates a new World with the given chunk memory size.
  explicit World(size_t chunk_size = 65536);

  /// Destroys the World and frees all allocated chunk memory.
  ~World();

  // Non-copyable, non-movable: owns physical chunks and entity storage
  World(const World &) = delete;
  World &operator=(const World &) = delete;
  World(World &&) = delete;
  World &operator=(World &&) = delete;

  /// Destroys all entities and returns all chunk buffers to the pool.
  void clear();

  // --- Entity Inspection -----------------------------------------------------

  /**
   * @brief Checks if an entity handle is currently alive.
   *
   * @details
   * ### Execution Steps:
   * 1. Extracts the slot index from the entity handle.
   * 2. Checks if the slot index is within the bounds of the SlotMap array.
   * 3. Compares the slot's stored generation counter with the handle's
   * generation. If they match, the entity is alive; otherwise, the handle is
   * stale.
   *
   * @param entity The entity handle to check.
   * @return true if the entity is alive; false if destroyed or invalid.
   */
  [[nodiscard]] bool is_alive(Entity entity) const noexcept {
    return slot_map_.is_alive(entity);
  }

  /**
   * @brief Backward-compatible alias for is_alive.
   */
  [[nodiscard]] bool is_valid(Entity entity) const noexcept {
    return is_alive(entity);
  }

  /**
   * @brief Returns the total number of live entities in the world.
   */
  [[nodiscard]] size_t entity_count() const noexcept {
    return slot_map_.size();
  }

  // --- Entity Lifecycle ------------------------------------------------------

  /**
   * @brief Creates a new entity with an initial set of components.
   *
   * @details
   * ### Execution Steps:
   * 1. **Register Types:** Registers each component type in the TypeRegistry if
   * not already known.
   * 2. **Resolve Archetype:** Finds or creates the target Archetype in the
   * graph matching this component set.
   * 3. **Allocate Entity ID:** Obtains a fresh generational Entity handle from
   * the SlotMap.
   * 4. **Allocate Chunk Slot:** Finds an active chunk with available space (or
   * acquires a new one from the ChunkPool) and stores the entity handle in its
   * entity array.
   * 5. **Construct Components:** Constructs each component value in-place
   * inside its respective column row.
   * 6. **Update Tracking:** Increments the archetype entity counter and records
   * the entity's chunk and row in the SlotMap.
   *
   * @tparam Comps Component types to attach to the entity.
   * @param components Values for each component.
   * @return The newly created Entity handle.
   */
  template <typename... Comps> Entity create_entity(Comps &&...components) {
    // 1. Ensure all component types are registered in the TypeRegistry
    (registry_.register_type<std::decay_t<Comps>>(), ...);

    // 2. Fetch or create the unique archetype corresponding to this set of
    // types
    std::vector<TypeID> types = {type_id<std::decay_t<Comps>>()...};
    Archetype *arch = graph_.get_or_create(types);

    // 3. Allocate a new entity ID and reserve a row in the archetype's chunk
    // storage
    Entity entity = slot_map_.allocate();
    auto [chunk, row] = allocate_slot(arch, entity);

    // 4. Construct each component in-place inside its respective column at the
    // assigned row
    (construct_component_in_row<std::decay_t<Comps>>(
         arch, chunk, row, std::forward<Comps>(components)
     ),
     ...);

    // 5. Track the newly added entity and record its chunk location in the slot
    // map
    arch->increment_entity_count();
    slot_map_.update_location(entity, arch, chunk, row);

    return entity;
  }

  /// Destroys an entity and reclaims its slot in constant time.
  bool destroy_entity(Entity entity);

  // --- Component Management --------------------------------------------------

  /**
   * @brief Checks if an entity has a specific component.
   *
   * @details
   * ### Execution Steps:
   * 1. **Check Liveness:** Verifies that the entity is alive in the SlotMap
   * (returns false if dead).
   * 2. **Get Archetype:** Retrieves the entity's current archetype from the
   * SlotMap record.
   * 3. **Check Signature:** Tests the archetype's 128-bit component bitmask to
   * see if the component's TypeID bit is set.
   *
   * @tparam T Component type to test for.
   * @param entity Entity handle to inspect.
   * @return true if the entity has the component; false otherwise.
   */
  template <typename T>
  [[nodiscard]] bool has_component(Entity entity) const noexcept {
    // 1. Verify that the entity is alive
    if (!slot_map_.is_alive(entity)) {
      return false;
    }
    // 2. Ensure the entity is assigned to an archetype
    const EntityRecord &record = slot_map_.get_record(entity);
    if (!record.archetype) {
      return false;
    }
    // 3. Test whether the component's TypeID bit is set in the archetype
    // bitmask
    return record.archetype->has_component(type_id<T>());
  }

  /**
   * @brief Returns a reference to an entity's component.
   *
   * @details
   * ### Execution Steps:
   * 1. **Validate Entity:** Verifies that the entity is alive and retrieves its
   * record from the SlotMap.
   * 2. **Find Column:** Looks up the component's column index in the entity's
   * archetype.
   * 3. **Locate Memory:** Calculates the exact byte address of the component
   * inside the chunk using its column offset and row.
   * 4. **Access Value:** Casts the memory address to the component type and
   * returns a mutable reference.
   *
   * @tparam T Component type to access.
   * @param entity Entity handle.
   * @return Mutable reference to the requested component.
   */
  template <typename T> [[nodiscard]] T &get_component(Entity entity) {
    // 1. Validate entity liveness and retrieve its archetype and chunk location
    const EntityRecord &record = get_alive_record(entity);
    Archetype *arch = record.archetype;

    // 2. Look up the column index of this component type within the archetype
    const TypeID tid = type_id<T>();
    const int16_t col = arch->column_index(tid);
    assert(col >= 0 && "Component not present on entity");

    // 3. Retrieve the chunk and compute the pointer to the component at this
    // row
    Chunk *chunk = record.chunk;
    assert(chunk != nullptr);

    const auto &layout = arch->column_layout(static_cast<size_t>(col));
    void *ptr = chunk->component_element(layout, record.row);
    return *reinterpret_cast<T *>(ptr);
  }

  /**
   * @brief Attaches a new component to an existing entity.
   *
   * @details
   * ### Execution Steps:
   * 1. **Register Type:** Ensures the component type is registered in the
   * TypeRegistry.
   * 2. **Validate State:** Asserts that the entity is alive and does not
   * already have this component.
   * 3. **Find Target Archetype:** Follows the graph transition edge
   * (+Component) to find or create the destination archetype.
   * 4. **Migrate Entity:**
   *    - Allocates a row and stores the entity handle in a destination chunk.
   *    - Copies or moves all existing components to their corresponding columns
   * in the destination chunk.
   *    - Writes the new component into its column.
   *    - Removes the old row from the source chunk using swap-and-pop.
   *    - Updates the entity's location in the SlotMap to point to the new chunk
   * and row.
   *
   * @tparam T Component type to add.
   * @param entity Entity handle to modify.
   * @param component Component data to attach.
   */
  template <typename T> void add_component(Entity entity, T &&component) {
    // 1. Register the component type and get its unique ID
    registry_.register_type<std::decay_t<T>>();
    const TypeID type = type_id<std::decay_t<T>>();

    // 2. Validate entity liveness and verify it doesn't already have this
    // component
    const EntityRecord &record = get_alive_record(entity);
    Archetype *src_arch = record.archetype;
    assert(
        !src_arch->has_component(type) && "Entity already contains component"
    );

    // 3. Follow or create the transition edge in the graph for (+component)
    Archetype *dst_arch = graph_.traverse_add(src_arch, type);

    // 4. Migrate the entity to the new archetype, copy existing data, and write
    // new component
    migrate_entity(entity, record, src_arch, dst_arch, type, &component);
  }

  /**
   * @brief Removes a component type from an existing entity.
   *
   * @details
   * ### Execution Steps:
   * 1. **Validate State:** Asserts that the entity is alive and currently has
   * this component.
   * 2. **Find Target Archetype:** Follows the graph transition edge
   * (-Component) to find the destination archetype.
   * 3. **Migrate Entity:**
   *    - Allocates a row and stores the entity handle in a destination chunk.
   *    - Copies or moves all remaining components to their columns in the
   * destination chunk.
   *    - Removes the old row from the source chunk using swap-and-pop (which
   * destructs the removed component).
   *    - Updates the entity's location in the SlotMap to point to the new chunk
   * and row.
   *
   * @tparam T Component type to remove.
   * @param entity Entity handle to modify.
   */
  template <typename T> void remove_component(Entity entity) {
    // 1. Get the unique ID for the component type to remove
    const TypeID type = type_id<std::decay_t<T>>();

    // 2. Validate entity liveness and verify it currently possesses this
    // component
    const EntityRecord &record = get_alive_record(entity);
    Archetype *src_arch = record.archetype;
    assert(
        src_arch->has_component(type) && "Entity does not contain component"
    );

    // 3. Follow or create the transition edge in the graph for (-component)
    Archetype *dst_arch = graph_.traverse_remove(src_arch, type);

    // 4. Migrate the entity to the new archetype, dropping the removed
    // component
    migrate_entity(entity, record, src_arch, dst_arch, type, nullptr);
  }

  // --- Subsystem Accessors ---------------------------------------------------

  /**
   * @brief Returns the component type registry.
   */
  [[nodiscard]] TypeRegistry &registry() noexcept { return registry_; }

  /**
   * @brief Returns the component type registry (read-only).
   */
  [[nodiscard]] const TypeRegistry &registry() const noexcept {
    return registry_;
  }

  /**
   * @brief Returns the entity slot map.
   */
  [[nodiscard]] SlotMap &slot_map() noexcept { return slot_map_; }

  /**
   * @brief Returns the entity slot map (read-only).
   */
  [[nodiscard]] const SlotMap &slot_map() const noexcept { return slot_map_; }

  /**
   * @brief Returns the archetype transition graph.
   */
  [[nodiscard]] ArchetypeGraph &graph() noexcept { return graph_; }

  /**
   * @brief Returns the archetype transition graph (read-only).
   */
  [[nodiscard]] const ArchetypeGraph &graph() const noexcept { return graph_; }

  /**
   * @brief Returns the chunk memory recycling pool.
   */
  [[nodiscard]] ChunkPool &chunk_pool() noexcept { return chunk_pool_; }

  /**
   * @brief Returns the chunk memory recycling pool (read-only).
   */
  [[nodiscard]] const ChunkPool &chunk_pool() const noexcept {
    return chunk_pool_;
  }

  // --- Query Factory ---------------------------------------------------------

  /**
   * @brief Creates a type-safe Query view over entities in this world.
   *
   * @details
   * Supports optional With<...> and Without<...> filter tags:
   * ```cpp
   * auto q1 = world.query<Position, Velocity>();
   * auto q2 = world.query<Position, Velocity>(arche::Without<DeadTag>{});
   * auto q3 = world.query<Entity, Position>();
   * ```
   *
   * @tparam Components Component types to traverse (and optionally Entity).
   * @tparam FilterArgs Optional With<...> or Without<...> filter arguments.
   * @param filters Filter tag instances.
   * @return Query<Components...> view.
   */
  template <typename... Components, typename... FilterArgs>
  [[nodiscard]] Query<Components...> query(FilterArgs &&...filters);

private:
  // --- Inlined Template / Lookup Helpers -------------------------------------

  /**
   * @brief Validates that an entity is alive and returns its record.
   */
  [[nodiscard]] const EntityRecord &get_alive_record(Entity entity) const {
    assert(
        slot_map_.is_alive(entity) &&
        "Attempted to access component on dead or invalid entity"
    );
    const EntityRecord &record = slot_map_.get_record(entity);
    assert(record.archetype != nullptr && "Entity has no archetype");
    return record;
  }

  /**
   * @brief Constructs a component value in-place inside a specific chunk row.
   */
  template <typename T, typename U>
  void construct_component_in_row(
      Archetype *arch,
      Chunk *chunk,
      uint32_t row,
      U &&value
  ) {
    const TypeID tid = type_id<std::decay_t<T>>();
    const int16_t col = arch->column_index(tid);
    assert(col >= 0);

    const auto &layout = arch->column_layout(static_cast<size_t>(col));
    void *slot = chunk->component_element(layout, row);
    new (slot) T(std::forward<U>(value));
  }

  // --- Chunk Storage & Physical Allocation -----------------------------------

  /// Finds or allocates an open slot in a chunk for the given archetype.
  std::pair<Chunk *, uint32_t> allocate_slot(Archetype *arch, Entity entity);

  /**
   * @brief Retrieves the active chunk list for an archetype.
   *
   * @param arch Pointer to the archetype.
   * @return Reference to the vector of chunk pointers.
   */
  inline std::vector<Chunk *> &get_archetype_chunks(Archetype *arch) {
    if (arch->id() >= archetype_chunks_.size()) {
      archetype_chunks_.resize(arch->id() + 1);
    }
    return archetype_chunks_[arch->id()];
  }

  /// Recycles an empty chunk back to the memory pool.
  void recycle_chunk(Archetype *arch, Chunk *chunk);

  // --- Structural Mutations --------------------------------------------------

  /// Moves an entity from one archetype to another, transferring shared
  /// components.
  void migrate_entity(
      Entity entity,
      const EntityRecord &old_record,
      Archetype *src_arch,
      Archetype *dst_arch,
      TypeID mutated_type,
      const void *added_component_data
  );

  // --- Deferred Command Playback ---------------------------------------------

  friend class CommandBuffer;
  template <typename... Components> friend class Query;

  /**
   * @brief Applies a deferred entity creation command during playback.
   *
   * @param spawn_fn Type-erased function pointer that unpacks components and
   * spawns the entity.
   * @param payload Pointer to serialized component tuple bytes in the command
   * buffer.
   */
  inline void apply_command_create_entity(
      void (*spawn_fn)(World &, const uint8_t *),
      const uint8_t *payload
  ) {
    assert(spawn_fn != nullptr);
    spawn_fn(*this, payload);
  }

  /// Applies a deferred component addition command during playback.
  void apply_command_add_component(Entity entity, TypeID tid, const void *data);

  /// Applies a deferred component removal command during playback.
  void apply_command_remove_component(Entity entity, TypeID tid);
};

} // namespace arche
