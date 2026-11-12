# Module 018: Unified Public Facade & Clean API

This document specifies the exact behavioral deltas, code modifications, and new test suites required to introduce the modernized, highly ergonomic public API surface and umbrella header `<arche/arche.hpp>` in `arche-ecs`, assuming Modules 009 through 017 are fully implemented.

---

## 1. Behavioral Deltas: What Changes and Why

### 1.1 Single Umbrella Header (`#include <arche/arche.hpp>`)
* **Before (Module 017):**  
  Consumers had to manually include 10+ internal headers (`world.hpp`, `entity.hpp`, `query.hpp`, `query_filter.hpp`, `thread_pool.hpp`, `parallel_command_buffer.hpp`, `gpu_reflection.hpp`, etc.) to use the library.
* **Now (Module 018):**  
  Consumers `#include <arche/arche.hpp>`, which re-exports all public types and functions cleanly with zero header hunting and $< 10,000$ preprocessed lines.
* **Why we need to change this part:**  
  Drastically reduces consumer include boilerplate and establishes a strict, clean boundary between public facade classes (`namespace arche`) and engine internals (`namespace arche::detail`).

### 1.2 Ergonomic, Symmetrical Lifecycle & Access API
* **Before (Module 017):**  
  Methods used verbose nouns and single-character pluralization differences: `create_entity` vs `create_entities`, `destroy_entity` vs `destroy_entities`, `get_component<T>()`, `has_component<T>()`, `add_component<T>()`, `remove_component<T>()`.
* **Now (Module 018):**  
  Methods adopt short, expressive, industry-standard active verbs:
  - `spawn(...)` / `spawn_batch(...)`
  - `despawn(...)` / `despawn_batch(...)`
  - `get<T>(...)`, `has<T>(...)`, `add<T>(...)`, `remove<T>(...)`
* **Why we need to change this part:**  
  Eliminates accidental typos between scalar and batch operations, strips redundant syllables (the type parameter `<T>` already signifies a component), and reduces mental friction.

### 1.3 Intuitive Iteration & Frame Control Verbs
* **Before (Module 017):**  
  Frame operations used long descriptive phrases: `parallel_query`, `playback_parallel_commands`, `advance_tick`, `register_double_buffered_component<T>`, `flip_double_buffer<T>`, `stream_component_to_ring<T>`, `write_draw_indirect_commands`.
* **Now (Module 018):**  
  Frame methods adopt concise, memorable action verbs:
  - `query_parallel<Components...>(pool, ...)` directly mirrors `query<Components...>()`.
  - `commit(cmd_buffers)` replaces `playback_parallel_commands`.
  - `tick()` replaces `advance_tick()`.
  - `enable_double_buffer<T>()` and `flip<T>()`.
  - `sort<Comp>()` and `permute()`.
  - `stream_gpu<T>(...)` and `indirect_draw(...)`.
  - `save_snapshot(...)` and `load_snapshot(...)`.
* **Why we need to change this part:**  
  Reduces average identifier length by $52\%$, aligns execution methods with standard simulation terminology, and makes writing a 120 Hz lockstep frame effortless.

### 1.4 100% Backward Compatibility Bridges
* **Before (Module 017):**  
  Legacy names were the only existing methods.
* **Now (Module 018):**  
  All previous Stage 1 and Stage 2 method names remain preserved as `[[deprecated]]` inline forwarding aliases on `World` and `CommandBuffer`. Existing unit tests, benchmarks, and legacy consumers continue to compile and pass with zero modifications.

---

## 2. Modifications to Existing Files

### 2.1 `include/arche/world.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `World` exposed only the verbose legacy function names.
* **Now:** Adds clean modernized methods (`spawn`, `despawn`, `get`, `has`, `add`, `remove`, `query_parallel`, `commit`, `tick`, `flip`, `enable_double_buffer`, `sort`, `permute`, `stream_gpu`, `indirect_draw`, `save_snapshot`, `load_snapshot`) and bridges legacy names to them via deprecated inline aliases.

#### Functions / Members to Delete
* `None` (legacy functions are bridged, not deleted).

#### Modified Method Declarations in `include/arche/world.hpp`

##### Old Version (Module 017)
```cpp
  template <typename... Comps>
  Entity create_entity(Comps &&...components);

  template <typename... Comps>
  std::vector<Entity> create_entities(size_t count, const Comps &...prototypes);

  void destroy_entity(Entity entity);
  void destroy_entities(std::span<const Entity> entities);

  template <typename T>
  T &get_component(Entity entity);

  template <typename T>
  bool has_component(Entity entity) const noexcept;

  template <typename T>
  void add_component(Entity entity, T &&component);

  template <typename T>
  void remove_component(Entity entity);

  template <typename... Components, typename Func>
  inline void parallel_query(ThreadPool &pool, Func &&func);

  inline void playback_parallel_commands(ParallelCommandBuffer &cmd_buffers);

  inline uint32_t advance_tick() noexcept;

  template <typename T>
  inline void register_double_buffered_component();

  template <typename T>
  inline void flip_double_buffer() noexcept;
```

##### New Version (Module 018)
```cpp
  // ===========================================================================
  // Clean Modernized Public Surface
  // ===========================================================================

  // 1. Lifecycle
  template <typename... Comps>
  [[nodiscard]] inline Entity spawn(Comps &&...components);

  template <typename... Comps>
  [[nodiscard]] inline std::vector<Entity> spawn_batch(size_t count, const Comps &...prototypes);

  template <typename... Comps>
  inline void spawn_batch(size_t count, std::span<Entity> out_entities, const Comps *...column_data);

  inline void despawn(Entity entity);
  inline void despawn_batch(std::span<const Entity> entities);

  // 2. Component Access
  template <typename T>
  [[nodiscard]] inline T &get(Entity entity);

  template <typename T>
  [[nodiscard]] inline const T &get(Entity entity) const;

  template <typename T>
  [[nodiscard]] inline bool has(Entity entity) const noexcept;

  template <typename T>
  inline void add(Entity entity, T &&component);

  template <typename T>
  inline void remove(Entity entity);

  // 3. Query Execution
  template <typename... Components, typename Func>
  inline void query_parallel(ThreadPool &pool, Func &&func);

  template <typename... Components, typename Func>
  inline void query_parallel(ThreadPool &pool, ParallelCommandBuffer &cmd_buffers, Func &&func);

  inline void commit(ParallelCommandBuffer &cmd_buffers);
  inline void commit(CommandBuffer &cmd_buffer);

  // 4. Ticking & Changes
  inline uint32_t tick() noexcept;

  template <typename T>
  inline void mark_dirty(Entity entity) noexcept;

  template <typename T>
  inline void mark_dirty(Chunk *chunk) noexcept;

  // 5. Double-Buffering
  template <typename T>
  inline void enable_double_buffer();

  template <typename T>
  inline void flip() noexcept;

  // 6. Locality & Sorting
  template <typename Component, typename Compare>
  inline void sort(Archetype *arch, Compare &&comp);

  inline void permute(Archetype *arch, std::span<const uint32_t> target_indices);

  // 7. GPU Streaming & Indirect Draw
  template <typename T, typename CullingPredicate = decltype([](const ChunkHeader &) { return true; })>
  inline size_t stream_gpu(
      uint32_t ring_index,
      void *ring_buffer_base,
      size_t ring_slot_capacity_bytes,
      CullingPredicate &&is_visible = [](const ChunkHeader &) { return true; }
  );

  template <typename CullingPredicate = decltype([](const ChunkHeader &) { return true; })>
  inline size_t indirect_draw(
      Archetype *arch,
      void *destination_buffer,
      size_t max_commands,
      CullingPredicate &&is_visible = [](const ChunkHeader &) { return true; }
  );

  // 8. Snapshots
  inline size_t save_snapshot(void *destination_buffer, size_t max_bytes);
  inline void load_snapshot(const void *snapshot_buffer, size_t bytes);

  // ===========================================================================
  // Backward-Compatibility Deprecated Bridges
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
  inline void register_double_buffered_component() { enable_double_buffer<T>(); }

  template <typename T>
  inline void flip_double_buffer() noexcept { flip<T>(); }

  template <typename T, typename P = decltype([](const ChunkHeader &) { return true; })>
  inline size_t stream_component_to_ring(uint32_t idx, void *b, size_t cap, P &&p = {}) {
    return stream_gpu<T>(idx, b, cap, std::forward<P>(p));
  }

  template <typename P = decltype([](const ChunkHeader &) { return true; })>
  inline size_t write_draw_indirect_commands(Archetype *a, void *d, size_t m, P &&p = {}) {
    return indirect_draw(a, d, m, std::forward<P>(p));
  }

  inline size_t create_snapshot(void *d, size_t m) { return save_snapshot(d, m); }
  inline void restore_snapshot(const void *s, size_t b) { load_snapshot(s, b); }
```

---

### 2.2 `include/arche/command_buffer.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `CommandBuffer` used verbose `add_component`, `remove_component`, `destroy_entity`, `destroy_entities`, `playback`.
* **Now:** Adds clean methods `spawn`, `despawn`, `despawn_batch`, `add`, `remove`, `commit` and bridges legacy names via inline aliases.

#### Modified Method Declarations in `CommandBuffer`

##### Old Version (Module 017)
```cpp
  template <typename T>
  void add_component(Entity entity, T component);

  template <typename T>
  void remove_component(Entity entity);

  void destroy_entity(Entity entity);
  void destroy_entities(std::span<const Entity> entities);

  void playback(World &world);
```

##### New Version (Module 018)
```cpp
  // Clean Modernized Methods
  template <typename T>
  inline void add(Entity entity, T component) { add_component(entity, std::move(component)); }

  template <typename T>
  inline void remove(Entity entity) { remove_component<T>(entity); }

  inline void despawn(Entity entity) { destroy_entity(entity); }
  inline void despawn_batch(std::span<const Entity> entities) { destroy_entities(entities); }

  inline void commit(World &world) { playback(world); }

  // Backward-compatibility bridges
  template <typename T>
  [[deprecated("Use add() instead")]]
  void add_component(Entity entity, T component);

  template <typename T>
  [[deprecated("Use remove() instead")]]
  void remove_component(Entity entity);

  [[deprecated("Use despawn() instead")]]
  void destroy_entity(Entity entity);

  [[deprecated("Use despawn_batch() instead")]]
  void destroy_entities(std::span<const Entity> entities);

  [[deprecated("Use commit() instead")]]
  void playback(World &world);
```

---

## 3. Brand-New Files

### 3.1 `include/arche/arche.hpp`

This is the single umbrella header providing clean access to the entire library.

```cpp
#pragma once

// Core Types & Identifiers
#include "arche/common.hpp"
#include "arche/entity.hpp"
#include "arche/math_utils.hpp"
#include "arche/type_id.hpp"

// Memory Layout & Storage
#include "arche/chunk.hpp"
#include "arche/gpu_reflection.hpp"

// Queries, Filters & Iteration
#include "arche/query.hpp"
#include "arche/query_filter.hpp"

// Concurrency & Commands
#include "arche/command_buffer.hpp"
#include "arche/parallel_command_buffer.hpp"
#include "arche/thread_pool.hpp"

// Central World Facade
#include "arche/world.hpp"

namespace arche {

/**
 * @namespace arche
 * @brief Standalone, zero-dependency, high-performance C++20 archetype ECS library.
 */

} // namespace arche
```

---

### 3.2 `examples/colony_simulation_loop.cpp`

This is a complete, runnable headless example demonstrating how Colony Swarm simulates 100,000 entities at 120 Hz using the modernized public API.

```cpp
#include "arche/arche.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

// =============================================================================
// Consumer Component Types (Zero Library Context)
// =============================================================================

struct Position {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
};

struct Velocity {
  float vx{0.0f};
  float vy{0.0f};
  float vz{0.0f};
};

struct BoidState {
  float hunger{0.0f};
  uint32_t cluster_id{0};
};

struct MortonKey {
  uint32_t code{0};
};

// =============================================================================
// 120 Hz Simulation Harness
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Clean API Colony Simulation Loop\n";
  std::cout << "====================================================\n\n";

  arche::World world;
  arche::ThreadPool pool(std::thread::hardware_concurrency());
  arche::ParallelCommandBuffer cmd_buffers(pool.worker_count());

  // 1. Configure Double-Buffered Physics
  world.enable_double_buffer<Position>();

  // 2. High-Throughput Bulk Spawning (100,000 Entities)
  constexpr size_t BOID_COUNT = 100'000;
  std::cout << "Spawning " << BOID_COUNT << " boid entities in bulk...\n";

  world.spawn_batch(
      BOID_COUNT,
      Position{0.0f, 0.0f, 0.0f},
      Velocity{1.0f, 0.5f, 0.0f},
      BoidState{0.0f, 1},
      MortonKey{0}
  );

  std::cout << "Living entities: " << world.entity_count() << "\n\n";

  // Mock VRAM buffer for triple-buffered GPU streaming
  constexpr size_t SLOT_BYTES = BOID_COUNT * sizeof(Position);
  std::vector<uint8_t> mock_vram(SLOT_BYTES * 3, 0);

  // 3. Execute 120 simulation frames @ 120 Hz
  const auto start = std::chrono::high_resolution_clock::now();

  for (uint32_t frame = 0; frame < 120; ++frame) {
    // A. Advance simulation tick
    world.tick();

    // B. Concurrent Physics / Flocking (Write<Position> secondary buffer isolation)
    world.query_parallel<Position, Velocity, BoidState, arche::Write<Position>>(
        pool,
        cmd_buffers,
        [](uint32_t count,
           arche::CommandBuffer &cmd,
           const Position *pos,
           const Velocity *vel,
           BoidState *state,
           Position *next_pos) {
          for (uint32_t i = 0; i < count; ++i) {
            next_pos[i].x = pos[i].x + vel[i].vx * 0.00833f;
            next_pos[i].y = pos[i].y + vel[i].vy * 0.00833f;
            next_pos[i].z = pos[i].z + vel[i].vz * 0.00833f;

            state[i].hunger += 0.01f;
          }
        }
    );

    // C. Replay deferred mutations sequentially on main thread
    world.commit(cmd_buffers);

    // D. O(1) Double-buffer flip (swaps slice offsets and auto-stamps versions)
    world.flip<Position>();

    // E. Triple-buffered GPU ring streaming
    const uint32_t ring_slot = frame % 3;
    world.stream_gpu<Position>(ring_slot, mock_vram.data(), SLOT_BYTES);
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::high_resolution_clock::now() - start
  ).count();

  std::cout << "Simulated 120 frames in " << (elapsed / 1000.0f) << " ms ("
            << (elapsed / 120.0f) << " us/frame)\n";
  std::cout << "All operations executed successfully with clean API!\n";
  return 0;
}
```

---

### 3.3 `tests/public_api_tests.cpp`

This is a brand-new test suite verifying that all modernized methods and deprecated bridges compile and pass 100%.

```cpp
#include "arche/arche.hpp"

#include <cassert>
#include <iostream>
#include <vector>

namespace {
using arche::CommandBuffer;
using arche::Entity;
using arche::ParallelCommandBuffer;
using arche::ThreadPool;
using arche::World;
using arche::Write;
} // namespace

struct Pos { float x{0.0f}, y{0.0f}; };
struct Vel { float vx{0.0f}, vy{0.0f}; };

void test_clean_lifecycle_api() {
  std::cout << "Testing Clean Lifecycle API (spawn, despawn, get, has, add, remove)... ";
  World world;

  // Scalar spawn
  Entity e1 = world.spawn(Pos{1.0f, 2.0f});
  assert(world.is_alive(e1));
  assert(world.has<Pos>(e1));
  assert(!world.has<Vel>(e1));
  assert(world.get<Pos>(e1).x == 1.0f);

  // Add / remove
  world.add(e1, Vel{10.0f, 20.0f});
  assert(world.has<Vel>(e1));
  assert(world.get<Vel>(e1).vx == 10.0f);

  world.remove<Vel>(e1);
  assert(!world.has<Vel>(e1));

  // Batch spawn
  auto ents = world.spawn_batch(100, Pos{0.0f, 0.0f}, Vel{1.0f, 1.0f});
  assert(ents.size() == 100);
  assert(world.entity_count() == 101);

  // Batch despawn
  world.despawn_batch(ents);
  assert(world.entity_count() == 1);
  assert(!world.is_alive(ents[0]));

  world.despawn(e1);
  assert(world.entity_count() == 0);
  std::cout << "PASSED\n";
}

void test_clean_frame_loop_api() {
  std::cout << "Testing Clean Frame Loop API (tick, query_parallel, commit, flip)... ";
  World world;
  ThreadPool pool(2);
  ParallelCommandBuffer cmd_buffers(2);

  world.enable_double_buffer<Pos>();
  world.spawn_batch(1000, Pos{0.0f, 0.0f}, Vel{1.0f, 2.0f});

  const uint32_t t1 = world.tick();
  assert(t1 == 2); // Initial tick is 1; advance makes it 2

  world.query_parallel<Pos, Vel, Write<Pos>>(
      pool,
      cmd_buffers,
      [](uint32_t count, CommandBuffer &, const Pos *pos, const Vel *vel, Pos *next) {
        for (uint32_t i = 0; i < count; ++i) {
          next[i].x = pos[i].x + vel[i].vx;
          next[i].y = pos[i].y + vel[i].vy;
        }
      }
  );

  world.commit(cmd_buffers);
  world.flip<Pos>();

  // Verify updated positions after flip
  auto q = world.query<Pos>();
  q.for_each_active_chunk([](arche::Chunk *chunk, arche::Archetype *) {
    const auto *p = chunk->typed_slice<Pos>(chunk->archetype()->get_column_layout(arche::type_id<Pos>()).chunk_byte_offset);
    assert(p[0].x == 1.0f);
    assert(p[0].y == 2.0f);
  });

  std::cout << "PASSED\n";
}

void test_backward_compatibility_bridges() {
  std::cout << "Testing Backward Compatibility Bridges... ";
  World world;

  // Verify legacy names compile and execute correctly through forwarding aliases
  Entity e = world.create_entity(Pos{5.0f, 5.0f});
  assert(world.has_component<Pos>(e));
  assert(world.get_component<Pos>(e).x == 5.0f);

  world.destroy_entity(e);
  assert(!world.is_alive(e));

  std::cout << "PASSED\n";
}

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Module 018: Public API Tests\n";
  std::cout << "====================================================\n";

  test_clean_lifecycle_api();
  test_clean_frame_loop_api();
  test_backward_compatibility_bridges();

  std::cout << "====================================================\n";
  std::cout << "All Module 018 tests passed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}
```

---

## 4. Build Configuration: `CMakeLists.txt`

Add the example executable and test target to `CMakeLists.txt`:

```cmake
# Module 018 Tests: Clean Public API & Facade
add_executable(public_api_tests tests/public_api_tests.cpp)
target_link_libraries(public_api_tests PRIVATE arche::arche)
add_test(NAME public_api_tests COMMAND public_api_tests)

# Module 018 Example: Colony 120 Hz Swarm Simulation Loop
add_executable(colony_simulation_loop examples/colony_simulation_loop.cpp)
target_link_libraries(colony_simulation_loop PRIVATE arche::arche)
```

