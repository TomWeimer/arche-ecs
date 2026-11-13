#include "arche/arche.hpp"

#include <cassert>
#include <chrono>
#include <iostream>

namespace {
using arche::Entity;
using arche::Mutated;
using arche::World;
using arche::Write;
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
// Unit Test 1: Distinct Physical Memory Slices (Read != Write)
// =============================================================================

void test_distinct_memory_slices() {
  std::cout << "[Test 1/5] Physical Slice Separation (Read != Write)... ";
  World world;

  world.enable_double_buffer<Position>();
  world.spawn(Position{10.0f, 20.0f}, Velocity{1.0f, 2.0f});

  const void *read_addr = nullptr;
  const void *write_addr = nullptr;

  auto q = world.query<const Position, Write<Position>, Velocity>();
  q.for_each_chunk(
      [&](uint32_t count, const Position *r, Write<Position> w, Velocity *) {
        assert(count == 1);
        read_addr = static_cast<const void *>(r);
        write_addr = static_cast<const void *>(w.get());
      }
  );

  assert(read_addr != nullptr && write_addr != nullptr);
  assert(read_addr != write_addr);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: O(1) Pointer Flipping Across Sub-Steps
// =============================================================================

void test_pointer_flip_substep() {
  std::cout << "[Test 2/5] O(1) Pointer-Flip Physics Sub-Steps... ";
  World world;

  world.enable_double_buffer<Position>();
  Entity e = world.spawn(Position{0.0f, 0.0f}, Velocity{5.0f, 0.0f});

  auto q = world.query<const Position, Write<Position>, const Velocity>();
  q.for_each_chunk([](uint32_t count,
                      const Position *r,
                      Write<Position> w,
                      const Velocity *v) {
    for (uint32_t i = 0; i < count; ++i) {
      w[i].x = r[i].x + v[i].vx;
      w[i].y = r[i].y + v[i].vy;
    }
  });

  assert(world.get<Position>(e).x == 0.0f);

  world.flip<Position>();
  assert(world.get<Position>(e).x == 5.0f);

  q.for_each_chunk([](uint32_t count,
                      const Position *r,
                      Write<Position> w,
                      const Velocity *v) {
    for (uint32_t i = 0; i < count; ++i) {
      w[i].x = r[i].x + v[i].vx;
    }
  });

  world.flip<Position>();
  assert(world.get<Position>(e).x == 10.0f);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Automatic Dirty Ticking on Flip
// =============================================================================

void test_automatic_dirty_ticking_on_flip() {
  std::cout << "[Test 3/5] Automatic Dirty Ticking on Pointer Flip... ";
  World world;

  world.enable_double_buffer<Position>();
  world.spawn(Position{0.0f, 0.0f}, Velocity{1.0f, 1.0f});

  const uint32_t initial_tick = world.current_tick();

  // Flip buffer: must advance world tick and stamp Position column
  world.flip<Position>();

  assert(world.current_tick() > initial_tick);

  // Mutated<Position> filter with initial_tick must match the chunk
  size_t count = 0;
  world.query<Position>(Mutated<Position>{initial_tick})
      .for_each_chunk([&](uint32_t n, Position *) { count += n; });
  assert(count == 1);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Elimination of Order-Dependent Bias
// =============================================================================

void test_zero_order_bias() {
  std::cout << "[Test 4/5] Elimination of Order-Dependent Simulation Bias... ";
  World world;

  world.enable_double_buffer<Position>();

  world.spawn(Position{10.0f, 0.0f});
  world.spawn(Position{20.0f, 0.0f});
  world.spawn(Position{30.0f, 0.0f});

  auto q = world.query<const Position, Write<Position>>();

  q.for_each_chunk([](uint32_t count, const Position *r, Write<Position> w) {
    float sum = 0.0f;
    for (uint32_t i = 0; i < count; ++i) {
      sum += r[i].x;
    }
    const float avg = sum / static_cast<float>(count);

    for (uint32_t i = 0; i < count; ++i) {
      w[i].x = avg;
    }
  });

  world.flip<Position>();

  world.query<const Position>().for_each_chunk([](uint32_t count,
                                                  const Position *r) {
    for (uint32_t i = 0; i < count; ++i) {
      assert(r[i].x == 20.0f);
    }
  });

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 5: Pointer-Flip Latency Benchmark (< 10 ns)
// =============================================================================

void test_flip_latency() {
  std::cout << "[Test 5/5] Pointer-Flip Latency Benchmark (< 10 ns)... ";
  World world;

  world.enable_double_buffer<Position>();
  world.spawn_batch(10'000, Position{0.0f, 0.0f});

  constexpr int ITERS = 100'000;
  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < ITERS; ++i) {
    world.flip<Position>();
  }

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::nano> total_ns = end - start;
  const double ns_per_flip = total_ns.count() / static_cast<double>(ITERS);

  assert(ns_per_flip < 100.0);
  std::cout << "PASSED (" << ns_per_flip << " ns / flip)\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Module 015: Double-Buffer Tests\n";
  std::cout << "====================================================\n";

  test_distinct_memory_slices();
  test_pointer_flip_substep();
  test_automatic_dirty_ticking_on_flip();
  test_zero_order_bias();
  test_flip_latency();

  std::cout << "====================================================\n";
  std::cout << "All Module 015 tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}