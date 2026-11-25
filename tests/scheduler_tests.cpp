import arche;

#include <atomic>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using arche::Access;
using arche::CommandBuffer;
using arche::internal::Scheduler;
using arche::SystemContext;
using arche::SystemScale;
using arche::internal::ThreadPool;
using arche::World;
} // namespace

// =============================================================================
// Test Component & Resource Types
// =============================================================================

struct Position {
  float x{0.0f}, y{0.0f}, z{0.0f};
};

struct Velocity {
  float vx{0.0f}, vy{0.0f}, vz{0.0f};
};

struct Health {
  int hp{100};
};

struct Score {
  int points{0};
};

struct SimulationClock {
  float total_time{0.0f};
  uint64_t tick_count{0};
};

struct GravityConfig {
  float gy{-9.81f};
};

struct MissingResource {
  int dummy{0};
};

// =============================================================================
// Unit Test 1: Empty Scheduler Execution
// =============================================================================

void test_empty_scheduler() {
  std::cout << "Testing Empty Scheduler Execution... ";

  World world;
  ThreadPool pool(2);
  Scheduler scheduler;

  // Compiling and running an empty scheduler must not throw or crash
  scheduler.compile(world);
  scheduler.run(world, pool);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: Narrow Parallel Batching (Disjoint Systems Packed Together)
// =============================================================================

void test_narrow_parallel_batching() {
  std::cout << "Testing Narrow Parallel Batching (Disjoint Systems)... ";

  World world;
  ThreadPool pool(4);
  Scheduler scheduler;

  std::atomic<int> system1_executed{0};
  std::atomic<int> system2_executed{0};

  // System 1 reads Position, writes Velocity
  scheduler.add_system(
      "PhysicsMotion",
      Access{}.reads<Position>().writes<Velocity>().build(),
      [&](SystemContext &) {
        system1_executed.fetch_add(1, std::memory_order_relaxed);
      },
      SystemScale::Narrow
  );

  // System 2 reads Health, writes Score (completely disjoint)
  scheduler.add_system(
      "ScoringUpdate",
      Access{}.reads<Health>().writes<Score>().build(),
      [&](SystemContext &) {
        system2_executed.fetch_add(1, std::memory_order_relaxed);
      },
      SystemScale::Narrow
  );

  scheduler.compile(world);
  scheduler.run(world, pool);

  assert(system1_executed.load() == 1);
  assert(system2_executed.load() == 1);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Conflict Separation (Consecutive Batches)
// =============================================================================

void test_conflict_separation() {
  std::cout << "Testing Conflict Separation (Consecutive Batches)... ";

  World world;
  ThreadPool pool(4);
  Scheduler scheduler;

  std::vector<int> execution_order;

  // System A: writes Velocity
  scheduler.add_system(
      "SystemA_WriteVelocity",
      Access{}.writes<Velocity>().build(),
      [&](SystemContext &) { execution_order.push_back(1); },
      SystemScale::Narrow
  );

  // System B: reads Velocity (RAW conflict with System A -> must run in Batch
  // 1)
  scheduler.add_system(
      "SystemB_ReadVelocity",
      Access{}.reads<Velocity>().build(),
      [&](SystemContext &) { execution_order.push_back(2); },
      SystemScale::Narrow
  );

  scheduler.compile(world);
  scheduler.run(world, pool);

  assert(execution_order.size() == 2);
  assert(execution_order[0] == 1);
  assert(execution_order[1] == 2);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Wide System Dedicated Batch
// =============================================================================

void test_wide_system_isolation() {
  std::cout << "Testing Wide System Batch Isolation... ";

  World world;
  ThreadPool pool(4);
  Scheduler scheduler;

  std::vector<std::string> log;

  // Narrow system 1
  scheduler.add_system(
      "Narrow1",
      Access{}.writes<Score>().build(),
      [&](SystemContext &) { log.push_back("Narrow1"); },
      SystemScale::Narrow
  );

  // Wide system: must get dedicated batch
  scheduler.add_system(
      "WidePhysics",
      Access{}.writes<Velocity>().build(),
      [&](SystemContext &) { log.push_back("WidePhysics"); },
      SystemScale::Wide
  );

  // Narrow system 2
  scheduler.add_system(
      "Narrow2",
      Access{}.writes<Health>().build(),
      [&](SystemContext &) { log.push_back("Narrow2"); },
      SystemScale::Narrow
  );

  scheduler.compile(world);
  scheduler.run(world, pool);

  assert(log.size() == 3);
  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 5: Deterministic Deferred Mutations Across Batches
// =============================================================================

void test_deterministic_deferred_mutations() {
  std::cout << "Testing Deterministic Deferred Mutations across Batches... ";

  World world;
  ThreadPool pool(2);
  Scheduler scheduler;

  // System 1 (Batch 0): Spawns 10 entities via deferred command buffer
  scheduler.add_system(
      "Spawner",
      Access{}.writes<Position>().build(),
      [](SystemContext &ctx) {
        for (int i = 0; i < 10; ++i) {
          ctx.cmd.spawn(Position{static_cast<float>(i), 0.0f, 0.0f});
        }
      },
      SystemScale::Narrow
  );

  // System 2 (Batch 1): Reads Position (RAW conflict forces it into Batch 1)
  // Must observe all 10 entities already committed by Batch 0 barrier!
  std::atomic<size_t> observed_entities{0};
  scheduler.add_system(
      "Verifier",
      Access{}.reads<Position>().build(),
      [&](SystemContext &ctx) {
        size_t count = 0;
        for (auto [pos] : ctx.query<Position>()) {
          (void)pos;
          ++count;
        }
        observed_entities.store(count);
      },
      SystemScale::Narrow
  );

  scheduler.compile(world);
  scheduler.run(world, pool);

  assert(observed_entities.load() == 10);
  assert(world.archetypes.count_with<Position>() == 10);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 6: Init-Time Resource Validation
// =============================================================================

void test_missing_resource_validation() {
  std::cout << "Testing Init-Time Resource Validation... ";

  World world;
  Scheduler scheduler;

  // System declares dependency on MissingResource
  scheduler.add_system(
      "RequiresMissingResource",
      Access{}.reads_resource<MissingResource>().build(),
      [](SystemContext &) {},
      SystemScale::Narrow
  );

  // Compiling without the resource registered must throw std::runtime_error
  bool threw = false;
  try {
    scheduler.compile(world);
  } catch (const std::runtime_error &e) {
    threw = true;
  }
  assert(threw);

  // Once resource is registered, compile succeeds cleanly
  world.resources.insert<MissingResource>();
  scheduler.compile(world);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 7: Dirty State & Recompilation
// =============================================================================

void test_dirty_recompilation() {
  std::cout << "Testing Dirty State & Recompilation... ";

  World world;
  ThreadPool pool(2);
  Scheduler scheduler;

  std::atomic<int> counter{0};

  scheduler.add_system(
      "Sys1",
      Access{}.build(),
      [&](SystemContext &) { counter.fetch_add(1); },
      SystemScale::Narrow
  );

  scheduler.compile(world);
  scheduler.run(world, pool);
  assert(counter.load() == 1);

  // Adding a new system marks scheduler dirty
  scheduler.add_system(
      "Sys2",
      Access{}.build(),
      [&](SystemContext &) { counter.fetch_add(10); },
      SystemScale::Narrow
  );

  // Next run must re-compile and execute both systems
  scheduler.run(world, pool);
  assert(counter.load() == 12); // 1 + 1 + 10

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 8: Rule of Five (Copy & Move Semantics)
// =============================================================================

void test_scheduler_copy_and_move() {
  std::cout << "Testing Scheduler Copy & Move Semantics... ";

  World world;
  ThreadPool pool(2);

  std::atomic<int> executions{0};

  Scheduler original;
  original.add_system(
      "Incrementer",
      Access{}.build(),
      [&](SystemContext &) { executions.fetch_add(1); },
      SystemScale::Narrow
  );

  // Move construction
  Scheduler moved_scheduler(std::move(original));
  moved_scheduler.run(world, pool);
  assert(executions.load() == 1);

  // Copy construction
  Scheduler copied_scheduler(moved_scheduler);
  copied_scheduler.run(world, pool);
  assert(executions.load() == 2);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 9: Wide System Chunk Work-Stealing with Deferred Mutations
// =============================================================================

struct Marker {
  uint32_t val;
};

void test_wide_system_chunk_work_stealing() {
  std::cout
      << "Testing Wide System Chunk Work-Stealing (ctx.query_parallel)... ";

  World world;
  ThreadPool pool(4);

  constexpr size_t COUNT = 10000;
  for (size_t i = 0; i < COUNT; ++i) {
    world.spawn(Position{float(i), 0.0f}, Velocity{1.0f, 2.0f});
  }

  Scheduler scheduler;
  scheduler.add_system(
      "WidePhysicsIntegrator",
      Access{}.writes<Position>().reads<Velocity>().build(),
      [](SystemContext &ctx) {
        ctx.query<Position, const Velocity>().each_parallel(
            [](uint32_t count,
               CommandBuffer &thread_cmd,
               Position *pos,
               const Velocity *vel) {
              for (uint32_t i = 0; i < count; ++i) {
                pos[i].x += vel[i].vx * 0.1f;
                pos[i].y += vel[i].vy * 0.1f;
                if (i == 0) {
                  // Deferred spawn from worker thread
                  thread_cmd.spawn(Marker{42});
                }
              }
            }
        );
      },
      SystemScale::Wide
  );

  scheduler.run(world, pool);

  // Verify all entity positions updated
  for (auto [pos, vel] : world.query<Position, Velocity>()) {
    assert(pos.y == 0.2f);
  }

  // Verify deferred commands committed deterministically at the batch barrier
  assert(world.archetypes.count_with<Marker>() > 0);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 10: Single-Threaded System Traversal via each_chunk
// =============================================================================

void test_wide_system_single_threaded_fallback() {
  std::cout
      << "Testing Single-Threaded System Traversal (ctx.query.each_chunk)... ";

  World world;
  world.spawn(Position{10.0f, 20.0f}, Velocity{5.0f, 5.0f});

  CommandBuffer cmd;
  SystemContext ctx = world.create_system_context(cmd);

  ctx.query<Position, const Velocity>().each_chunk(
      [&](uint32_t count, Position *pos, const Velocity *vel) {
        for (uint32_t i = 0; i < count; ++i) {
          pos[i].x += vel[i].vx;
          ctx.cmd.spawn(Marker{99});
        }
      }
  );

  // Verify positions updated synchronously
  for (auto [pos] : world.query<Position>()) {
    assert(pos.x == 15.0f);
  }

  // Commit manual command buffer
  world.commit(cmd);
  assert(world.archetypes.count_with<Marker>() == 1);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 11: Narrow Systems Exceeding ThreadPool Worker Count
// =============================================================================

void test_narrow_systems_exceeding_worker_count() {
  std::cout
      << "Testing Narrow Systems Exceeding Worker Count (Dynamic Stealing)... ";

  World world;
  // Pool has only 2 worker threads:
  ThreadPool pool(2);

  Scheduler scheduler;
  std::atomic<uint32_t> executed{0};

  // Register 8 independent Narrow systems in a single non-conflicting batch:
  for (int i = 0; i < 8; ++i) {
    scheduler.add_system(
        "Sys" + std::to_string(i),
        Access{}.build(),
        [&](SystemContext &) { executed.fetch_add(1, std::memory_order_relaxed); },
        SystemScale::Narrow
    );
  }

  scheduler.run(world, pool);

  // All 8 systems must have executed, even though worker_count was only 2!
  assert(executed.load() == 8);

  std::cout << "PASSED\n";
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "     Module 021: Scheduler DAG & Parallel Tests      \n";
  std::cout << "====================================================\n";

  test_empty_scheduler();
  test_narrow_parallel_batching();
  test_conflict_separation();
  test_wide_system_isolation();
  test_deterministic_deferred_mutations();
  test_missing_resource_validation();
  test_dirty_recompilation();
  test_scheduler_copy_and_move();
  test_wide_system_chunk_work_stealing();
  test_wide_system_single_threaded_fallback();
  test_narrow_systems_exceeding_worker_count();

  std::cout << "\nAll Scheduler unit tests passed successfully!\n";
  return 0;
}
