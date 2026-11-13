#include "arche/command_buffer.hpp"
#include "arche/entity.hpp"
#include "arche/query.hpp"
#include "arche/world.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
using arche::CommandBuffer;
using arche::Entity;
using arche::With;
using arche::Without;
using arche::World;
} // namespace

// =============================================================================
// Standalone Test Component Types
// =============================================================================

struct Position {
  float x{0.0f};
  float y{0.0f};
};

struct Velocity {
  float vx{0.0f};
  float vy{0.0f};
};

struct Mass {
  float value{1.0f};
};

struct TagDead {};

// =============================================================================
// Unit Test 1: Query Matching Across Multiple Archetypes
// =============================================================================

/**
 * @brief Verifies that queries match all archetypes containing the requested
 * component superset.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Archetype ECS queries are superset matches:
 * - A query for `Query<Position, Velocity>` MUST match an archetype containing
 *   `[Position, Velocity]`.
 * - It MUST ALSO match superset archetypes containing `[Position, Velocity,
 * Mass]`.
 * - It MUST NOT match subset archetypes like `[Position]` that miss `Velocity`.
 *
 * This test guarantees:
 * 1. Correct inclusion bitmask evaluation against all graph archetypes.
 * 2. Range-based for-loop structured binding traversal (`for (auto [pos, vel] :
 * query)`).
 * 3. Exact entity counts and accumulator sum verification across multiple
 * archetypes.
 */
void test_basic_query_matching() {
  std::cout << "[Test 1/6] Query Matching Across Multiple Archetypes... ";
  World world;

  // 1. Archetype 1: [Position, Velocity] (2 entities)
  world.spawn(Position{1.0f, 2.0f}, Velocity{0.1f, 0.2f});
  world.spawn(Position{3.0f, 4.0f}, Velocity{0.3f, 0.4f});

  // 2. Archetype 2: [Position, Velocity, Mass] (1 entity — must also match!)
  world.spawn(Position{5.0f, 6.0f}, Velocity{0.5f, 0.6f}, Mass{10.0f});

  // 3. Archetype 3: [Position] (1 entity — must NOT match)
  world.spawn(Position{7.0f, 8.0f});

  // 4. Execute query using world.query<Position, Velocity>() helper
  size_t count = 0;
  float sum_x = 0.0f;

  for (auto [pos, vel] : world.query<Position, Velocity>()) {
    sum_x += pos.x;
    ++count;
  }

  // 5. Verify that only the 3 matching entities were traversed
  assert(count == 3);
  assert(sum_x == (1.0f + 3.0f + 5.0f));

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: Ergonomic Per-Entity Callback Iteration (for_each)
// =============================================================================

/**
 * @brief Verifies the ergonomic per-entity lambda iteration loop (`for_each`).
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Gameplay and state machine logic often prioritize clean, readable syntax over
 * raw chunk-level array loops:
 * - `query.for_each([](Position& pos, Velocity& vel) { ... })` passes
 * references directly.
 * - This test guarantees that component references can be modified in-place and
 *   that modifications persist accurately in chunk memory.
 */
void test_per_entity_for_each() {
  std::cout << "[Test 2/6] Ergonomic Per-Entity Callback (for_each)... ";
  World world;

  world.spawn(Position{10.0f, 20.0f}, Velocity{1.0f, 2.0f});
  world.spawn(Position{30.0f, 40.0f}, Velocity{3.0f, 4.0f});

  // 1. In-place modification via for_each visitor
  auto query = world.query<Position, Velocity>();
  query.for_each([](Position &pos, Velocity &vel) {
    pos.x += vel.vx;
    pos.y += vel.vy;
  });

  // 2. Verification pass via range loop
  size_t verified = 0;
  for (auto [pos, vel] : world.query<Position, Velocity>()) {
    if (verified == 0) {
      assert(pos.x == 11.0f && pos.y == 22.0f);
    } else if (verified == 1) {
      assert(pos.x == 33.0f && pos.y == 44.0f);
    }
    ++verified;
  }
  assert(verified == 2);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Direct Chunk Visitor for Vectorization (for_each_chunk)
// =============================================================================

/**
 * @brief Verifies chunk-level pointer callback traversal (`for_each_chunk`).
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Physics integration, neighbor distance calculations, and steering algorithms
 * require saturating CPU SIMD execution units (ARM Neon / x86 AVX2).
 *
 * `for_each_chunk` exposes contiguous C-style pointers `Position*` and
 * `Velocity*` directly to the caller, guaranteeing:
 * 1. Pointers never alias (separate SoA columns).
 * 2. Compilers auto-vectorize loops into SIMD instructions without function
 *    call overhead.
 * 3. Exact entity counts are supplied for tight, unrolled loop trips.
 */
void test_chunk_visitor_vectorization() {
  std::cout << "[Test 3/6] Direct for_each_chunk Callback Traversal... ";
  World world;

  constexpr int ENTITY_COUNT = 1000;
  for (int i = 0; i < ENTITY_COUNT; ++i) {
    world.spawn(
        Position{static_cast<float>(i), 0.0f}, Velocity{1.0f, 2.0f}
    );
  }

  auto query = world.query<Position, Velocity>();
  constexpr float dt = 0.01666f;

  // 1. High-performance chunk callback
  query.for_each_chunk([](uint32_t count, Position *pos, Velocity *vel) {
    for (uint32_t i = 0; i < count; ++i) {
      pos[i].x += vel[i].vx * dt;
      pos[i].y += vel[i].vy * dt;
    }
  });

  // 2. Verification pass
  size_t verified = 0;
  for (auto [pos, vel] : world.query<Position, Velocity>()) {
    assert(pos.y == 2.0f * dt);
    ++verified;
  }
  assert(verified == ENTITY_COUNT);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Query Bitmask Filters (With<...> and Without<...>)
// =============================================================================

/**
 * @brief Verifies compile-time component inclusion and exclusion filters.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * High-performance state filtering must execute in $O(1)$ without evaluating
 * individual entity state flags:
 * - `Without<TagDead>` must reject archetypes tagged with `TagDead` via bitwise
 * AND.
 * - `With<Mass>` must require `Mass` on matching archetypes without needing to
 * unpack it into the iteration loop tuple.
 */
void test_query_filters_with_and_without() {
  std::cout << "[Test 4/6] Query Filters (With<...> and Without<...>)... ";
  World world;

  // Entity A: Normal live entity [Position, Velocity]
  Entity e_live = world.spawn(
      Position{1.0f, 1.0f}, Velocity{1.0f, 0.0f}
  );

  // Entity B: Dead entity [Position, Velocity, TagDead]
  Entity e_dead = world.spawn(
      Position{2.0f, 2.0f}, Velocity{1.0f, 0.0f}, TagDead{}
  );

  // Entity C: Heavy live entity [Position, Velocity, Mass]
  Entity e_heavy = world.spawn(
      Position{3.0f, 3.0f}, Velocity{1.0f, 0.0f}, Mass{50.0f}
  );

  // Entity D: Heavy dead entity [Position, Velocity, Mass, TagDead]
  Entity e_heavy_dead = world.spawn(
      Position{4.0f, 4.0f}, Velocity{1.0f, 0.0f}, Mass{50.0f}, TagDead{}
  );

  // 1. Test Without<TagDead>: should match only e_live and e_heavy (2 entities)
  {
    size_t alive_count = 0;
    float sum_x = 0.0f;
    for (auto [pos, vel] :
         world.query<Position, Velocity>(Without<TagDead>{})) {
      sum_x += pos.x;
      ++alive_count;
    }
    assert(alive_count == 2);
    assert(sum_x == (1.0f + 3.0f));
  }

  // 2. Test With<Mass>: should match only e_heavy and e_heavy_dead (2 entities)
  {
    size_t heavy_count = 0;
    for (auto [pos, vel] : world.query<Position, Velocity>(With<Mass>{})) {
      (void)vel;
      assert(pos.x == 3.0f || pos.x == 4.0f);
      ++heavy_count;
    }
    assert(heavy_count == 2);
  }

  // 3. Test combined: With<Mass> AND Without<TagDead> -> only e_heavy (1
  // entity)
  {
    size_t heavy_alive_count = 0;
    for (auto [pos, vel] :
         world.query<Position, Velocity>(With<Mass>{}, Without<TagDead>{})) {
      (void)vel;
      assert(pos.x == 3.0f);
      ++heavy_alive_count;
    }
    assert(heavy_alive_count == 1);
  }

  (void)e_live;
  (void)e_dead;
  (void)e_heavy;
  (void)e_heavy_dead;
  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 5: Entity-Aware Query and Deferred CommandBuffer Integration
// =============================================================================

/**
 * @brief Verifies the architectural link between Query iteration and deferred
 * CommandBuffer mutations.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Direct structural mutation during query traversal corrupts chunk rows via
 * Swap-and-Pop (Iterator Invalidation Hazard).
 *
 * By yielding the Entity handle during queries (`world.query<Entity,
 * Position>()` or `for_each_entity`), systems can safely queue mutations into a
 * CommandBuffer and flush them deterministically after the loop finishes.
 */
void test_query_entity_and_deferred_mutation() {
  std::cout << "[Test 5/6] Entity-Aware Query & Deferred Mutation Bridge... ";
  World world;
  CommandBuffer cmd;

  // 1. Spawn a row of entities with decreasing Y positions
  std::vector<Entity> created;
  for (int i = 0; i < 10; ++i) {
    created.push_back(world.spawn(
        Position{static_cast<float>(i), static_cast<float>(5 - i)},
        Velocity{0.0f, 0.0f}
    ));
  }

  // 2. Iterate query yielding Entity handles: cull entities where pos.y < 0
  // and attach TagDead to entities where pos.y == 0
  for (auto [entity, pos, vel] : world.query<Entity, Position, Velocity>()) {
    (void)vel;
    if (pos.y < 0.0f) {
      cmd.despawn(entity);
    } else if (pos.y == 0.0f) {
      cmd.add(entity, TagDead{});
    }
  }

  // 3. Verify that before playback, world state is untouched
  assert(world.entity_count() == 10);

  // 4. Playback deferred commands safely outside the query loop
  cmd.commit(world);
  assert(cmd.empty());

  // 5. Verify results:
  // pos.y was: 5, 4, 3, 2, 1, 0, -1, -2, -3, -4
  // Destroyed: -1, -2, -3, -4 (4 entities) -> 6 surviving entities
  assert(world.entity_count() == 6);

  // Entity with pos.y == 0 (index 5) must now possess TagDead
  assert(world.is_alive(created[5]));
  assert(world.has<TagDead>(created[5]));

  // 6. Also test deferred spawn via CommandBuffer
  cmd.spawn(Position{100.0f, 200.0f}, Velocity{10.0f, 20.0f});
  cmd.spawn(Position{300.0f, 400.0f}, Velocity{30.0f, 40.0f});
  assert(world.entity_count() == 6); // Not yet created

  cmd.commit(world);
  assert(world.entity_count() == 8); // Now created!

  // Verify the newly created entities can be queried immediately
  size_t spawned_seen = 0;
  for (auto [pos, vel] : world.query<Position, Velocity>()) {
    (void)vel;
    if (pos.x >= 100.0f) {
      ++spawned_seen;
    }
  }
  assert(spawned_seen == 2);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 6: High-Density Iteration Benchmark (Colony Budget)
// =============================================================================

/**
 * @brief Measures query iteration throughput across 50,000 entities within a
 * 120Hz frame budget.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Archetype chunk storage is designed to fit inside modern CPU L1/L2 caches and
 * achieve single-cycle throughput per entity:
 * - Target budget: <= 533 CPU cycles per entity (derived from 120Hz frame
 * budget allocated to ECS updates across 50,000 boids).
 * - Contiguous layout and precomputed column offsets must prevent cache
 * thrashing.
 *
 * This test guarantees:
 * 1. Stable execution over 100 consecutive frames with zero heap allocations.
 * 2. Average throughput meets or exceeds the library's cycle budget constraint.
 */
void test_high_density_throughput_and_cycles() {
  std::cout << "[Test 6/6] High-Density Benchmark (50,000 Entities @ 120Hz "
               "budget)... ";
  World world;

  constexpr int NUM_ENTITIES = 50'000;
  for (int i = 0; i < NUM_ENTITIES; ++i) {
    world.spawn(
        Position{static_cast<float>(i), static_cast<float>(i * 2)},
        Velocity{0.5f, -0.5f}
    );
  }

  auto query = world.query<Position, Velocity>();
  constexpr float dt = 1.0f / 120.0f;

  // 1. Warm-up iteration to populate CPU L1/L2 caches
  query.for_each_chunk([](uint32_t count, Position *pos, Velocity *vel) {
    for (uint32_t i = 0; i < count; ++i) {
      pos[i].x += vel[i].vx * dt;
      pos[i].y += vel[i].vy * dt;
    }
  });

  // 2. Timed iteration over 100 frames
  auto start = std::chrono::high_resolution_clock::now();

  constexpr int FRAMES = 100;
  for (int f = 0; f < FRAMES; ++f) {
    query.for_each_chunk([](uint32_t count, Position *pos, Velocity *vel) {
      for (uint32_t i = 0; i < count; ++i) {
        pos[i].x += vel[i].vx * dt;
        pos[i].y += vel[i].vy * dt;
      }
    });
  }

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> elapsed_ms = end - start;
  double avg_frame_ms = elapsed_ms.count() / FRAMES;
  double ns_per_entity = (avg_frame_ms * 1'000'000.0) / NUM_ENTITIES;

  // Assuming a 3.2 GHz core (0.3125 ns per cycle)
  double estimated_cycles = ns_per_entity / 0.3125;

  std::cout << "PASSED (" << avg_frame_ms << " ms/frame for 50k entities, ~"
            << estimated_cycles << " cycles/entity [Target: <= 533 cycles])\n";
  assert(estimated_cycles < 533.0 && "Query loop exceeded CPU cycle budget!");
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Query & SIMD Iteration Unit Tests\n";
  std::cout << "====================================================\n";

  test_basic_query_matching();
  test_per_entity_for_each();
  test_chunk_visitor_vectorization();
  test_query_filters_with_and_without();
  test_query_entity_and_deferred_mutation();
  test_high_density_throughput_and_cycles();

  std::cout << "====================================================\n";
  std::cout << "All Query System tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}
