#include "arche/entity.hpp"
#include "arche/query.hpp"
#include "arche/world.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

namespace {
using arche::Archetype;
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

struct ParticleKey {
  uint32_t morton_code{0};
};

// =============================================================================
// Unit Test 1: Single-Chunk In-Place Permutation
// =============================================================================

/**
 * @brief Validates permutation of entities within a single chunk and component
 * alignment.
 */
void test_single_chunk_permutation() {
  std::cout << "[Test 1/4] Single-Chunk In-Place Permutation... ";
  World world;

  constexpr size_t COUNT = 5;
  std::vector<Entity> entities;
  for (size_t i = 0; i < COUNT; ++i) {
    entities.push_back(world.create_entity(
        Position{static_cast<float>(i), 0.0f},
        Velocity{static_cast<float>(i * 10), 0.0f}
    ));
  }

  // Reverse permutation: [4, 3, 2, 1, 0]
  std::vector<uint32_t> perm = {4, 3, 2, 1, 0};
  Archetype *arch = world.get_record(entities[0]).archetype;

  world.apply_permutation(arch, perm);

  // Verify all entity handles remain alive and point to their permuted values
  for (size_t i = 0; i < COUNT; ++i) {
    assert(world.is_valid(entities[i]));
    const auto &pos = world.get_component<Position>(entities[i]);
    const auto &vel = world.get_component<Velocity>(entities[i]);
    assert(pos.x == static_cast<float>(i));
    assert(vel.vx == static_cast<float>(i * 10));
  }

  // Verify query traverses in permuted order
  std::vector<float> found_x;
  world.query<Position>().for_each_chunk([&](uint32_t n, Position *p) {
    for (uint32_t i = 0; i < n; ++i) {
      found_x.push_back(p[i].x);
    }
  });

  assert(found_x.size() == COUNT);
  assert(found_x[0] == 4.0f);
  assert(found_x[1] == 3.0f);
  assert(found_x[2] == 2.0f);
  assert(found_x[3] == 1.0f);
  assert(found_x[4] == 0.0f);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: Multi-Chunk Permutation Across Boundaries
// =============================================================================

/**
 * @brief Validates permuting 10,000 entities spanning multiple 64 KB memory
 * chunks.
 */
void test_multi_chunk_permutation() {
  std::cout
      << "[Test 2/4] Multi-Chunk Permutation Across 64 KB Boundaries (10k)... ";
  World world;

  constexpr size_t COUNT = 10'000;
  std::vector<Entity> entities;
  entities.reserve(COUNT);

  for (size_t i = 0; i < COUNT; ++i) {
    entities.push_back(world.create_entity(
        Position{static_cast<float>(i), static_cast<float>(i * 2)},
        Velocity{1.0f, -1.0f}
    ));
  }

  // Reverse all 10,000 entities across chunks
  std::vector<uint32_t> perm(COUNT);
  for (size_t i = 0; i < COUNT; ++i) {
    perm[i] = static_cast<uint32_t>(COUNT - 1 - i);
  }

  Archetype *arch = world.get_record(entities[0]).archetype;

  auto start = std::chrono::high_resolution_clock::now();
  world.apply_permutation(arch, perm);
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> duration = end - start;

  // Verify bit-exact integrity for all 10,000 handles
  for (size_t i = 0; i < COUNT; ++i) {
    assert(world.is_valid(entities[i]));
    const auto &pos = world.get_component<Position>(entities[i]);
    assert(pos.x == static_cast<float>(i));
    assert(pos.y == static_cast<float>(i * 2));
  }

  std::cout << "PASSED (" << duration.count() << " ms)\n";
}

// =============================================================================
// Unit Test 3: In-Place Comparator Sorting (Morton Code Ordering)
// =============================================================================

/**
 * @brief Validates sorting an archetype in-place by ParticleKey (Morton code
 * simulation).
 */
void test_comparator_sorting() {
  std::cout << "[Test 3/4] In-Place Comparator Sorting (Morton Key)... ";
  World world;

  // Create entities with scrambled Morton codes
  world.create_entity(Position{0.0f, 0.0f}, ParticleKey{500});
  world.create_entity(Position{1.0f, 1.0f}, ParticleKey{100});
  world.create_entity(Position{2.0f, 2.0f}, ParticleKey{900});
  world.create_entity(Position{3.0f, 3.0f}, ParticleKey{200});

  Archetype *arch = world.graph().archetypes().back().get();

  // Sort by ParticleKey::morton_code ascending
  world.sort_archetype<ParticleKey>(
      arch, [](const ParticleKey &a, const ParticleKey &b) {
        return a.morton_code < b.morton_code;
      }
  );

  // Verify chunk order is strictly ascending: 100, 200, 500, 900
  std::vector<uint32_t> keys;
  std::vector<float> x_coords;

  world.query<Position, ParticleKey>().for_each_chunk(
      [&](uint32_t n, Position *p, ParticleKey *k) {
        for (uint32_t i = 0; i < n; ++i) {
          keys.push_back(k[i].morton_code);
          x_coords.push_back(p[i].x);
        }
      }
  );

  assert(keys.size() == 4);
  assert(keys[0] == 100 && x_coords[0] == 1.0f);
  assert(keys[1] == 200 && x_coords[1] == 3.0f);
  assert(keys[2] == 500 && x_coords[2] == 0.0f);
  assert(keys[3] == 900 && x_coords[3] == 2.0f);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Generational Handle Stability Stress Test
// =============================================================================

/**
 * @brief Validates that entity handles remain permanently stable across
 * repeated permutations.
 */
void test_handle_stability_stress() {
  std::cout
      << "[Test 4/4] Generational Handle Stability Across Permutations... ";
  World world;

  constexpr size_t COUNT = 2000;
  std::vector<Entity> entities;
  entities.reserve(COUNT);

  for (size_t i = 0; i < COUNT; ++i) {
    entities.push_back(
        world.create_entity(Position{static_cast<float>(i), 0.0f})
    );
  }

  Archetype *arch = world.get_record(entities[0]).archetype;

  // Run 5 random permutations
  std::vector<uint32_t> perm(COUNT);
  std::iota(perm.begin(), perm.end(), 0);

  for (int iter = 0; iter < 5; ++iter) {
    std::reverse(perm.begin(), perm.end());
    world.apply_permutation(arch, perm);

    // Verify all 2,000 entities still resolve correctly
    for (size_t i = 0; i < COUNT; ++i) {
      assert(world.is_valid(entities[i]));
      const auto &pos = world.get_component<Position>(entities[i]);
      assert(pos.x == static_cast<float>(i));
    }
  }

  std::cout << "PASSED\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Module 014: Permutation Tests\n";
  std::cout << "====================================================\n";

  test_single_chunk_permutation();
  test_multi_chunk_permutation();
  test_comparator_sorting();
  test_handle_stability_stress();

  std::cout << "====================================================\n";
  std::cout << "All Module 014 tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}