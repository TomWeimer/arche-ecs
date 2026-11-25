module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

export module arche:world;

export import :entity;
export import :entity_view;
export import :domain.archetype;
export import :domain.command;
export import :domain.entity;
export import :domain.resource;
export import :domain.snapshot;
export import :domain.system;
export import :system_context;
export import :query;
export import :mutation;
export import :command;
export import :sort;

import :world.detail.world_storage;

export namespace arche {

// =============================================================================
// World: Central Simulation Coordinator
// =============================================================================

/**
 * @class World
 * @brief Central simulation coordinator for arche-ecs.
 *
 * ### Quick API Guide
 * - **Spawning & Destruction:**  `world.spawn(...)`, `world.despawn(...)`
 * - **Chained Handle Access:**    `world.entity(id)` -> `.get()`, `.get_mut()`,
 * `.add()`, `.remove()`, `.despawn()`
 * - **Direct Component Access:**  `world.get<T>(id)` (const),
 * `world.get_mut<T>(id)` (dirty-tracked)
 * - **Streaming Queries:**        `world.query<Position, const
 * Shield*>(Without<Dead>{})`
 *
 * ### Domain Sub-Facades
 * - `world.entities`   -> Entity alive checks, total count, slot resolution.
 * - `world.resources`  -> Global singleton resources (Time, Input, Config).
 * - `world.snapshots`  -> Binary persistence (save / load world state).
 * - `world.systems`    -> Stage scheduler and pipeline execution.
 * - `world.archetypes` -> Structural graph metrics and chunk memory inspection.
 */
class World {
private:
  internal::WorldStorage *storage_{nullptr};

public:
  // --- Internal Storage Access -----------------------------------------------

  /// Direct access to internal storage for world algorithms and modules.
  [[nodiscard]] internal::WorldStorage *storage() noexcept { return storage_; }
  [[nodiscard]] const internal::WorldStorage *storage() const noexcept { return storage_; }

  // --- Domain Sub-Facades ----------------------------------------------------

  /// Global entity table introspection: count, capacity, contains, and slot resolution.
  EntityFacade entities;

  /// Serialization & persistence: saves and loads complete simulation state to binary buffers.
  SnapshotFacade snapshots;

  /// Global singleton resources independent of entity archetypes.
  ResourceFacade resources;

  /// Low-level archetype graph metrics and chunk memory layout inspection.
  ArchetypeFacade archetypes;

  /// Stage-based system scheduler: register stages, systems, and execution pipelines.
  SystemFacade systems;

  /// Deferred command buffer management: commit or clear world-level command buffers.
  CommandFacade commands;

  // --- Constructors & Lifecycle ----------------------------------------------

  /// Constructs an ECS World with the specified chunk memory page size
  /// (default: 64 KB) and worker thread pool size (default: host CPU core count).
  explicit World(
      size_t chunk_size = 65536,
      uint32_t thread_count = internal::Hardware::core_count()
  );
  ~World();

  World(const World &) = delete;
  World &operator=(const World &) = delete;
  World(World &&) = delete;
  World &operator=(World &&) = delete;

  /// Reconfigures the world's internal worker thread pool.
  void set_threads(uint32_t count);

  /// Retrieves a reference to the world's internal worker thread pool.
  [[nodiscard]] internal::ThreadPool &thread_pool() noexcept;
  [[nodiscard]] const internal::ThreadPool &thread_pool() const noexcept;

  /// Returns the number of worker threads in the world's thread pool.
  [[nodiscard]] uint32_t thread_count() const noexcept;

  /// Destroys all active entities and recycles all allocated chunks back to the
  /// pool.
  void clear();

  // --- Entity Lifecycle (Creation & Destruction) -----------------------------

  /// Spawns a single entity from individual component values.
  /// ```cpp
  /// // Example:
  /// Entity e = world.spawn(Position{0.0f, 0.0f}, Velocity{1.0f, 0.0f});
  /// ```
  template <Component... Comps>
    requires(!(std::is_same_v<std::decay_t<Comps>, Batch> || ...))
  Entity spawn(Comps &&...components);

  /// Batch spawn with broadcast replication (clones component values across
  /// count entities).
  /// ```cpp
  /// // Example:
  /// auto ids = world.spawn({.count = 1000}, Position{0.0f, 0.0f},
  /// Velocity{1.0f, 0.0f});
  /// ```
  template <Component... Comps>
    requires(!(std::is_same_v<std::decay_t<Comps>, std::span<Entity>> || ...))
  std::vector<Entity> spawn(Batch batch, const Comps &...components);

  /// High-performance zero-allocation SoA column streaming into pre-allocated
  /// memory.
  /// ```cpp
  /// // Example:
  /// world.spawn({.count = N}, out_span, pos_ptr, vel_ptr);
  /// ```
  template <Component... Comps>
  void spawn(
      Batch batch,
      std::span<Entity> out_entities,
      const Comps *...column_data
  );

  /// Destroys a single entity, recycling its slot and updating the chunk via
  /// Swap-and-Pop. Returns true if the entity was alive and successfully
  /// destroyed.
  bool despawn(Entity entity);

  /// Bulk despawns a batch of entities.
  void despawn(std::span<const Entity> entities);

  // --- Entity Handle Views (Const & Mutable) ---------------------------------

  /// Creates a mutable entity handle for multi-step chained operations on
  /// World&.
  /// ```cpp
  /// // Example:
  /// auto e = world.entity(id);
  /// if (e.is_alive() && e.has<Position>()) {
  ///     e.get_mut<Position>().x += 5.0f;
  /// }
  /// ```
  [[nodiscard]] EntityMut entity(Entity entity) noexcept;

  /// Creates a read-only entity handle for inspections on const World&.
  /// ```cpp
  /// // Example:
  /// auto e = cworld.entity(id);
  /// if (e.is_alive()) {
  ///     const auto &p = e.get<Position>();
  /// }
  /// ```
  [[nodiscard]] EntityRef entity(Entity entity) const noexcept;

  // --- Direct Component Access -----------------------------------------------

  /// Checks whether the entity possesses component T in O(1) via archetype
  /// bitmask.
  template <Component T> [[nodiscard]] bool has(Entity entity) const noexcept;

  /// Fast one-off component read. Pure read-only; never marks chunk dirty.
  /// ```cpp
  /// // Example:
  /// const Position &p = world.get<Position>(e);
  /// ```
  template <Component T> [[nodiscard]] const T &get(Entity entity) const;

  /// Fast one-off component write. Automatically updates the chunk's dirty
  /// tick.
  /// ```cpp
  /// // Example:
  /// world.get_mut<Position>(e).x += 10.0f;
  /// ```
  template <Component T> [[nodiscard]] T &get_mut(Entity entity);

  /// Dynamically attaches component T to entity, relocating it to the target
  /// archetype.
  template <Component T> void add(Entity entity, T &&component);

  /// Dynamically removes component T from entity, relocating it to the target
  /// archetype.
  template <Component T> void remove(Entity entity);

  // --- Query Factory ---------------------------------------------------------

  /// Creates a bounded execution context for a system.
  [[nodiscard]] SystemContext create_system_context(
      CommandBuffer &cmd,
      ThreadPool *pool = nullptr,
      ParallelCommandBuffer *parallel_cmd = nullptr
  );

  /// Constructs a streaming query matching entities with requested components
  /// and optional filters. Supports With<...>, Without<...>, Mutated<T>, Added,
  /// Or<...>, and optional pointer components.
  /// ```cpp
  /// // Example:
  /// for (auto [pos, shield] : world.query<Position, const
  /// Shield*>(Without<Dead>{}) {
  ///     // ...
  /// }
  /// ```
  template <QueryTerm... Components, QueryFilter... FilterArgs>
  [[nodiscard]] Query<Components...> query(FilterArgs &&...filters);

  // --- Versioning & Epoch Inspection -----------------------------------------

  /// Monotonically increasing simulation version sequence, incremented on mutations.
  [[nodiscard]] uint32_t version() const noexcept;

  /// Previous simulation version sequence before the most recent tick advancement.
  [[nodiscard]] uint32_t previous_version() const noexcept;

  /// Advances the simulation version sequence monotonically, skipping reserved sentinels.
  uint32_t advance_version() noexcept;

  /// Sets the current simulation version sequence directly (e.g. for testing rollover).
  void set_version(uint32_t v) noexcept;

  /// Retrieves a snapshot storage view over internal storage for serialization.
  [[nodiscard]] SnapshotStorageView snapshot_storage_view() const noexcept;

  /// Resolves or instantiates a matching QueryCache for the specified inclusion/exclusion masks.
  internal::QueryCache *query_cache(
      const internal::ComponentMask &include,
      const internal::ComponentMask &exclude,
      const internal::ComponentMask &any = {}
  );

  // --- Mutation Execution Context --------------------------------------------

  /// Creates a lightweight mutation execution context view over the world storage.
  [[nodiscard]] internal::MutationContext mutation_context() noexcept;

  /// Implicit conversion enabling direct `cmd.commit(world)` syntax without upward dependencies.
  operator internal::MutationContext() noexcept;

  // --- System Execution View -------------------------------------------------

  /// Creates a decoupled execution view over the world storage for the scheduler.
  [[nodiscard]] internal::SystemExecutionTarget system_execution_target() noexcept;

  /// Implicit conversion enabling `scheduler.run(world, pool)` and `scheduler.compile(world)` syntax.
  operator internal::SystemExecutionTarget() noexcept;
};

// --- Spatial Locality Sorting Algorithms -------------------------------------

/**
 * @brief Sorts all archetypes containing component `Comp` matching `Filters...` in-place.
 *
 * Reorders entity rows and chunk columns to achieve contiguous spatial cache locality.
 */
template <typename Component, typename... Filters, typename Compare>
void sort(World &world, Compare &&comp);

} // namespace arche
