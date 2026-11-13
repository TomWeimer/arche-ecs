#include "arche/chunk.hpp"
#include "arche/entity.hpp"
#include "arche/gpu_reflection.hpp"
#include "arche/parallel_command_buffer.hpp"
#include "arche/query.hpp"
#include "arche/thread_pool.hpp"
#include "arche/world.hpp"

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
  world.spawn_batch(COUNT, Position{1.0f, 2.0f}, Velocity{3.0f, 4.0f});

  world.query_parallel<Position, const Velocity>(
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
  std::vector<Entity> entities = world.spawn_batch(
      COUNT, Position{0.0f, 0.0f}
  );

  world.query_parallel<Entity, const Position>(
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

  world.commit(cmd_buffers);
  assert(world.entity_count() == COUNT / 2);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Triple-Buffered GPU Ring Streaming
// =============================================================================

void test_triple_buffered_ring_streaming() {
  std::cout << "[Test 3/4] Triple-Buffered GPU Ring Streaming... ";
  World world;

  constexpr size_t COUNT = 1000;
  world.spawn_batch(COUNT, Position{42.0f, 84.0f});

  constexpr size_t SLOT_BYTES = 64 * 1024;
  std::vector<uint8_t> vram_base(SLOT_BYTES * 3, 0);

  size_t b0 = world.stream_gpu<Position>(
      0, vram_base.data(), SLOT_BYTES
  );
  assert(b0 == COUNT * sizeof(Position));

  size_t b1 = world.stream_gpu<Position>(
      1, vram_base.data(), SLOT_BYTES
  );
  assert(b1 == COUNT * sizeof(Position));

  size_t b2 = world.stream_gpu<Position>(
      2, vram_base.data(), SLOT_BYTES
  );
  assert(b2 == COUNT * sizeof(Position));

  size_t b3 = world.stream_gpu<Position>(
      3, vram_base.data(), SLOT_BYTES
  );
  assert(b3 == COUNT * sizeof(Position));

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Multi-Draw Indirect Generation with Culling
// =============================================================================

void test_draw_indirect_with_culling() {
  std::cout << "[Test 4/4] Multi-Draw Indirect Generation with Macro AABB "
               "Culling... ";
  World world;

  constexpr size_t COUNT = 5000;
  world.spawn_batch(COUNT, Position{1.0f, 1.0f});

  Archetype *arch = world.graph().archetypes().back().get();
  auto &chunks = world.get_archetype_chunks(arch);
  assert(chunks.size() >= 2);

  // Set user metadata on chunk 0 (visible) and chunk 1 (culled)
  chunks[0]->user_metadata<TestAABB>() = TestAABB{-10.0f, 10.0f};
  chunks[1]->user_metadata<TestAABB>() = TestAABB{-999.0f, -900.0f};

  std::vector<DrawIndirectCommand> cmd_buffer(64);

  // Culling predicate: only draw if max_x > 0
  size_t count = world.indirect_draw(
      arch,
      cmd_buffer.data(),
      cmd_buffer.size(),
      [](const arche::ChunkHeader &header) {
        const auto *aabb = reinterpret_cast<const TestAABB *>(
            header.user_metadata
        );
        return aabb->max_x > 0.0f;
      }
  );

  // Only the visible chunk should be emitted!
  assert(count < chunks.size());
  assert(count >= 1);
  assert(cmd_buffer[0].instance_count == 1);

  std::cout << "PASSED\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Module 016: Multithreading & Ring Tests\n";
  std::cout << "====================================================\n";

  test_parallel_query_parity();
  test_parallel_command_buffers();
  test_triple_buffered_ring_streaming();
  test_draw_indirect_with_culling();

  std::cout << "====================================================\n";
  std::cout << "All Module 016 tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}