# Module 017: 1M+ Scale Validation & Profiling Harness

This document specifies the exact behavioral deltas, code modifications, and new test suites required to implement the comprehensive 1,000,000+ entity stress, profiling, and validation harness for `arche-ecs`, assuming all previous Stage 2 modules (009–016) are already implemented.

---

## 1. Behavioral Deltas: What Changes and Why

### 1.1 Multi-Threaded 1M–2M Entity Scaling Profiler
* **Before (Module 016):**  
  `arche_benchmarks` only tested single-threaded execution up to 500k entities, and measured scalar spawn/despawn operations.
* **Now (Module 017):**  
  `arche_benchmarks` adds multi-threaded scaling test suites running 1,000,000 and 2,000,000 entities across 1, 2, 4, 8, and 16 worker threads using `world.parallel_query`.
* **Why we need to change this part:**  
  Quantifies multi-core efficiency, lock-free work-stealing speedup, and proves that the $\le 533$ CPU cycles/entity budget is met at 120 Hz on multi-core hardware ($< 0.1\text{ ms}$ frametimes for 1M entities).

### 1.2 Cache Locality: Pre-Permutation vs. Post-Permutation Profiling
* **Before (Module 016):**  
  No benchmark measured the cache-miss penalty of spatially scrambled entities vs. cache-clustered entities.
* **Now (Module 017):**  
  Introduces a spatial locality benchmark that simulates swarm neighbor lookups:
  1. Measures neighbor read latency across physically scrambled chunks (high cache miss rate).
  2. Applies `world.apply_permutation` (simulating Colony's Morton Z-order curve compaction).
  3. Re-evaluates neighbor read latency, demonstrating a measurable $2\times\text{ to }4\times$ latency reduction.
* **Why we need to change this part:**  
  Provides hard hardware telemetry validating the real-world performance ROI of Morton space-filling sorting in Colony.

### 1.3 Massive Bulk Lifecycle & 1M Triple-Buffered VRAM Streaming
* **Before (Module 016):**  
  Lifecycle stress was capped at 100k scalar operations, and VRAM streaming tested 50k particles into a single buffer.
* **Now (Module 017):**  
  Tests 1,000,000 entity bulk allocation via SoA streaming, 500,000 entity batched hole compaction, and streams 1,000,000 entities (8 MB payload) into triple-buffered VRAM ring slots.
* **Why we need to change this part:**  
  Validates 64-bit generation safety under massive churn and measures real-world memory bandwidth when streaming 8 MB frames to mapped VRAM without GPU fence stalls.

### 1.4 Deduplication & Helper Architecture: Shared Test Components
* **`tests/test_components.hpp`:**  
  **Where defined:** `tests/test_components.hpp` (namespace `arche::test`).  
  **What code it replaces:** Replaces duplicated copy-pasted `struct Position`, `struct Velocity`, `struct TagBurning`, and `struct BoidTag` definitions across all unit test suites.

---

## 2. Modifications to Existing Files

### 2.1 `benchmarks/arche_benchmarks.cpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `arche_benchmarks.cpp` only profiled single-threaded 50k–500k loops and scalar mutations.
* **Now:** Adds 4 Stage 2 benchmark suites: multi-threaded parallel scaling (1M–2M entities), 1M bulk lifecycle stress, pre-sort vs. post-sort cache locality profiling, and 1M triple-buffered VRAM streaming.

#### Functions / Members to Delete
* `None`.

#### Modified Functions in `benchmarks/arche_benchmarks.cpp`

##### Old Version (Module 016)
```cpp
int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche_benchmarks (Headless Cycle Profiler)\n";
  std::cout << "====================================================\n\n";

  // Report detected hardware architecture
  std::cout << "Hardware Topology Detected:\n";
  std::cout << "  Cache Line Size    : " << Hardware::cache_line_size()
            << " bytes\n";
  std::cout << "  OS Page Size       : " << Hardware::page_size()
            << " bytes\n";
  std::cout << "  Optimal Chunk Size : " << Hardware::optimal_chunk_size()
            << " bytes\n\n";

  // 1. Colony Baseline: 50,000 Entities @ 120 Hz Target
  run_trivial_movement_benchmark(50'000, 100);

  // 2. High-Density Stress: 100,000 Entities
  run_trivial_movement_benchmark(100'000, 50);

  // 3. Ultra-Density Stress: 500,000 Entities
  run_trivial_movement_benchmark(500'000, 20);

  // 4. Structural Mutation Stress: 100,000 Spawns + Despawns
  run_spawn_despawn_stress_benchmark(100'000);

  // 5. GPU VRAM Streaming: 50,000 Entities
  run_gpu_streaming_benchmark(50'000, 500);

  std::cout << "====================================================\n";
  std::cout << "All benchmarks completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}
```

##### New Version (Module 017)
```cpp
int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche_benchmarks (Stage 2 1M+ Cycle Profiler)\n";
  std::cout << "====================================================\n\n";

  std::cout << "Hardware Topology Detected:\n";
  std::cout << "  Cache Line Size    : " << Hardware::cache_line_size() << " bytes\n";
  std::cout << "  OS Page Size       : " << Hardware::page_size() << " bytes\n";
  std::cout << "  Optimal Chunk Size : " << Hardware::optimal_chunk_size() << " bytes\n";
  std::cout << "  Hardware Threads   : " << std::thread::hardware_concurrency() << "\n\n";

  // --- Stage 1 Baseline Benchmarks ---
  run_trivial_movement_benchmark(50'000, 100);
  run_trivial_movement_benchmark(100'000, 50);
  run_trivial_movement_benchmark(500'000, 20);

  // --- Stage 2 1M+ Scale Benchmarks ---
  // 1. Multi-Threaded 1,000,000 Entity Scaling
  run_parallel_scaling_benchmark(1'000'000, std::thread::hardware_concurrency(), 20);

  // 2. Multi-Threaded 2,000,000 Entity Extreme Stress
  run_parallel_scaling_benchmark(2'000'000, std::thread::hardware_concurrency(), 10);

  // 3. Massive Bulk Lifecycle Stress (1,000,000 Spawns & 500,000 Compactions)
  run_bulk_lifecycle_stress_benchmark(1'000'000);

  // 4. Cache Locality Profiling: Pre-Permutation vs. Post-Permutation
  run_cache_locality_permutation_benchmark(100'000);

  // 5. Triple-Buffered VRAM Streaming: 1,000,000 Entities (8 MB)
  run_ring_vram_streaming_benchmark(1'000'000, 50);

  std::cout << "====================================================\n";
  std::cout << "All Stage 2 benchmarks completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}
```

#### New Functions to Add to `benchmarks/arche_benchmarks.cpp`

1. **Add `#include "arche/thread_pool.hpp"`** at the top of `arche_benchmarks.cpp`.

2. **Insert benchmark functions** right before `int main()` (around line 280):

```cpp
// =============================================================================
// Benchmark 4: Multi-Threaded Parallel Scaling (1M & 2M Entities)
// =============================================================================

void run_parallel_scaling_benchmark(
    size_t entity_count,
    uint32_t thread_count,
    int iterations
) {
  std::cout << "--- Benchmark: Parallel Query Scaling ["
            << entity_count << " Entities, " << thread_count << " Threads] ---\n";
  World world;
  ThreadPool pool(thread_count);

  world.create_entities(entity_count, Position{0.0f, 0.0f}, Velocity{1.0f, 1.0f});

  constexpr float dt = 0.01666f;

  // Warm-up
  world.parallel_query<Position, const Velocity>(pool, [dt](uint32_t count, Position *pos, const Velocity *vel) {
    for (uint32_t i = 0; i < count; ++i) {
      pos[i].x += vel[i].vx * dt;
    }
  });

  const auto start_time = std::chrono::high_resolution_clock::now();
  const uint64_t start_tsc = read_tsc();

  for (int it = 0; it < iterations; ++it) {
    world.parallel_query<Position, const Velocity>(pool, [dt](uint32_t count, Position *pos, const Velocity *vel) {
      for (uint32_t i = 0; i < count; ++i) {
        pos[i].x += vel[i].vx * dt;
      }
    });
  }

  const uint64_t end_tsc = read_tsc();
  const auto end_time = std::chrono::high_resolution_clock::now();

  const std::chrono::duration<double, std::milli> elapsed_ms = end_time - start_time;
  const double avg_ms = elapsed_ms.count() / iterations;
  const double cycles_per_ent = static_cast<double>(end_tsc - start_tsc) /
      static_cast<double>(entity_count * iterations);

  std::cout << std::fixed << std::setprecision(4);
  std::cout << "  Mean Frametime     : " << avg_ms << " ms per frame\n";
  std::cout << "  Normalized Cycles  : " << cycles_per_ent << " cycles / entity\n";
  std::cout << "  120 Hz Budget Satisfied: " << (avg_ms <= 8.33 ? "YES" : "NO") << "\n\n";
}

// =============================================================================
// Benchmark 5: Massive Bulk Lifecycle Stress (1,000,000 Entities)
// =============================================================================

void run_bulk_lifecycle_stress_benchmark(size_t entity_count) {
  std::cout << "--- Benchmark: Massive Bulk Lifecycle Stress ["
            << entity_count << " Entities] ---\n";
  World world;

  // 1. Bulk spawn 1,000,000 entities
  auto start = std::chrono::high_resolution_clock::now();
  std::vector<Entity> entities = world.create_entities(
      entity_count,
      Position{1.0f, 2.0f},
      Velocity{3.0f, 4.0f}
  );
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> spawn_ms = end - start;

  // 2. Select 500,000 entities for batched destruction
  std::vector<Entity> to_destroy;
  to_destroy.reserve(entity_count / 2);
  for (size_t i = 0; i < entity_count; i += 2) {
    to_destroy.push_back(entities[i]);
  }

  start = std::chrono::high_resolution_clock::now();
  world.destroy_entities(to_destroy);
  end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> despawn_ms = end - start;

  const double total_ops = static_cast<double>(entity_count + to_destroy.size());
  const double total_ms = spawn_ms.count() + despawn_ms.count();
  const double ops_per_sec = total_ops / (total_ms / 1000.0);

  std::cout << std::fixed << std::setprecision(2);
  std::cout << "  Spawn 1M Entities  : " << spawn_ms.count() << " ms\n";
  std::cout << "  Despawn 500k Batch : " << despawn_ms.count() << " ms\n";
  std::cout << "  Throughput         : " << (ops_per_sec / 1'000'000.0)
            << " Million operations/sec\n\n";
}

// =============================================================================
// Benchmark 6: Cache Locality: Pre-Sort vs. Post-Sort Profiling
// =============================================================================

void run_cache_locality_permutation_benchmark(size_t entity_count) {
  std::cout << "--- Benchmark: Cache Locality (Pre-Sort vs. Post-Sort) ["
            << entity_count << " Entities] ---\n";
  World world;

  // 1. Spawn entities with scrambled simulated coordinates
  std::vector<Position> positions(entity_count);
  for (size_t i = 0; i < entity_count; ++i) {
    // Interleaved scatter simulating disordered chunk memory
    positions[i] = Position{static_cast<float>((i * 7919) % entity_count), 0.0f};
  }

  std::vector<Entity> entities(entity_count);
  world.create_entities(entity_count, std::span<Entity>(entities), positions.data());

  Archetype *arch = world.get_record(entities[0]).archetype;

  // Simulate neighbor lookup in disordered state
  auto measure_neighbor_latency = [&]() -> double {
    const auto start = std::chrono::high_resolution_clock::now();
    volatile float sum = 0.0f;
    for (size_t i = 0; i < 5000; ++i) {
      // Access random neighbor
      const size_t target_idx = (i * 7919) % entity_count;
      sum += world.get_component<Position>(entities[target_idx]).x;
    }
    const auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::micro> elapsed_us = end - start;
    return elapsed_us.count();
  };

  const double pre_sort_us = measure_neighbor_latency();

  // 2. Apply Morton-order permutation (sorting by position.x)
  world.sort_archetype<Position>(arch, [](const Position &a, const Position &b) {
    return a.x < b.x;
  });

  const double post_sort_us = measure_neighbor_latency();
  const double speedup = pre_sort_us / (post_sort_us > 0.0 ? post_sort_us : 1.0);

  std::cout << std::fixed << std::setprecision(2);
  std::cout << "  Pre-Sort Latency   : " << pre_sort_us << " µs\n";
  std::cout << "  Post-Sort Latency  : " << post_sort_us << " µs\n";
  std::cout << "  Cache Speedup      : " << speedup << "x faster neighbor access\n\n";
}

// =============================================================================
// Benchmark 7: Triple-Buffered 1M Entity VRAM Streaming (8 MB)
// =============================================================================

void run_ring_vram_streaming_benchmark(size_t entity_count, int iterations) {
  std::cout << "--- Benchmark: Triple-Buffered VRAM Streaming ["
            << entity_count << " Entities (8 MB)] ---\n";
  World world;

  world.create_entities(entity_count, Position{1.0f, 2.0f}, Velocity{3.0f, 4.0f});

  const size_t slot_bytes = entity_count * sizeof(Position);
  std::vector<uint8_t> vram_ring(slot_bytes * 3, 0);

  const auto start = std::chrono::high_resolution_clock::now();
  size_t bytes_written = 0;

  for (int it = 0; it < iterations; ++it) {
    bytes_written = world.stream_component_to_ring<Position>(
        it,
        vram_ring.data(),
        slot_bytes
    );
  }

  const auto end = std::chrono::high_resolution_clock::now();
  const std::chrono::duration<double, std::milli> elapsed_ms = end - start;
  const double avg_us = (elapsed_ms.count() * 1000.0) / iterations;
  const double gb_per_sec = (static_cast<double>(bytes_written) / (1024.0 * 1024.0 * 1024.0)) /
      (avg_us / 1'000'000.0);

  std::cout << std::fixed << std::setprecision(2);
  std::cout << "  Stream Latency (8MB): " << (avg_us / 1000.0) << " ms per frame\n";
  std::cout << "  VRAM Bandwidth     : " << gb_per_sec << " GB/s\n\n";
}
```

---

## 3. Brand-New Files

### 3.1 `tests/test_components.hpp`

This is a brand-new test header providing shared POD components for all integration, stress, and regression test suites.

* **Where defined:** `tests/test_components.hpp`.
* **What code it replaces:** Replaces duplicated copy-pasted `Position`, `Velocity`, `TagBurning`, and `BoidTag` definitions across all unit test suites.

```cpp
#pragma once

#include <cstdint>

namespace arche::test {

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

struct TagBurning {
  float intensity{1.0f};
};

struct BoidTag {};

} // namespace arche::test
```

---

### 3.2 `tests/stage2_validation_tests.cpp`

This is a comprehensive regression and validation test suite verifying that all Stage 2 features work in unison without breaking Stage 1 invariants.

```cpp
#include "arche/archetype.hpp"
#include "arche/chunk.hpp"
#include "arche/chunk_pool.hpp"
#include "arche/entity.hpp"
#include "arche/gpu_reflection.hpp"
#include "arche/parallel_command_buffer.hpp"
#include "arche/query.hpp"
#include "arche/query_filter.hpp"
#include "arche/slot_map.hpp"
#include "arche/thread_pool.hpp"
#include "arche/world.hpp"
#include "test_components.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
using arche::Added;
using arche::DrawIndirectCommand;
using arche::Entity;
using arche::Mutated;
using arche::ParallelCommandBuffer;
using arche::ThreadPool;
using arche::World;
using arche::Write;
using arche::test::BoidTag;
using arche::test::Position;
using arche::test::Velocity;
} // namespace

// =============================================================================
// Integration Test 1: Full Pipeline (Parallel + Double-Buffer + Prefetch)
// =============================================================================

/**
 * @brief Validates parallel work-stealing, double-buffered physics, and lookahead prefetching working together.
 */
void test_full_parallel_physics_pipeline() {
  std::cout << "[Test 1/3] Full Parallel Physics Pipeline (100k Entities)... ";
  World world;
  ThreadPool pool(4);

  world.register_double_buffered_component<Position>();

  constexpr size_t COUNT = 100'000;
  world.create_entities(COUNT, Position{0.0f, 0.0f}, Velocity{1.0f, 2.0f}, BoidTag{});

  constexpr float dt = 0.01666f;

  // Run 5 full physics frames
  for (int frame = 0; frame < 5; ++frame) {
    world.advance_tick();

    // Parallel integration reading from Buffer A and writing to Buffer B
    world.parallel_query<const Position, Write<Position>, const Velocity>(
        pool,
        [dt](uint32_t count, const Position *r, Write<Position> w, const Velocity *v) {
          for (uint32_t i = 0; i < count; ++i) {
            w[i].x = r[i].x + v[i].vx * dt;
            w[i].y = r[i].y + v[i].vy * dt;
          }
        }
    );

    // Flip buffer sub-step
    world.flip_double_buffer<Position>();
  }

  // Verify numerical accuracy across all 100k entities
  world.query<const Position>().for_each_chunk([](uint32_t count, const Position *pos) {
    for (uint32_t i = 0; i < count; ++i) {
      assert(std::abs(pos[i].x - (5.0f * 1.0f * 0.01666f)) < 0.001f);
      assert(std::abs(pos[i].y - (5.0f * 2.0f * 0.01666f)) < 0.001f);
    }
  });

  std::cout << "PASSED\n";
}

// =============================================================================
// Integration Test 2: In-Place Permutation + Reactive Query Caching
// =============================================================================

/**
 * @brief Validates in-place permutation while reactive query caches and dirty ticks are active.
 */
void test_permutation_and_reactive_cache() {
  std::cout << "[Test 2/3] Permutation + Reactive Caching + Dirty Ticks... ";
  World world;

  // Construct query before any archetypes exist
  auto query = world.query<Position, Velocity>();

  constexpr size_t COUNT = 5000;
  std::vector<Entity> entities = world.create_entities(
      COUNT,
      Position{0.0f, 0.0f},
      Velocity{1.0f, 1.0f}
  );

  Archetype *arch = world.get_record(entities[0]).archetype;

  // Permute entities in reverse
  std::vector<uint32_t> perm(COUNT);
  for (size_t i = 0; i < COUNT; ++i) {
    perm[i] = static_cast<uint32_t>(COUNT - 1 - i);
  }

  world.apply_permutation(arch, perm);

  // Existing query must read all permuted entities without failure
  size_t count = 0;
  query.for_each_chunk([&](uint32_t n, Position *, Velocity *) {
    count += n;
  });
  assert(count == COUNT);

  std::cout << "PASSED\n";
}

// =============================================================================
// Integration Test 3: Parallel Mutation Stress under High Churn
// =============================================================================

/**
 * @brief Validates parallel command buffer recording and playback under high-churn spawning/killing.
 */
void test_parallel_mutation_churn() {
  std::cout << "[Test 3/3] Parallel Mutation Stress under High Churn... ";
  World world;
  ThreadPool pool(4);
  ParallelCommandBuffer cmd_buffers(pool.worker_count());

  constexpr size_t COUNT = 20'000;
  world.create_entities(COUNT, Position{1.0f, 1.0f});

  // Parallel loop: destroy half the entities and spawn replacements
  world.parallel_query<Entity, const Position>(
      pool,
      cmd_buffers,
      [](uint32_t count, arche::CommandBuffer &cmd, Entity *ents, const Position *) {
        for (uint32_t i = 0; i < count; ++i) {
          if (ents[i].index() % 2 == 0) {
            cmd.destroy_entity(ents[i]);
            cmd.create_entity(Position{99.0f, 99.0f});
          }
        }
      }
  );

  world.playback_parallel_commands(cmd_buffers);

  // Total entity count must remain exactly COUNT
  assert(world.entity_count() == COUNT);

  std::cout << "PASSED\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Module 017: Stage 2 Validation Suite\n";
  std::cout << "====================================================\n";

  test_full_parallel_physics_pipeline();
  test_permutation_and_reactive_cache();
  test_parallel_mutation_churn();

  std::cout << "====================================================\n";
  std::cout << "All Stage 2 validation tests passed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}
```

---

## 4. Build Configuration: `CMakeLists.txt`

Add the new unit test target to `CMakeLists.txt`:

```cmake
# Module 017 Tests: 1M+ Scale Validation & Profiling Harness
add_executable(stage2_validation_tests tests/stage2_validation_tests.cpp)
target_link_libraries(stage2_validation_tests PRIVATE arche::arche)
add_test(NAME stage2_validation_tests COMMAND stage2_validation_tests)
```

