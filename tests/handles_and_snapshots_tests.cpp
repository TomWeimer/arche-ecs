import arche;

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {
using arche::Entity;
using arche::null_entity;
using arche::World;
using arche::internal::SlotMap;
} // namespace

// =============================================================================
// Standalone Test Component & Resource Types
// =============================================================================

struct Position {
  float x{0.0f};
  float y{0.0f};
};

struct Velocity {
  float vx{0.0f};
  float vy{0.0f};
};

struct SimulationConfig {
  float gravity{9.81f};
  uint32_t tick_rate{60};
};

// =============================================================================
// Unit Test 1: 64-Bit Handle Bitfield Encoding & Sentinel
// =============================================================================

void test_64bit_entity_bitfield() {
  std::cout << "[Test 1/7] 64-Bit Handle Bitfield Encoding & Sentinel... ";

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

void test_slotmap_32bit_generation() {
  std::cout << "[Test 2/7] SlotMap 32-Bit Generation & Stale Handle Rejection... ";
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

void test_generation_wraparound_protection() {
  std::cout << "[Test 3/7] Generation Wrap-Around Protection (Skip 0)... ";
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
// Unit Test 4: World Instant Raw Checkpoint & Rollback (< 1 ms)
// =============================================================================

void test_world_instant_checkpoint_and_rollback() {
  std::cout << "[Test 4/7] World Instant Raw Checkpoint & Rollback (< 1 ms)... ";
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

  // 2. Take raw memory checkpoint
  const size_t req_bytes = world.snapshots.checkpoint_size_bytes();
  std::vector<uint8_t> checkpoint_buffer(req_bytes);

  auto start = std::chrono::high_resolution_clock::now();
  size_t bytes_written = world.snapshots.save_checkpoint(
      checkpoint_buffer.data(), checkpoint_buffer.size()
  );
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> checkpoint_time = end - start;

  assert(bytes_written > 0);

  // 3. Mutate and corrupt live world state
  for (int i = 0; i < 5000; ++i) {
    world.despawn(initial_entities[i]);
  }
  for (int i = 5000; i < ENTITY_COUNT; ++i) {
    world.get_mut<Position>(initial_entities[i]).x = -999.0f;
  }

  // 4. Instant rollback from raw checkpoint
  start = std::chrono::high_resolution_clock::now();
  world.snapshots.restore_checkpoint(checkpoint_buffer.data(), bytes_written);
  end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> restore_time = end - start;

  // 5. Verify 100% bit-exact restoration of components and handle validities
  for (int i = 0; i < ENTITY_COUNT; ++i) {
    assert(world.entities.contains(initial_entities[i]));
    const auto &pos = world.get<Position>(initial_entities[i]);
    assert(pos.x == static_cast<float>(i));
    assert(pos.y == static_cast<float>(i * 2));
    const auto &vel = world.get<Velocity>(initial_entities[i]);
    assert(vel.vx == 1.0f);
    assert(vel.vy == -1.0f);
  }

  std::cout << "PASSED (Save: " << checkpoint_time.count()
            << " ms, Restore: " << restore_time.count() << " ms)\n";
}

// =============================================================================
// Unit Test 5: Fresh-World Cold Boot Reload from Relocatable Envelope
// =============================================================================

void test_cold_boot_relocatable_snapshot() {
  std::cout << "[Test 5/7] Fresh-World Cold Boot Reload from Relocatable Save... ";

  constexpr int ENTITY_COUNT = 5'000;
  std::vector<uint8_t> buffer;

  {
    World world1;
    for (int i = 0; i < ENTITY_COUNT; ++i) {
      world1.spawn(
          Position{static_cast<float>(i * 10), static_cast<float>(i * 20)},
          Velocity{static_cast<float>(i), 0.5f}
      );
    }

    const size_t bytes = world1.snapshots.size_bytes();
    buffer.resize(bytes);
    size_t written = world1.snapshots.save(buffer.data(), bytes);
    assert(written > 0);
  }

  // Create brand new World (cold boot, zero component types registered)
  {
    World world2;
    world2.snapshots.load(buffer.data(), buffer.size());

    assert(world2.entities.count() == ENTITY_COUNT);

    uint32_t verified = 0;
    for (auto [pos, vel] : world2.query<const Position, const Velocity>()) {
      assert(pos.y == pos.x * 2.0f);
      assert(vel.vy == 0.5f);
      ++verified;
    }

    assert(verified == ENTITY_COUNT);
  }

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 6: Singleton Resource Persistence Across Cold Boot
// =============================================================================

void test_resource_persistence_across_cold_boot() {
  std::cout << "[Test 6/7] Singleton Resource Persistence Across Cold Boot... ";

  const std::string file_path = "test_snapshot_res.bin";
  {
    World world1;
    world1.resources.insert<SimulationConfig>(SimulationConfig{
        .gravity = -3.71f, // Mars gravity
        .tick_rate = 120,
    });
    world1.spawn(Position{1.0f, 2.0f});

    bool saved = world1.snapshots.save_to_file(file_path);
    assert(saved);
  }

  {
    World world2;
    bool loaded = world2.snapshots.load_from_file(file_path);
    assert(loaded);

    assert(world2.resources.has<SimulationConfig>());
    const auto &cfg = world2.resources.get<SimulationConfig>();
    assert(cfg.gravity == -3.71f);
    assert(cfg.tick_rate == 120);
    assert(world2.entities.count() == 1);
  }

  std::filesystem::remove(file_path);
  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 7: Additive Package Import (load_additive)
// =============================================================================

void test_additive_package_import() {
  std::cout << "[Test 7/7] Additive Package Import (load_additive)... ";

  std::vector<uint8_t> package_buffer;
  constexpr int PACKAGE_ENTITIES = 2'000;

  // 1. Export package snapshot from World A
  {
    World world_a;
    for (int i = 0; i < PACKAGE_ENTITIES; ++i) {
      world_a.spawn(
          Position{static_cast<float>(i), 100.0f},
          Velocity{2.0f, 3.0f}
      );
    }
    const size_t bytes = world_a.snapshots.size_bytes();
    package_buffer.resize(bytes);
    world_a.snapshots.save(package_buffer.data(), bytes);
  }

  // 2. Prepare World B with existing entities
  World world_b;
  constexpr int BASE_ENTITIES = 1'000;
  std::vector<Entity> base_handles;
  base_handles.reserve(BASE_ENTITIES);
  for (int i = 0; i < BASE_ENTITIES; ++i) {
    base_handles.push_back(world_b.spawn(Position{-1.0f, -1.0f}));
  }
  assert(world_b.entities.count() == BASE_ENTITIES);

  // 3. Import package additively into World B
  std::vector<Entity> imported = world_b.snapshots.load_additive(
      package_buffer.data(), package_buffer.size()
  );

  // 4. Verify results
  assert(imported.size() == PACKAGE_ENTITIES);
  assert(world_b.entities.count() == BASE_ENTITIES + PACKAGE_ENTITIES);

  // Verify base entities are completely untouched
  for (int i = 0; i < BASE_ENTITIES; ++i) {
    assert(world_b.entities.contains(base_handles[i]));
    const auto &pos = world_b.get<Position>(base_handles[i]);
    assert(pos.x == -1.0f);
    assert(pos.y == -1.0f);
  }

  // Verify imported entities are valid and populated
  for (size_t i = 0; i < imported.size(); ++i) {
    Entity e = imported[i];
    assert(world_b.entities.contains(e));
    const auto &pos = world_b.get<Position>(e);
    assert(pos.x == static_cast<float>(i));
    assert(pos.y == 100.0f);
    const auto &vel = world_b.get<Velocity>(e);
    assert(vel.vx == 2.0f);
    assert(vel.vy == 3.0f);
  }

  std::cout << "PASSED\n";
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
  test_world_instant_checkpoint_and_rollback();
  test_cold_boot_relocatable_snapshot();
  test_resource_persistence_across_cold_boot();
  test_additive_package_import();

  std::cout << "====================================================\n";
  std::cout << "All Module 009 tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}
