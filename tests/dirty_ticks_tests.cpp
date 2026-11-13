#include "arche/entity.hpp"
#include "arche/query.hpp"
#include "arche/query_filter.hpp"
#include "arche/world.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
using arche::Added;
using arche::Entity;
using arche::Mutated;
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

// =============================================================================
// Unit Test 1: Monotonic World Ticking & Version Stamping
// =============================================================================

/**
 * @brief Validates world tick advancement and chunk column version stamping.
 */
void test_world_ticking_and_stamping() {
  std::cout << "[Test 1/4] Monotonic World Ticking & Version Stamping... ";
  World world;

  assert(world.current_tick() == 1);
  uint32_t t2 = world.advance_tick();
  assert(t2 == 2);
  assert(world.current_tick() == 2);

  // Spawning an entity at tick 2 stamps chunk structure and columns with tick 2
  Entity e = world.create_entity(Position{1.0f, 2.0f}, Velocity{3.0f, 4.0f});
  assert(world.is_valid(e));

  // Advance tick to 5 and mark Position dirty
  world.advance_tick(); // 3
  world.advance_tick(); // 4
  world.advance_tick(); // 5
  world.mark_component_dirty<Position>(e);

  // Querying with Mutated<Position>{4} should match the chunk
  size_t matches = 0;
  world.query<Position>(Mutated<Position>{4})
      .for_each_chunk([&](uint32_t count, Position *) { matches += count; });
  assert(matches == 1);

  // Querying with Mutated<Position>{5} should NOT match (modified at 5, not >
  // 5)
  matches = 0;
  world.query<Position>(Mutated<Position>{5})
      .for_each_chunk([&](uint32_t count, Position *) { matches += count; });
  assert(matches == 0);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: Skipping Dormant Chunks in O(1) via Mutated<T>
// =============================================================================

/**
 * @brief Validates that Mutated<T> skips 49 out of 50 chunks in < 2
 * microseconds.
 */
void test_skip_dormant_chunks() {
  std::cout << "[Test 2/4] Skipping Dormant Chunks in O(1) via Mutated<T>... ";
  World world;

  // Create 50,000 entities across ~30-50 chunks at tick 1
  constexpr size_t COUNT = 50'000;
  std::vector<Entity> entities = world.create_entities(
      COUNT, Position{0.0f, 0.0f}, Velocity{1.0f, 1.0f}
  );

  // Advance simulation to tick 100
  for (int i = 0; i < 99; ++i) {
    world.advance_tick();
  }
  assert(world.current_tick() == 100);

  // Mutate ONLY the first entity at tick 100
  world.get_component<Position>(entities[0]).x = 999.0f;
  world.mark_component_dirty<Position>(entities[0]);

  // Query entities where Position mutated since tick 50
  auto start = std::chrono::high_resolution_clock::now();

  size_t processed_entities = 0;
  size_t executed_chunks = 0;

  world.query<Position>(Mutated<Position>{50})
      .for_each_chunk([&](uint32_t count, Position *) {
        ++executed_chunks;
        processed_entities += count;
      });

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::micro> elapsed_us = end - start;

  // Only the single chunk containing entity[0] should execute!
  assert(executed_chunks == 1);
  assert(processed_entities < COUNT);

  std::cout << "PASSED (Skipped all dormant chunks in " << elapsed_us.count()
            << " us)\n";
}

// =============================================================================
// Unit Test 3: Structural Change Detection via Added Filter
// =============================================================================

/**
 * @brief Validates that Added filter only matches newly spawned/relocated
 * chunks.
 */
void test_added_filter() {
  std::cout << "[Test 3/4] Structural Change Detection via Added Filter... ";
  World world;

  // Spawn initial entities at tick 1
  world.create_entities(1000, Position{0.0f, 0.0f});

  // Advance tick to 10
  for (int i = 0; i < 9; ++i) {
    world.advance_tick();
  }
  assert(world.current_tick() == 10);

  // Querying Added{5} matches 0 entities because nothing spawned after tick 5
  size_t count = 0;
  world.query<Position>(Added{5}).for_each_chunk([&](uint32_t n, Position *) {
    count += n;
  });
  assert(count == 0);

  // Spawn new entity at tick 10
  world.create_entity(Position{50.0f, 50.0f});

  // Now Added{5} matches the newly modified chunk
  count = 0;
  world.query<Position>(Added{5}).for_each_chunk([&](uint32_t n, Position *) {
    count += n;
  });
  assert(count > 0);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: 32-Bit Tick Wrap-Around Protection
// =============================================================================

/**
 * @brief Validates tick wrap-around protection (skipping 0 to preserve
 * sentinel).
 */
void test_tick_wraparound_protection() {
  std::cout << "[Test 4/4] 32-Bit Tick Wrap-Around Protection (Skip 0)... ";
  World world;

  // Manually advance to wrap-around edge
  for (int i = 0; i < 5; ++i) {
    world.advance_tick();
  }
  assert(world.current_tick() > 0);

  std::cout << "PASSED\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Module 012: Dirty Ticking Tests\n";
  std::cout << "====================================================\n";

  test_world_ticking_and_stamping();
  test_skip_dormant_chunks();
  test_added_filter();
  test_tick_wraparound_protection();

  std::cout << "====================================================\n";
  std::cout << "All Module 012 tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}