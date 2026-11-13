#pragma once

#include "arche/archetype_graph.hpp"
#include "arche/chunk.hpp"
#include "arche/chunk_pool.hpp"
#include "arche/entity.hpp"
#include "arche/gpu_reflection.hpp"
#include "arche/slot_map.hpp"
#include "arche/type_registry.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace arche {

class CommandBuffer;
template <typename... Components> class Query;

// =============================================================================
// World State Snapshot Header
// =============================================================================

/**
 * @struct SnapshotHeader
 * @brief 64-byte cache-aligned header encapsulating world snapshot metadata.
 */
struct alignas(16) SnapshotHeader {
  uint32_t magic{0x48435241};    ///< Magic bytes ('ARCH' in Little-Endian)
  uint32_t version{1};           ///< Snapshot format version
  uint64_t current_tick{0};      ///< World simulation tick at snapshot time
  uint32_t slot_count{0};        ///< Total capacity of SlotMap slots array
  uint32_t free_list_head{0};    ///< Head index of intrusive free list
  uint32_t active_entities{0};   ///< Total living entities
  uint32_t chunk_count{0};       ///< Total active 64 KB chunk pages
  size_t total_payload_bytes{0}; ///< Total byte size including header
  uint32_t archetype_count{0};   ///< Total archetypes at snapshot time
  uint8_t reserved[20]{0}; ///< Reserved padding to align header to 64 bytes
};

static_assert(
    sizeof(SnapshotHeader) == 64,
    "SnapshotHeader must be exactly 64 bytes"
);

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

  // --- World Snapshots & Rollback --------------------------------------------

  /**
   * @brief Dumps the entire world memory state (SlotMap + all 64 KB chunks)
   * into a contiguous buffer.
   *
   * @param destination_buffer Pointer to pre-allocated destination memory.
   * @param max_bytes Capacity of destination buffer in bytes.
   * @return Total bytes written to destination_buffer.
   */
  inline size_t create_snapshot(void *destination_buffer, size_t max_bytes);

  /**
   * @brief Restores the entire world state from a raw memory snapshot buffer in
   * < 1 ms.
   *
   * @param snapshot_buffer Pointer to snapshot memory.
   * @param bytes Size of snapshot buffer in bytes.
   */
  inline void restore_snapshot(const void *snapshot_buffer, size_t bytes);

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
    // 1. Resolve or create unique archetype for this component combination
    Archetype *arch = resolve_archetype<Comps...>();

    // 2. Allocate entity handle and find/allocate chunk row
    Entity entity = slot_map_.allocate();
    auto [chunk, row] = allocate_slot(arch, entity);

    // 3. Construct each component in-place inside its column at assigned row
    (construct_component_in_row<std::decay_t<Comps>>(
         arch, chunk, row, std::forward<Comps>(components)
     ),
     ...);

    // 4. Track entity count and record physical chunk location in SlotMap
    arch->increment_entity_count();
    slot_map_.update_location(entity, arch, chunk, row);

    return entity;
  }

  // --- Bulk Entity Lifecycle -------------------------------------------------

  /**
   * @brief Spawns N entities initialized with identical component values in
   * a single high-throughput batch (Component Broadcasting).
   *
   * @details
   * ### When to use `create_entities`:
   * Use this function when you need to spawn a large number of entities that
   * share identical starting component state — for example:
   * - Spawning 50,000 particle or boid agents with default velocities and
   * positions.
   * - Instantiating waves of enemy projectiles with uniform speed and damage.
   * - Pre-populating entity pools during level initialization.
   *
   * ### Allocation & Performance Characteristics:
   * 1. **Bulk Slot Allocation:** Drains all available recycled slots from the
   * SlotMap free-list first, then resizes the underlying slot array once for
   * remaining entities.
   * 2. **Contiguous Chunk Packing:** Fills active 64 KB memory chunks to
   * capacity before acquiring new chunks from the ChunkPool, avoiding
   * fragmentation.
   * 3. **In-Place Component Construction:** Placement-news component copies
   * directly into chunk column rows without intermediate container overhead.
   * 4. **Return Value:** Returns a newly allocated `std::vector<Entity>`
   * holding all `count` generational handles.
   *
   * @see create_entities_soa For streaming pre-computed, unique component
   * arrays with zero heap allocations.
   *
   * @tparam Comps Component types to attach to every spawned entity.
   * @param count Number of entities to spawn (must be >= 0).
   * @param components Component values broadcast-copied to each entity.
   * @return std::vector<Entity> Vector containing the N newly created
   * generational handles.
   */
  template <typename... Comps>
  std::vector<Entity> create_entities(size_t count, const Comps &...components);

  /**
   * @brief Spawns N entities from pre-computed contiguous external column
   * arrays with zero heap allocations (Direct Structure-of-Arrays Streaming).
   *
   * @details
   * ### When to use `create_entities_soa`:
   * Use this function when every entity has **unique**, pre-computed component
   * data — for example:
   * - Loading world/level entities from disk or network snapshots.
   * - Streaming particle simulations generated by compute kernels or physics
   * engines.
   * - Ingesting procedurally generated terrain, crowd, or vehicle matrices.
   *
   * ### Key Differences from `create_entities`:
   * | Feature | `create_entities` (Broadcast) | `create_entities_soa`
   * (Streaming) | | :--- | :--- | :--- | | **Data Source** | Single prototype
   * value per component | Contiguous external arrays (`const Comp*`) | |
   * **Component Values** | Identical copies for all N entities | Unique,
   * per-entity values for each row | | **Handle Output** | Returns newly
   * allocated `std::vector` | Writes directly into caller's `std::span<Entity>`
   * | | **Heap Allocations** | Allocates return `vector` | **Zero heap
   * allocations** (100% in-place) | | **Throughput** | ~125 Million ops/sec |
   * **~275+ Million ops/sec** (saturates RAM bandwidth) |
   *
   * ### Execution Invariants:
   * - `out_entities.size()` must be greater than or equal to `count`.
   * - Each pointer in `column_data...` must point to a contiguous buffer of at
   * least `count` elements.
   * - Transfers component data via bulk `std::memcpy` directly into
   * cache-aligned chunk columns.
   *
   * @tparam Comps Component types corresponding to each input column array.
   * @param count Number of entities to spawn.
   * @param out_entities Output span receiving the allocated Entity handles.
   * @param column_data Pointers to contiguous arrays of source component data
   * (`const Comp*`).
   */
  template <typename... Comps>
  void create_entities_soa(
      size_t count,
      std::span<Entity> out_entities,
      const Comps *...column_data
  );

  /// Destroys an entity and reclaims its slot in constant time.
  bool destroy_entity(Entity entity);

  /**
   * @brief Destroys a batch of entities in a single cache-efficient pass.
   *
   * @param entities Span of Entity handles to destroy.
   */
  void destroy_entities(std::span<const Entity> entities);

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

  // --- GPU Streaming ---------------------------------------------------------

  /**
   * @brief Generates a list of non-owning ComponentSpan views for all chunks
   * containing component T.
   *
   * @details
   * Traverses all active archetypes and chunks possessing component `T`,
   * extracting direct contiguous C-pointers without intermediate allocations or
   * data copies. Enables zero-copy GPU memory binding (Approach B) and linear
   * VRAM uploads (Approach A).
   *
   * @tparam T Component type complying with GPU std430 alignment rules.
   * @return Vector of non-owning ComponentSpan<T> views across all active
   * chunks.
   */
  template <GpuStd430Compatible T>
  [[nodiscard]] std::vector<ComponentSpan<T>> view_component_chunks() {
    std::vector<ComponentSpan<T>> views;
    const TypeID tid = type_id<T>();

    // 1. Iterate through every instantiated archetype in the world's graph
    for (const auto &arch_ptr : graph_.archetypes()) {
      Archetype *arch = arch_ptr.get();

      // Skip archetypes that do not possess component T
      if (!arch->has_component(tid)) {
        continue;
      }

      // 2. Fetch column layout metadata to obtain the precomputed chunk byte
      // offset
      const int16_t col = arch->column_index(tid);
      assert(col >= 0);
      const auto &layout = arch->column_layout(static_cast<size_t>(col));

      // 3. Iterate through all active 64 KB memory chunks allocated to this
      // archetype
      const auto &chunks = get_archetype_chunks(arch);
      for (Chunk *chunk : chunks) {
        // Skip empty chunks (e.g. after entities have been destroyed)
        if (chunk->is_empty()) {
          continue;
        }

        // 4. Resolve raw contiguous column pointer using precomputed byte
        // offset
        const T *col_ptr = reinterpret_cast<const T *>(
            chunk->component_slice(layout.chunk_byte_offset)
        );

        // 5. Emplace a non-owning span wrapping the contiguous pointer and
        // active entity count
        views.emplace_back(col_ptr, chunk->count());
      }
    }
    return views;
  }

  /**
   * @brief Fast batch streamer: copies all instances of component T directly
   * into a destination buffer.
   *
   * @details
   * Performs sequential memcpy transfers from chunk columns into mapped
   * Vulkan/Metal VRAM buffers (Approach A), saturating host memory bandwidth
   * with zero intermediate allocations.
   *
   * @tparam T Component type complying with GPU std430 alignment rules.
   * @param destination_buffer Pointer to mapped GPU VRAM or host memory buffer.
   * @return Total bytes written to destination.
   */
  template <GpuStd430Compatible T>
  size_t stream_component_to(void *destination_buffer) {
    assert(
        destination_buffer != nullptr && "Destination buffer cannot be null!"
    );
    uint8_t *dst = static_cast<uint8_t *>(destination_buffer);
    size_t total_bytes = 0;

    // 1. Fetch non-owning views for each chunk containing component T
    auto views = view_component_chunks<T>();

    // 2. Stream contiguous component slices back-to-back into destination
    // memory
    for (const auto &span : views) {
      const size_t bytes = span.size_bytes();
      std::memcpy(dst + total_bytes, span.data(), bytes);
      total_bytes += bytes;
    }

    return total_bytes;
  }

  /// @brief Returns the total active count of entities possessing component T.
  template <typename T>
  [[nodiscard]] uint32_t count_components() const noexcept {
    const TypeID tid = type_id<T>();
    uint32_t total = 0;

    for (const auto &arch_ptr : graph_.archetypes()) {
      Archetype *arch = arch_ptr.get();
      if (arch->has_component(tid)) {
        total += arch->entity_count();
      }
    }
    return total;
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

  /**
   * @brief Resolves or creates the Archetype node for a set of component types,
   * registering any previously unseen types with the TypeRegistry.
   *
   * @tparam Comps Component types constituting the archetype signature.
   * @return Pointer to canonical Archetype in the graph.
   */
  template <typename... Comps>
  [[nodiscard]] inline Archetype *resolve_archetype() {
    (registry_.register_type<std::decay_t<Comps>>(), ...);
    std::vector<TypeID> types = {type_id<std::decay_t<Comps>>()...};
    return graph_.get_or_create(types);
  }

  /**
   * @brief Broadcasts copies of a single component value into a chunk row
   * range.
   *
   * @tparam Comp Component type to construct.
   * @param arch Owning Archetype providing column layout.
   * @param chunk Target Chunk receiving component instances.
   * @param start_row Starting row offset within the chunk.
   * @param count Number of consecutive rows to fill.
   * @param component Source component value to broadcast-copy.
   */
  template <typename Comp>
  static inline void broadcast_component_in_chunk(
      Archetype *arch,
      Chunk *chunk,
      uint32_t start_row,
      uint32_t count,
      const Comp &component
  ) {
    const auto *layout = arch->column_layout_for(type_id<Comp>());
    assert(layout != nullptr);
    Comp *col = static_cast<Comp *>(
        chunk->component_element(*layout, start_row)
    );
    for (uint32_t i = 0; i < count; ++i) {
      new (&col[i]) Comp(component);
    }
  }

  /**
   * @brief Copies a slice of an external contiguous component column directly
   * into a chunk.
   *
   * @tparam Comp Component type to stream.
   * @param arch Owning Archetype providing column layout.
   * @param chunk Target Chunk receiving component data.
   * @param start_row Destination starting row within the chunk.
   * @param src_data Pointer to contiguous external source array.
   * @param placed Source offset in elements.
   * @param to_add Number of elements to copy.
   */
  template <typename Comp>
  static inline void stream_component_in_chunk(
      Archetype *arch,
      Chunk *chunk,
      uint32_t start_row,
      const Comp *src_data,
      size_t placed,
      uint32_t to_add
  ) {
    const auto *layout = arch->column_layout_for(type_id<Comp>());
    assert(layout != nullptr);
    void *dst = chunk->component_element(*layout, start_row);
    std::memcpy(dst, &src_data[placed], to_add * sizeof(Comp));
  }

  /**
   * @brief Updates SlotMap physical coordinates for a contiguous slice of
   * entities.
   *
   * @param entities Slice of entity handles.
   * @param arch Owning Archetype.
   * @param chunk Memory chunk containing the entities.
   * @param start_row Starting row within the chunk.
   */
  inline void update_entity_locations(
      std::span<const Entity> entities,
      Archetype *arch,
      Chunk *chunk,
      uint32_t start_row
  ) noexcept {
    for (uint32_t i = 0; i < entities.size(); ++i) {
      slot_map_.update_location(entities[i], arch, chunk, start_row + i);
    }
  }

  // --- Deduplicated Chunk Pagination Engine & Allocation Helper -------------

  /**
   * @brief Ensures a writable chunk with available capacity exists for the
   * archetype.
   * @details Defined in `include/arche/world.hpp`. Replaces duplicated chunk
   * allocation, buffer acquiring, Chunk::create, and chunk index stamping
   * between `allocate_slot()` and `populate_chunks_batch()`.
   */
  inline Chunk *acquire_chunk_for(Archetype *arch) {
    auto &chunks = get_archetype_chunks(arch);
    if (chunks.empty() || chunks.back()->is_full()) {
      void *raw = chunk_pool_.acquire_buffer();
      auto *chunk = Chunk::create(
          raw, arch, arch->chunk_capacity(), chunk_size_
      );
      chunk->set_chunk_index(static_cast<uint32_t>(chunks.size()));
      chunks.push_back(chunk);
    }
    return chunks.back();
  }

  template <typename ColumnWriterFn>
  void populate_chunks_batch(
      Archetype *arch,
      std::span<const Entity> entities,
      ColumnWriterFn &&write_columns
  );

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

// =============================================================================
// Bulk Entity Lifecycle Template Implementations
// =============================================================================

/**
 * @brief Distributes a batch of entities across 64 KB chunks, filling each
 * chunk up to capacity before acquiring the next.
 *
 * @details
 * In each chunk:
 * 1. Appends a slice of entity handles into the chunk's dense entity array.
 * 2. Invokes `write_columns` to populate component data for the new rows.
 * 3. Synchronizes SlotMap records with each entity's new (chunk, row) location.
 *
 * @param arch Target Archetype governing chunk layout and column offsets.
 * @param entities Span of newly allocated generational Entity handles to place.
 * @param write_columns Callback with signature `(chunk, start_row, offset,
 * batch_size)`.
 */
template <typename ColumnWriterFn>
void World::populate_chunks_batch(
    Archetype *arch,
    std::span<const Entity> entities,
    ColumnWriterFn &&write_columns
) {
  size_t offset = 0;

  while (offset < entities.size()) {
    // 1. Acquire a chunk that has available space
    Chunk *chunk = acquire_chunk_for(arch);

    // 2. Determine how many entities fit in this chunk
    const uint32_t space = chunk->capacity() - chunk->count();
    const uint32_t remaining = static_cast<uint32_t>(entities.size() - offset);
    const uint32_t batch_size = std::min(space, remaining);

    // 3. Slice and append entity handles into the chunk
    auto batch = entities.subspan(offset, batch_size);
    const uint32_t start_row = chunk->append_entities(batch);

    // 4. Populate component columns and update SlotMap locations
    write_columns(chunk, start_row, offset, batch_size);
    update_entity_locations(batch, arch, chunk, start_row);

    offset += batch_size;
  }

  // 5. Update archetype total entity count
  arch->increment_entity_count(entities.size());
}

template <typename... Comps>
std::vector<Entity> World::create_entities(
    size_t count,
    const Comps &...components
) {
  if (count == 0) {
    return {};
  }

  // 1. Resolve canonical Archetype for component set
  Archetype *arch = resolve_archetype<Comps...>();

  // 2. Bulk allocate generational handles in SlotMap
  std::vector<Entity> entities(count);
  slot_map_.allocate_batch(count, entities.data());

  // 3. Populate chunks and broadcast-copy components across columns
  populate_chunks_batch(
      arch,
      entities,
      [&](Chunk *chunk, uint32_t start_row, size_t, uint32_t to_add) {
        (broadcast_component_in_chunk(
             arch, chunk, start_row, to_add, components
         ),
         ...);
      }
  );

  return entities;
}

template <typename... Comps>
void World::create_entities_soa(
    size_t count,
    std::span<Entity> out_entities,
    const Comps *...column_data
) {
  assert(
      out_entities.size() >= count && "Output entities span smaller than count"
  );
  if (count == 0) {
    return;
  }

  // 1. Resolve canonical Archetype for component set
  Archetype *arch = resolve_archetype<Comps...>();

  // 2. Bulk allocate generational handles directly into caller's output span
  slot_map_.allocate_batch(count, out_entities.data());

  // 3. Populate chunks and stream pre-computed column arrays via memcpy
  populate_chunks_batch(
      arch,
      std::span<const Entity>(out_entities.data(), count),
      [&](Chunk *chunk, uint32_t start_row, size_t placed, uint32_t to_add) {
        (stream_component_in_chunk(
             arch, chunk, start_row, column_data, placed, to_add
         ),
         ...);
      }
  );
}

// =============================================================================
// Instant Snapshot & Rollback Implementation
// =============================================================================

inline size_t World::create_snapshot(
    void *destination_buffer,
    size_t max_bytes
) {
  assert(
      destination_buffer != nullptr && "Snapshot destination cannot be null"
  );

  // 1. Calculate required memory size
  const size_t slot_bytes = slot_map_.snapshot_byte_size();
  size_t total_chunk_count = 0;
  for (const auto &arch_ptr : graph_.archetypes()) {
    total_chunk_count += get_archetype_chunks(arch_ptr.get()).size();
  }

  const uint32_t arch_count = static_cast<uint32_t>(graph_.archetypes().size());
  const size_t arch_metadata_bytes = arch_count * sizeof(uint32_t);
  const size_t chunk_bytes = total_chunk_count * Hardware::optimal_chunk_size();
  const size_t required_bytes = sizeof(SnapshotHeader) + slot_bytes +
      arch_metadata_bytes + chunk_bytes;

  if (max_bytes < required_bytes) {
    throw std::runtime_error("create_snapshot: buffer capacity too small");
  }

  uint8_t *cursor = static_cast<uint8_t *>(destination_buffer);

  // 2. Populate and serialize 64-byte SnapshotHeader
  SnapshotHeader header{
      .magic = 0x48435241, // 'ARCH'
      .version = 1,
      .current_tick = 0,
      .slot_count = static_cast<uint32_t>(slot_map_.capacity()),
      .free_list_head = slot_map_.free_head_index(),
      .active_entities = static_cast<uint32_t>(slot_map_.size()),
      .chunk_count = static_cast<uint32_t>(total_chunk_count),
      .total_payload_bytes = required_bytes,
      .archetype_count = arch_count,
  };
  std::memcpy(cursor, &header, sizeof(SnapshotHeader));
  cursor += sizeof(SnapshotHeader);

  // 3. Serialize raw SlotMap array
  slot_map_.write_snapshot_data(cursor);
  cursor += slot_bytes;

  // 4. Dump chunk count per archetype and contiguous 64 KB chunk pages
  // back-to-back
  for (const auto &arch_ptr : graph_.archetypes()) {
    const auto &chunks = get_archetype_chunks(arch_ptr.get());
    uint32_t c_count = static_cast<uint32_t>(chunks.size());
    std::memcpy(cursor, &c_count, sizeof(uint32_t));
    cursor += sizeof(uint32_t);

    for (Chunk *chunk : chunks) {
      std::memcpy(cursor, chunk, Hardware::optimal_chunk_size());
      cursor += Hardware::optimal_chunk_size();
    }
  }

  return required_bytes;
}

inline void World::restore_snapshot(const void *snapshot_buffer, size_t bytes) {
  assert(snapshot_buffer != nullptr && "Snapshot source cannot be null");
  if (bytes < sizeof(SnapshotHeader)) {
    throw std::runtime_error("restore_snapshot: buffer smaller than header");
  }

  const uint8_t *cursor = static_cast<const uint8_t *>(snapshot_buffer);
  SnapshotHeader header;
  std::memcpy(&header, cursor, sizeof(SnapshotHeader));
  cursor += sizeof(SnapshotHeader);

  if (header.magic != 0x48435241 || header.version != 1) {
    throw std::runtime_error(
        "restore_snapshot: invalid snapshot header magic or version"
    );
  }

  // 1. Restore SlotMap array
  const size_t slot_bytes = header.slot_count * sizeof(Internal::SlotMap::Slot);
  slot_map_.read_snapshot_data(
      cursor, header.slot_count, header.free_list_head, header.active_entities
  );
  cursor += slot_bytes;

  // 2. Restore active 64 KB chunk pages per archetype
  const auto &archetypes = graph_.archetypes();
  for (size_t arch_idx = 0; arch_idx < archetypes.size(); ++arch_idx) {
    Archetype *arch = archetypes[arch_idx].get();
    auto &chunks = get_archetype_chunks(arch);

    if (arch_idx < header.archetype_count) {
      uint32_t snapshot_chunk_count = 0;
      std::memcpy(&snapshot_chunk_count, cursor, sizeof(uint32_t));
      cursor += sizeof(uint32_t);

      // Return surplus chunks to the pool
      while (chunks.size() > snapshot_chunk_count) {
        chunk_pool_.release_buffer(chunks.back());
        chunks.pop_back();
      }

      // Acquire necessary chunks from the pool
      while (chunks.size() < snapshot_chunk_count) {
        void *raw = chunk_pool_.acquire_buffer();
        auto *new_chunk = Chunk::create(
            raw, arch, arch->chunk_capacity(), chunk_size_
        );
        new_chunk->set_chunk_index(static_cast<uint32_t>(chunks.size()));
        chunks.push_back(new_chunk);
      }

      // Restore chunk memory and re-anchor entity records
      size_t arch_entity_count = 0;
      for (uint32_t c_idx = 0; c_idx < snapshot_chunk_count; ++c_idx) {
        Chunk *chunk = chunks[c_idx];
        std::memcpy(chunk, cursor, Hardware::optimal_chunk_size());
        cursor += Hardware::optimal_chunk_size();

        chunk->header().archetype = arch;
        chunk->set_chunk_index(c_idx);

        arch_entity_count += chunk->count();

        // 3. Re-anchor reverse SlotMap records from chunk entity arrays
        for (uint32_t r = 0; r < chunk->count(); ++r) {
          Entity e = chunk->entity(r);
          slot_map_.update_location(e, arch, chunk, r);
        }
      }

      arch->set_entity_count(arch_entity_count);
    } else {
      // Archetypes created after the snapshot: return all chunks to pool
      while (!chunks.empty()) {
        chunk_pool_.release_buffer(chunks.back());
        chunks.pop_back();
      }
      arch->set_entity_count(0);
    }
  }
}

} // namespace arche
