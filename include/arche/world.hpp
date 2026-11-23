#pragma once

#include "arche/types/entity.hpp"
#include "arche/types/entity_ref.hpp"

#include "arche/contracts/archetype.hpp"
#include "arche/contracts/entity.hpp"
#include "arche/contracts/gpu.hpp"
#include "arche/contracts/resource.hpp"
#include "arche/contracts/snapshot.hpp"
#include "arche/contracts/system.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace arche::internal {
struct WorldAccess;
struct WorldStorage;
}

namespace arche {

template <typename... Components> class Query;

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
 * - `world.gpu`        -> GPU buffer layout and shader reflection.
 * - `world.archetypes` -> Structural graph metrics and chunk memory inspection.
 */
class World {
  friend struct internal::WorldAccess;

private:
  internal::WorldStorage *storage_{nullptr};

public:
  // --- Domain Sub-Facades ----------------------------------------------------

  /// Global entity table introspection: count, capacity, contains, and slot
  /// resolution.
  EntityFacade entities{this};

  /// Serialization & persistence: saves and loads complete simulation state to
  /// binary buffers.
  SnapshotFacade snapshots{this};

  /// Global singleton resources independent of entity archetypes.
  ResourceFacade resources{this};

  /// Low-level archetype graph metrics and chunk memory layout inspection.
  ArchetypeFacade archetypes{this};

  /// Stage-based system scheduler: register stages, systems, and execution
  /// pipelines.
  SystemFacade systems{this};

  /// GPU memory reflection: inspect contiguous component columns and stream to
  /// GPU buffers.
  GpuFacade gpu{this};

  // --- Constructors & Lifecycle ----------------------------------------------

  /// Constructs an ECS World with the specified chunk memory page size
  /// (default: 64 KB).
  explicit World(size_t chunk_size = 65536);
  ~World();

  World(const World &) = delete;
  World &operator=(const World &) = delete;
  World(World &&) = delete;
  World &operator=(World &&) = delete;

  /// Destroys all active entities and recycles all allocated chunks back to the
  /// pool.
  void clear();

  // --- Entity Lifecycle (Creation & Destruction) -----------------------------

  /// Spawns a single entity from individual component values.
  /// ```cpp
  /// // Example:
  /// Entity e = world.spawn(Position{0.0f, 0.0f}, Velocity{1.0f, 0.0f});
  /// ```
  template <typename... Comps>
    requires(!(std::is_same_v<std::decay_t<Comps>, Batch> || ...))
  inline Entity spawn(Comps &&...components);

  /// Batch spawn with broadcast replication (clones component values across
  /// count entities).
  /// ```cpp
  /// // Example:
  /// auto ids = world.spawn({.count = 1000}, Position{0.0f, 0.0f},
  /// Velocity{1.0f, 0.0f});
  /// ```
  template <typename... Comps>
    requires(!(std::is_same_v<std::decay_t<Comps>, std::span<Entity>> || ...))
  inline std::vector<Entity> spawn(Batch batch, const Comps &...components);

  /// High-performance zero-allocation SoA column streaming into pre-allocated
  /// memory.
  /// ```cpp
  /// // Example:
  /// world.spawn({.count = N}, out_span, pos_ptr, vel_ptr);
  /// ```
  template <typename... Comps>
  inline void spawn(
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
  [[nodiscard]] inline EntityMut entity(Entity entity) noexcept;

  /// Creates a read-only entity handle for inspections on const World&.
  /// ```cpp
  /// // Example:
  /// auto e = cworld.entity(id);
  /// if (e.is_alive()) {
  ///     const auto &p = e.get<Position>();
  /// }
  /// ```
  [[nodiscard]] inline EntityRef entity(Entity entity) const noexcept;

  // --- Direct Component Access -----------------------------------------------

  /// Checks whether the entity possesses component T in O(1) via archetype
  /// bitmask.
  template <typename T>
  [[nodiscard]] inline bool has(Entity entity) const noexcept;

  /// Fast one-off component read. Pure read-only; never marks chunk dirty.
  /// ```cpp
  /// // Example:
  /// const Position &p = world.get<Position>(e);
  /// ```
  template <typename T> [[nodiscard]] inline const T &get(Entity entity) const;

  /// Fast one-off component write. Automatically updates the chunk's dirty
  /// tick.
  /// ```cpp
  /// // Example:
  /// world.get_mut<Position>(e).x += 10.0f;
  /// ```
  template <typename T> [[nodiscard]] inline T &get_mut(Entity entity);

  /// Dynamically attaches component T to entity, relocating it to the target
  /// archetype.
  template <typename T> inline void add(Entity entity, T &&component);

  /// Dynamically removes component T from entity, relocating it to the target
  /// archetype.
  template <typename T> inline void remove(Entity entity);

  // --- Query Factory ---------------------------------------------------------

  /// Constructs a streaming query matching entities with requested components
  /// and optional filters. Supports With<...>, Without<...>, Mutated<T>, Added,
  /// Or<...>, and optional pointer components.
  /// ```cpp
  /// // Example:
  /// for (auto [pos, shield] : world.query<Position, const
  /// Shield*>(Without<Dead>{})) {
  ///     // ...
  /// }
  /// ```
  template <typename... Components, typename... FilterArgs>
  [[nodiscard]] Query<Components...> query(FilterArgs &&...filters);

  // --- Versioning & Epoch Inspection -----------------------------------------

  /// Monotonically increasing simulation version sequence, incremented on
  /// mutations.
  [[nodiscard]] uint32_t version() const noexcept;
};

} // namespace arche

#include "world/world_access.hpp"
#include "arche/command.hpp"
#include "component/component_access.hpp"
#include "entity/entity_allocator.hpp"
#include "component/component_access.inl"
#include "entity/entity_allocator.inl"
#include "entity/entity_view.inl"
#include "world/world_facades_impl.hpp"
#include "command/command_buffer.inl"