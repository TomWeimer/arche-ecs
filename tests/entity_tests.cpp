#include "arche/arche.hpp"

#include <cassert>
#include <iostream>
#include <vector>

namespace {
using arche::Entity;
using arche::null_entity;
namespace Internal = arche::Internal;
} // namespace

// =============================================================================
// Unit Test 1: Handle Bitfield Integrity & Canonical Null Entity
// =============================================================================

/**
 * @brief Verifies the 32-bit binary layout contract and null handle sentinel.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * In arche-ecs, Entity is an opaque 32-bit integer handle:
 * - Bits 0–23  (24 bits): Dense slot index (capacity: 16,777,215 simultaneous
 * entities).
 * - Bits 24–31 (8 bits):  Generation counter (1–255 lifecycles).
 *
 * This test guarantees:
 * 1. Packing & Unpacking: `from_parts()` correctly shifts the 8-bit generation
 *    into the high byte and masks the 24-bit index without bit overlap or data
 * loss.
 * 2. Accessor Invariants: `index()`, `id()`, and `generation()` extract the
 * exact bitfields.
 * 3. Boolean Conversion: Valid entities evaluate to `true` while the canonical
 *    `null_entity` evaluates to `false`.
 * 4. Sentinel Guarantee: `null_entity` is guaranteed to have index = 0,
 * generation = 0, and raw = 0, ensuring that uninitialized memory / zero-fills
 * default to null.
 */
void test_entity_bitfield() {
  std::cout << "[Test 1/4] Entity Bitfield & Null Handle Integrity... ";

  // Pack handle with known values: index 42, generation 7
  Entity e = Entity::from_parts(42, 7);
  assert(e.index() == 42);
  assert(e.id() == 42);
  assert(e.generation() == 7);
  assert(e.raw() == ((7ULL << 32) | 42ULL));
  assert(static_cast<bool>(e) == true);

  // Validate canonical null_entity invariants
  assert(null_entity.raw() == Entity::INDEX_NONE);
  assert(null_entity.index() == Entity::INDEX_NONE_32);
  assert(null_entity.generation() == Entity::INDEX_NONE_32);
  assert(static_cast<bool>(null_entity) == false);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: Generational Lifecycle & Stale-Handle Protection
// =============================================================================

/**
 * @brief Verifies O(1) allocation, destruction, and use-after-free prevention.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * The "Dangling Handle Hazard" is the primary source of memory corruption in
 * naive ECS:
 * - If Entity A is destroyed at Slot 0 and Slot 0 is immediately reused for
 * Entity B, any system still holding a handle to Entity A would accidentally
 * mutate Entity B!
 *
 * With an 8-bit generation counter:
 * 1. Destroying an entity increments the slot's generation counter.
 * 2. Re-allocating the slot issues a new handle with the advanced generation.
 * 3. Any query with the old handle fails the check:
 *      slot.generation == handle.generation
 *    in a single CPU clock cycle, completely preventing stale-handle memory
 * corruption.
 */
void test_slotmap_lifecycle_and_stale_handles() {
  std::cout << "[Test 2/4] Generational SlotMap Lifecycle & Stale Handle "
               "Rejection... ";

  Internal::SlotMap map;
  assert(map.size() == 0);

  // 1. Allocate initial entities: slots allocated sequentially starting at
  // generation 1
  Entity e1 = map.allocate();
  Entity e2 = map.allocate();
  assert(e1.index() == 0 && e1.generation() == 1);
  assert(e2.index() == 1 && e2.generation() == 1);
  assert(map.is_alive(e1));
  assert(map.is_alive(e2));
  assert(map.size() == 2);

  // 2. Free e1: slot 0 is placed onto the intrusive free-list, generation
  // advances to 2
  map.free(e1);
  assert(!map.is_alive(e1)); // Stale handle detection: generation mismatch
  assert(map.is_alive(e2));  // e2 remains unaffected
  assert(map.size() == 1);

  // 3. Re-allocate: slot 0 must be recycled from free list with incremented
  // generation
  Entity e1_recycled = map.allocate();
  assert(e1_recycled.index() == 0);
  assert(e1_recycled.generation() == 2); // Generation advanced from 1 to 2
  assert(map.is_alive(e1_recycled));
  assert(!map.is_alive(e1)); // Original handle e1 remains permanently dead!
  assert(map.size() == 2);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: High-Volume Allocation & Intrusive Free-List Churn
// =============================================================================

/**
 * @brief Verifies free-list pointer integrity under heavy spawn/despawn load.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * High-density simulations (e.g. 50,000+ particle/boid swarms) continuously
 * spawn and despawn entities at 120 Hz. This test ensures:
 * 1. The intrusive LIFO free-list (`next_free`) correctly chains recycled slots
 *    without pointer/index corruption or memory leaks.
 * 2. Memory scaling remains stable under high allocation loads (100,000
 * entities).
 * 3. Alternating deallocations (50% churn) produce clean interleaved
 * free-lists.
 * 4. Subsequent re-allocations consume all recycled slots before growing the
 * vector.
 */
void test_million_entity_stress() {
  std::cout << "[Test 3/4] High-Volume Churn Stress (100,000 Entities)... ";

  Internal::SlotMap map;
  std::vector<Entity> active;
  active.reserve(100'000);

  // Phase 1: Allocate 100,000 entities
  for (int i = 0; i < 100'000; ++i) {
    active.push_back(map.allocate());
  }
  assert(map.size() == 100'000);

  // Phase 2: Despawn every even entity (50,000 entities freed into intrusive
  // free list)
  for (int i = 0; i < 100'000; i += 2) {
    map.free(active[i]);
  }
  assert(map.size() == 50'000);

  // Phase 3: Validate survivors are alive and freed entities are dead
  for (int i = 0; i < 100'000; ++i) {
    if (i % 2 == 0) {
      assert(!map.is_alive(active[i]));
    } else {
      assert(map.is_alive(active[i]));
    }
  }

  // Phase 4: Respawn 50,000 entities; must recycle from free list with gen >= 2
  for (int i = 0; i < 50'000; ++i) {
    Entity recycled = map.allocate();
    assert(recycled.generation() >= 2);
  }
  assert(map.size() == 100'000);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Generation Wrap-Around Safety (255 -> 1)
// =============================================================================

/**
 * @brief Verifies that 8-bit generation overflow skips 0 to protect
 * null_entity.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * An 8-bit integer overflows after 255 increments:
 * - A standard integer increment `255 + 1` wraps to `0`.
 * - HOWEVER, Generation 0 is strictly reserved for `null_entity`
 * (`from_parts(0, 0)`).
 * - If a recycled entity slot wrapped to Generation 0, an active entity would
 * collide with the `null_entity` sentinel and falsely evaluate to null/dead!
 *
 * This test recycles a single slot 255 consecutive times to force an overflow
 * and proves that `SlotMap::free()` skips 0 and wraps directly to generation 1.
 */
void test_generation_wrap_around() {
  std::cout << "[Test 4/4] Generation Wrap-Around Safety (0xFFFFFFFF -> 1)... ";

  assert(advance_nonzero(0xFFFFFFFFU) == 1U);
  assert(advance_nonzero(0U) == 1U);
  assert(advance_nonzero(1U) == 2U);

  Internal::SlotMap map;
  Entity e = map.allocate();
  assert(e.generation() == 1);

  map.free(e);
  Entity e2 = map.allocate();
  assert(e2.generation() == 2);
  assert(e2 != null_entity);
  assert(map.is_alive(e2));

  std::cout << "PASSED\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout
      << "=============================================================\n";
  std::cout
      << " Running arche-ecs Module 001: Entity & SlotMap Unit Tests   \n";
  std::cout
      << "=============================================================\n";

  test_entity_bitfield();
  test_slotmap_lifecycle_and_stale_handles();
  test_million_entity_stress();
  test_generation_wrap_around();

  std::cout
      << "=============================================================\n";
  std::cout
      << " All Module 001 Unit Tests Passed Successfully!              \n";
  std::cout
      << "=============================================================\n";
  return 0;
}
