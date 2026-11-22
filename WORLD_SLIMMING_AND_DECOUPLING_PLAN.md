# World Class Slimming & Decoupling Architecture Plan

> **Target File:** [`include/arche/world.hpp`](file:///Users/student/Documents/arche-ecs/include/arche/world.hpp)  
> **Status:** Architecture Proposal & Migration Checklist  
> **Hard Invariants ([GEMINI.md](file:///Users/student/Documents/arche-ecs/GEMINI.md)):** Flat Data over Abstraction, Zero `virtual` methods, Zero abstract classes, Zero polymorphic interfaces, Zero runtime performance overhead (0 CPU cycles, 0 heap allocations).

---

## Executive Summary

`World` currently acts as both a **public coordinator** for user applications and an **internal dumping ground** for low-level engine mechanics. This document details the step-by-step strategy to:
1. **Part 1:** Eliminate the 19 `friend` declarations down to a single pass-key or storage abstraction.
2. **Part 2:** Reduce the 38 member methods on `World` down to the **12 core user-facing primitives**, eliminating redundant duplicates and relocating leaked engine plumbing into their respective domain subsystems.

---

## Part 1: Cleaning the 19 `friend` Declarations (Lines 48–71)

### Current Clutter
```cpp
// 15 Subsystem Classes
friend struct ChunkDistributor;
friend struct ComponentMigration;
friend struct ChunkDispatcher;
friend struct CommandPlayback;
friend struct EntityAllocator;
friend struct ComponentAccess;
friend class SnapshotFacade;
friend class ResourceFacade;
friend class ArchetypeFacade;
friend class SystemFacade;
friend class GpuFacade;
friend class Scheduler;
friend struct SystemContext;
friend class CommandBuffer;
friend class ParallelCommandBuffer;

// 4 Free Function Templates
friend void permute(World &world, Archetype *arch, std::span<const uint32_t> target_indices);
template <typename Component, typename... Filters, typename Compare>
friend void sort(World &world, Compare &&comp);
template <typename Component, typename Compare>
friend void sort(World &world, Archetype *arch, Compare &&comp);
template <typename Component, typename... Filters, typename KeyExtractor>
friend void sort_by_key(World &world, KeyExtractor &&key_fn);
```

### Immediate Pruning (False Friends)
Several items declared as `friend` do **not** access private fields of `World`:
- `CommandBuffer` & `ParallelCommandBuffer`: Only call public methods (`world.despawn`, `world.commit`).
- `sort` & `sort_by_key`: Only access `world.archetypes.chunks(arch)` and `world.registry()`.

### Implementation Options

#### Option 1A: Single Pass-Key Idiom (`WorldAccess`) *(Recommended)*
Define an internal accessor struct `WorldAccess` in a private/internal header:
```cpp
// include/arche/world_access.hpp
namespace arche::internal {
struct WorldAccess {
  static SlotMap &slot_map(World &w) noexcept { return w.slot_map_; }
  static ChunkPool &chunk_pool(World &w) noexcept { return w.chunk_pool_; }
  static ArchetypeGraph &graph(World &w) noexcept { return w.graph_; }
  static auto &chunks(World &w) noexcept { return w.archetype_chunks_; }
  static uint32_t &version(World &w) noexcept { return w.current_version_; }
};
}
```
Inside `World`:
```cpp
class World {
  friend struct internal::WorldAccess;
  // ZERO other friend declarations!
};
```
- **Pros:** 
  - Exactly **1 friend declaration** in `World`.
  - Subsystems (`ChunkDistributor`, `EntityAllocator`, etc.) access raw fields via `WorldAccess::slot_map(world)` without polluting `World`'s public interface.
  - Fully compile-time inlined (0 CPU cycles, 0 memory overhead).
- **Cons:** Minor rename across the internal `.inl` files (`world.slot_map_` $\to$ `WorldAccess::slot_map(world)`).

---

#### Option 1B: Internal Flat Storage Struct (`WorldStorage`)
Group all raw simulation containers into a standalone POD struct:
```cpp
struct WorldStorage {
  uint32_t current_version{1};
  uint32_t chunk_size{65536};
  TypeRegistry registry;
  ResourceContainer resources;
  Scheduler scheduler;
  SlotMap slot_map;
  ChunkPool chunk_pool;
  ArchetypeGraph graph;
  std::vector<std::vector<Chunk *>> archetype_chunks;
  std::unordered_map<QueryCacheKey, std::unique_ptr<QueryCache>, QueryCacheKeyHash> query_caches;
};
```
Inside `World`:
```cpp
class World {
  friend struct internal::WorldAccess;
private:
  WorldStorage storage_;
};
```
- **Pros:** Completely isolates data definition from coordinator methods.
- **Cons:** Slightly larger refactor across member call sites.

---

#### Option 1C: Pruned Explicit Friends
Keep only the real subsystem friends and delete the redundant ones:
```cpp
class World {
  friend struct ChunkDistributor;
  friend struct ComponentMigration;
  friend struct ChunkDispatcher;
  friend struct CommandPlayback;
  friend struct EntityAllocator;
  friend struct ComponentAccess;
  friend class SnapshotFacade;
};
```
- **Pros:** Zero changes to `.inl` file implementation bodies.
- **Cons:** Still leaves 7 friend declarations in the class header.

---

## Part 2: Pruning and Relocating Methods on `World` (Lines 106–262)

The current 38 methods on `World` can be partitioned into three strict categories:

### Category A: Core Ergonomic Primitives (KEEP on `World` — 12 Methods)
These provide the zero-indirection, daily API for game and simulation logic:
1. `bool is_alive(Entity entity) const noexcept;`
2. `size_t entity_count() const noexcept;`
3. `const EntityRecord &get_record(Entity entity) const;`
4. `template <typename... Comps> Entity spawn(Comps &&...);`
5. `template <typename... Comps> std::vector<Entity> spawn_batch(size_t, const Comps &...);`
6. `template <typename... Comps> void spawn_batch(size_t, std::span<Entity>, const Comps *...);`
7. `bool despawn(Entity entity);`
8. `void despawn_batch(std::span<const Entity> entities);`
9. `template <typename T> bool has(Entity entity) const noexcept;`
10. `template <typename T> T &get(Entity entity);` / `const T &get(Entity) const;`
11. `template <typename T> void add(Entity entity, T &&);`
12. `template <typename T> void remove(Entity entity);`
13. `template <typename... Comps, typename... Filters> Query<Comps...> query(Filters &&...);`
14. `void commit(CommandBuffer &);` / `void commit(ParallelCommandBuffer &);`
15. `uint32_t version() const noexcept;`

---

### Category B: Redundant Duplicate Aliases (DELETE from `World` — 10 Methods)
These duplicate functionality already cleanly exposed on dedicated sub-facades or free functions:

| Method on `World` | Canonical Sub-Facade / Standalone Location | Reason to Remove |
| :--- | :--- | :--- |
| `add_plugin()` (4 overloads) | `world.systems.add_plugin(...)` | Redundant alias on `World`. |
| `permute(...)` | `arche::permute(world, arch, indices)` | Spatial sorting algorithm belongs in `arche/spatial/sort.hpp`. |
| `get_archetype_chunks(...)` (2 overloads) | `world.archetypes.chunks(arch)` | Duplicates `ArchetypeFacade`. |
| `current_tick()`, `tick()` | `world.version()`, `world.systems.run()` | Confusing legacy alias for `current_version_`. |
| `query_parallel(...)` (2 overloads) | `ChunkDispatcher::query_parallel(...)` / `ctx.query_parallel(...)` | Parallel work distribution belongs in `concurrency/`. |
| `dispatch_parallel_chunks(...)` | `ChunkDispatcher::dispatch_parallel_chunks(...)` | Low-level thread-pool task distribution. |

---

### Category C: Leaked Internal Plumbing (RELOCATE to Subsystems — 16 Methods)
These are engine mechanics that leaked into `World`'s public interface:

1. **Storage & Allocation Mechanics $\to$ Move to [`ChunkDistributor`](file:///Users/student/Documents/arche-ecs/include/arche/chunk/chunk_distributor.hpp):**
   - `allocate_slot(...)`
   - `acquire_chunk_for(...)`
   - `recycle_chunk(...)`
2. **Command Playback Application $\to$ Move to [`CommandPlayback`](file:///Users/student/Documents/arche-ecs/include/arche/command/command_playback.hpp):**
   - `apply_command_create_entity(...)`
   - `apply_command_add_component(...)`
   - `apply_command_remove_component(...)`
   - `migrate_entity(...)` $\to$ already in `ComponentMigration::migrate`.
3. **Query Cache Generation $\to$ Move to [`ArchetypeGraph`](file:///Users/student/Documents/arche-ecs/include/arche/archetype/archetype_graph.hpp) or [`QueryCache`](file:///Users/student/Documents/arche-ecs/include/arche/query/query_cache.hpp):**
   - `get_or_create_query_cache(...)`: 30 lines of cache building logic currently bloating `world.hpp`.
4. **Plugin Bookkeeping $\to$ Move to [`SystemFacade`](file:///Users/student/Documents/arche-ecs/include/arche/world_facades.hpp):**
   - `register_plugin_name<P>()`
5. **Change Detection Stamping $\to$ Move to [`ComponentAccess`](file:///Users/student/Documents/arche-ecs/include/arche/component/component_access.hpp):**
   - `stamp_component_version(...)`
   - `mark_dirty<T>(chunk)`

---

## Target Interface for `include/arche/world.hpp` (< 90 Lines)

After applying Part 1 (Option 1A) and Part 2:

```cpp
#pragma once

#include "arche/entity/entity.hpp"
#include "arche/entity/entity_record.hpp"
#include "arche/world_facades.hpp"
#include "arche/world_access.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace arche {

class CommandBuffer;
class ParallelCommandBuffer;
template <typename... Components> class Query;

class World {
  friend struct internal::WorldAccess;

private:
  uint32_t current_version_{1};
  uint32_t chunk_size_{65536};

  TypeRegistry registry_;
  ResourceContainer resources_;
  Scheduler scheduler_;
  std::vector<std::string> installed_plugins_;

  SlotMap slot_map_;
  ChunkPool chunk_pool_;
  ArchetypeGraph graph_;
  std::vector<std::vector<Chunk *>> archetype_chunks_;
  std::unordered_map<QueryCacheKey, std::unique_ptr<QueryCache>, QueryCacheKeyHash> query_caches_{};

public:
  // --- Domain Sub-Facades (Property Syntax) ---
  SnapshotFacade  snapshots{this};
  ResourceFacade  resources{this};
  ArchetypeFacade archetypes{this};
  SystemFacade    systems{this};
  GpuFacade       gpu{this};

  // --- Constructors & Destructor ---
  explicit World(size_t chunk_size = 65536);
  ~World();

  World(const World &) = delete;
  World &operator=(const World &) = delete;
  World(World &&) = delete;
  World &operator=(World &&) = delete;

  void clear();

  // --- Entity Inspection & CRUD ---
  [[nodiscard]] inline bool is_alive(Entity entity) const noexcept;
  [[nodiscard]] inline size_t entity_count() const noexcept;
  [[nodiscard]] inline const EntityRecord &get_record(Entity entity) const;

  template <typename... Comps>
  inline Entity spawn(Comps &&...components);

  template <typename... Comps>
    requires(!(std::is_same_v<std::decay_t<Comps>, std::span<Entity>> || ...))
  inline std::vector<Entity> spawn_batch(size_t count, const Comps &...components);

  template <typename... Comps>
  inline void spawn_batch(size_t count, std::span<Entity> out_entities, const Comps *...column_data);

  bool despawn(Entity entity);
  void despawn_batch(std::span<const Entity> entities);

  // --- Component Access ---
  template <typename T> [[nodiscard]] inline bool has(Entity entity) const noexcept;
  template <typename T> [[nodiscard]] inline T &get(Entity entity);
  template <typename T> [[nodiscard]] inline const T &get(Entity entity) const;
  template <typename T> inline void add(Entity entity, T &&component);
  template <typename T> inline void remove(Entity entity);

  // --- Query Factory ---
  template <typename... Components, typename... FilterArgs>
  [[nodiscard]] Query<Components...> query(FilterArgs &&...filters);

  // --- Deferred Commands & Monotonic Version ---
  void commit(CommandBuffer &cmd_buffer);
  void commit(ParallelCommandBuffer &cmd_buffers);
  [[nodiscard]] inline uint32_t version() const noexcept { return current_version_; }
};

} // namespace arche

#include "arche/entity/entity_allocator.inl"
#include "arche/component/component_access.inl"
#include "arche/chunk/chunk_distributor.inl"
#include "arche/concurrency/chunk_dispatcher.inl"
#include "arche/command/command_buffer.inl"
#include "arche/world_facades_impl.hpp"
#include "arche/spatial/sort.hpp"
```

---

## Refactoring Execution Checklist

### Phase 1: Pass-Key & Friend Elimination & WorldStorage Extraction
- [x] Create `include/arche/world_access.hpp` and `include/arche/world_access.inl` defining `struct WorldAccess`.
- [x] Replace all 19 `friend` declarations in `World` with single `friend struct internal::WorldAccess;`.
- [x] Create `include/arche/world_storage.hpp` defining flat `struct WorldStorage`.
- [x] Encapsulate `World` state into single `WorldStorage storage_;` member.
- [x] Route internal subsystem access points (`ChunkDistributor`, `EntityAllocator`, `ComponentAccess`, `CommandPlayback`) via `WorldAccess`.
- [x] Remove loose `friend class CommandBuffer;`, `friend class ParallelCommandBuffer;`, and `friend void sort(...)`.

### Phase 2: Relocating Leaked Engine Plumbing
- [x] Move `get_or_create_query_cache(...)` logic from `world.hpp` into `WorldAccess::query_cache`.
- [x] Move `allocate_slot`, `acquire_chunk_for`, and `recycle_chunk` into `ChunkDistributor`.
- [x] Move `apply_command_create_entity`, `apply_command_add_component`, and `apply_command_remove_component` into `CommandPlayback`.
- [x] Move `register_plugin_name` into `SystemFacade`.
- [x] Move `stamp_component_version` and `mark_dirty` into `ComponentAccess`.

### Phase 3: Pruning Redundant Aliases
- [x] Remove `World::add_plugin` overloads (consumers use `world.systems.add_plugin`).
- [x] Remove `World::permute` (consumers use `arche::permute(world, ...)`).
- [x] Remove `World::get_archetype_chunks` (consumers and subsystems use `world.archetypes.chunks(arch)` and `ChunkDistributor`).
- [x] Remove `World::current_tick` and `World::tick` (use `world.version()` / `world.advance_version()`).
- [x] Remove `World::query_parallel` and `World::dispatch_parallel_chunks` (use `Query::each_parallel` or `ChunkDispatcher`).

### Phase 4: Verification & Zero-Regression Guard
- [x] Build static library `libarche.a` with zero warnings (`-Wall -Wextra -Wpedantic`).
- [x] Build all 24 test executables.
- [x] Run `ctest --test-dir build --output-on-failure` and verify **100% pass rate (24/24)**.
- [x] Run `./build/colony_simulation_loop` and verify latency remains identical (~118 µs / frame for 100k entities).
- [x] Update [`FUNCTION_AUDIT_REPORT.md`](file:///Users/student/Documents/arche-ecs/FUNCTION_AUDIT_REPORT.md).
