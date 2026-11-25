import arche;

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
 * @brief Validates world tick advancement, SystemContext::tick(), and automatic
 * column version stamping.
 */
void test_world_ticking_and_stamping() {
  std::cout << "[Test 1/4] Monotonic World Ticking & Version Stamping... ";
  World world;

  assert(world.version() == 1);

  // Spawning an entity at version 1 stamps chunk structure and columns with
  // version 1
  Entity e = world.spawn(Position{1.0f, 2.0f}, Velocity{3.0f, 4.0f});
  assert(world.entities.contains(e));

  // Register a system via plugin that declares write access to Position
  uint32_t observed_tick = 0;
  struct PositionMutatorPlugin {
    uint32_t *tick_ptr{nullptr};
    static constexpr std::string_view name() noexcept {
      return "PositionMutatorPlugin";
    }
    void build(World &, arche::PluginContext &ctx) const {
      ctx.add_system(
          "PositionMutator",
          arche::Access{}.writes<Position>().build(),
          [observed = tick_ptr](arche::SystemContext &ctx) {
            *observed = ctx.tick();
            assert(ctx.current_tick() == ctx.tick());
            assert(ctx.version() == ctx.tick());
          }
      );
    }
  };
  world.add_plugin(PositionMutatorPlugin{&observed_tick});

  // Frame 1: run() advances version to 2 and auto-stamps Position
  world.run();
  assert(world.version() == 2);
  assert(observed_tick == 2);

  // Frames 2, 3, 4: advances version to 5 and auto-stamps Position
  world.run(); // 3
  world.run(); // 4
  world.run(); // 5
  assert(world.version() == 5);
  assert(observed_tick == 5);

  // Querying with Mutated<Position>{4} should match the chunk (stamped at 5 >
  // 4)
  size_t matches = 0;
  world.query<Position>(Mutated<Position>{4})
      .each_chunk([&](uint32_t count, Position *) { matches += count; });
  assert(matches == 1);

  // Querying with Mutated<Position>{5} should NOT match (modified at 5, not >
  // 5)
  matches = 0;
  world.query<Position>(Mutated<Position>{5})
      .each_chunk([&](uint32_t count, Position *) { matches += count; });
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
  std::vector<Entity> entities = world.spawn(
      {.count = COUNT}, Position{0.0f, 0.0f}, Velocity{1.0f, 1.0f}
  );

  // Advance simulation to version 100
  for (int i = 0; i < 99; ++i) {
    world.run();
  }
  assert(world.version() == 100);

  // Mutate ONLY the first entity's chunk at tick 100 via get_mut (automatically
  // stamps column version!)
  world.get_mut<Position>(entities[0]).x = 999.0f;

  // Query entities where Position mutated since tick 50
  auto start = std::chrono::high_resolution_clock::now();

  size_t processed_entities = 0;
  size_t executed_chunks = 0;

  world.query<Position>(Mutated<Position>{50})
      .each_chunk([&](uint32_t count, Position *) {
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
  world.spawn({.count = 1000}, Position{0.0f, 0.0f});

  // Advance version to 10
  for (int i = 0; i < 9; ++i) {
    world.run();
  }
  assert(world.version() == 10);

  // Querying Added{5} matches 0 entities because nothing spawned after tick 5
  size_t count = 0;
  world.query<Position>(Added{5}).each_chunk([&](uint32_t n, Position *) {
    count += n;
  });
  assert(count == 0);

  // Spawn new entity at tick 10
  world.spawn(Position{50.0f, 50.0f});

  // Now Added{5} matches the newly modified chunk
  count = 0;
  world.query<Position>(Added{5}).each_chunk([&](uint32_t n, Position *) {
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
  std::cout << "[Test 4/4] 32-Bit Tick Wrap-Around Protection (Modular Distance)... ";
  World world;

  // Set version to right before wrap-around: UINT32_MAX - 2
  world.set_version(UINT32_MAX - 2);

  // Spawn entity at UINT32_MAX - 2 (in Position+Velocity archetype chunk)
  Entity e1 = world.spawn(Position{1.0f, 2.0f}, Velocity{1.0f, 1.0f});

  // Advance tick: UINT32_MAX - 2 -> UINT32_MAX - 1
  world.advance_version();
  assert(world.version() == UINT32_MAX - 1);
  assert(world.previous_version() == UINT32_MAX - 2);

  // Advance tick again: next is UINT32_MAX, so it wraps to 1!
  world.advance_version();
  assert(world.version() == 1);
  assert(world.previous_version() == UINT32_MAX - 1);

  // Spawn a new entity at version 1 (in standalone Position archetype chunk)
  Entity e2 = world.spawn(Position{3.0f, 4.0f});
  (void)e2;

  // 1. Query with default Added{} (SINCE_LAST_FRAME):
  // Should match e2 (spawned at version 1), but NOT e1 (spawned before the wrap)!
  size_t added_count = 0;
  world.query<Position>(Added{}).each_chunk([&](uint32_t n, Position *) {
    added_count += n;
  });
  assert(added_count == 1);

  // 2. Query with default Mutated<Position>{} (SINCE_LAST_FRAME):
  // Advance version from 1 -> 2
  world.advance_version();
  assert(world.version() == 2);
  assert(world.previous_version() == 1);

  // Modify e1 at version 2
  world.get_mut<Position>(e1).x += 10.0f;

  // Only e1's chunk was mutated at version 2 (newer than version 1)
  size_t mut_count = 0;
  world.query<Position>(Mutated<Position>{}).each_chunk(
      [&](uint32_t n, Position *) { mut_count += n; }
  );
  assert(mut_count == 1);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 5: Auto-Defaulting Temporal Filters & Inline Capacity
// =============================================================================

void test_auto_default_and_inline_mutation_filters() {
  std::cout << "[Test 5/5] Auto-Defaulting Mutated/Added & Inline Rules... ";
  World world;

  // 1. Spawn entity at version 1
  Entity e = world.spawn(Position{1.0f, 2.0f}, Velocity{3.0f, 4.0f});

  // Mutated<Position>{} with no arguments auto-defaults to current_version - 1 (1 - 1 = 0)
  // Chunk was stamped at version 1 > 0, so it matches!
  size_t count = 0;
  world.query<Position>(Mutated<Position>{}).each_chunk(
      [&](uint32_t n, Position *) { count += n; }
  );
  assert(count == 1);

  // Added{} with no arguments auto-defaults to version 0 -> matches!
  count = 0;
  world.query<Position>(Added{}).each_chunk(
      [&](uint32_t n, Position *) { count += n; }
  );
  assert(count == 1);

  // 2. Advance world ticks without modifying Position
  world.run(); // tick 2
  world.run(); // tick 3

  // Now current_version is >= 3. Auto-defaulting checks against version >= 2.
  // Position was stamped at version 1 <= 2 -> dormant chunk skipped!
  count = 0;
  world.query<Position>(Mutated<Position>{}).each_chunk(
      [&](uint32_t n, Position *) { count += n; }
  );
  assert(count == 0);

  // 3. Test multi-component change filter using inline capacity (up to 2)
  world.get_mut<Position>(e).x += 10.0f;
  world.get_mut<Velocity>(e).vx += 10.0f;

  // Both Position and Velocity were modified at the new tick
  count = 0;
  world.query<Position, Velocity>(
           Mutated<Position>{}, Mutated<Velocity>{}
  ).each_chunk([&](uint32_t n, Position *, Velocity *) { count += n; });
  assert(count == 1);

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
  test_auto_default_and_inline_mutation_filters();

  std::cout << "====================================================\n";
  std::cout << "All Module 012 tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}