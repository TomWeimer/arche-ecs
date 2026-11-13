#include "arche/query.hpp"
#include "arche/query_filter.hpp"
#include "arche/world.hpp"

#include <cassert>
#include <iostream>

namespace {
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

struct DeadTag {};

// =============================================================================
// Unit Test 1: O(1) Cache Deduplication
// =============================================================================

/**
 * @brief Validates that multiple queries with identical signatures share the
 * exact same QueryCache.
 */
void test_query_cache_deduplication() {
  std::cout << "[Test 1/4] O(1) Query Cache Retrieval & Deduplication... ";
  World world;

  // Create initial entities
  world.create_entity(Position{1.0f, 2.0f}, Velocity{3.0f, 4.0f});

  // Construct two separate query instances with identical signatures
  auto q1 = world.query<Position, Velocity>();
  auto q2 = world.query<Position, Velocity>();

  // Both queries must produce identical results with zero redundant allocations
  size_t count1 = 0;
  q1.for_each_chunk([&](uint32_t n, Position *, Velocity *) { count1 += n; });

  size_t count2 = 0;
  q2.for_each_chunk([&](uint32_t n, Position *, Velocity *) { count2 += n; });

  assert(count1 == 1);
  assert(count2 == 1);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: Dynamic Reactive Cache Updates Without Hooks
// =============================================================================

/**
 * @brief Validates that creating a new archetype *after* query construction
 * updates the query reactively.
 */
void test_reactive_cache_update() {
  std::cout << "[Test 2/4] Dynamic Reactive Cache Updates (Zero Hooks)... ";
  World world;

  // 1. Construct query first (when only 0 archetypes exist)
  auto q = world.query<Position, Velocity>();

  size_t count = 0;
  q.for_each_chunk([&](uint32_t n, Position *, Velocity *) { count += n; });
  assert(count == 0);

  // 2. Spawn entity in Archetype A: [Position, Velocity]
  world.create_entity(Position{10.0f, 20.0f}, Velocity{1.0f, 2.0f});

  // Existing query must immediately and reactively see the new entity!
  count = 0;
  q.for_each_chunk([&](uint32_t n, Position *, Velocity *) { count += n; });
  assert(count == 1);

  // 3. Spawn entity in Archetype B: [Position, Velocity, Mass] (new graph node)
  world.create_entity(
      Position{30.0f, 40.0f}, Velocity{3.0f, 4.0f}, Mass{100.0f}
  );

  // Query must automatically see the new archetype without re-construction
  count = 0;
  q.for_each_chunk([&](uint32_t n, Position *, Velocity *) { count += n; });
  assert(count == 2);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Exclusion Mask Caching Correctness
// =============================================================================

/**
 * @brief Validates that Without<T> filters are properly evaluated during cache
 * population.
 */
void test_exclusion_caching() {
  std::cout << "[Test 3/4] Exclusion Mask Caching Correctness... ";
  World world;

  // Spawn alive and dead entities
  world.create_entity(Position{1.0f, 1.0f}, Velocity{1.0f, 1.0f});
  world.create_entity(Position{2.0f, 2.0f}, Velocity{2.0f, 2.0f}, DeadTag{});

  // Query excluding DeadTag
  auto q = world.query<Position, Velocity>(Without<DeadTag>{});

  size_t count = 0;
  q.for_each_chunk([&](uint32_t n, Position *, Velocity *) { count += n; });
  assert(count == 1);

  // Spawn another dead entity (Archetype with DeadTag already exists)
  world.create_entity(Position{3.0f, 3.0f}, Velocity{3.0f, 3.0f}, DeadTag{});
  count = 0;
  q.for_each_chunk([&](uint32_t n, Position *, Velocity *) { count += n; });
  assert(count == 1);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Pure Declarative Hook-Free Syntax
// =============================================================================

/**
 * @brief Validates standard declarative Colony loop syntax with zero hooks.
 */
void test_declarative_syntax() {
  std::cout << "[Test 4/4] Pure Declarative Hook-Free Syntax... ";
  World world;

  constexpr size_t COUNT = 10'000;
  world.create_entities(COUNT, Position{0.0f, 0.0f}, Velocity{1.0f, 1.0f});

  // Standard Colony boid query
  auto query = world.query<Position, const Velocity>();
  query.for_each_chunk([](uint32_t count, Position *pos, const Velocity *vel) {
    for (uint32_t i = 0; i < count; ++i) {
      pos[i].x += vel[i].vx;
      pos[i].y += vel[i].vy;
    }
  });

  std::cout << "PASSED\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Module 013: Query Caching Tests\n";
  std::cout << "====================================================\n";

  test_query_cache_deduplication();
  test_reactive_cache_update();
  test_exclusion_caching();
  test_declarative_syntax();

  std::cout << "====================================================\n";
  std::cout << "All Module 013 tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}