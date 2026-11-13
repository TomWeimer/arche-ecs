#include "arche/arche.hpp"
#include "arche/detail/chunk_pool.hpp"
#include "arche/detail/slot_map.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
using arche::Archetype;
using arche::Chunk;
using arche::ChunkPool;
using arche::ComponentColumnLayout;
using arche::ComponentMask;
using arche::Entity;
using arche::Hardware;
using arche::SlotMap;
using arche::TypeID;
using arche::TypeRegistry;
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

struct Health {
  int32_t hp{100};
  int32_t max_hp{100};
};

// =============================================================================
// Unit Test 1: Hardware Topology & Detection Queries
// =============================================================================

/**
 * @brief Verifies runtime host hardware queries for Level 1 cache and virtual
 * memory pages.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * High-performance ECS layouts cannot hardcode cache line sizes or page sizes:
 * - Apple Silicon M-series CPUs use 128-byte cache lines and 16 KB virtual
 * memory pages.
 * - x86-64 CPUs use 64-byte cache lines and 4 KB virtual memory pages.
 *
 * This test guarantees:
 * 1. Cache line query returns a valid hardware boundary (64 or 128 bytes).
 * 2. Page size query returns a valid OS page boundary (4 KB, 16 KB, or 64 KB).
 * 3. `optimal_chunk_size()` produces an exact multiple of the host page size.
 */
void test_hardware_queries() {
  std::cout << "[Test 1/4] Hardware Topology Detection... ";

  const size_t cache_line = Hardware::cache_line_size();
  const size_t page_size = Hardware::page_size();
  const size_t optimal_chunk = Hardware::optimal_chunk_size();

  assert(cache_line == 64 || cache_line == 128);
  assert(page_size == 4096 || page_size == 16384 || page_size == 65536);
  assert(optimal_chunk % page_size == 0);
  assert(optimal_chunk >= 65536);

  std::cout << "PASSED (CacheLine=" << cache_line << "B, Page=" << page_size
            << "B, OptimalChunk=" << optimal_chunk << "B)\n";
}

// =============================================================================
// Unit Test 2: Aligned Chunk Allocation & SoA Physical Slices
// =============================================================================

/**
 * @brief Verifies that allocated chunk buffers and component slices meet
 * alignment contracts.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Every component column within a Chunk must start at an address aligned to
 * hardware cache line boundaries to guarantee that vector SIMD loads
 * (AVX2/AVX-512/NEON) execute with zero unaligned memory penalties.
 */
void test_chunk_allocation_and_alignment() {
  std::cout << "[Test 2/4] Aligned Chunk Allocation & Slice Math... ";

  TypeRegistry registry;
  registry.register_type<Position>();
  registry.register_type<Velocity>();

  const uint32_t chunk_size = static_cast<uint32_t>(
      Hardware::optimal_chunk_size()
  );
  const TypeID pos_t = arche::type_id<Position>();
  const TypeID vel_t = arche::type_id<Velocity>();

  ComponentMask mask;
  mask.set(pos_t);
  mask.set(vel_t);

  std::vector<TypeID> types = {pos_t, vel_t};
  Archetype arch(1, mask, std::move(types), registry, chunk_size);
  assert(arch.chunk_capacity() > 0);

  ChunkPool pool(chunk_size, 1);
  void *buffer = pool.acquire_buffer();
  Chunk *chunk = Chunk::create(
      buffer, &arch, arch.chunk_capacity(), chunk_size
  );
  assert(chunk != nullptr);

  // Verify chunk buffer base address is aligned to cache line boundary
  const uintptr_t chunk_addr = reinterpret_cast<uintptr_t>(chunk);
  assert(chunk_addr % Hardware::cache_line_size() == 0);

  // Verify Position slice alignment
  const auto *pos_layout = arch.column_layout_for(pos_t);
  assert(pos_layout != nullptr);
  const uintptr_t pos_addr = reinterpret_cast<uintptr_t>(
      chunk->component_slice(pos_layout->chunk_byte_offset)
  );
  assert(pos_addr % pos_layout->alignment == 0);

  // Verify Velocity slice alignment
  const auto *vel_layout = arch.column_layout_for(vel_t);
  assert(vel_layout != nullptr);
  const uintptr_t vel_addr = reinterpret_cast<uintptr_t>(
      chunk->component_slice(vel_layout->chunk_byte_offset)
  );
  assert(vel_addr % vel_layout->alignment == 0);

  pool.release_buffer(buffer);
  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Swap-and-Pop Compaction & SlotMap Synchronization
// =============================================================================

/**
 * @brief Verifies O(1) entity removal via Swap-and-Pop and SlotMap record
 * updating.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * When an entity is destroyed:
 * 1. The last entity in the chunk is moved into the vacated slot (O(1)).
 * 2. CRITICAL: The moved entity's physical row coordinate must be updated in
 * the SlotMap.
 * 3. The chunk entity count decrements by 1 with zero memory fragmentation.
 */
void test_swap_and_pop_compaction() {
  std::cout << "[Test 3/4] Swap-and-Pop Compaction & SlotMap Sync... ";

  TypeRegistry registry;
  registry.register_type<Position>();
  registry.register_type<Health>();

  SlotMap slot_map;
  const uint32_t chunk_size = 16384;
  const TypeID pos_t = arche::type_id<Position>();
  const TypeID hp_t = arche::type_id<Health>();

  ComponentMask mask;
  mask.set(pos_t);
  mask.set(hp_t);

  std::vector<TypeID> types = {pos_t, hp_t};
  Archetype arch(1, mask, std::move(types), registry, chunk_size);
  ChunkPool pool(chunk_size, 1);
  void *buffer = pool.acquire_buffer();
  Chunk *chunk = Chunk::create(
      buffer, &arch, arch.chunk_capacity(), chunk_size
  );

  const auto *pos_layout = arch.column_layout_for(pos_t);
  const auto *hp_layout = arch.column_layout_for(hp_t);
  assert(pos_layout != nullptr && hp_layout != nullptr);

  auto *positions = reinterpret_cast<Position *>(
      chunk->component_slice(pos_layout->chunk_byte_offset)
  );
  auto *healths = reinterpret_cast<Health *>(
      chunk->component_slice(hp_layout->chunk_byte_offset)
  );

  // Spawn 4 entities (E0, E1, E2, E3)
  std::vector<Entity> entities;
  for (int i = 0; i < 4; ++i) {
    Entity e = slot_map.allocate();
    uint32_t row = chunk->append_entity(e);
    slot_map.update_location(e, &arch, chunk, row);

    positions[row] = {static_cast<float>(i * 10), static_cast<float>(i * 20)};
    healths[row] = {100 - i, 100};
    entities.push_back(e);
  }
  assert(chunk->count() == 4);

  // Target: Delete Entity 1 at row 1 (Entities: [E0, E1, E2, E3])
  // After Swap-and-Pop: E3 should move into row 1 (Entities: [E0, E3, E2],
  // count = 3)
  const Entity moved_entity = entities[3];

  std::vector<ComponentColumnLayout> layouts = {*pos_layout, *hp_layout};
  chunk->swap_and_pop(1, slot_map, layouts);

  assert(chunk->count() == 3);
  assert(chunk->entity_slice()[1] == moved_entity);
  assert(positions[1].x == 30.0f);
  assert(healths[1].hp == 97);

  // Verify SlotMap was updated for moved_entity
  const auto &rec = slot_map.get_record(moved_entity);
  assert(rec.row == 1); // Row was updated from 3 to 1!

  pool.release_buffer(buffer);
  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: ChunkPool Allocation & Recycling
// =============================================================================

/**
 * @brief Verifies that ChunkPool recycles 64 KB memory slabs without OS page
 * faults.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Prevents kernel mode context switches during dynamic simulation entity
 * spawning.
 */
void test_chunk_pool_recycling() {
  std::cout << "[Test 4/4] ChunkPool Allocation & Recycling... ";

  ChunkPool pool(65536, 4);
  assert(pool.free_count() == 4);

  void *b1 = pool.acquire_buffer();
  void *b2 = pool.acquire_buffer();
  assert(b1 != nullptr && b2 != nullptr);
  assert(b1 != b2);
  assert(pool.free_count() == 2);

  pool.release_buffer(b1);
  pool.release_buffer(b2);
  assert(pool.free_count() == 4);

  // Re-acquire: must return warm pooled memory without OS calls
  void *b3 = pool.acquire_buffer();
  assert(b3 == b2 || b3 == b1);

  pool.release_buffer(b3);
  std::cout << "PASSED\n";
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Chunk Storage Unit & Perf Tests\n";
  std::cout << "====================================================\n";

  test_hardware_queries();
  test_chunk_allocation_and_alignment();
  test_swap_and_pop_compaction();
  test_chunk_pool_recycling();

  std::cout << "All Chunk Storage tests completed successfully!\n";
  return 0;
}
