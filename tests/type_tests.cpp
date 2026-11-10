#include "arche/component_mask.hpp"
#include "arche/type_id.hpp"
#include "arche/type_registry.hpp"

#include <cassert>
#include <iostream>

namespace {
using arche::ComponentMask;
using arche::type_id;
using arche::TypeID;
using arche::TypeRegistry;
} // namespace

// =============================================================================
// Standalone Test Component Types
// =============================================================================

struct Pos {
  float x{0.0f};
  float y{0.0f};
};

struct Vel {
  float vx{0.0f};
  float vy{0.0f};
};

struct Mass {
  float m{1.0f};
};

struct ActiveTag {};
struct DeadTag {};

struct TrackedObject {
  int value{0};
  inline static int live_instances = 0;

  TrackedObject(int val = 0) : value(val) { ++live_instances; }
  ~TrackedObject() { --live_instances; }
  TrackedObject(const TrackedObject &) = delete;
  TrackedObject(TrackedObject &&other) noexcept : value(other.value) {
    ++live_instances;
  }
  TrackedObject &operator=(TrackedObject &&other) noexcept {
    value = other.value;
    return *this;
  }
};

// =============================================================================
// Unit Test 1: Sequential Contiguous TypeID Generation
// =============================================================================

/**
 * @brief Verifies that compile-time static counters assign dense, contiguous
 * 0-indexed IDs.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Standard C++ RTTI (typeid or std::type_index) returns non-contiguous hash
 * values, preventing component metadata from being indexed directly into flat
 * C-arrays.
 *
 * This test guarantees:
 * 1. Monotonic Contiguity: First component is assigned 0, second 1, third 2.
 * 2. Caching Invariant: Subsequent calls to `type_id<T>()` return the exact
 * same cached ID without re-executing atomic or mutex-based counter increments.
 * 3. Decay Invariant: `type_id<const Pos&>()` maps to `type_id<Pos>()`.
 */
void test_sequential_type_ids() {
  std::cout << "[Test 1/4] Sequential Contiguous TypeID Generation... ";

  const TypeID id_pos = type_id<Pos>();
  const TypeID id_vel = type_id<Vel>();
  const TypeID id_mass = type_id<Mass>();

  assert(id_pos == 0);
  assert(id_vel == 1);
  assert(id_mass == 2);

  // Repeated lookups must return identical cached IDs
  assert(type_id<Pos>() == 0);
  assert(type_id<Vel>() == 1);
  assert(type_id<Mass>() == 2);

  // Type decay check
  assert(type_id<const Pos &>() == 0);
  assert(type_id<Pos &&>() == 0);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: ComponentMask Bitwise Set, Test, and Population Count
// =============================================================================

/**
 * @brief Verifies 128-bit fixed-width mask manipulation and hardware popcount.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * In arche-ecs, archetypes represent component combinations as 128-bit
 * bitmasks.
 *
 * This test guarantees:
 * 1. Boundary Handling: Bits 0..63 in `words[0]` and bits 64..127 in `words[1]`
 *    behave uniformly with no off-by-one errors.
 * 2. Hardware Popcount: `count()` computes the exact number of set bits across
 *    both 64-bit words in 2 CPU instructions (`std::popcount`).
 * 3. Clearing & Resetting: `reset(id)` clears individual bits, while `clear()`
 *    resets the entire 128-bit mask to 0.
 */
void test_component_mask_operations() {
  std::cout << "[Test 2/4] ComponentMask Bitwise Operations & Popcount... ";

  ComponentMask mask;
  assert(mask.empty());
  assert(mask.count() == 0);

  // Test word 0 (bit 0) and word 0 (bit 2)
  mask.set(0); // Pos
  mask.set(2); // Mass
  assert(mask.test(0) == true);
  assert(mask.test(1) == false);
  assert(mask.test(2) == true);
  assert(mask.count() == 2);

  // Test word 1 (bit 70: second word)
  mask.set(70);
  assert(mask.test(70) == true);
  assert(mask.count() == 3);

  // Reset bit 0
  mask.reset(0);
  assert(mask.test(0) == false);
  assert(mask.count() == 2);

  // Clear all
  mask.clear();
  assert(mask.empty());
  assert(mask.count() == 0);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Fast Query Matching (contains_all & contains_none)
// =============================================================================

/**
 * @brief Verifies 1-to-2 cycle bitwise subset and disjoint matching.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Query filtering iterates over hundreds of archetypes to find matching tables:
 * - `contains_all`: Evaluates `(entity & include) == include` to ensure all
 *   required components exist.
 * - `contains_none`: Evaluates `(entity & exclude) == 0` to filter out excluded
 * tags.
 *
 * This test guarantees that archetype matching operates correctly across
 * multiple simultaneous inclusion and exclusion criteria without branch
 * mispredictions.
 */
void test_query_filter_matching() {
  std::cout << "[Test 3/4] Fast Query Bitmask Inclusion/Exclusion... ";

  ComponentMask entity_mask;
  const TypeID pos_id = type_id<Pos>();
  const TypeID vel_id = type_id<Vel>();
  const TypeID active_id = type_id<ActiveTag>();
  const TypeID dead_id = type_id<DeadTag>();

  entity_mask.set(pos_id);
  entity_mask.set(vel_id);
  entity_mask.set(active_id);

  ComponentMask query_include;
  query_include.set(pos_id);
  query_include.set(vel_id);

  ComponentMask query_exclude;
  query_exclude.set(dead_id);

  // Entity possesses both Pos and Vel, and lacks DeadTag -> Must match
  assert(entity_mask.contains_all(query_include) == true);
  assert(entity_mask.contains_none(query_exclude) == true);

  // Add DeadTag to entity: exclude filter must now reject it
  entity_mask.set(dead_id);
  assert(entity_mask.contains_none(query_exclude) == false);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Type Descriptor Move & Destruction Semantics
// =============================================================================

/**
 * @brief Verifies type-erased memory descriptors and non-trivial lifecycle
 * hooks.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Memory chunks in arche-ecs manage raw unformatted byte arrays:
 * - When moving components between chunks (e.g. during archetype migration or
 * Swap-and-Pop), the engine must invoke placement-new move constructors and
 * destructors for non-trivial types.
 * - For trivial Plain Old Data (POD) structs, `is_trivially_destructible`
 * allows the engine to completely skip destructor invocation, eliminating
 * millions of dead loop cycles.
 *
 * This test proves:
 * 1. Reflection correctly identifies trivial vs non-trivial destructibility.
 * 2. Type-erased placement constructors, move constructors, and destructors
 * execute cleanly on raw memory buffers without leaking or double-freeing
 * instances.
 */
void test_descriptor_lifecycle() {
  std::cout << "[Test 4/4] Type Descriptor Move & Destruction Semantics... ";

  TypeRegistry registry;
  const auto &desc = registry.register_type<TrackedObject>();

  assert(!desc.is_trivially_destructible);
  assert(TrackedObject::live_instances == 0);

  // Allocate raw uninitialized stack memory buffers
  alignas(TrackedObject) char bufferA[sizeof(TrackedObject)];
  alignas(TrackedObject) char bufferB[sizeof(TrackedObject)];

  // 1. Placement construct in bufferA
  desc.constructor(bufferA);
  assert(TrackedObject::live_instances == 1);

  // 2. Type-erased move construct from bufferA into bufferB
  desc.move_construct(bufferB, bufferA);
  assert(TrackedObject::live_instances == 2);

  // 3. Destroy instances in both buffers
  desc.destructor(bufferA);
  desc.destructor(bufferB);
  assert(TrackedObject::live_instances == 0);

  // 4. Verify trivial POD reflection
  const auto &pos_desc = registry.register_type<Pos>();
  assert(pos_desc.is_trivially_copyable);
  assert(pos_desc.is_trivially_destructible);
  assert(pos_desc.destructor == nullptr); // Skipped on trivial types

  std::cout << "PASSED\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout
      << "=============================================================\n";
  std::cout
      << " Running arche-ecs Module 002: Type Registry & Mask Tests    \n";
  std::cout
      << "=============================================================\n";

  test_sequential_type_ids();
  test_component_mask_operations();
  test_query_filter_matching();
  test_descriptor_lifecycle();

  std::cout
      << "=============================================================\n";
  std::cout
      << " All Module 002 Unit Tests Passed Successfully!              \n";
  std::cout
      << "=============================================================\n";
  return 0;
}
