import arche;

#include <cassert>
#include <iostream>
#include <vector>

namespace {
using arche::Archetype;
using arche::DrawIndirectCommand;
using arche::Entity;
using arche::ParallelCommandBuffer;
using arche::ThreadPool;
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

struct TestAABB {
  float min_x{-50.0f};
  float max_x{50.0f};
};

// =============================================================================
// Unit Test 1: Chunk-Level Parallel Work-Stealing Parity
// =============================================================================

void test_parallel_query_parity() {
  std::cout << "[Test 1/4] Chunk-Level Parallel Work-Stealing Parity (50k)... ";
  World world;
  ThreadPool pool(4);

  constexpr size_t COUNT = 50'000;
  world.spawn({.count = COUNT}, Position{1.0f, 2.0f}, Velocity{3.0f, 4.0f});

  world.query<Position, const Velocity>().each_parallel(
      pool, [](uint32_t count, Position *pos, const Velocity *vel) {
        for (uint32_t i = 0; i < count; ++i) {
          pos[i].x += vel[i].vx;
          pos[i].y += vel[i].vy;
        }
      }
  );

  size_t checked = 0;
  world.query<const Position>().for_each_chunk([&](uint32_t count,
                                                   const Position *pos) {
    for (uint32_t i = 0; i < count; ++i) {
      assert(pos[i].x == 4.0f);
      assert(pos[i].y == 6.0f);
    }
    checked += count;
  });

  assert(checked == COUNT);
  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: Lock-Free Thread-Local Command Buffers
// =============================================================================

void test_parallel_command_buffers() {
  std::cout << "[Test 2/4] Lock-Free Thread-Local Command Buffers... ";
  World world;
  ThreadPool pool(4);
  ParallelCommandBuffer cmd_buffers(pool.worker_count());

  constexpr size_t COUNT = 10'000;
  std::vector<Entity> entities = world.spawn(
      {.count = COUNT}, Position{0.0f, 0.0f}
  );

  world.query<Entity, const Position>().each_parallel(
      pool,
      cmd_buffers,
      [](uint32_t count,
         arche::CommandBuffer &cmd,
         Entity *ents,
         const Position *) {
        for (uint32_t i = 0; i < count; ++i) {
          if (ents[i].index() % 2 == 0) {
            cmd.despawn(ents[i]);
          }
        }
      }
  );

  cmd_buffers.commit(world);
  assert(world.entities.count() == COUNT / 2);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Triple-Buffered GPU Ring Streaming
// =============================================================================

void test_chunk_views_and_streaming() {
  std::cout << "[Test 3/4] Component Chunk Views & Linear Streaming... ";
  World world;

  constexpr size_t COUNT = 1000;
  world.spawn({.count = COUNT}, Position{42.0f, 84.0f});

  auto views = world.gpu.view_chunks<Position>();
  assert(!views.empty());

  size_t total_count = 0;
  for (const auto &span : views) {
    total_count += span.count();
    assert(span.data() != nullptr);
  }
  assert(total_count == COUNT);

  std::vector<Position> dest(COUNT);
  size_t streamed = world.gpu.stream_to<Position>(dest.data());
  assert(streamed == COUNT * sizeof(Position));
  assert(dest[0].x == 42.0f && dest[0].y == 84.0f);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Chunk User Metadata Filtering
// =============================================================================

void test_chunk_user_metadata_culling() {
  std::cout << "[Test 4/4] Chunk User Metadata Macro AABB Filtering... ";
  World world;

  constexpr size_t COUNT = 5000;
  world.spawn({.count = COUNT}, Position{1.0f, 1.0f});

  Archetype *arch = arche::internal::WorldAccess::graph(world)
                        .archetypes()
                        .back()
                        .get();
  auto &chunks = world.archetypes.chunks(arch);
  assert(chunks.size() >= 2);

  // Set user metadata on chunk 0 (visible) and chunk 1 (culled)
  chunks[0]->user_metadata<TestAABB>() = TestAABB{-10.0f, 10.0f};
  chunks[1]->user_metadata<TestAABB>() = TestAABB{-999.0f, -900.0f};

  // Filter chunks using user metadata predicate
  size_t visible_chunks = 0;
  for (arche::Chunk *chunk : chunks) {
    if (chunk->is_empty())
      continue;
    const auto *aabb = reinterpret_cast<const TestAABB *>(
        chunk->header().user_metadata
    );
    if (aabb->max_x > 0.0f) {
      visible_chunks++;
    }
  }

  assert(visible_chunks >= 1);
  assert(visible_chunks < chunks.size());

  std::cout << "PASSED\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Module 016: Multithreading & Chunk Tests\n";
  std::cout << "====================================================\n";

  test_parallel_query_parity();
  test_parallel_command_buffers();
  test_chunk_views_and_streaming();
  test_chunk_user_metadata_culling();

  std::cout << "====================================================\n";
  std::cout << "All Module 016 tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}