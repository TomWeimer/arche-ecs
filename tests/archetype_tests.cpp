#include "arche/archetype.hpp"
#include "arche/archetype_graph.hpp"
#include "arche/component_mask.hpp"
#include "arche/entity.hpp"
#include "arche/type_registry.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
using arche::Archetype;
using arche::ArchetypeGraph;
using arche::ComponentMask;
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

struct Mass {
  float value{1.0f};
};

struct Color {
  uint8_t r{255};
  uint8_t g{255};
  uint8_t b{255};
  uint8_t a{255};
};

struct alignas(16) LargeTag {
  char buffer[128]{};
};

// =============================================================================
// Unit Test 1: Root Initialization & Empty Archetype Invariants
// =============================================================================

/**
 * @brief Verifies the canonical root node initialization and empty state
 * contracts.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * The ArchetypeGraph must have a unique, well-defined root node representing
 * entities with zero components:
 * 1. Root Archetype ID is guaranteed to be 0.
 * 2. Component count is exactly 0 and its ComponentMask has zero bits set
 * (`none()`).
 * 3. The graph begins with exactly 1 instantiated archetype (the root itself).
 * 4. Root pointer stability: The root pointer remains valid for the graph's
 * lifetime.
 */
void test_root_initialization() {
  std::cout << "[Test 1/6] Root Initialization & Empty Archetype... ";

  TypeRegistry registry;
  ArchetypeGraph graph(registry);

  Archetype *root = graph.root();
  assert(root != nullptr);
  assert(root->id() == 0);
  assert(root->component_count() == 0);
  assert(root->mask().empty());
  assert(graph.archetype_count() == 1);
  assert(graph.archetypes().size() == 1);
  assert(graph.archetypes()[0].get() == root);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: Single Component Addition & Bidirectional Edge Caching
// =============================================================================

/**
 * @brief Verifies transition edge discovery, cold traversal, and bidirectional
 * caching.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Dynamic entity mutation relies on pre-cached edges to avoid repeated hash
 * lookups:
 * 1. Cold Traversal: First transition from Root adding `Position` creates
 * Archetype 1.
 * 2. Warm Traversal: Second transition traverses the pre-cached edge pointer in
 * O(1) time and returns the identical Archetype pointer.
 * 3. Bidirectional Symmetry: The forward edge `Root + Position -> Arch_Pos`
 * automatically wires the reverse edge `Arch_Pos - Position -> Root`.
 */
void test_single_addition_and_caching() {
  std::cout
      << "[Test 2/6] Single Component Addition & Bidirectional Caching... ";

  TypeRegistry registry;
  registry.register_type<Position>();
  registry.register_type<Velocity>();

  ArchetypeGraph graph(registry);
  Archetype *root = graph.root();

  const TypeID pos_t = arche::type_id<Position>();
  const TypeID vel_t = arche::type_id<Velocity>();

  // 1. Cold traversal: First addition of Position (creates target archetype)
  Archetype *arch_pos = graph.traverse_add(root, pos_t);
  assert(arch_pos != nullptr);
  assert(arch_pos != root);
  assert(arch_pos->component_count() == 1);
  assert(arch_pos->has_component(pos_t));
  assert(!arch_pos->has_component(vel_t));
  assert(graph.archetype_count() == 2);

  // 2. Warm traversal: Direct cache hit must return exact same pointer
  Archetype *cached_pos = graph.traverse_add(root, pos_t);
  assert(cached_pos == arch_pos);

  // 3. Reverse edge verification: Target must point back to Root on removal
  Archetype *back_root = arch_pos->get_remove_edge(pos_t);
  assert(back_root == root);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Diamond Convergence (Commutative Addition)
// =============================================================================

/**
 * @brief Verifies that archetype addition transitions commute to the exact same
 * node.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Archetype signatures are sets, meaning order of addition must not affect
 * identity:
 * - Path 1: Root -> (+Position) -> Arch(Pos) -> (+Velocity) -> Arch(Pos, Vel)
 * - Path 2: Root -> (+Velocity) -> Arch(Vel) -> (+Position) -> Arch(Pos, Vel)
 *
 * Both paths MUST converge to the identical canonical Archetype pointer
 * instance. Moreover, all reverse edges along both paths must point to the
 * respective intermediates.
 */
void test_diamond_convergence() {
  std::cout << "[Test 3/6] Diamond Convergence (Commutative Addition)... ";

  TypeRegistry registry;
  registry.register_type<Position>();
  registry.register_type<Velocity>();

  ArchetypeGraph graph(registry);
  Archetype *root = graph.root();

  const TypeID pos_t = arche::type_id<Position>();
  const TypeID vel_t = arche::type_id<Velocity>();

  // Path 1: Root -> +Position -> +Velocity
  Archetype *p1_pos = graph.traverse_add(root, pos_t);
  Archetype *p1_final = graph.traverse_add(p1_pos, vel_t);

  // Path 2: Root -> +Velocity -> +Position
  Archetype *p2_vel = graph.traverse_add(root, vel_t);
  Archetype *p2_final = graph.traverse_add(p2_vel, pos_t);

  // Both paths must converge to the exact same archetype pointer
  assert(p1_final == p2_final);
  assert(p1_final->component_count() == 2);
  assert(p1_final->has_component(pos_t));
  assert(p1_final->has_component(vel_t));

  // Verify reverse edges
  assert(p1_final->get_remove_edge(vel_t) == p1_pos);
  assert(p1_final->get_remove_edge(pos_t) == p2_vel);

  // Total archetypes created: Root(0), Pos(1), Vel(2), PosVel(3)
  assert(graph.archetype_count() == 4);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Component Removal Transitions
// =============================================================================

/**
 * @brief Verifies descending traversals through removal edges down to root.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * When entities shed components, `traverse_remove()` must safely locate or
 * create subsets, wire bidirectional edges, and eventually return to the
 * canonical root when the last component is removed.
 */
void test_removal_transitions() {
  std::cout << "[Test 4/6] Component Removal Transitions... ";

  TypeRegistry registry;
  registry.register_type<Position>();
  registry.register_type<Velocity>();
  registry.register_type<Mass>();

  ArchetypeGraph graph(registry);
  const TypeID pos_t = arche::type_id<Position>();
  const TypeID vel_t = arche::type_id<Velocity>();
  const TypeID mass_t = arche::type_id<Mass>();

  // Create 3-component archetype directly via get_or_create
  std::vector<TypeID> types = {pos_t, vel_t, mass_t};
  Archetype *full = graph.get_or_create(types);
  assert(full->component_count() == 3);

  // 1. Remove Mass: (Pos, Vel, Mass) -> (Pos, Vel)
  Archetype *minus_mass = graph.traverse_remove(full, mass_t);
  assert(minus_mass->component_count() == 2);
  assert(!minus_mass->has_component(mass_t));
  assert(minus_mass->has_component(pos_t));
  assert(minus_mass->has_component(vel_t));

  // 2. Remove Velocity: (Pos, Vel) -> (Pos)
  Archetype *only_pos = graph.traverse_remove(minus_mass, vel_t);
  assert(only_pos->component_count() == 1);
  assert(only_pos->has_component(pos_t));
  assert(!only_pos->has_component(vel_t));

  // 3. Remove Position: (Pos) -> Root (Empty)
  Archetype *empty = graph.traverse_remove(only_pos, pos_t);
  assert(empty == graph.root());
  assert(empty->component_count() == 0);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 5: Chunk Layout & Column Offset Calculation
// =============================================================================

/**
 * @brief Verifies that column layouts align strictly to component alignment
 * boundaries.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Archetype chunk layouts are dense SoA (Structure of Arrays) slices inside
 * page-sized buffers. Each column must start at an offset satisfying: offset %
 * alignment == 0 to avoid unaligned SIMD/CPU memory access penalties or bus
 * faults.
 */
void test_chunk_layout_and_alignment() {
  std::cout << "[Test 5/6] Chunk Layout & Column Offset Calculation... ";

  TypeRegistry registry;
  registry.register_type<Position>();
  registry.register_type<Color>();
  registry.register_type<LargeTag>();

  const uint32_t chunk_size = 16384; // 16 KB chunk test
  ArchetypeGraph graph(registry, chunk_size);

  const TypeID pos_t = arche::type_id<Position>();
  const TypeID col_t = arche::type_id<Color>();
  const TypeID tag_t = arche::type_id<LargeTag>();

  std::vector<TypeID> types = {pos_t, col_t, tag_t};
  Archetype *arch = graph.get_or_create(types);
  assert(arch->chunk_capacity() > 0);

  // Column count matches component count
  assert(arch->component_count() == 3);

  // Verify column ordering matches strictly sorted TypeIDs
  for (size_t i = 1; i < arch->component_count(); ++i) {
    assert(arch->column_layout(i - 1).type_id < arch->column_layout(i).type_id);
  }

  // Verify each column begins on a strictly aligned byte boundary
  for (size_t i = 0; i < arch->component_count(); ++i) {
    const auto &col = arch->column_layout(i);
    assert(col.alignment > 0);
    assert(col.chunk_byte_offset % col.alignment == 0);
  }

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 6: Transition Traversal Speed (L1 Cache Hit Benchmark)
// =============================================================================

/**
 * @brief Measures the latency of an O(1) cached pointer-hop edge traversal.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * In hot simulation loops with 100k+ mutations per frame, transition lookups
 * must resolve in single-digit nanoseconds (1–3 clock cycles) via flat array
 * indexing.
 */
void test_transition_traversal_speed() {
  std::cout << "[Test 6/6] Transition Traversal Benchmark (L1 Cache Hit)... ";

  TypeRegistry registry;
  registry.register_type<Position>();
  registry.register_type<Velocity>();

  ArchetypeGraph graph(registry);
  Archetype *root = graph.root();
  const TypeID pos_t = arche::type_id<Position>();

  Archetype *arch_pos = graph.traverse_add(root, pos_t);

  constexpr int ITERATIONS = 5'000'000;
  auto start = std::chrono::high_resolution_clock::now();

  Archetype *sink = nullptr;
  for (int i = 0; i < ITERATIONS; ++i) {
    sink = root->get_add_edge(pos_t);
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : "+r"(sink));
#endif
  }

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::nano> elapsed = end - start;
  double ns_per_lookup = elapsed.count() / static_cast<double>(ITERATIONS);

  assert(sink == arch_pos);
  std::cout << "PASSED (" << ns_per_lookup << " ns per pointer-hop)\n";
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Archetype Graph Unit & Perf Tests\n";
  std::cout << "====================================================\n";

  test_root_initialization();
  test_single_addition_and_caching();
  test_diamond_convergence();
  test_removal_transitions();
  test_chunk_layout_and_alignment();
  test_transition_traversal_speed();

  std::cout << "All Archetype Graph tests completed successfully!\n";
  return 0;
}
