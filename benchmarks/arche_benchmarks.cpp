#include "arche/entity.hpp"
#include "arche/gpu_reflection.hpp"
#include "arche/hardware.hpp"
#include "arche/query.hpp"
#include "arche/world.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
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
 * @brief Reads the hardware cycle or high-resolution counter with instruction serialization.
 *
 * @details
 * Hardware timer mechanics by platform:
 * - **x86-64 (Intel / AMD):** Serializes instructions and reads the Time-Stamp Counter via `__rdtsc()`.
 * - **ARM64 (Apple Silicon M-Series):** Issues an Instruction Synchronization Barrier (`isb`)
 *   and reads the virtual counter register (`cntvct_el0`).
 * - **Generic Fallback:** Uses `std::chrono::high_resolution_clock`.
 *
 * Colony Budget Reference:
 * In Colony, the simulation runs 50,000 entities at 120 Hz (8.33 ms frame budget).
 * On a 3.2 GHz CPU:
 *   3,200,000,000 cycles/sec / 120 frames/sec = 26,666,667 cycles/frame.
 *   For 50,000 entities: 26,666,667 / 50,000 = 533.3 CPU cycles/entity budget.
 *
 * ECS query traversal must consume <= 5 to 15 cycles/entity for basic updates,
 * leaving > 90% of frame time for boid flocking steering and spatial partitioning.
 */
#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
[[nodiscard]] inline uint64_t read_tsc() noexcept {
  return __rdtsc();
}
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
 * @brief Measures raw chunk-iteration throughput for dense physics integration: `pos += vel * dt`.
 *
 * --- Why This Benchmark Is Necessary (Documentation Rationale) ---
 * Validates the core value proposition of Archetype SoA chunk storage:
 * Iterating contiguous component columns eliminates pointer indirection and cache-line misses,
 * allowing hardware prefetchers and compiler auto-vectorizers (AVX2 / NEON) to saturate L1/L2 caches.
 *
 * @param entity_count Total number of entities instantiated across 64 KB chunks.
 * @param iterations Number of simulated frames to average.
 */
void run_trivial_movement_benchmark(size_t entity_count, int iterations) {
  std::cout << "--- Benchmark: Dense Query Iteration (pos += vel * dt) ["
            << entity_count << " Entities] ---\n";
  World world;

  // 1. Populate world with entity_count entities in [Position, Velocity] archetype
  for (size_t i = 0; i < entity_count; ++i) {
    world.create_entity(
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
  const double raw_cycles_per_entity =
      static_cast<double>(total_cycles) / (entity_count * iterations);

  const std::chrono::duration<double, std::milli> elapsed_ms =
      end_time - start_time;
  const double ms_per_frame = elapsed_ms.count() / iterations;

  // Normalized to Colony's 3.2 GHz reference CPU:
  // cycles = (time_in_seconds * 3.2 GHz) / entity_count
  constexpr double REF_CLOCK_GHZ = 3.2;
  const double normalized_cycles_per_entity =
      ((ms_per_frame / 1000.0) * (REF_CLOCK_GHZ * 1e9)) / entity_count;

  std::cout << std::fixed << std::setprecision(4);
  std::cout << "  Mean Frametime     : " << ms_per_frame << " ms\n";
  std::cout << "  Raw Counter / Ent. : " << raw_cycles_per_entity << " ticks\n";
  std::cout << "  Normalized Cycles  : " << normalized_cycles_per_entity
            << " cycles @ 3.2GHz (Target: <= 533 cycles)\n";

  if (normalized_cycles_per_entity <= 533.0) {
    std::cout << "  Status             : PASSED (Well within Colony 120Hz Budget)\n";
  } else {
    std::cout << "  Status             : FAILED (Exceeded Cycle Budget)\n";
  }
  std::cout << "\n";
}

// =============================================================================
// Benchmark 2: Structural Mutation Stress (Spawn / Despawn)
// =============================================================================

/**
 * @brief Measures bulk allocation and deallocation throughput for Entity SlotMap and Chunk storage.
 *
 * --- Why This Benchmark Is Necessary (Documentation Rationale) ---
 * High-velocity games continuously spawn particle effects, projectiles, and hazards.
 * Validates that `World::create_entity` and `World::destroy_entity` maintain high throughput
 * without memory fragmentation or allocation stalls.
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
        world.create_entity(Position{0.0f, 0.0f}, Velocity{1.0f, 1.0f})
    );
  }

  // 2. Bulk despawn phase (Swap-and-Pop)
  for (Entity e : batch) {
    world.destroy_entity(e);
  }

  const auto end_time = std::chrono::high_resolution_clock::now();
  const std::chrono::duration<double, std::milli> elapsed_ms =
      end_time - start_time;

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
 * @brief Measures memory bandwidth when streaming chunk SoA columns into mapped GPU VRAM.
 *
 * --- Why This Benchmark Is Necessary (Documentation Rationale) ---
 * Module 007 establishes zero-repack GPU streaming. This benchmark measures the real-world
 * host memory copy throughput when streaming 50,000 particle positions (400 KB) into a mock
 * mapped VRAM buffer.
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
    world.create_entity(
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
    bytes_written = world.stream_component_to<Position>(mock_mapped_vram.data());
  }
  const auto end_time = std::chrono::high_resolution_clock::now();

  const std::chrono::duration<double, std::milli> elapsed_ms =
      end_time - start_time;
  const double avg_us = (elapsed_ms.count() * 1000.0) / iterations;
  const double gb_per_sec =
      (static_cast<double>(bytes_written) / (1024.0 * 1024.0 * 1024.0)) /
      (avg_us / 1'000'000.0);

  std::cout << std::fixed << std::setprecision(2);
  std::cout << "  Mean Stream Latency: " << avg_us << " µs per full frame\n";
  std::cout << "  Memory Bandwidth   : " << gb_per_sec << " GB/s\n\n";
}

// =============================================================================
// Benchmark Runner Entry Point
// =============================================================================

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
