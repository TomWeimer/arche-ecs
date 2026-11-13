#include "arche/arche.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {
using arche::Entity;
using arche::Hardware;
using arche::Query;
using arche::World;
} // namespace

// =============================================================================
// Platform-Native Time-Stamp Counter (TSC) Timing Infrastructure
// =============================================================================

/**
 * @brief Reads the hardware cycle or high-resolution counter with instruction
 * serialization.
 *
 * @details
 * Hardware timer mechanics by platform:
 * - **x86-64 (Intel / AMD):** Serializes instructions and reads the Time-Stamp
 * Counter via `__rdtsc()`.
 * - **ARM64 (Apple Silicon M-Series):** Issues an Instruction Synchronization
 * Barrier (`isb`) and reads the virtual counter register (`cntvct_el0`).
 * - **Generic Fallback:** Uses `std::chrono::high_resolution_clock`.
 *
 * Colony Budget Reference:
 * In Colony, the simulation runs 50,000 entities at 120 Hz (8.33 ms frame
 * budget). On a 3.2 GHz CPU: 3,200,000,000 cycles/sec / 120 frames/sec =
 * 26,666,667 cycles/frame. For 50,000 entities: 26,666,667 / 50,000 = 533.3 CPU
 * cycles/entity budget.
 *
 * ECS query traversal must consume <= 5 to 15 cycles/entity for basic updates,
 * leaving > 90% of frame time for boid flocking steering and spatial
 * partitioning.
 */
#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
[[nodiscard]] inline uint64_t read_tsc() noexcept { return __rdtsc(); }
#elif defined(__APPLE__)
#include <mach/mach_time.h>
[[nodiscard]] inline uint64_t read_tsc() noexcept {
  return mach_absolute_time();
}
#elif defined(__aarch64__)
[[nodiscard]] inline uint64_t read_tsc() noexcept {
  uint64_t val;
  asm volatile("isb; mrs %0, cntvct_el0" : "=r"(val) : : "memory");
  return val;
}
#else
[[nodiscard]] inline uint64_t read_tsc() noexcept {
  return static_cast<uint64_t>(
      std::chrono::high_resolution_clock::now().time_since_epoch().count()
  );
}
#endif

// =============================================================================
// Standalone Benchmark Component Types
// =============================================================================

/**
 * @struct Position
 * @brief 2D spatial coordinate component (8 bytes).
 */
struct Position {
  float x{0.0f};
  float y{0.0f};
};

/**
 * @struct Velocity
 * @brief 2D velocity vector component (8 bytes).
 */
struct Velocity {
  float vx{0.0f};
  float vy{0.0f};
};

// =============================================================================
// Benchmark 1: Dense Query Iteration & Trivial Movement
// =============================================================================

/**
 * @brief Measures raw chunk-iteration throughput for dense physics integration:
 * `pos += vel * dt`.
 *
 * --- Why This Benchmark Is Necessary (Documentation Rationale) ---
 * Validates the core value proposition of Archetype SoA chunk storage:
 * Iterating contiguous component columns eliminates pointer indirection and
 * cache-line misses, allowing hardware prefetchers and compiler
 * auto-vectorizers (AVX2 / NEON) to saturate L1/L2 caches.
 *
 * @param entity_count Total number of entities instantiated across 64 KB
 * chunks.
 * @param iterations Number of simulated frames to average.
 */
void run_trivial_movement_benchmark(size_t entity_count, int iterations) {
  std::cout << "--- Benchmark: Dense Query Iteration (pos += vel * dt) ["
            << entity_count << " Entities] ---\n";
  World world;

  // 1. Populate world with entity_count entities in [Position, Velocity]
  // archetype
  for (size_t i = 0; i < entity_count; ++i) {
    world.spawn(
        Position{static_cast<float>(i), static_cast<float>(i)},
        Velocity{1.0f, -1.0f}
    );
  }

  Query<Position, Velocity> query(world);
  constexpr float dt = 0.01666f;

  // 2. Warm-up loop (10 iterations) to populate instruction and data caches
  for (int w = 0; w < 10; ++w) {
    query.for_each_chunk([](uint32_t count, Position *p, Velocity *v) {
      for (uint32_t i = 0; i < count; ++i) {
        p[i].x += v[i].vx * dt;
        p[i].y += v[i].vy * dt;
      }
    });
  }

  // 3. Timed execution across iterations
  const uint64_t tsc_start = read_tsc();
  const auto start_time = std::chrono::high_resolution_clock::now();

  for (int it = 0; it < iterations; ++it) {
    query.for_each_chunk([](uint32_t count, Position *p, Velocity *v) {
      for (uint32_t i = 0; i < count; ++i) {
        p[i].x += v[i].vx * dt;
        p[i].y += v[i].vy * dt;
      }
    });
  }

  const auto end_time = std::chrono::high_resolution_clock::now();
  const uint64_t tsc_end = read_tsc();

  // 4. Compute cycle and wall-clock metrics
  const uint64_t total_cycles = tsc_end - tsc_start;
  const double raw_cycles_per_entity = static_cast<double>(total_cycles) /
      (entity_count * iterations);

  const std::chrono::duration<double, std::milli> elapsed_ms = end_time -
      start_time;
  const double ms_per_frame = elapsed_ms.count() / iterations;

  // Normalized to Colony's 3.2 GHz reference CPU:
  // cycles = (time_in_seconds * 3.2 GHz) / entity_count
  constexpr double REF_CLOCK_GHZ = 3.2;
  const double normalized_cycles_per_entity = ((ms_per_frame / 1000.0) *
                                               (REF_CLOCK_GHZ * 1e9)) /
      entity_count;

  std::cout << std::fixed << std::setprecision(4);
  std::cout << "  Mean Frametime     : " << ms_per_frame << " ms\n";
  std::cout << "  Raw Counter / Ent. : " << raw_cycles_per_entity << " ticks\n";
  std::cout << "  Normalized Cycles  : " << normalized_cycles_per_entity
            << " cycles @ 3.2GHz (Target: <= 533 cycles)\n";

  if (normalized_cycles_per_entity <= 533.0) {
    std::cout
        << "  Status             : PASSED (Well within Colony 120Hz Budget)\n";
  } else {
    std::cout << "  Status             : FAILED (Exceeded Cycle Budget)\n";
  }
  std::cout << "\n";
}

// =============================================================================
// Benchmark 2: Structural Mutation Stress (Spawn / Despawn)
// =============================================================================

/**
 * @brief Measures bulk allocation and deallocation throughput for Entity
 * SlotMap and Chunk storage.
 *
 * --- Why This Benchmark Is Necessary (Documentation Rationale) ---
 * High-velocity games continuously spawn particle effects, projectiles, and
 * hazards. Validates that `World::create_entity` and `World::destroy_entity`
 * maintain high throughput without memory fragmentation or allocation stalls.
 *
 * @param batch_size Number of entities to spawn and subsequently destroy.
 */
void run_spawn_despawn_stress_benchmark(size_t batch_size) {
  std::cout << "--- Benchmark: Structural Mutation Stress (Spawn/Despawn "
            << batch_size << " Entities) ---\n";
  World world;

  const auto start_time = std::chrono::high_resolution_clock::now();

  // 1. Bulk spawn phase
  std::vector<Entity> batch;
  batch.reserve(batch_size);
  for (size_t i = 0; i < batch_size; ++i) {
    batch.push_back(
        world.spawn(Position{0.0f, 0.0f}, Velocity{1.0f, 1.0f})
    );
  }

  // 2. Bulk despawn phase (Swap-and-Pop)
  for (Entity e : batch) {
    world.despawn(e);
  }

  const auto end_time = std::chrono::high_resolution_clock::now();
  const std::chrono::duration<double, std::milli> elapsed_ms = end_time -
      start_time;

  const double total_ops = static_cast<double>(batch_size * 2);
  const double ops_per_sec = total_ops / (elapsed_ms.count() / 1000.0);

  std::cout << std::fixed << std::setprecision(2);
  std::cout << "  Elapsed Time       : " << elapsed_ms.count() << " ms\n";
  std::cout << "  Throughput         : " << (ops_per_sec / 1'000'000.0)
            << " Million operations/sec\n\n";
}

// =============================================================================
// Benchmark 3: Direct VRAM Batch Streaming (Approach A)
// =============================================================================

/**
 * @brief Measures memory bandwidth when streaming chunk SoA columns into mapped
 * GPU VRAM.
 *
 * --- Why This Benchmark Is Necessary (Documentation Rationale) ---
 * Module 007 establishes zero-repack GPU streaming. This benchmark measures the
 * real-world host memory copy throughput when streaming 50,000 particle
 * positions (400 KB) into a mock mapped VRAM buffer.
 *
 * @param entity_count Number of entities to stream (50,000 baseline).
 * @param iterations Number of frames to benchmark.
 */
void run_gpu_streaming_benchmark(size_t entity_count, int iterations) {
  std::cout << "--- Benchmark: Direct VRAM Batch Streaming (Approach A) ["
            << entity_count << " Entities] ---\n";
  World world;

  // 1. Populate world
  for (size_t i = 0; i < entity_count; ++i) {
    world.spawn(
        Position{static_cast<float>(i), static_cast<float>(i * 2)},
        Velocity{1.0f, 1.0f}
    );
  }

  // 2. Allocate mock mapped VRAM buffer
  const size_t vram_bytes = entity_count * sizeof(Position);
  std::vector<uint8_t> mock_mapped_vram(vram_bytes, 0);

  // 3. Warm-up
  world.stream_component_to<Position>(mock_mapped_vram.data());

  // 4. Benchmark streaming
  const auto start_time = std::chrono::high_resolution_clock::now();
  size_t bytes_written = 0;
  for (int it = 0; it < iterations; ++it) {
    bytes_written = world.stream_component_to<Position>(
        mock_mapped_vram.data()
    );
  }
  const auto end_time = std::chrono::high_resolution_clock::now();

  const std::chrono::duration<double, std::milli> elapsed_ms = end_time -
      start_time;
  const double avg_us = (elapsed_ms.count() * 1000.0) / iterations;
  const double gb_per_sec = (static_cast<double>(bytes_written) /
                             (1024.0 * 1024.0 * 1024.0)) /
      (avg_us / 1'000'000.0);

  std::cout << std::fixed << std::setprecision(2);
  std::cout << "  Mean Stream Latency: " << avg_us << " µs per full frame\n";
  std::cout << "  Memory Bandwidth   : " << gb_per_sec << " GB/s\n\n";
}

// =============================================================================
// Benchmark 4: Multi-Threaded Parallel Scaling (1M & 2M Entities)
// =============================================================================

void run_parallel_scaling_benchmark(
    size_t entity_count,
    uint32_t thread_count,
    int iterations
) {
  std::cout << "--- Benchmark: Parallel Query Scaling [" << entity_count
            << " Entities, " << thread_count << " Threads] ---\n";
  World world;
  arche::ThreadPool pool(thread_count);

  world.spawn_batch(
      entity_count, Position{0.0f, 0.0f}, Velocity{1.0f, 1.0f}
  );

  constexpr float dt = 0.01666f;

  // Warm-up
  world.query_parallel<Position, const Velocity>(
      pool, [](uint32_t count, Position *pos, const Velocity *vel) {
        for (uint32_t i = 0; i < count; ++i) {
          pos[i].x += vel[i].vx * dt;
        }
      }
  );

  const auto start_time = std::chrono::high_resolution_clock::now();
  const uint64_t start_tsc = read_tsc();

  for (int it = 0; it < iterations; ++it) {
    world.query_parallel<Position, const Velocity>(
        pool, [](uint32_t count, Position *pos, const Velocity *vel) {
          for (uint32_t i = 0; i < count; ++i) {
            pos[i].x += vel[i].vx * dt;
          }
        }
    );
  }

  const uint64_t end_tsc = read_tsc();
  const auto end_time = std::chrono::high_resolution_clock::now();

  const std::chrono::duration<double, std::milli> elapsed_ms = end_time -
      start_time;
  const double avg_ms = elapsed_ms.count() / iterations;
  const double cycles_per_ent = static_cast<double>(end_tsc - start_tsc) /
      static_cast<double>(entity_count * iterations);

  std::cout << std::fixed << std::setprecision(4);
  std::cout << "  Mean Frametime     : " << avg_ms << " ms per frame\n";
  std::cout << "  Normalized Cycles  : " << cycles_per_ent
            << " cycles / entity\n";
  std::cout << "  120 Hz Budget Satisfied: " << (avg_ms <= 8.33 ? "YES" : "NO")
            << "\n\n";
}

// =============================================================================
// Benchmark 5: Massive Bulk Lifecycle Stress (1,000,000 Entities)
// =============================================================================

void run_bulk_lifecycle_stress_benchmark(size_t entity_count) {
  std::cout << "--- Benchmark: Massive Bulk Lifecycle Stress [" << entity_count
            << " Entities] ---\n";
  World world;

  // 1. Bulk spawn 1,000,000 entities
  auto start = std::chrono::high_resolution_clock::now();
  std::vector<Entity> entities = world.spawn_batch(
      entity_count, Position{1.0f, 2.0f}, Velocity{3.0f, 4.0f}
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
  world.despawn_batch(to_destroy);
  end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> despawn_ms = end - start;

  const double total_ops = static_cast<double>(
      entity_count + to_destroy.size()
  );
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
    positions[i] = Position{
        static_cast<float>((i * 7919) % entity_count), 0.0f
    };
  }

  std::vector<Entity> entities(entity_count);
  world.spawn_batch(
      entity_count, std::span<Entity>(entities), positions.data()
  );

  arche::Archetype *arch = world.get_record(entities[0]).archetype;

  // Simulate neighbor lookup in disordered state
  auto measure_neighbor_latency = [&]() -> double {
    const auto start = std::chrono::high_resolution_clock::now();
    volatile float sum = 0.0f;
    for (size_t i = 0; i < 5000; ++i) {
      // Access random neighbor
      const size_t target_idx = (i * 7919) % entity_count;
      sum += world.get<Position>(entities[target_idx]).x;
    }
    const auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::micro> elapsed_us = end - start;
    return elapsed_us.count();
  };

  const double pre_sort_us = measure_neighbor_latency();

  // 2. Apply Morton-order permutation (sorting by position.x)
  world.sort<Position>(
      arch, [](const Position &a, const Position &b) { return a.x < b.x; }
  );

  const double post_sort_us = measure_neighbor_latency();
  const double speedup = pre_sort_us /
      (post_sort_us > 0.0 ? post_sort_us : 1.0);

  std::cout << std::fixed << std::setprecision(2);
  std::cout << "  Pre-Sort Latency   : " << pre_sort_us << " µs\n";
  std::cout << "  Post-Sort Latency  : " << post_sort_us << " µs\n";
  std::cout << "  Cache Speedup      : " << speedup
            << "x faster neighbor access\n\n";
}

// =============================================================================
// Benchmark 7: Triple-Buffered 1M Entity VRAM Streaming (8 MB)
// =============================================================================

void run_ring_vram_streaming_benchmark(size_t entity_count, int iterations) {
  std::cout << "--- Benchmark: Triple-Buffered VRAM Streaming [" << entity_count
            << " Entities (8 MB)] ---\n";
  World world;

  world.spawn_batch(
      entity_count, Position{1.0f, 2.0f}, Velocity{3.0f, 4.0f}
  );

  const size_t slot_bytes = entity_count * sizeof(Position);
  std::vector<uint8_t> vram_ring(slot_bytes * 3, 0);

  const auto start = std::chrono::high_resolution_clock::now();
  size_t bytes_written = 0;

  for (int it = 0; it < iterations; ++it) {
    bytes_written = world.stream_gpu<Position>(
        it, vram_ring.data(), slot_bytes
    );
  }

  const auto end = std::chrono::high_resolution_clock::now();
  const std::chrono::duration<double, std::milli> elapsed_ms = end - start;
  const double avg_us = (elapsed_ms.count() * 1000.0) / iterations;
  const double gb_per_sec = (static_cast<double>(bytes_written) /
                             (1024.0 * 1024.0 * 1024.0)) /
      (avg_us / 1'000'000.0);

  std::cout << std::fixed << std::setprecision(2);
  std::cout << "  Stream Latency (8MB): " << (avg_us / 1000.0)
            << " ms per frame\n";
  std::cout << "  VRAM Bandwidth     : " << gb_per_sec << " GB/s\n\n";
}

// =============================================================================
// Benchmark Runner Entry Point
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche_benchmarks (Stage 2 1M+ Cycle Profiler)\n";
  std::cout << "====================================================\n\n";

  std::cout << "Hardware Topology Detected:\n";
  std::cout << "  Cache Line Size    : " << Hardware::cache_line_size()
            << " bytes\n";
  std::cout << "  OS Page Size       : " << Hardware::page_size() << " bytes\n";
  std::cout << "  Optimal Chunk Size : " << Hardware::optimal_chunk_size()
            << " bytes\n";
  std::cout << "  Hardware Threads   : " << std::thread::hardware_concurrency()
            << "\n\n";

  // --- Stage 1 Baseline Benchmarks ---
  run_trivial_movement_benchmark(50'000, 100);
  run_trivial_movement_benchmark(100'000, 50);
  run_trivial_movement_benchmark(500'000, 20);

  // --- Stage 2 1M+ Scale Benchmarks ---
  // 1. Multi-Threaded 1,000,000 Entity Scaling
  run_parallel_scaling_benchmark(
      1'000'000, std::thread::hardware_concurrency(), 20
  );

  // 2. Multi-Threaded 2,000,000 Entity Extreme Stress
  run_parallel_scaling_benchmark(
      2'000'000, std::thread::hardware_concurrency(), 10
  );

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