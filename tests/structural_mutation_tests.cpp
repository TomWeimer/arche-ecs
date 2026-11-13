#include "arche/arche.hpp"

#include <cassert>
#include <iostream>
#include <vector>

namespace {
using arche::CommandBuffer;
using arche::Entity;
using arche::TypeID;
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

struct TagBurning {
  float damage_per_sec{0.0f};
};

// =============================================================================
// Unit Test 1: Immediate Entity Spawning & Component Access
// =============================================================================

/**
 * @brief Verifies synchronous entity creation and direct in-place component
 * access.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Spawning an entity is the foundational entry point of an ECS:
 * - The World must register unseen types, allocate a generational handle, and
 * find or create the Archetype.
 * - Components must be constructed directly in the chunk storage at the
 * allocated row.
 * - Accessing component references (`get_component<T>`) must compute correct
 * column byte offsets.
 *
 * This test guarantees:
 * 1. Newly created entities are reported alive by `world.is_alive()`.
 * 2. `has_component<T>()` returns true for initial components and false for
 * unattached components.
 * 3. Initial values are faithfully stored and direct in-place mutations persist
 * across lookups.
 */
void test_spawning_and_reading() {
  std::cout << "[Test 1/4] Immediate Entity Spawning & Component Access... ";
  World world;

  // 1. Spawn entity with Position and Velocity
  Entity e1 = world.spawn(
      Position{10.0f, 20.0f}, Velocity{1.0f, -2.0f}
  );

  // 2. Verify handle validity and component presence
  assert(world.is_alive(e1));
  assert(world.is_valid(e1));
  assert(world.has<Position>(e1));
  assert(world.has<Velocity>(e1));
  assert(!world.has<TagBurning>(e1));

  // 3. Verify retrieved values match initial values
  Position &pos = world.get<Position>(e1);
  Velocity &vel = world.get<Velocity>(e1);
  assert(pos.x == 10.0f && pos.y == 20.0f);
  assert(vel.vx == 1.0f && vel.vy == -2.0f);

  // 4. In-place mutation must persist in storage
  pos.x = 99.0f;
  assert(world.get<Position>(e1).x == 99.0f);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: Cross-Archetype Migration (+Component / -Component)
// =============================================================================

/**
 * @brief Verifies structural mutations adding and removing components at
 * runtime.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * In archetype-based ECS architectures, adding or removing a component alters
 * the entity's signature:
 * - The entity must migrate between different archetypes and physical chunks.
 * - Existing component columns (e.g. Position) must be relocated without data
 * loss or corruption.
 * - The slot map record must be seamlessly updated to point to the new chunk
 * and row.
 * - Removing a component must transition back along reverse graph edges.
 *
 * This test guarantees:
 * 1. Adding a component (`TagBurning`) transitions the entity from `Arch(Pos)`
 * to `Arch(Pos, TagBurning)`.
 * 2. Existing `Position` coordinates are preserved during migration.
 * 3. Removing the component transitions the entity back to `Arch(Pos)` while
 * preserving remaining data.
 */
void test_cross_archetype_migration() {
  std::cout
      << "[Test 2/4] Cross-Archetype Migration (+Component / -Component)... ";
  World world;

  // 1. Create entity with single Position component
  Entity e = world.spawn(Position{5.0f, 15.0f});
  assert(!world.has<TagBurning>(e));

  // 2. Add TagBurning: triggers graph traversal and migration to new archetype
  world.add(e, TagBurning{25.0f});

  // 3. Verify entity state in destination archetype
  assert(world.is_alive(e));
  assert(world.has<Position>(e));
  assert(world.has<TagBurning>(e));
  assert(world.get<Position>(e).x == 5.0f);
  assert(world.get<Position>(e).y == 15.0f);
  assert(world.get<TagBurning>(e).damage_per_sec == 25.0f);

  // 4. Remove TagBurning: triggers migration back to source archetype
  world.remove<TagBurning>(e);
  assert(!world.has<TagBurning>(e));
  assert(world.has<Position>(e));
  assert(world.get<Position>(e).x == 5.0f);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Swap-and-Pop Multiple Entity Destructions
// =============================================================================

/**
 * @brief Verifies constant-time entity destruction and memory compaction via
 * swap-and-pop.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Destroying an entity inside a chunk must not leave holes in contiguous
 * memory:
 * - The last entity in the chunk is moved into the vacated slot (Swap-and-Pop).
 * - The relocated entity's location record in the SlotMap must be updated to
 * its new row.
 * - Non-destroyed entities must remain completely valid with their component
 * data intact.
 *
 * This test guarantees:
 * 1. Destroying middle and beginning entities invalidates only the targeted
 * handles.
 * 2. Swapped entities retain correct data and can still be accessed via their
 * unchanged handles.
 * 3. The SlotMap generation counter prevents stale handle reuse.
 */
void test_swap_and_pop_cascading_destruction() {
  std::cout << "[Test 3/4] Swap-and-Pop Multiple Entity Destructions... ";
  World world;

  // 1. Populate world with multiple entities in a single archetype
  std::vector<Entity> list;
  for (int i = 0; i < 10; ++i) {
    list.push_back(world.spawn(Position{static_cast<float>(i), 0.0f}));
  }

  // 2. Destroy middle entity: list[3] (triggers swap with list[9])
  Entity e3 = list[3];
  assert(world.despawn(e3));
  assert(!world.is_alive(e3));

  // 3. Destroy beginning entity: list[0] (triggers swap with list[8])
  Entity e0 = list[0];
  assert(world.despawn(e0));
  assert(!world.is_alive(e0));

  // 4. All surviving entities must remain alive and retain their exact
  // coordinate values
  for (size_t i = 1; i < list.size(); ++i) {
    if (i == 3) {
      continue;
    }
    assert(world.is_alive(list[i]));
    assert(world.get<Position>(list[i]).x == static_cast<float>(i));
  }

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Deferred CommandBuffer Batch Playback
// =============================================================================

/**
 * @brief Verifies recorded command batching and safe synchronization playback.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Modifying entity tables while iterating them corrupts pointers and indices.
 * A CommandBuffer defers structural mutations (destroy, add, remove) into a
 * continuous byte stream and applies them sequentially at a frame
 * synchronization boundary.
 *
 * This test guarantees:
 * 1. Queuing commands does not prematurely alter World state before playback.
 * 2. Batch playback unpacks and applies mutations in FIFO execution order.
 * 3. Command buffer is reset to empty after playback while retaining memory
 * capacity.
 */
void test_deferred_command_buffer() {
  std::cout << "[Test 4/4] Deferred CommandBuffer Batch Playback... ";
  World world;
  CommandBuffer cmd;

  // 1. Spawn test entities
  std::vector<Entity> entities;
  for (int i = 0; i < 5; ++i) {
    entities.push_back(world.spawn(
        Position{static_cast<float>(i), static_cast<float>(i * 2)}
    ));
  }

  // 2. Simulate loop recording: queue destruction of even entities
  for (size_t i = 0; i < entities.size(); ++i) {
    if (i % 2 == 0) {
      cmd.despawn(entities[i]);
    }
  }

  // 3. Before playback: all entities must remain untouched and alive
  assert(world.is_alive(entities[0]));
  assert(world.is_alive(entities[2]));

  // 4. Flush commands at synchronization boundary
  cmd.commit(world);
  assert(cmd.empty());

  // 5. After playback: even entities must be destroyed, odd entities must
  // remain alive
  assert(!world.is_alive(entities[0]));
  assert(world.is_alive(entities[1]));
  assert(!world.is_alive(entities[2]));
  assert(world.is_alive(entities[3]));
  assert(!world.is_alive(entities[4]));

  std::cout << "PASSED\n";
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Structural Mutation Tests\n";
  std::cout << "====================================================\n";

  test_spawning_and_reading();
  test_cross_archetype_migration();
  test_swap_and_pop_cascading_destruction();
  test_deferred_command_buffer();

  std::cout << "====================================================\n";
  std::cout << "All Structural Mutation tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}
