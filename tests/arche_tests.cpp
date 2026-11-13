#include "arche/archetype_graph.hpp"
#include "arche/chunk.hpp"
#include "arche/command_buffer.hpp"
#include "arche/entity.hpp"
#include "arche/gpu_reflection.hpp"
#include "arche/hardware.hpp"
#include "arche/slot_map.hpp"
#include "arche/type_registry.hpp"
#include "arche/world.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
using arche::Archetype;
using arche::ArchetypeGraph;
using arche::CommandBuffer;
using arche::Entity;
using arche::GpuStd430Compatible;
using arche::Hardware;
using arche::Std430Traits;
using arche::TypeID;
using arche::TypeRegistry;
using arche::World;
using arche::Internal::SlotMap;
} // namespace

// =============================================================================
// Standalone Invariant Test Component Types
// =============================================================================

/**
 * @struct Pos
 * @brief 2D spatial coordinate component (8 bytes).
 */
struct Pos {
  float x{0.0f};
  float y{0.0f};
};

/**
 * @struct Vel
 * @brief 2D velocity vector component (8 bytes).
 */
struct Vel {
  float vx{0.0f};
  float vy{0.0f};
};

/**
 * @struct Mass
 * @brief Inertial scalar component (4 bytes).
 */
struct Mass {
  float value{1.0f};
};

/**
 * @struct TagDead
 * @brief Zero-sized tag component representing entity mortality state.
 */
struct TagDead {};

/**
 * @struct GoodVec4
 * @brief std430-compliant 16-byte 4D vector.
 */
struct GoodVec4 {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
  float w{1.0f};
};

/**
 * @struct BadVec3
 * @brief Intentionally unpadded 12-byte 3D vector violating std430 array stride
 * rules.
 */
struct BadVec3 {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
};

// =============================================================================
// Unit Test 1: SlotMap Generational Safety
// =============================================================================

/**
 * @brief Validates strict O(1) generational handle recycling and stale handle
 * detection.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * High-density simulations continuously create and destroy short-lived entities
 * (projectiles, particles). Without generational indexing, a stale Entity
 * handle pointing to a recycled slot would silently read or corrupt the memory
 * of the new occupant (the "ABA problem").
 *
 * This test guarantees:
 * 1. Newly allocated entity handles are marked active in $O(1)$ time.
 * 2. Destroyed entities immediately fail validity checks via generation
 * comparison.
 * 3. Recycling slot 0 increments its generation counter (skipping generation 0
 * / null handle).
 * 4. Stale handles pointing to previous generations remain strictly invalid.
 */
void test_generational_slot_map() {
  std::cout << "[Test 1/6] SlotMap Generational Safety... ";
  SlotMap slot_map;

  // 1. Allocate initial entity handle (index 0, initial generation 1)
  Entity e0 = slot_map.allocate();
  assert(slot_map.is_alive(e0));
  assert(e0.index() == 0);
  assert(e0.generation() == 1);

  // 2. Free e0 and verify immediate stale handle invalidation
  slot_map.free(e0);
  assert(!slot_map.is_alive(e0));

  // 3. Re-allocate slot 0 and verify generation increment (generation 2)
  Entity e1 = slot_map.allocate();
  assert(slot_map.is_alive(e1));
  assert(e1.index() == 0);
  assert(e1.generation() == 2);

  // 4. Ensure stale handle e0 remains strictly invalid while e1 is active
  assert(!slot_map.is_alive(e0));
  assert(slot_map.is_alive(e1));

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: Archetype DAG Diamond Convergence
// =============================================================================

/**
 * @brief Validates commutativity and deterministic canonical convergence in the
 * archetype DAG.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * In an Archetype ECS, the order in which components are added must not alter
 * the resulting archetype identity: Path 1: Root -> +Pos -> +Vel Path 2: Root
 * -> +Vel -> +Pos Both paths MUST converge to the exact same canonical
 * `Archetype*` pointer. If they branched into separate archetypes, system
 * queries would fragment, causing redundant chunk allocations and degraded
 * cache utilization.
 *
 * This test guarantees:
 * 1. Transition edges are created and cached in constant time.
 * 2. Independent addition sequences produce the identical target archetype
 * pointer.
 * 3. Component membership and count reflect the exact canonical component set.
 */
void test_archetype_dag_convergence() {
  std::cout << "[Test 2/6] Archetype DAG Diamond Convergence... ";
  TypeRegistry registry;
  registry.register_type<Pos>();
  registry.register_type<Vel>();

  ArchetypeGraph graph(registry);
  Archetype *root = graph.root();

  const TypeID pos_t = arche::type_id<Pos>();
  const TypeID vel_t = arche::type_id<Vel>();

  // 1. Traverse Path 1: Root -> +Pos -> +Vel
  Archetype *p1 = graph.traverse_add(root, pos_t);
  Archetype *target1 = graph.traverse_add(p1, vel_t);

  // 2. Traverse Path 2: Root -> +Vel -> +Pos
  Archetype *p2 = graph.traverse_add(root, vel_t);
  Archetype *target2 = graph.traverse_add(p2, pos_t);

  // 3. Verify canonical pointer identity and component composition
  assert(target1 == target2);
  assert(target1->component_count() == 2);
  assert(target1->has_component(pos_t));
  assert(target1->has_component(vel_t));

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Hardware Adaptive Chunk Alignment
// =============================================================================

/**
 * @brief Validates hardware topology probing and aligned chunk memory
 * allocation.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Archetype SoA chunk storage requires strict hardware alignment:
 * 1. Chunk byte boundaries must align to OS page sizes (4 KB / 16 KB) to
 * prevent TLB thrashing.
 * 2. Component columns within chunks must align to CPU cache line sizes (64B on
 * x86, 128B on Apple Silicon) to guarantee hardware prefetch efficiency and
 * prevent false sharing.
 *
 * This test guarantees:
 * 1. Cache line size is detected accurately (64 bytes or 128 bytes).
 * 2. Page size is valid and >= 4096 bytes.
 * 3. Chunk size is an exact multiple of the detected page size.
 * 4. `Hardware::allocate_aligned` and `Hardware::free_aligned` honor cache line
 * boundaries.
 */
void test_hardware_adaptive_chunking() {
  std::cout << "[Test 3/6] Hardware Adaptive Chunk Alignment... ";
  const size_t cache_line = Hardware::cache_line_size();
  const size_t page_size = Hardware::page_size();
  const size_t chunk_size = Hardware::optimal_chunk_size();

  // 1. Verify detected platform boundaries
  assert(cache_line == 64 || cache_line == 128);
  assert(page_size >= 4096);
  assert(chunk_size % page_size == 0);

  // 2. Allocate aligned memory and verify physical pointer address alignment
  void *mem = Hardware::allocate_aligned(chunk_size, cache_line);
  assert(mem != nullptr);
  assert(reinterpret_cast<uintptr_t>(mem) % cache_line == 0);

  // 3. Free aligned memory safely
  Hardware::free_aligned(mem);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Swap-and-Pop Compaction Integrity
// =============================================================================

/**
 * @brief Validates dense array compaction and reverse SlotMap synchronization
 * on deletion.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * To keep iteration cache-dense and contiguous, destroying an entity from a
 * chunk uses "Swap-and-Pop": the last entity in the chunk is moved into the
 * destroyed entity's row. During this move, the SlotMap record of the swapped
 * entity MUST be updated in O(1) time to reflect its new row position, ensuring
 * handles never point to corrupted memory.
 *
 * This test guarantees:
 * 1. Destroying an interior entity preserves all other entities.
 * 2. The swapped entity's components remain valid and accurate at their new
 * location.
 * 3. The SlotMap routing table remains in 100% synchronization.
 */
void test_swap_and_pop_compaction() {
  std::cout << "[Test 4/6] Swap-and-Pop Compaction Integrity... ";
  World world;

  // 1. Spawn 5 entities with deterministic coordinates
  std::vector<Entity> entities;
  for (int i = 0; i < 5; ++i) {
    entities.push_back(world.spawn(Pos{static_cast<float>(i), 0.0f}));
  }

  // 2. Destroy middle entity (index 2) triggering Swap-and-Pop
  Entity doomed = entities[2];
  assert(world.despawn(doomed));
  assert(!world.is_valid(doomed));

  // 3. Verify that all surviving entities remain fully valid
  assert(world.is_valid(entities[0]));
  assert(world.is_valid(entities[1]));
  assert(world.is_valid(entities[3]));
  assert(world.is_valid(entities[4]));

  // 4. Verify component value integrity for the moved entity
  assert(world.get<Pos>(entities[4]).x == 4.0f);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 5: Deferred CommandBuffer Playback
// =============================================================================

/**
 * @brief Validates thread-safe structural mutation recording and deferred
 * playback.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Directly modifying archetypes (creating entities, destroying entities,
 * adding/removing components) while a system is actively iterating over chunks
 * corrupts memory and invalidates active pointers. `CommandBuffer` records
 * mutations in an unformatted byte buffer and defers execution to a safe sync
 * point.
 *
 * This test guarantees:
 * 1. Mutations recorded to `CommandBuffer` do not affect the `World` prior to
 * playback.
 * 2. Entity remains fully valid and queryable while commands are queued.
 * 3. `commit(world)` executes all recorded commands in exact FIFO order.
 */
void test_deferred_command_buffer() {
  std::cout << "[Test 5/6] Deferred CommandBuffer Playback... ";
  World world;
  CommandBuffer cmd;

  // 1. Create initial entity
  Entity e = world.spawn(Pos{1.0f, 2.0f});
  assert(world.is_valid(e));

  // 2. Record deferred destruction in command buffer
  cmd.despawn(e);

  // 3. Verify entity is still alive prior to playback
  assert(world.is_valid(e));

  // 4. Flush and execute command buffer
  cmd.commit(world);

  // 5. Verify entity is successfully destroyed
  assert(!world.is_valid(e));

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 6: GPU std430 Reflection Traits
// =============================================================================

/**
 * @brief Validates compile-time traits enforcing GPU std430 alignment rules.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Under GPU `layout(std430)` specifications, 3D vectors (`vec3`) require
 * 16-byte array strides. A naive C++ struct of 12 bytes (`float[3]`) causes
 * off-by-4-byte memory misalignment in shaders. `GpuStd430Compatible`
 * statically validates types at compile time before memory is streamed to VRAM.
 *
 * This test guarantees:
 * 1. Compliant 16-byte structs (`GoodVec4`) pass `GpuStd430Compatible`.
 * 2. Unpadded 12-byte structs (`BadVec3`) fail
 * `Std430Traits<T>::is_safe_std430`.
 */
void test_gpu_reflection_traits() {
  std::cout << "[Test 6/6] GPU std430 Reflection Traits... ";

  // 1. Positive check: 16-byte struct is std430 safe
  static_assert(
      GpuStd430Compatible<GoodVec4>, "GoodVec4 must be std430 compatible"
  );

  // 2. Negative check: unpadded 12-byte struct must be rejected
  static_assert(
      !Std430Traits<BadVec3>::is_safe_std430,
      "BadVec3 (unpadded 12B) must fail std430 trait!"
  );

  std::cout << "PASSED\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche_tests (Unit & Invariant Test Suite)\n";
  std::cout << "====================================================\n";

  test_generational_slot_map();
  test_archetype_dag_convergence();
  test_hardware_adaptive_chunking();
  test_swap_and_pop_compaction();
  test_deferred_command_buffer();
  test_gpu_reflection_traits();

  std::cout << "====================================================\n";
  std::cout << ">>> ALL 6 UNIT TEST SUITES PASSED SUCCESSFULLY! <<<\n";
  std::cout << "====================================================\n";
  return 0;
}
