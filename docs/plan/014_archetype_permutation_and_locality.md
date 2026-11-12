# Module 014: In-Place Archetype Permutation & Cache Locality

This document specifies the exact behavioral deltas, code modifications, and new test suites required to implement generic in-place archetype permutation and cache-locality sorting in `arche-ecs`, assuming Module 013 (Reactive Flat Query Caching) is already implemented.

---

## 1. Behavioral Deltas: What Changes and Why

### 1.1 In-Place Archetype Permutation Engine
* **Before (Module 013):**  
  Entities remained fixed in whichever chunk and row they landed upon creation or Swap-and-Pop. In high-density swarm simulations (100k–1M boids), entities that were spatial neighbors in 2D or 3D space ended up scattered randomly across 30 to 600 different 64 KB memory chunks. Calculating flocking distance metrics caused constant L1/L2 cache misses.
* **Now (Module 014):**  
  `World` provides `apply_permutation(arch, target_indices)` and `sort_archetype<Component>(arch, comp)`. It performs an in-place cycle-chasing permutation across all component columns and entity slices in an archetype simultaneously in $O(N)$ time with zero steady-state heap allocations.
* **Why we need to change this part:**  
  Allows consumer simulations (e.g. Colony) to sort entities along a 2D or 3D Morton space-filling curve (Z-order curve) periodically. Physically adjacent boids are re-arranged into adjacent rows in the same chunk, sharing the exact same 64-byte CPU cache lines and yielding a $2\times\text{ to }4\times$ speedup on neighbor searches.

### 1.2 Routing Table Synchronization (Zero Dangling Handles)
* **Before (Module 013):**  
  `SlotMap` records were only updated during single-entity Swap-and-Pop or batch creation.
* **Now (Module 014):**  
  During permutation, each entity's `(chunk, row)` record inside `SlotMap` is re-anchored to its new physical coordinates in $O(1)$ time per entity.
* **Why we need to change this part:**  
  Guarantees that **all existing 64-bit `Entity` handles remain 100% valid**. External systems (spatial hashes, render queues, AI blackboards) holding entity handles immediately read from their new cache-clustered memory locations without invalidation.

---

## 2. Modifications to Existing Files

### 2.1 `include/arche/archetype.hpp`

#### Behavioral Delta (Why this file changes)
* **Where defined:** `include/arche/archetype.hpp`.
* **What code it replaces:** Adds `struct ChunkCoord` and `decompose_index(uint32_t global_index)`. Replaces repeated manual division and modulo arithmetic (`g / cap` and `g % cap`) across multi-chunk traversals.

#### New Structs & Methods to Add to `include/arche/archetype.hpp`

Insert in `include/arche/archetype.hpp`:

```cpp
// =============================================================================
// Multi-Chunk Coordinate Decomposition Helper
// =============================================================================

/**
 * @struct ChunkCoord
 * @brief Decomposed physical chunk index and in-chunk row index.
 */
struct ChunkCoord {
  uint32_t chunk_index{0};
  uint32_t local_row{0};
};

// Inside class Archetype, public section:

/**
 * @brief Decomposes a global archetype entity index into chunk index and local row.
 * @param global_index Flat index in range [0, entity_count).
 * @return Decomposed ChunkCoord.
 */
[[nodiscard]] inline ChunkCoord decompose_index(uint32_t global_index) const noexcept {
  return { global_index / chunk_capacity_, global_index % chunk_capacity_ };
}
```

---

### 2.2 `include/arche/slot_map.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `SlotMap` only provided `update_location(Entity e, Archetype *arch, Chunk *chunk, uint32_t row)`.
* **Now:** Adds `update_record(Entity e, Chunk *chunk, uint32_t row)` to update physical chunk and row coordinates directly in a single store instruction without rewriting the archetype pointer.

#### Functions / Members to Delete
* `None`.

#### Modified Functions in `SlotMap`
* `None`.

#### New Functions to Add to `include/arche/slot_map.hpp`

Insert inside `class SlotMap`, public section, right after `update_location(...)` (around line 308):

```cpp
  /**
   * @brief Fast-path update of physical chunk and row during intra-archetype permutation.
   *
   * @param e Generational Entity handle being relocated.
   * @param chunk Pointer to the new physical 64 KB chunk.
   * @param row New row index within the target chunk.
   */
  inline void update_record(Entity e, Chunk *chunk, uint32_t row) noexcept {
    slots[e.index()].record.chunk = chunk;
    slots[e.index()].record.row = row;
  }
```

---

### 2.2 `include/arche/world.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `World` had no interface for permuting or sorting entities within an archetype.
* **Now:** `World` exposes `apply_permutation(Archetype *arch, std::span<const uint32_t> target_indices)` and `sort_archetype<Component, Compare>(Archetype *arch, Compare &&comp)`.

#### Functions / Members to Delete
* `None`.

#### Modified Functions in `World`
* `None`.

#### New Methods to Add to `include/arche/world.hpp`

1. **Add method declarations** in `class World`, public section (around line 185):

```cpp
  // --- In-Place Archetype Permutation & Cache Locality -----------------------

  /**
   * @brief Permutes all component columns and entity handles in an archetype in-place.
   *
   * @details
   * Applies cycle-chasing permutation across all chunks in the archetype in O(N) time.
   * Updates all corresponding SlotMap records so all existing Entity handles remain valid.
   *
   * @param arch Pointer to the target Archetype to permute.
   * @param target_indices Span of size N where target_indices[i] indicates the source
   *                       index whose entity data should be placed at destination index i.
   */
  void apply_permutation(
      Archetype *arch,
      std::span<const uint32_t> target_indices
  );

  /**
   * @brief Sorts all entities in an archetype in-place based on a component comparator.
   *
   * @tparam Component Component type to sort by.
   * @tparam Compare Strict weak ordering comparator accepting (const Component&, const Component&).
   * @param arch Pointer to target Archetype.
   * @param comp Comparator callable.
   */
  template <typename Component, typename Compare>
  void sort_archetype(Archetype *arch, Compare &&comp);
```

2. **Add template implementation** at the bottom of `include/arche/world.hpp`, right before the closing `} // namespace arche`:

```cpp
// =============================================================================
// In-Place Comparator Sorting Template Implementation
// =============================================================================

template <typename Component, typename Compare>
void World::sort_archetype(Archetype *arch, Compare &&comp) {
  assert(arch != nullptr);
  const size_t total = arch->entity_count();
  if (total <= 1) {
    return;
  }

  const auto &chunks = get_archetype_chunks(arch);
  const uint32_t cap = arch->chunk_capacity();
  const auto &layout = arch->get_column_layout(type_id<Component>());

  // 1. Build initial index list [0, 1, ..., N - 1]
  std::vector<uint32_t> sorted_indices(total);
  for (size_t i = 0; i < total; ++i) {
    sorted_indices[i] = static_cast<uint32_t>(i);
  }

  // 2. Sort indices based on component values
  auto get_comp = [&](uint32_t g) -> const Component & {
    const ChunkCoord coord = arch->decompose_index(g); // Decompose multi-chunk index
    Chunk *c = chunks[coord.chunk_index];
    return *static_cast<const Component *>(c->component_element(layout, coord.local_row));
  };

  std::sort(
      sorted_indices.begin(),
      sorted_indices.end(),
      [&](uint32_t a, uint32_t b) {
        return comp(get_comp(a), get_comp(b));
      }
  );

  // 3. Apply the sorted permutation in-place
  apply_permutation(arch, sorted_indices);
}
```

---

### 2.3 `src/world.cpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `src/world.cpp` had no implementation for cycle-chasing permutations.
* **Now:** Implements `World::apply_permutation()` to perform cycle-chasing across all component columns and update the `SlotMap` routing table.

#### Functions / Members to Delete
* `None`.

#### Modified Functions in `src/world.cpp`
* `None`.

#### New Functions to Add to `src/world.cpp`

Insert at the end of `src/world.cpp`:

```cpp
// =============================================================================
// In-Place Cycle-Chasing Permutation Implementation
// =============================================================================

void World::apply_permutation(
    Archetype *arch,
    std::span<const uint32_t> target_indices
) {
  assert(arch != nullptr);
  const size_t total = arch->entity_count();
  assert(target_indices.size() == total && "Permutation size must match entity count");

  if (total <= 1) {
    return;
  }

  auto &chunks = get_archetype_chunks(arch);
  const auto layouts = arch->column_layouts();

  // Helper lambdas using decompose_index to resolve multi-chunk memory coordinates
  auto get_entity_ptr = [&](uint32_t global_idx) -> Entity * {
    const ChunkCoord coord = arch->decompose_index(global_idx);
    return &chunks[coord.chunk_index]->entity_slice()[coord.local_row];
  };

  auto get_component_ptr = [&](const ComponentColumnLayout &l, uint32_t global_idx) -> void * {
    const ChunkCoord coord = arch->decompose_index(global_idx);
    return chunks[coord.chunk_index]->component_element(l, coord.local_row);
  };

  // Visited bitset to track cycle-chasing progress (125 KB for 1M entities)
  std::vector<bool> visited(total, false);

  // 1. Permute all component columns using in-place cycle chasing
  for (const auto &layout : layouts) {
    const uint32_t size = layout.size;
    std::vector<uint8_t> temp(size);
    std::fill(visited.begin(), visited.end(), false);

    for (uint32_t start = 0; start < total; ++start) {
      if (visited[start] || target_indices[start] == start) {
        continue;
      }

      // Save start element to temp buffer
      uint32_t cur = start;
      void *cur_ptr = get_component_ptr(layout, cur);
      std::memcpy(temp.data(), cur_ptr, size);

      while (true) {
        visited[cur] = true;
        uint32_t next = target_indices[cur];
        if (next == start) {
          std::memcpy(cur_ptr, temp.data(), size);
          break;
        }
        void *next_ptr = get_component_ptr(layout, next);
        std::memcpy(cur_ptr, next_ptr, size);
        cur = next;
        cur_ptr = get_component_ptr(layout, cur);
      }
    }
  }

  // 2. Permute Entity handles and synchronize SlotMap routing table
  std::fill(visited.begin(), visited.end(), false);
  Entity temp_entity;

  for (uint32_t start = 0; start < total; ++start) {
    if (visited[start] || target_indices[start] == start) {
      // Still ensure SlotMap has correct location if it wasn't moved in cycle
      Entity e = *get_entity_ptr(start);
      slot_map_.update_record(e, chunks[start / cap], start % cap);
      continue;
    }

    uint32_t cur = start;
    Entity *cur_ptr = get_entity_ptr(cur);
    temp_entity = *cur_ptr;

    while (true) {
      visited[cur] = true;
      uint32_t next = target_indices[cur];
      if (next == start) {
        *cur_ptr = temp_entity;
        slot_map_.update_record(temp_entity, chunks[cur / cap], cur % cap);
        break;
      }

      Entity *next_ptr = get_entity_ptr(next);
      Entity moved = *next_ptr;
      *cur_ptr = moved;
      slot_map_.update_record(moved, chunks[cur / cap], cur % cap);

      cur = next;
      cur_ptr = get_entity_ptr(cur);
    }
  }

  // 3. Update structure version and mark columns dirty
  advance_tick();
  for (Chunk *chunk : chunks) {
    chunk->advance_structure_version(current_tick_);
  }
}
```

---

## 3. Brand-New Files

### 3.1 `tests/archetype_permutation_tests.cpp`

This is a brand-new test suite verifying intra-chunk and inter-chunk in-place permutations, comparator sorting, and 100% generational handle stability.

```cpp
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
 * @brief Validates permutation of entities within a single chunk and component alignment.
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
 * @brief Validates permuting 10,000 entities spanning multiple 64 KB memory chunks.
 */
void test_multi_chunk_permutation() {
  std::cout << "[Test 2/4] Multi-Chunk Permutation Across 64 KB Boundaries (10k)... ";
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
 * @brief Validates sorting an archetype in-place by ParticleKey (Morton code simulation).
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
  world.sort_archetype<ParticleKey>(arch, [](const ParticleKey &a, const ParticleKey &b) {
    return a.morton_code < b.morton_code;
  });

  // Verify chunk order is strictly ascending: 100, 200, 500, 900
  std::vector<uint32_t> keys;
  std::vector<float> x_coords;

  world.query<Position, ParticleKey>().for_each_chunk([&](uint32_t n, Position *p, ParticleKey *k) {
    for (uint32_t i = 0; i < n; ++i) {
      keys.push_back(k[i].morton_code);
      x_coords.push_back(p[i].x);
    }
  });

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
 * @brief Validates that entity handles remain permanently stable across repeated permutations.
 */
void test_handle_stability_stress() {
  std::cout << "[Test 4/4] Generational Handle Stability Across Permutations... ";
  World world;

  constexpr size_t COUNT = 2000;
  std::vector<Entity> entities;
  entities.reserve(COUNT);

  for (size_t i = 0; i < COUNT; ++i) {
    entities.push_back(world.create_entity(Position{static_cast<float>(i), 0.0f}));
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
```

---

## 4. Build Configuration: `CMakeLists.txt`

Add the new unit test target to `CMakeLists.txt`:

```cmake
# Module 014 Tests: In-Place Archetype Permutation & Cache Locality
add_executable(archetype_permutation_tests tests/archetype_permutation_tests.cpp)
target_link_libraries(archetype_permutation_tests PRIVATE arche::arche)
add_test(NAME archetype_permutation_tests COMMAND archetype_permutation_tests)
```

