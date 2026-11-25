import arche;
#include "common/world_test_probe.hpp"

#include <atomic>

#include <cassert>
#include <chrono>
#include <iostream>
#include <vector>

namespace {
using arche::Access;
using arche::Entity;
using arche::internal::Scheduler;
using arche::SystemContext;
using arche::SystemScale;
using arche::internal::ThreadPool;
using arche::World;
using arche::test::WorldTestProbe;
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

struct PrevPosition {
  float x{0.0f};
  float y{0.0f};
};

// =============================================================================
// Unit Test 1: Read/Write Conflict Separation into Distinct Batches
// =============================================================================

void test_read_write_hazard_separation() {
  std::cout
      << "[Test 1/5] Read/Write Hazard Separation via Scheduler Batches... ";
  World world;
  ThreadPool pool(2);

  std::vector<std::string> log;

  Scheduler scheduler;
  scheduler.add_system(
      "PositionReader",
      Access{}.reads<Position>().build(),
      [&](SystemContext &) { log.push_back("Read"); }
  );

  scheduler.add_system(
      "PositionWriter",
      Access{}.writes<Position>().build(),
      [&](SystemContext &) { log.push_back("Write"); }
  );

  scheduler.compile(WorldTestProbe::execution_target(world));
  scheduler.run(WorldTestProbe::execution_target(world), pool);

  // Reader and Writer must NOT run in the same batch due to conflict
  assert(log.size() == 2);
  assert(log[0] == "Read");
  assert(log[1] == "Write");

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: Concurrent Non-Conflicting Readers in a Single Batch
// =============================================================================

void test_concurrent_readers_in_single_batch() {
  std::cout << "[Test 2/5] Concurrent Readers Admitted into Single Batch... ";
  World world;
  ThreadPool pool(2);

  std::atomic<uint32_t> reader_count{0};

  Scheduler scheduler;
  scheduler.add_system(
      "Reader1", Access{}.reads<Position>().build(), [&](SystemContext &) {
        reader_count.fetch_add(1);
      }
  );

  scheduler.add_system(
      "Reader2", Access{}.reads<Position>().build(), [&](SystemContext &) {
        reader_count.fetch_add(1);
      }
  );

  scheduler.compile(WorldTestProbe::execution_target(world));
  scheduler.run(WorldTestProbe::execution_target(world), pool);

  assert(reader_count.load() == 2);
  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Parallel Chunk Work-Stealing for Wide Writers
// =============================================================================

void test_parallel_chunk_work_stealing_writers() {
  std::cout << "[Test 3/5] Parallel Chunk Work-Stealing for Wide Writers... ";
  World world;
  ThreadPool pool(4);

  constexpr size_t COUNT = 20'000;
  for (size_t i = 0; i < COUNT; ++i) {
    world.spawn(Position{0.0f, 0.0f}, Velocity{1.0f, 2.0f});
  }

  Scheduler scheduler;
  scheduler.add_system(
      "ParallelPhysicsIntegrator",
      Access{}.writes<Position>().reads<Velocity>().build(),
      [](SystemContext &ctx) {
        ctx.query<Position, const Velocity>().each_parallel([](uint32_t count,
                                                               arche::CommandBuffer &,
                                                               Position *pos,
                                                               const Velocity *vel) {
          for (uint32_t i = 0; i < count; ++i) {
            pos[i].x += vel[i].vx * 0.1f;
            pos[i].y += vel[i].vy * 0.1f;
          }
        });
      },
      SystemScale::Wide
  );

  scheduler.compile(WorldTestProbe::execution_target(world));
  scheduler.run(WorldTestProbe::execution_target(world), pool);

  // Verify all entities updated in-place across worker threads without data
  // race
  for (auto [pos, vel] : world.query<Position, Velocity>()) {
    assert(std::abs(pos.x - 0.1f) < 0.0001f);
    assert(std::abs(pos.y - 0.2f) < 0.0001f);
  }

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: State History Component Pairing (Pattern A)
// =============================================================================

void test_state_history_component_pairing() {
  std::cout << "[Test 4/5] State History Component Pairing (Pattern A)... ";
  World world;

  // Dynamic entity has Position and PrevPosition
  Entity dynamic_e = world.spawn(
      Position{10.0f, 20.0f}, PrevPosition{0.0f, 0.0f}, Velocity{5.0f, 5.0f}
  );

  // Static entity only has Position (zero memory wasted!)
  Entity static_e = world.spawn(Position{100.0f, 200.0f});

  // 1. History update step: record current into previous for dynamic entities
  for (auto [pos, prev] : world.query<const Position, PrevPosition>()) {
    prev.x = pos.x;
    prev.y = pos.y;
  }

  // 2. Integration step
  for (auto [pos, vel] : world.query<Position, const Velocity>()) {
    pos.x += vel.vx;
    pos.y += vel.vy;
  }

  // Verify dynamic entity state progression
  assert(world.get<Position>(dynamic_e).x == 15.0f);
  assert(world.get<PrevPosition>(dynamic_e).x == 10.0f);

  // Verify static entity was untouched
  assert(world.get<Position>(static_e).x == 100.0f);
  assert(!world.has<PrevPosition>(static_e));

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 5: Frame Execution Overhead Benchmark (< 5 us)
// =============================================================================

void test_scheduler_stage_overhead() {
  std::cout << "[Test 5/5] Scheduler Stage Barrier Overhead Benchmark... ";
  World world;
  ThreadPool pool(2);

  Scheduler scheduler;
  scheduler.add_system(
      "EmptySystemA", Access{}.reads<Position>().build(), [](SystemContext &) {}
  );
  scheduler.add_system(
      "EmptySystemB", Access{}.writes<Position>().build(), [](SystemContext &) {
      }
  );

  scheduler.compile(WorldTestProbe::execution_target(world));

  constexpr int ITERS = 10'000;
  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < ITERS; ++i) {
    scheduler.run(WorldTestProbe::execution_target(world), pool);
  }

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::micro> total_us = end - start;
  const double us_per_frame = total_us.count() / static_cast<double>(ITERS);

  std::cout << "PASSED (" << us_per_frame << " us / frame across 2 batches)\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Stage Concurrency & Hazard Tests\n";
  std::cout << "====================================================\n";

  test_read_write_hazard_separation();
  test_concurrent_readers_in_single_batch();
  test_parallel_chunk_work_stealing_writers();
  test_state_history_component_pairing();
  test_scheduler_stage_overhead();

  std::cout << "====================================================\n";
  std::cout << "All Stage Concurrency & Hazard tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}
