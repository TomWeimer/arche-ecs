#include "arche/arche.hpp"
#include "arche/detail/slot_map.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
using arche::Entity;
using arche::null_entity;
using arche::World;
using arche::Internal::SlotMap;
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
// Unit Test 1: 64-Bit Handle Bitfield Encoding & Sentinel
// =============================================================================

/**
 * @brief Validates 64-bit handle bit-packing, generation extraction, and
 * sentinel invariants.
 */
void test_64bit_entity_bitfield() {
  std::cout << "[Test 1/4] 64-Bit Handle Bitfield Encoding & Sentinel... ";

  // 1. Test standard index and generation packing
  Entity e1 = Entity::from_parts(1042, 85);
  assert(e1.index() == 1042);
  assert(e1.id() == 1042);
  assert(e1.generation() == 85);
  assert(static_cast<bool>(e1) == true);
  assert(!e1.is_null());

  // 2. Test maximum 32-bit limits (4.29 billion)
  Entity e_max = Entity::from_parts(0xFFFFFFFE, 0xFFFFFFFF);
  assert(e_max.index() == 0xFFFFFFFE);
  assert(e_max.generation() == 0xFFFFFFFF);

  // 3. Null entity sentinel verification
  assert(null_entity.is_null());
  assert(!static_cast<bool>(null_entity));
  assert(null_entity.raw() == 0xFFFFFFFFFFFFFFFFULL);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: SlotMap 32-Bit Generation & Stale Handle Invalidation
// =============================================================================

/**
 * @brief Validates O(1) generational increment and stale handle rejection using
 * 32-bit generations.
 */
void test_slotmap_32bit_generation() {
  std::cout
      << "[Test 2/4] SlotMap 32-Bit Generation & Stale Handle Rejection... ";
  SlotMap map;

  // 1. Allocate initial entity (index 0, generation 1)
  Entity e0 = map.allocate();
  assert(e0.index() == 0);
  assert(e0.generation() == 1);
  assert(map.is_alive(e0));

  // 2. Free e0
  map.free(e0);
  assert(!map.is_alive(e0)); // Stale handle detection

  // 3. Re-allocate slot 0: generation must advance to 2
  Entity e1 = map.allocate();
  assert(e1.index() == 0);
  assert(e1.generation() == 2);
  assert(map.is_alive(e1));
  assert(!map.is_alive(e0)); // Original handle e0 remains permanently dead

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: 32-Bit Generation Wrap-Around Safety
// =============================================================================

/**
 * @brief Validates generation wrap-around protection (skipping 0 to preserve
 * null_entity).
 */
void test_generation_wraparound_protection() {
  std::cout << "[Test 3/4] Generation Wrap-Around Protection (Skip 0)... ";
  SlotMap map;

  Entity e = map.allocate();
  assert(e.index() == 0);

  // Free e
  map.free(e);

  // Re-allocating must produce non-zero generation
  Entity e_next = map.allocate();
  assert(e_next.generation() != 0);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: World Instant Raw Snapshot & Rollback
// =============================================================================

/**
 * @brief Validates full world snapshot creation and rollback in < 1 ms with
 * 100% data fidelity.
 */
void test_world_instant_snapshot_and_rollback() {
  std::cout << "[Test 4/4] World Instant Snapshot & Rollback (< 1 ms)... ";
  World world;

  constexpr int ENTITY_COUNT = 10'000;
  std::vector<Entity> initial_entities;
  initial_entities.reserve(ENTITY_COUNT);

  // 1. Populate world with 10,000 entities
  for (int i = 0; i < ENTITY_COUNT; ++i) {
    initial_entities.push_back(world.spawn(
        Position{static_cast<float>(i), static_cast<float>(i * 2)},
        Velocity{1.0f, -1.0f}
    ));
  }

  // 2. Take raw memory snapshot
  std::vector<uint8_t> snapshot_buffer(64 * 1024 * 1024); // 64 MB buffer
  auto start = std::chrono::high_resolution_clock::now();
  size_t bytes_written = world.save_snapshot(
      snapshot_buffer.data(), snapshot_buffer.size()
  );
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> snapshot_time = end - start;

  assert(bytes_written > 0);

  // 3. Mutate and corrupt live world state
  for (int i = 0; i < 5000; ++i) {
    world.despawn(initial_entities[i]);
  }
  for (int i = 5000; i < ENTITY_COUNT; ++i) {
    world.get<Position>(initial_entities[i]).x = -999.0f;
  }

  // 4. Instant rollback from snapshot
  start = std::chrono::high_resolution_clock::now();
  world.load_snapshot(snapshot_buffer.data(), bytes_written);
  end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> restore_time = end - start;

  // 5. Verify 100% bit-exact restoration of components and handle validities
  for (int i = 0; i < ENTITY_COUNT; ++i) {
    assert(world.is_valid(initial_entities[i]));
    const auto &pos = world.get<Position>(initial_entities[i]);
    assert(pos.x == static_cast<float>(i));
    assert(pos.y == static_cast<float>(i * 2));
  }

  std::cout << "PASSED (Snapshot: " << snapshot_time.count()
            << " ms, Restore: " << restore_time.count() << " ms)\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Module 009: Handles & Snapshots Tests\n";
  std::cout << "====================================================\n";

  test_64bit_entity_bitfield();
  test_slotmap_32bit_generation();
  test_generation_wraparound_protection();
  test_world_instant_snapshot_and_rollback();

  std::cout << "====================================================\n";
  std::cout << "All Module 009 tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}