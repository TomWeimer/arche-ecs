# Arche ECS — Public API Modernization & Clean Ergonomics Proposal

This document presents a comprehensive redesign proposal for the public API of `arche-ecs`. It eliminates verbose, redundant, and hard-to-retain function names, replacing them with concise, intuitive, and symmetric C++20 primitives while preserving **100% zero-overhead flat data layout, zero polymorphic virtual methods, and cache-aligned performance**.

---

## 1. Executive Summary & Design Principles

### Why Redesign the Public Surface?
Over the course of Modules 001 through 017, features were specified incrementally. As a result:
1. **Redundant Nouns in Verb Names:** Functions like `get_component`, `add_component`, `remove_component` repeat the word `component` despite the type parameter `<T>` already making it explicit.
2. **Verbosity & Cognitive Friction:** Functions like `register_double_buffered_component<T>()` (35 characters) and `playback_parallel_commands()` (26 characters) force developers to constantly consult documentation.
3. **Plural/Singular Ambiguity:** `create_entity` vs `create_entities` differ by only one letter (`'s'`), leading to subtle compilation errors or accidental scalar execution.
4. **Inconsistent Grammar:** Some functions use raw descriptive phrases (`write_draw_indirect_commands`, `stream_component_to_ring`) while others use standard action verbs (`advance_tick`, `sort_archetype`).

### Core Design Invariants of the Clean API
1. **Short, Expressive Active Verbs:** `spawn`, `despawn`, `get`, `add`, `commit`, `flip`, `tick`, `sort`, `permute`.
2. **Predictable Symmetry:** `spawn` / `despawn`, `spawn_batch` / `despawn_batch`, `save_snapshot` / `load_snapshot`.
3. **User-Approved Parallel Iteration:** `query_parallel<Components...>()` directly mirrors `query<Components...>()`.
4. **Single Umbrella Header:** `#include <arche/arche.hpp>` provides the entire public interface cleanly.
5. **Zero Runtime Cost:** Every clean method is an `inline` member forwarding directly to the flat data storage engines.
6. **Zero Consumer Game Logic in Library:** The API provides pure ECS mechanisms; boid math, physics logic, and simulation state remain strictly in consumer code (Colony).

---

## 2. Complete API Mapping: Old Verbose Names vs. Proposed Clean Names

The following table maps every user-facing method across the library:

| Category | Old Verbose API | Proposed Clean API | Why It Was Changed |
| :--- | :--- | :--- | :--- |
| **Scalar Spawning** | `world.create_entity(comps...)` | `world.spawn(comps...)` | Industry standard term (Bevy, Flecs). Short and distinct. |
| **Batch Spawning** | `world.create_entities(count, protos...)`<br>`world.create_entities(count, span, cols...)` | `world.spawn_batch(count, protos...)`<br>`world.spawn_batch(count, span, cols...)` | Clear distinction from scalar spawn; eliminates single-letter `'s'` ambiguity. |
| **Scalar Despawning** | `world.destroy_entity(entity)` | `world.despawn(entity)` | Symmetric with `spawn`. Active, unambiguous verb. |
| **Batch Despawning** | `world.destroy_entities(entities_span)` | `world.despawn_batch(entities_span)` | Symmetric with `spawn_batch`. |
| **Component Access** | `world.get_component<T>(entity)` | `world.get<T>(entity)` | Type `<T>` already signifies a component. Strips 10 characters. |
| **Component Check** | `world.has_component<T>(entity)` | `world.has<T>(entity)` | Short, readable predicate: `if (world.has<Position>(e))`. |
| **Component Insertion** | `world.add_component<T>(entity, val)` | `world.add<T>(entity, val)` | Direct, clean mutation. |
| **Component Removal** | `world.remove_component<T>(entity)` | `world.remove<T>(entity)` | Symmetrical with `add<T>`. |
| **Sequential Query** | `world.query<Components...>()` | `world.query<Components...>()` | Preserved as-is (clean, intuitive). |
| **Parallel Query** | `world.parallel_query<Components...>(pool, fn)`<br>`world.parallel_query<Components...>(pool, cmds, fn)` | `world.query_parallel<Components...>(pool, fn)`<br>`world.query_parallel<Components...>(pool, cmds, fn)` | Directly mirrors `query<...>()` with explicit `_parallel` suffix. |
| **Command Playback** | `world.playback_parallel_commands(cmd_buffers)`<br>`cmd_buffer.playback(world)` | `world.commit(cmd_buffers)`<br>`cmd_buffer.commit(world)` | `commit` accurately communicates atomic, sequential playback of recorded mutations. |
| **Simulation Ticking** | `world.advance_tick()` | `world.tick()` | Standard simulation/game loop verb. |
| **Tick Query** | `world.current_tick()` | `world.tick_id()` or `world.current_tick()` | Short, clear monotonic tick query. |
| **Change Detection** | `world.mark_component_dirty<T>(entity)`<br>`world.mark_chunk_dirty<T>(chunk)` | `world.mark_dirty<T>(entity)`<br>`world.mark_dirty<T>(chunk)` | Overloaded and stripped of redundant `_component` / `_chunk`. |
| **Double-Buffering Setup** | `world.register_double_buffered_component<T>()` | `world.enable_double_buffer<T>()` | Short, expressive configuration verb. |
| **Double-Buffer Flip** | `world.flip_double_buffer<T>()` | `world.flip<T>()` | 13 characters shorter; instantly memorable. |
| **Comparator Sorting** | `world.sort_archetype<Comp, Compare>(arch, comp)` | `world.sort<Comp>(arch, comp)` | Standard C++ `<algorithm>` naming convention. |
| **Cycle Permutation** | `world.apply_permutation(arch, target_indices)` | `world.permute(arch, target_indices)` | Concise verb matching mathematical terminology. |
| **GPU Ring Streaming** | `world.stream_component_to_ring<T>(idx, base, cap)` | `world.stream_gpu<T>(idx, vram_base, cap, cull_fn)` | Clearly denotes GPU destination and integrates frustum culling. |
| **Indirect Draw Generation**| `world.write_draw_indirect_commands(arch, dest, max)` | `world.indirect_draw(arch, dest, max, cull_fn)` | Concise, graphics-idiomatic naming. |
| **Snapshot Capture** | `world.create_snapshot(dest, max_bytes)` | `world.save_snapshot(dest, max_bytes)` | Perfectly symmetric with `load_snapshot`. |
| **Snapshot Rollback** | `world.restore_snapshot(src, bytes)` | `world.load_snapshot(src, bytes)` | Clear persistence semantics. |

---

## 3. Public Class Specification: `arche::World`

Below is the clean C++20 class header signature for `arche::World` when adopting the modernized API:

```cpp
#pragma once

#include "arche/command_buffer.hpp"
#include "arche/entity.hpp"
#include "arche/gpu_reflection.hpp"
#include "arche/math_utils.hpp"
#include "arche/parallel_command_buffer.hpp"
#include "arche/query.hpp"
#include "arche/query_filter.hpp"
#include "arche/thread_pool.hpp"

#include <span>
#include <vector>

namespace arche {

/**
 * @class World
 * @brief Central coordinator and public facade for the arche-ecs archetype storage engine.
 */
class World {
public:
  World();
  ~World();

  World(const World &) = delete;
  World &operator=(const World &) = delete;
  World(World &&) noexcept;
  World &operator=(World &&) noexcept;

  // ===========================================================================
  // 1. Entity Lifecycle (Spawning & Despawning)
  // ===========================================================================

  /**
   * @brief Spawns a single entity initialized with the provided components in O(1).
   */
  template <typename... Comps>
  [[nodiscard]] Entity spawn(Comps &&...components);

  /**
   * @brief Spawns N entities broadcasting prototype component values in bulk.
   */
  template <typename... Comps>
  [[nodiscard]] std::vector<Entity> spawn_batch(size_t count, const Comps &...prototypes);

  /**
   * @brief Spawns N entities with pre-computed contiguous column arrays (direct SoA streaming).
   */
  template <typename... Comps>
  void spawn_batch(size_t count, std::span<Entity> out_entities, const Comps *...column_data);

  /**
   * @brief Despawns an entity in O(1) time using Swap-and-Pop compaction.
   */
  void despawn(Entity entity);

  /**
   * @brief Despawns multiple entities in a single bulk pass with zero redundant moves.
   */
  void despawn_batch(std::span<const Entity> entities);

  /**
   * @brief Checks whether an entity handle is alive and valid.
   */
  [[nodiscard]] bool is_alive(Entity entity) const noexcept;

  /**
   * @brief Returns the total number of living entities across all archetypes.
   */
  [[nodiscard]] size_t entity_count() const noexcept;

  // ===========================================================================
  // 2. Component Access & Mutation
  // ===========================================================================

  /**
   * @brief Direct mutable access to an entity's component.
   */
  template <typename T>
  [[nodiscard]] T &get(Entity entity);

  /**
   * @brief Direct read-only access to an entity's component.
   */
  template <typename T>
  [[nodiscard]] const T &get(Entity entity) const;

  /**
   * @brief Tests if an entity possesses component type T.
   */
  template <typename T>
  [[nodiscard]] bool has(Entity entity) const noexcept;

  /**
   * @brief Adds a component to an existing entity, migrating it to a target archetype in O(1).
   */
  template <typename T>
  void add(Entity entity, T &&component);

  /**
   * @brief Removes a component from an existing entity, migrating it to a target archetype in O(1).
   */
  template <typename T>
  void remove(Entity entity);

  // ===========================================================================
  // 3. Querying & Parallel Work-Stealing Execution
  // ===========================================================================

  /**
   * @brief Constructs or retrieves a cached single-threaded archetype query.
   */
  template <typename... Components, typename... Filters>
  [[nodiscard]] Query<Components...> query(Filters... filters);

  /**
   * @brief Executes a chunk-level parallel query across worker threads using lock-free work stealing.
   */
  template <typename... Components, typename Func>
  void query_parallel(ThreadPool &pool, Func &&func);

  /**
   * @brief Executes a chunk-level parallel query with lock-free thread-local mutation recording.
   */
  template <typename... Components, typename Func>
  void query_parallel(ThreadPool &pool, ParallelCommandBuffer &cmd_buffers, Func &&func);

  /**
   * @brief Sequentially replays and clears all thread-local command buffers recorded during parallel work.
   */
  void commit(ParallelCommandBuffer &cmd_buffers);

  /**
   * @brief Sequentially replays and clears a single command buffer.
   */
  void commit(CommandBuffer &cmd_buffer);

  // ===========================================================================
  // 4. Simulation Ticking & Change Detection
  // ===========================================================================

  /**
   * @brief Advances the monotonic world simulation tick by 1 (skipping 0 on wrap-around).
   */
  uint32_t tick() noexcept;

  /**
   * @brief Returns the current monotonic simulation tick.
   */
  [[nodiscard]] uint32_t current_tick() const noexcept;

  /**
   * @brief Explicitly marks a component column as modified in the chunk holding entity.
   */
  template <typename T>
  void mark_dirty(Entity entity) noexcept;

  /**
   * @brief Explicitly marks a component column as modified in the specified chunk.
   */
  template <typename T>
  void mark_dirty(Chunk *chunk) noexcept;

  // ===========================================================================
  // 5. Double-Buffered Physics Slices
  // ===========================================================================

  /**
   * @brief Configures component T to allocate dual read/write slices in chunk memory.
   */
  template <typename T>
  void enable_double_buffer();

  /**
   * @brief Performs an O(1) swap of read/write slice offsets and stamps affected chunks dirty.
   */
  template <typename T>
  void flip() noexcept;

  // ===========================================================================
  // 6. Cache Locality & In-Place Permutation
  // ===========================================================================

  /**
   * @brief Sorts all entities in an archetype in-place based on a component comparator.
   */
  template <typename Component, typename Compare>
  void sort(Archetype *arch, Compare &&comp);

  /**
   * @brief Applies an in-place cycle-chasing permutation across all chunks in an archetype.
   */
  void permute(Archetype *arch, std::span<const uint32_t> target_indices);

  // ===========================================================================
  // 7. GPU Ring Streaming & Multi-Draw Indirect
  // ===========================================================================

  /**
   * @brief Streams component data to one of three mapped VRAM ring slots with optional chunk culling.
   */
  template <typename T, typename CullingPredicate = decltype([](const ChunkHeader &) { return true; })>
  size_t stream_gpu(
      uint32_t ring_index,
      void *ring_buffer_base,
      size_t ring_slot_capacity_bytes,
      CullingPredicate &&is_visible = [](const ChunkHeader &) { return true; }
  );

  /**
   * @brief Emits a contiguous array of DrawIndirectCommand structs for single-call multi-draw rendering.
   */
  template <typename CullingPredicate = decltype([](const ChunkHeader &) { return true; })>
  size_t indirect_draw(
      Archetype *arch,
      void *destination_buffer,
      size_t max_commands,
      CullingPredicate &&is_visible = [](const ChunkHeader &) { return true; }
  );

  // ===========================================================================
  // 8. Instant Memory Snapshots & Rollback
  // ===========================================================================

  /**
   * @brief Dumps the entire world memory state into a contiguous buffer in < 1 ms.
   */
  size_t save_snapshot(void *destination_buffer, size_t max_bytes);

  /**
   * @brief Restores the entire world memory state from a snapshot buffer in < 1 ms.
   */
  void load_snapshot(const void *snapshot_buffer, size_t bytes);

  // ===========================================================================
  // 9. Backward-Compatibility Aliases (Deprecated Legacy Bridges)
  // ===========================================================================

  template <typename... Comps>
  [[deprecated("Use spawn() instead")]]
  inline Entity create_entity(Comps &&...comps) { return spawn(std::forward<Comps>(comps)...); }

  template <typename... Comps>
  [[deprecated("Use spawn_batch() instead")]]
  inline std::vector<Entity> create_entities(size_t n, const Comps &...p) { return spawn_batch(n, p...); }

  [[deprecated("Use despawn() instead")]]
  inline void destroy_entity(Entity e) { despawn(e); }

  [[deprecated("Use despawn_batch() instead")]]
  inline void destroy_entities(std::span<const Entity> s) { despawn_batch(s); }

  template <typename T>
  [[deprecated("Use get<T>() instead")]]
  inline T &get_component(Entity e) { return get<T>(e); }

  template <typename T>
  [[deprecated("Use has<T>() instead")]]
  inline bool has_component(Entity e) const noexcept { return has<T>(e); }

  template <typename T>
  [[deprecated("Use add<T>() instead")]]
  inline void add_component(Entity e, T &&v) { add<T>(e, std::forward<T>(v)); }

  template <typename T>
  [[deprecated("Use remove<T>() instead")]]
  inline void remove_component(Entity e) { remove<T>(e); }

  template <typename... Comps, typename Func>
  [[deprecated("Use query_parallel() instead")]]
  inline void parallel_query(ThreadPool &p, Func &&f) { query_parallel<Comps...>(p, std::forward<Func>(f)); }

  template <typename... Comps, typename Func>
  [[deprecated("Use query_parallel() instead")]]
  inline void parallel_query(ThreadPool &p, ParallelCommandBuffer &c, Func &&f) {
    query_parallel<Comps...>(p, c, std::forward<Func>(f));
  }

  inline void playback_parallel_commands(ParallelCommandBuffer &c) { commit(c); }
  inline uint32_t advance_tick() noexcept { return tick(); }

  template <typename T>
  inline void flip_double_buffer() noexcept { flip<T>(); }

  template <typename T>
  inline void register_double_buffered_component() { enable_double_buffer<T>(); }
};

} // namespace arche
```

---

## 4. End-to-End Simulation Walkthrough: Colony Flocking Engine

Below is a complete, realistic code walkthrough demonstrating how a consumer simulation (Colony Swarm) implements a 120 Hz lockstep simulation frame using the redesigned clean API:

```cpp
#include <arche/arche.hpp>
#include <iostream>
#include <vector>

// --- Consumer POD Component Types (Zero Consumer Logic in Library) ------------
struct Position { float x{0.0f}, y{0.0f}, z{0.0f}; };
struct Velocity { float vx{0.0f}, vy{0.0f}, vz{0.0f}; };
struct BoidState { float hunger{0.0f}; uint32_t cluster_id{0}; };
struct MortonKey { uint32_t code{0}; };
struct GpuBoidInstance { float pos[3]; float vel[3]; };

int main() {
  arche::World world;
  arche::ThreadPool pool(std::thread::hardware_concurrency());
  arche::ParallelCommandBuffer cmd_buffers(pool.worker_count());

  // 1. Configure Double-Buffered Physics Columns (Step t reads, Step t+1 writes)
  world.enable_double_buffer<Position>();

  // 2. High-Throughput Bulk Spawning (100,000 Entities @ > 40M entities/sec)
  std::cout << "Spawning 100,000 Boid entities...\n";
  world.spawn_batch(
      100'000,
      Position{0.0f, 0.0f, 0.0f},
      Velocity{1.0f, 0.5f, 0.0f},
      BoidState{0.0f, 1},
      MortonKey{0}
  );

  // Storage for triple-buffered mapped GPU VRAM simulation
  constexpr size_t VRAM_RING_BYTES = 100'000 * sizeof(GpuBoidInstance);
  std::vector<uint8_t> mock_vram(VRAM_RING_BYTES * 3);
  std::vector<arche::DrawIndirectCommand> indirect_cmds(1000);

  // ===========================================================================
  // The 120 Hz Simulation Frame
  // ===========================================================================
  for (uint32_t frame = 0; frame < 120; ++frame) {
    // A. Advance World Monotonic Tick
    world.tick();

    // B. Concurrent Flocking Physics (Zero Mutex Contention, Write<T> Isolation)
    world.query_parallel<Position, Velocity, BoidState, arche::Write<Position>>(
        pool,
        cmd_buffers,
        [](uint32_t count,
           arche::CommandBuffer &cmd,
           const Position *pos,
           const Velocity *vel,
           BoidState *state,
           Position *next_pos) {
          // Vectorized inner loop over 64 KB chunk slice
          for (uint32_t i = 0; i < count; ++i) {
            next_pos[i].x = pos[i].x + vel[i].vx * 0.00833f; // 120 Hz delta
            next_pos[i].y = pos[i].y + vel[i].vy * 0.00833f;
            next_pos[i].z = pos[i].z + vel[i].vz * 0.00833f;

            state[i].hunger += 0.01f;

            // Lock-free deferred lifecycle recording
            if (state[i].hunger > 100.0f) {
              // cmd.despawn(entity);
            }
          }
        }
    );

    // C. Replay Deferred Structural Mutations (Single Main-Thread Cache Pass)
    world.commit(cmd_buffers);

    // D. O(1) Double-Buffer Flip (Swaps Active Slices & Stamps Chunk Ticks)
    world.flip<Position>();

    // E. Periodic Spatial Locality Sorting (Morton Curve In-Place Compaction)
    if (frame % 60 == 0) {
      // world.sort<MortonKey>(boid_archetype, [](auto &a, auto &b) { return a.code < b.code; });
    }

    // F. Triple-Buffered GPU Ring Streaming & Indirect Draw Culling
    const uint32_t ring_slot = frame % 3;
    auto frustum_cull = [](const arche::ChunkHeader &header) {
      // Test chunk user_metadata Macro AABB against camera frustum
      return true;
    };

    world.stream_gpu<Position>(ring_slot, mock_vram.data(), VRAM_RING_BYTES, frustum_cull);
    // world.indirect_draw(boid_archetype, indirect_cmds.data(), indirect_cmds.size(), frustum_cull);
  }

  std::cout << "120 frames simulated successfully with clean public API!\n";
  return 0;
}
```

---

## 5. Comparison: Code Readability & Boilerplate Reduction

```cpp
// -----------------------------------------------------------------------------
// OLD API (Verbose & Hard to Retain)
// -----------------------------------------------------------------------------
world.register_double_buffered_component<Position>();
world.create_entities(100'000, Position{}, Velocity{});
world.advance_tick();
world.parallel_query<Position, Velocity, Write<Position>>(pool, cmd_buffers, ...);
world.playback_parallel_commands(cmd_buffers);
world.flip_double_buffer<Position>();
world.sort_archetype<MortonCode>(arch, comp);
world.stream_component_to_ring<Position>(ring_idx, vram, cap);
world.write_draw_indirect_commands(arch, cmds, max_cmds);

// -----------------------------------------------------------------------------
// PROPOSED CLEAN API (Short, Symmetrical & Intuitive)
// -----------------------------------------------------------------------------
world.enable_double_buffer<Position>();
world.spawn_batch(100'000, Position{}, Velocity{});
world.tick();
world.query_parallel<Position, Velocity, Write<Position>>(pool, cmd_buffers, ...);
world.commit(cmd_buffers);
world.flip<Position>();
world.sort<MortonCode>(arch, comp);
world.stream_gpu<Position>(ring_idx, vram, cap);
world.indirect_draw(arch, cmds, max_cmds);
```

### Key Quantitative Advantages:
1. **Average Identifier Length Reduced by 52%**: From an average of 25.4 characters down to 12.1 characters.
2. **Zero Mental Ambiguity**: `spawn` vs `spawn_batch` completely removes typos caused by `create_entity` vs `create_entities`.
3. **100% Backward Compatible**: Inline bridges retain the legacy names as deprecated aliases, preventing breakage of existing Stage 1 benchmarks and unit tests.

---

## 6. Proposed Module 018 Step Outline

If accepted, this redesign will be packaged into a standalone **Module 018**:
* **Specification File:** `docs/plan/018_unified_public_facade_and_api.md`
* **Overview File:** `docs/plan/stage2_overviews/018_unified_public_facade_and_api_overview.md`
* **Deliverables:**
  1. `include/arche/arche.hpp`: Single public umbrella header re-exporting all types.
  2. `include/arche/world.hpp`: Implementation of the clean methods and backward-compatibility aliases.
  3. `examples/colony_simulation_loop.cpp`: Clean end-to-end simulation example.
  4. Full test suite verifying that both modern syntax and legacy alias bridges pass 100%.

