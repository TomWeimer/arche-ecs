#include "arche/command_buffer.hpp"
#include "arche/entity.hpp"
#include "arche/world.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <vector>

namespace {
using arche::CommandBuffer;
using arche::Entity;
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
// Unit Test 1: Bulk Entity Creation via Prototype Broadcast
// =============================================================================

void test_bulk_creation_prototype() {
  std::cout
      << "[Test 1/5] Bulk Entity Creation via Prototype Broadcast (100k)... ";
  World world;

  constexpr size_t COUNT = 100'000;
  auto start = std::chrono::high_resolution_clock::now();

  std::vector<Entity> entities = world.create_entities(
      COUNT, Position{10.0f, 20.0f}, Velocity{1.0f, -1.0f}
  );

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> duration = end - start;

  assert(entities.size() == COUNT);
  assert(world.entity_count() == COUNT);

  for (size_t i = 0; i < COUNT; i += 1337) {
    assert(world.is_valid(entities[i]));
    const auto &pos = world.get_component<Position>(entities[i]);
    const auto &vel = world.get_component<Velocity>(entities[i]);
    assert(pos.x == 10.0f && pos.y == 20.0f);
    assert(vel.vx == 1.0f && vel.vy == -1.0f);
  }

  std::cout << "PASSED (" << duration.count() << " ms, "
            << (COUNT / (duration.count() / 1000.0) / 1'000'000.0)
            << " M ops/sec)\n";
}

// =============================================================================
// Unit Test 2: Bulk Entity Creation via Direct SoA Streaming
// =============================================================================

void test_bulk_creation_soa_streaming() {
  std::cout << "[Test 2/5] Bulk Creation via Direct SoA Streaming (100k)... ";
  World world;

  constexpr size_t COUNT = 100'000;
  std::vector<Position> positions(COUNT);
  std::vector<Velocity> velocities(COUNT);

  for (size_t i = 0; i < COUNT; ++i) {
    positions[i] = Position{static_cast<float>(i), static_cast<float>(i * 2)};
    velocities[i] = Velocity{
        static_cast<float>(i * 3), static_cast<float>(i * 4)
    };
  }

  std::vector<Entity> entities(COUNT);
  auto start = std::chrono::high_resolution_clock::now();

  world.create_entities_soa(
      COUNT, std::span<Entity>(entities), positions.data(), velocities.data()
  );

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> duration = end - start;

  assert(world.entity_count() == COUNT);

  for (size_t i = 0; i < COUNT; i += 2048) {
    assert(world.is_valid(entities[i]));
    const auto &pos = world.get_component<Position>(entities[i]);
    const auto &vel = world.get_component<Velocity>(entities[i]);
    assert(pos.x == static_cast<float>(i));
    assert(pos.y == static_cast<float>(i * 2));
    assert(vel.vx == static_cast<float>(i * 3));
    assert(vel.vy == static_cast<float>(i * 4));
  }

  std::cout << "PASSED (" << duration.count() << " ms, "
            << (COUNT / (duration.count() / 1000.0) / 1'000'000.0)
            << " M ops/sec)\n";
}

// =============================================================================
// Unit Test 3: Batched Entity Destruction & Hole Compaction
// =============================================================================

void test_batched_destruction() {
  std::cout
      << "[Test 3/5] Batched Entity Destruction & Compaction (50k / 100k)... ";
  World world;

  constexpr size_t COUNT = 100'000;
  std::vector<Entity> entities = world.create_entities(
      COUNT, Position{1.0f, 2.0f}, Velocity{3.0f, 4.0f}
  );

  std::vector<Entity> to_destroy;
  to_destroy.reserve(COUNT / 2);
  for (size_t i = 0; i < COUNT; i += 2) {
    to_destroy.push_back(entities[i]);
  }

  auto start = std::chrono::high_resolution_clock::now();
  world.destroy_entities(to_destroy);
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> duration = end - start;

  assert(world.entity_count() == COUNT / 2);

  for (Entity e : to_destroy) {
    assert(!world.is_valid(e));
  }

  for (size_t i = 1; i < COUNT; i += 2) {
    assert(world.is_valid(entities[i]));
    const auto &pos = world.get_component<Position>(entities[i]);
    assert(pos.x == 1.0f && pos.y == 2.0f);
  }

  std::cout << "PASSED (" << duration.count() << " ms)\n";
}

// =============================================================================
// Unit Test 4: CommandBuffer Batched Deletion Recording & Playback
// =============================================================================

void test_command_buffer_batch_destruction() {
  std::cout << "[Test 4/5] CommandBuffer Batched Deletion Playback... ";
  World world;
  CommandBuffer cmd;

  constexpr size_t COUNT = 10'000;
  std::vector<Entity> entities = world.create_entities(
      COUNT, Position{0.0f, 0.0f}
  );

  // Record batched deletion into command buffer
  cmd.destroy_entities(std::span<const Entity>(entities.data(), 5000));
  assert(world.entity_count() == COUNT); // Deferred until playback

  cmd.playback(world);
  assert(world.entity_count() == 5000);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 5: Generation Recycling via Bulk Allocations
// =============================================================================

void test_bulk_generation_recycling() {
  std::cout
      << "[Test 5/5] Bulk Generation Recycling & Stale Handle Invalidation... ";
  World world;

  constexpr size_t COUNT = 10'000;
  std::vector<Entity> batch1 = world.create_entities(
      COUNT, Position{0.0f, 0.0f}
  );

  world.destroy_entities(batch1);
  assert(world.entity_count() == 0);

  std::vector<Entity> batch2 = world.create_entities(
      COUNT, Position{100.0f, 200.0f}
  );
  assert(world.entity_count() == COUNT);

  for (Entity e : batch1) {
    assert(!world.is_valid(e));
  }

  for (Entity e : batch2) {
    assert(world.is_valid(e));
    assert(e.generation() >= 2);
  }

  std::cout << "PASSED\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Module 010: Bulk Lifecycle Tests\n";
  std::cout << "====================================================\n";

  test_bulk_creation_prototype();
  test_bulk_creation_soa_streaming();
  test_batched_destruction();
  test_command_buffer_batch_destruction();
  test_bulk_generation_recycling();

  std::cout << "====================================================\n";
  std::cout << "All Module 010 tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}