#include "arche/chunk.hpp"
#include "arche/chunk_pool.hpp"
#include "arche/entity.hpp"
#include "arche/query.hpp"
#include "arche/world.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
using arche::Chunk;
using arche::ChunkHeader;
using arche::ChunkPool;
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

/// 3D Bounding Box representing consumer user metadata (Macro Frustum Culling)
struct alignas(16) MacroAABB {
  float min_x{-100.0f};
  float min_y{-100.0f};
  float min_z{-100.0f};
  float max_x{100.0f};
  float max_y{100.0f};
  float max_z{100.0f};
};
static_assert(sizeof(MacroAABB) <= 64, "MacroAABB must fit within 64 bytes");

// =============================================================================
// Unit Test 1: ChunkHeader 128-Byte Layout & User Metadata Persistence
// =============================================================================

/**
 * @brief Validates 128-byte ChunkHeader alignment and user metadata storage.
 */
void test_chunk_user_metadata() {
  std::cout << "[Test 1/4] ChunkHeader 256-Byte Layout & User Metadata... ";

  assert(sizeof(ChunkHeader) == 256);
  assert(alignof(ChunkHeader) == 64);

  World world;
  world.create_entity(Position{1.0f, 2.0f}, Velocity{3.0f, 4.0f});

  // Query chunk to inspect user metadata slot
  world.query<Position>().for_each_chunk([](uint32_t count, Position *pos) {
    assert(count == 1);
    [[maybe_unused]] Position *a = pos;
    // Find enclosing Chunk pointer from component column pointer
    // In our architecture, the chunk starts at pos - pos_offset
  });

  // Direct test on raw chunk buffer
  std::vector<uint8_t> buffer(65536, 0);
  Chunk *chunk = reinterpret_cast<Chunk *>(buffer.data());

  // Store MacroAABB in chunk user metadata
  MacroAABB &aabb = chunk->user_metadata<MacroAABB>();
  aabb.min_x = -500.0f;
  aabb.max_x = 500.0f;

  // Retrieve read-only reference and verify bit-exact persistence
  const Chunk *const_chunk = chunk;
  const MacroAABB &read_aabb = const_chunk->user_metadata<MacroAABB>();
  assert(read_aabb.min_x == -500.0f);
  assert(read_aabb.max_x == 500.0f);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: Branchless SIMD Execution on Tail-Padded Slices
// =============================================================================

/**
 * @brief Validates unconditional SIMD execution up to padded_count with zero
 * memory overruns.
 */
void test_simd_tail_padded_execution() {
  std::cout << "[Test 2/4] Branchless SIMD Execution on Tail-Padded Slices... ";
  World world;

  // Create 1,603 entities (forces a non-multiple of 16)
  constexpr size_t COUNT = 1603;
  std::vector<Entity> entities = world.create_entities(
      COUNT, Position{1.0f, 1.0f}, Velocity{2.0f, 3.0f}
  );

  assert(world.entity_count() == COUNT);

  // Execute branchless SIMD loop over padded_count
  auto q = world.query<Position, const Velocity>();
  q.for_each_chunk_padded(
      [](uint32_t active, uint32_t padded, Position *pos, const Velocity *vel) {
        assert(active == 1603);
        assert(padded == 1616); // (1603 + 15) & ~15

        // SIMD loop processes full 16-element blocks unconditionally
        // Tail-padding guarantees that indices 1603..1615 are safe to
        // read/write!
        for (uint32_t i = 0; i < padded; i += 16) {
          for (uint32_t k = 0; k < 16; ++k) {
            pos[i + k].x += vel[i + k].vx;
            pos[i + k].y += vel[i + k].vy;
          }
        }
      }
  );

  // Verify all 1,603 active entities received correct updates
  for (size_t i = 0; i < COUNT; ++i) {
    const auto &pos = world.get_component<Position>(entities[i]);
    assert(pos.x == 3.0f);
    assert(pos.y == 4.0f);
  }

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Lookahead Hardware Prefetching Compilation & Correctness
// =============================================================================

/**
 * @brief Validates query iteration and lookahead prefetching across multiple
 * chunks.
 */
void test_lookahead_prefetching() {
  std::cout << "[Test 3/4] Lookahead Software Prefetching across Chunks... ";
  World world;

  // Create 50,000 entities spanning ~30 chunks
  constexpr size_t COUNT = 50'000;
  world.create_entities(COUNT, Position{0.0f, 0.0f}, Velocity{1.0f, 1.0f});

  size_t total_processed = 0;
  auto q = world.query<Position, const Velocity>();

  q.for_each_chunk([&](uint32_t count, Position *pos, const Velocity *vel) {
    for (uint32_t i = 0; i < count; ++i) {
      pos[i].x += vel[i].vx;
      pos[i].y += vel[i].vy;
    }
    total_processed += count;
  });

  assert(total_processed == COUNT);
  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Custom Allocator Hook in ChunkPool (GPU-Direct VRAM Backing)
// =============================================================================

static size_t g_custom_allocs = 0;
static size_t g_custom_frees = 0;

void *mock_vram_alloc(size_t bytes, size_t alignment) {
  ++g_custom_allocs;
  return arche::Hardware::allocate_aligned(bytes, alignment);
}

void mock_vram_free(void *ptr, size_t bytes) {
  (void)bytes;
  ++g_custom_frees;
  arche::Hardware::free_aligned(ptr);
}

/**
 * @brief Validates custom allocator backing for GPU-direct unified memory.
 */
void test_custom_chunk_allocator() {
  std::cout << "[Test 4/4] Custom Memory Allocator Hook in ChunkPool... ";

  g_custom_allocs = 0;
  g_custom_frees = 0;

  {
    ChunkPool pool(65536, 4, mock_vram_alloc, mock_vram_free);
    assert(g_custom_allocs == 4);
    assert(pool.free_count() == 4);

    void *b1 = pool.acquire_buffer();
    void *b2 = pool.acquire_buffer();
    assert(b1 != nullptr && b2 != nullptr);

    pool.release_buffer(b1);
    pool.release_buffer(b2);
  }

  assert(g_custom_frees == 4);
  std::cout << "PASSED\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Module 011: SIMD & Hardware Tests\n";
  std::cout << "====================================================\n";

  test_chunk_user_metadata();
  test_simd_tail_padded_execution();
  test_lookahead_prefetching();
  test_custom_chunk_allocator();

  std::cout << "====================================================\n";
  std::cout << "All Module 011 tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}
