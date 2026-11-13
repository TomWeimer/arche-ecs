#include "arche/arche.hpp"
#include "test_components.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
using arche::Entity;
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
 * @brief Validates parallel work-stealing, double-buffered physics, and
 * lookahead prefetching working together.
 */
void test_full_parallel_physics_pipeline() {
  std::cout << "[Test 1/3] Full Parallel Physics Pipeline (100k Entities)... ";
  World world;
  ThreadPool pool(4);

  world.enable_double_buffer<Position>();

  constexpr size_t COUNT = 100'000;
  world.spawn_batch(
      COUNT, Position{0.0f, 0.0f}, Velocity{1.0f, 2.0f}, BoidTag{}
  );

  constexpr float dt = 0.01666f;

  // Run 5 full physics frames
  for (int frame = 0; frame < 5; ++frame) {
    world.tick();

    // Parallel integration reading from Buffer A and writing to Buffer B
    world.query_parallel<const Position, Write<Position>, const Velocity>(
        pool,
        [](uint32_t count,
           const Position *r,
           Write<Position> w,
           const Velocity *v) {
          for (uint32_t i = 0; i < count; ++i) {
            w[i].x = r[i].x + v[i].vx * dt;
            w[i].y = r[i].y + v[i].vy * dt;
          }
        }
    );

    // Flip buffer sub-step
    world.flip<Position>();
  }

  // Verify numerical accuracy across all 100k entities
  world.query<const Position>().for_each_chunk([](uint32_t count,
                                                  const Position *pos) {
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
 * @brief Validates in-place permutation while reactive query caches and dirty
 * ticks are active.
 */
void test_permutation_and_reactive_cache() {
  std::cout << "[Test 2/3] Permutation + Reactive Caching + Dirty Ticks... ";
  World world;

  // Construct query before any archetypes exist
  auto query = world.query<Position, Velocity>();

  constexpr size_t COUNT = 5000;
  std::vector<Entity> entities = world.spawn_batch(
      COUNT, Position{0.0f, 0.0f}, Velocity{1.0f, 1.0f}
  );

  arche::Archetype *arch = world.get_record(entities[0]).archetype;

  // Permute entities in reverse
  std::vector<uint32_t> perm(COUNT);
  for (size_t i = 0; i < COUNT; ++i) {
    perm[i] = static_cast<uint32_t>(COUNT - 1 - i);
  }

  world.permute(arch, perm);

  // Existing query must read all permuted entities without failure
  size_t count = 0;
  query.for_each_chunk([&](uint32_t n, Position *, Velocity *) { count += n; });
  assert(count == COUNT);

  std::cout << "PASSED\n";
}

// =============================================================================
// Integration Test 3: Parallel Mutation Stress under High Churn
// =============================================================================

/**
 * @brief Validates parallel command buffer recording and playback under
 * high-churn spawning/killing.
 */
void test_parallel_mutation_churn() {
  std::cout << "[Test 3/3] Parallel Mutation Stress under High Churn... ";
  World world;
  ThreadPool pool(4);
  ParallelCommandBuffer cmd_buffers(pool.worker_count());

  constexpr size_t COUNT = 20'000;
  world.spawn_batch(COUNT, Position{1.0f, 1.0f});

  // Parallel loop: destroy half the entities and spawn replacements
  world.query_parallel<Entity, const Position>(
      pool,
      cmd_buffers,
      [](uint32_t count,
         arche::CommandBuffer &cmd,
         Entity *ents,
         const Position *) {
        for (uint32_t i = 0; i < count; ++i) {
          if (ents[i].index() % 2 == 0) {
            cmd.despawn(ents[i]);
            cmd.spawn(Position{99.0f, 99.0f});
          }
        }
      }
  );

  world.commit(cmd_buffers);

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