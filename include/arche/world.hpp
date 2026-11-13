#pragma once

#include "arche/chunk.hpp"
#include "arche/detail/archetype_graph.hpp"
#include "arche/detail/chunk_pool.hpp"
#include "arche/detail/query_cache.hpp"
#include "arche/detail/slot_map.hpp"
#include "arche/entity.hpp"
#include "arche/gpu_reflection.hpp"
#include "arche/thread_pool.hpp"
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
class ParallelCommandBuffer;
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

  /// Global monotonic simulation tick counter (starts at 1; 0 is reserved).
  uint32_t current_tick_{1};

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

  /// Central pool of deduplicated query caches.
  std::unordered_map<
      QueryCacheKey,
      std::unique_ptr<QueryCache>,
      QueryCacheKeyHash>
      query_caches_{};

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
  inline size_t save_snapshot(void *destination_buffer, size_t max_bytes);

  /**
   * @brief Restores the entire world state from a raw memory snapshot buffer in
   * < 1 ms.
   *
   * @param snapshot_buffer Pointer to snapshot memory.
   * @param bytes Size of snapshot buffer in bytes.
   */
  inline void load_snapshot(const void *snapshot_buffer, size_t bytes);

  // --- In-Place Archetype Permutation & Cache Locality -----------------------

  /**
   * @brief Permutes all component columns and entity handles in an archetype
   * in-place.
   *
   * @details
   * Applies cycle-chasing permutation across all chunks in the archetype in
   * O(N) time. Updates all corresponding SlotMap records so all existing Entity
   * handles remain valid.
   *
   * @param arch Pointer to the target Archetype to permute.
   * @param target_indices Span of size N where target_indices[i] indicates the
   * source index whose entity data should be placed at destination index i.
   */
  void permute(
      Archetype *arch,
      std::span<const uint32_t> target_indices
  );

  /**
   * @brief Sorts all entities in an archetype in-place based on a component
   * comparator.
   *
   * @tparam Component Component type to sort by.
   * @tparam Compare Strict weak ordering comparator accepting (const
   * Component&, const Component&).
   * @param arch Pointer to target Archetype.
   * @param comp Comparator callable.
   */
  template <typename Component, typename Compare>
  void sort(Archetype *arch, Compare &&comp);

  // --- Double-Buffered Component Columns -------------------------------------

  /**
   * @brief Registers component T as double-buffered with dual read/write chunk
   * slices.
   */
  template <typename T> inline void enable_double_buffer() {
    registry_.register_double_buffered<std::decay_t<T>>();
  }

  /**
   * @brief Performs an O(1) pointer-flip swapping read and write slices for
   * component T, advancing the simulation tick and stamping chunk dirty
   * versions automatically.
   */
  template <typename T> inline void flip() noexcept {
    const TypeID tid = type_id<std::decay_t<T>>();
    tick(); // Advances monotonic simulation tick
    for (const auto &arch_ptr : graph_.archetypes()) {
      arch_ptr->flip_double_buffer(tid);
      for (Chunk *chunk : get_archetype_chunks(arch_ptr.get())) {
        mark_dirty<T>(chunk); // Automatically synchronize dirty version!
      }
    }
  }

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
   * @brief Retrieves the storage record for a live entity.
   */
  [[nodiscard]] const EntityRecord &get_record(Entity entity) const {
    return get_alive_record(entity);
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
   * @tparam Comps Component types to attach to the entity.
   * @param components Values for each component.
   * @return The newly created Entity handle.
   */
  template <typename... Comps> Entity spawn(Comps &&...components) {
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

    // 5. Mark the chunk as dirty
    chunk->advance_structure_version(current_tick_);
    (mark_dirty<std::decay_t<Comps>>(chunk), ...);

    return entity;
  }

  // --- Bulk Entity Lifecycle -------------------------------------------------

  /**
   * @brief Spawns N entities initialized with identical component values in
   * a single high-throughput batch (Component Broadcasting).
   *
   * @tparam Comps Component types to attach to every spawned entity.
   * @param count Number of entities to spawn (must be >= 0).
   * @param components Component values broadcast-copied to each entity.
   * @return std::vector<Entity> Vector containing the N newly created
   * generational handles.
   */
  template <typename... Comps>
  requires (!(std::is_same_v<std::decay_t<Comps>, std::span<Entity>> || ...))
  std::vector<Entity> spawn_batch(size_t count, const Comps &...components);

  /**
   * @brief Spawns N entities from pre-computed contiguous external column
   * arrays with zero heap allocations (Direct Structure-of-Arrays Streaming).
   *
   * @tparam Comps Component types corresponding to each input column array.
   * @param count Number of entities to spawn.
   * @param out_entities Output span receiving the allocated Entity handles.
   * @param column_data Pointers to contiguous arrays of source component data
   * (`const Comp*`).
   */
  template <typename... Comps>
  void spawn_batch(
      size_t count,
      std::span<Entity> out_entities,
      const Comps *...column_data
  );

  /// Destroys an entity and reclaims its slot in constant time.
  bool despawn(Entity entity);

  /**
   * @brief Destroys a batch of entities in a single cache-efficient pass.
   *
   * @param entities Span of Entity handles to destroy.
   */
  void despawn_batch(std::span<const Entity> entities);

  // --- Component Management --------------------------------------------------

  /**
   * @brief Checks if an entity has a specific component.
   *
   * @tparam T Component type to test for.
   * @param entity Entity handle to inspect.
   * @return true if the entity has the component; false otherwise.
   */
  template <typename T>
  [[nodiscard]] bool has(Entity entity) const noexcept {
    const EntityRecord *record = slot_map_.find_record(entity);
    if (!record || !record->archetype) {
      return false;
    }
    return record->archetype->has_component(type_id<T>());
  }

  /**
   * @brief Returns a reference to an entity's component.
   *
   * @tparam T Component type to access.
   * @param entity Entity handle.
   * @return Mutable reference to the requested component.
   */
  template <typename T> [[nodiscard]] T &get(Entity entity) {
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
   * @brief Returns a const reference to an entity's component.
   *
   * @tparam T Component type to access.
   * @param entity Entity handle.
   * @return Const reference to the requested component.
   */
  template <typename T> [[nodiscard]] const T &get(Entity entity) const {
    const EntityRecord &record = get_alive_record(entity);
    Archetype *arch = record.archetype;

    const TypeID tid = type_id<T>();
    const int16_t col = arch->column_index(tid);
    assert(col >= 0 && "Component not present on entity");

    const Chunk *chunk = record.chunk;
    assert(chunk != nullptr);

    const auto &layout = arch->column_layout(static_cast<size_t>(col));
    const void *ptr = chunk->component_element(layout, record.row);
    return *reinterpret_cast<const T *>(ptr);
  }

  /**
   * @brief Attaches a new component to an existing entity.
   *
   * @tparam T Component type to add.
   * @param entity Entity handle to modify.
   * @param component Component data to attach.
   */
  template <typename T> void add(Entity entity, T &&component) {
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
   * @tparam T Component type to remove.
   * @param entity Entity handle to modify.
   */
  template <typename T> void remove(Entity entity) {
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

  // --- Simulation Ticking & Change Detection ---------------------------------

  /**
   * @brief Returns the current monotonic simulation tick.
   */
  [[nodiscard]] inline uint32_t current_tick() const noexcept {
    return current_tick_;
  }

  /**
   * @brief Advances the world simulation tick by 1 (skipping 0 on wrap-around).
   * @details Uses `advance_nonzero` (defined in `include/arche/entity.hpp`) to
   * skip sentinel 0 on 32-bit wrap-around.
   *
   * @return The newly incremented simulation tick.
   */
  inline uint32_t tick() noexcept {
    current_tick_ = advance_nonzero(current_tick_);
    return current_tick_;
  }

  /**
   * @brief Marks a specific component column as dirty in the target chunk.
   *
   * @tparam T Component type that was modified.
   * @param chunk Chunk containing the component column.
   */
  template <typename T> inline void mark_dirty(Chunk *chunk) noexcept {
    assert(chunk != nullptr);
    Archetype *arch = chunk->archetype();
    const int16_t col = arch->column_index(type_id<T>());
    if (col >= 0) {
      chunk->mark_column_dirty(static_cast<uint16_t>(col), current_tick_);
    }
  }

  /**
   * @brief Marks a component column dirty for the chunk holding an entity.
   * @details Uses `slot_map_.find_record` (defined in
   * `include/arche/slot_map.hpp`) to combine liveness validation and record
   * retrieval into a single branch.
   *
   * @tparam T Component type that was modified.
   * @param entity Entity whose component was modified.
   */
  template <typename T>
  inline void mark_dirty(Entity entity) noexcept {
    const EntityRecord *rec = slot_map_.find_record(entity);
    if (rec && rec->chunk) {
      mark_dirty<T>(rec->chunk);
    }
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
        const T *col_ptr = chunk->typed_slice<T>(layout.chunk_byte_offset);

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

  inline std::span<Chunk *const> get_archetype_chunks(
      const Archetype *arch
  ) const {
    if (arch->id() >= archetype_chunks_.size()) {
      return {};
    }
    return std::span<Chunk *const>(
        archetype_chunks_[arch->id()].data(),
        archetype_chunks_[arch->id()].size()
    );
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

  // --- Query Cache Management ------------------------------------------------

  /**
   * @brief Retrieves or creates a deduplicated QueryCache for the given
   * component masks.
   *
   * @param include Component inclusion mask.
   * @param exclude Component exclusion mask.
   * @return Pointer to shared QueryCache.
   */
  inline QueryCache *get_or_create_query_cache(
      const ComponentMask &include,
      const ComponentMask &exclude
  ) {
    QueryCacheKey key{include, exclude};
    auto it = query_caches_.find(key);
    if (it != query_caches_.end()) {
      return it->second.get();
    }

    // 1. Construct new QueryCache
    auto cache = std::make_unique<QueryCache>();
    cache->include_mask = include;
    cache->exclude_mask = exclude;

    // 2. Initial scan over currently existing archetypes in graph
    for (const auto &arch_ptr : graph_.archetypes()) {
      const ComponentMask &mask = arch_ptr->mask();
      if (mask.contains_all(include)) {
        if (exclude.empty() || !mask.contains_any(exclude)) {
          cache->matching_archetypes.push_back(arch_ptr.get());
        }
      }
    }

    QueryCache *raw_cache = cache.get();
    query_caches_.emplace(key, std::move(cache));

    // 3. Register cache with graph for reactive updates on future archetypes
    graph_.register_query_cache(raw_cache);

    return raw_cache;
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

  // --- Parallel Chunk Query & Work-Stealing ----------------------------------

  /**
   * @brief Executes a chunk-level parallel query across worker threads using
   * lock-free work-stealing.
   */
  template <typename... Components, typename Func>
  inline void query_parallel(ThreadPool &pool, Func &&func);

  /**
   * @brief Executes a parallel query with lock-free thread-local mutation
   * recording.
   */
  template <typename... Components, typename Func>
  inline void query_parallel(
      ThreadPool &pool,
      ParallelCommandBuffer &cmd_buffers,
      Func &&func
  );

  /**
   * @brief Sequentially replays and clears all thread-local command buffers
   * recorded during parallel work.
   */
  inline void commit(ParallelCommandBuffer &cmd_buffers);

  /**
   * @brief Sequentially replays and clears a single command buffer.
   */
  inline void commit(CommandBuffer &cmd_buffer);

  // --- Triple-Buffered GPU Ring Streaming & Multi-Draw Indirect --------------

  /**
   * @brief Streams component data to one of three mapped VRAM ring slots with
   * optional chunk culling.
   */
  struct PassAllCullingPredicate {
    constexpr bool operator()(const ChunkHeader &) const noexcept {
      return true;
    }
  };

  template <typename T, typename CullingPredicate = PassAllCullingPredicate>
  inline size_t stream_gpu(
      uint32_t ring_index,
      void *ring_buffer_base,
      size_t ring_slot_capacity_bytes,
      CullingPredicate &&is_visible = PassAllCullingPredicate{}
  );

  /**
   * @brief Emits a contiguous array of DrawIndirectCommand structs for
   * single-call multi-draw rendering, with optional chunk culling via user
   * metadata predicate.
   */
  template <typename CullingPredicate = PassAllCullingPredicate>
  inline size_t indirect_draw(
      Archetype *arch,
      void *destination_buffer,
      size_t max_commands,
      CullingPredicate &&is_visible = PassAllCullingPredicate{}
  );

private:
  // --- Inlined Template / Lookup Helpers -------------------------------------

  /**
   * @brief Validates that an entity is alive and returns its record.
   */
  [[nodiscard]] const EntityRecord &get_alive_record(Entity entity) const {
    const EntityRecord *record = slot_map_.find_record(entity);
    assert(
        record != nullptr &&
        "Attempted to access component on dead or invalid entity"
    );
    assert(record->archetype != nullptr && "Entity has no archetype");
    return *record;
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

  // --- Parallel Chunk Work-Stealing Driver -----------------------------------

  template <typename... Components, typename WorkerFn>
  inline void dispatch_parallel_chunks(ThreadPool &pool, WorkerFn &&worker);

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
requires (!(std::is_same_v<std::decay_t<Comps>, std::span<Entity>> || ...))
std::vector<Entity> World::spawn_batch(
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

        // 3.5 Mark the chunks as dirty
        chunk->advance_structure_version(current_tick_);
        (mark_dirty<std::decay_t<Comps>>(chunk), ...);
      }
  );

  return entities;
}

template <typename... Comps>
void World::spawn_batch(
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

        // 3.5 Mark the chunks as dirty
        chunk->advance_structure_version(current_tick_);
        (mark_dirty<std::decay_t<Comps>>(chunk), ...);
      }
  );
}

// =============================================================================
// In-Place Comparator Sorting Template Implementation
// =============================================================================

template <typename Component, typename Compare>
void World::sort(Archetype *arch, Compare &&comp) {
  assert(arch != nullptr);
  const size_t total = arch->entity_count();
  if (total <= 1) {
    return;
  }

  const auto &chunks = get_archetype_chunks(arch);
  const ComponentColumnLayout *layout = arch->column_layout_for(
      type_id<Component>()
  );
  assert(
      layout != nullptr && "Archetype does not contain component to sort by"
  );

  // 1. Gather component keys and their original global indices contiguously.
  // This avoids 20+ million expensive integer divisions and modulos during std::sort.
  struct KeyIndex {
    Component key;
    uint32_t index;
  };

  std::vector<KeyIndex> keys(total);
  uint32_t global_idx = 0;
  for (Chunk *chunk : chunks) {
    const uint32_t count = chunk->count();
    const Component *comp_ptr = static_cast<const Component *>(
        chunk->component_element(*layout, 0)
    );
    for (uint32_t r = 0; r < count; ++r) {
      keys[global_idx] = KeyIndex{comp_ptr[r], global_idx};
      ++global_idx;
    }
  }

  // 2. Sort the contiguous key-index array in CPU cache with zero chunk indirection
  std::sort(
      keys.begin(),
      keys.end(),
      [&](const KeyIndex &a, const KeyIndex &b) { return comp(a.key, b.key); }
  );

  // 3. Extract the permutation target indices
  std::vector<uint32_t> sorted_indices(total);
  for (size_t i = 0; i < total; ++i) {
    sorted_indices[i] = keys[i].index;
  }

  // 4. Apply the sorted permutation in-place via cycle chasing
  permute(arch, sorted_indices);
}

// =============================================================================
// Parallel Query & Ring Streaming Implementations
// =============================================================================

// --- Unified Parallel Work-Stealing Driver -----------------------------------

template <typename... Components, typename WorkerFn>
inline void World::dispatch_parallel_chunks(
    ThreadPool &pool,
    WorkerFn &&worker
) {
  auto q = query<Components...>();

  struct ChunkTask {
    Chunk *chunk{nullptr};
    std::array<uint32_t, sizeof...(Components)> offsets;
  };

  std::vector<ChunkTask> tasks;
  // Pre-resolve offsets per archetype to avoid redundant lookups on worker
  // threads
  q.for_each_active_chunk([&](Chunk *chunk, Archetype *arch) {
    tasks.push_back(
        {chunk,
         {Query<Components...>::template resolve_column_offset<Components>(
             arch
         )...}}
    );
  });

  if (tasks.empty()) {
    return;
  }

  const size_t total_tasks = tasks.size();
  std::atomic<size_t> next_task{0};

  pool.parallel_for([&](uint32_t thread_id) {
    while (true) {
      const size_t idx = next_task.fetch_add(1, std::memory_order_relaxed);
      if (idx >= total_tasks) {
        break;
      }
      worker(thread_id, tasks[idx].chunk, tasks[idx].offsets);
    }
  });
}

template <typename... Components, typename Func>
inline void World::query_parallel(ThreadPool &pool, Func &&func) {
  auto unpack_ptrs =
      []<size_t... Is>(
          Chunk *chunk, const auto &offsets, std::index_sequence<Is...>
      ) {
        return std::make_tuple(
            Query<Components...>::template get_column_ptr<Components>(
                chunk, offsets[Is]
            )...
        );
      };

  dispatch_parallel_chunks<Components...>(
      pool, [&](uint32_t, Chunk *chunk, const auto &offsets) {
        std::apply(
            [&](auto &&...ptrs) { func(chunk->count(), ptrs...); },
            unpack_ptrs(
                chunk, offsets, std::index_sequence_for<Components...>{}
            )
        );
      }
  );
}

template <typename T, typename CullingPredicate>
inline size_t World::stream_gpu(
    uint32_t ring_index,
    void *ring_buffer_base,
    size_t ring_slot_capacity_bytes,
    CullingPredicate &&is_visible
) {
  assert(ring_buffer_base != nullptr && "Ring buffer pointer cannot be null");
  const uint32_t slot = ring_index % 3;
  uint8_t *slot_dest = static_cast<uint8_t *>(ring_buffer_base) +
      (slot * ring_slot_capacity_bytes);

  const TypeID tid = type_id<T>();
  size_t total_bytes = 0;

  for (const auto &arch_ptr : graph_.archetypes()) {
    Archetype *arch = arch_ptr.get();
    if (!arch->has_component(tid)) {
      continue;
    }

    const auto *layout = arch->column_layout_for(tid);
    assert(layout != nullptr);

    for (Chunk *chunk : get_archetype_chunks(arch)) {
      if (chunk->is_empty()) {
        continue;
      }
      if (!is_visible(chunk->header())) {
        continue;
      }
      const size_t bytes = chunk->count() * sizeof(T);
      if (total_bytes + bytes > ring_slot_capacity_bytes) {
        return total_bytes;
      }
      const void *src = chunk->component_element(*layout, 0);
      std::memcpy(slot_dest + total_bytes, src, bytes);
      total_bytes += bytes;
    }
  }

  return total_bytes;
}

template <typename CullingPredicate>
inline size_t World::indirect_draw(
    Archetype *arch,
    void *destination_buffer,
    size_t max_commands,
    CullingPredicate &&is_visible
) {
  assert(destination_buffer != nullptr && arch != nullptr);
  auto *cmds = static_cast<DrawIndirectCommand *>(destination_buffer);
  size_t written = 0;
  uint32_t vertex_offset = 0;

  for (Chunk *chunk : get_archetype_chunks(arch)) {
    if (chunk->is_empty()) {
      continue;
    }
    // Test culling predicate against chunk header (e.g. Macro AABB)
    if (!is_visible(chunk->header())) {
      vertex_offset += chunk->count();
      continue;
    }
    if (written >= max_commands) {
      break;
    }

    cmds[written] = DrawIndirectCommand{
        .vertex_count = chunk->count(),
        .instance_count = 1,
        .first_vertex = vertex_offset,
        .first_instance = 0,
    };
    vertex_offset += chunk->count();
    ++written;
  }
  return written;
}

// =============================================================================
// Instant Snapshot & Rollback Implementation
// =============================================================================

inline size_t World::save_snapshot(
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
    throw std::runtime_error("save_snapshot: buffer capacity too small");
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

inline void World::load_snapshot(const void *snapshot_buffer, size_t bytes) {
  assert(snapshot_buffer != nullptr && "Snapshot source cannot be null");
  if (bytes < sizeof(SnapshotHeader)) {
    throw std::runtime_error("load_snapshot: buffer smaller than header");
  }

  const uint8_t *cursor = static_cast<const uint8_t *>(snapshot_buffer);
  SnapshotHeader header;
  std::memcpy(&header, cursor, sizeof(SnapshotHeader));
  cursor += sizeof(SnapshotHeader);

  if (header.magic != 0x48435241 || header.version != 1) {
    throw std::runtime_error(
        "load_snapshot: invalid snapshot header magic or version"
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
