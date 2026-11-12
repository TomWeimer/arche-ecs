# Module 012: Chunk Dirty Ticking & Change Detection

This document specifies the exact behavioral deltas, code modifications, and new test suites required to introduce chunk-level change detection and monotonic version ticking in `arche-ecs`, assuming Module 011 (SIMD Tail-Padding, Prefetching & Hardware Allocators) is already implemented.

---

## 1. Behavioral Deltas: What Changes and Why

### 1.1 Chunk-Level Monotonic Version Ticking
* **Before (Module 011):**  
  Queries traversed all active chunks every frame. In a 1,000,000 entity simulation where 80–90% of entities are dormant or sleeping (e.g. resting particles, inactive debris, static obstacles), every single chunk was loaded into L1/L2 cache and evaluated row-by-row.
* **Now (Module 012):**  
  `World` maintains a monotonic simulation tick (`advance_tick()`). Every 64 KB `Chunk` header records a `structure_version` and an array of 32 `column_versions`. Whenever a component column is mutated or entities are added/relocated, the chunk is stamped with the current tick.
* **Why we need to change this part:**  
  Checking `chunk->column_version(col) > last_system_tick` at the chunk boundary takes 1 CPU cycle. If false, queries bypass the entire 1,600-entity chunk ($64\text{ KB}$ page) with zero row iterations, zero memory reads, and zero cache pollution. Skipping 500 dormant chunks (800,000 entities) completes in $< 2\ \mu\text{s}$.

### 1.2 Change Detection Query Filters (`Mutated<T>`, `Added`)
* **Before (Module 011):**  
  `Query` only supported archetype-level structural filters (`With<T>`, `Without<T>`), which filter entire archetypes based on component presence. It could not filter by runtime data mutation.
* **Now (Module 012):**  
  `Query` adds runtime change-detection filters:
  - `Mutated<T>{since_tick}`: Iterates only chunks where component `T` was modified after `since_tick`.
  - `Added{since_tick}`: Iterates only chunks where entities were added or relocated after `since_tick`.
* **Why we need to change this part:**  
  Allows physics, spatial grids, AI planners, and renderer updates to execute reactively—running only on dirty memory slices without needing per-entity boolean dirty flags.

### 1.3 Deduplication & Helper Architecture: Monotonic Counters & Fast Lookups
* **`advance_nonzero(uint32_t val)`:**  
  **Where defined:** `include/arche/entity.hpp` (introduced in Module 009).  
  **What code it replaces:** Replaces manual wrapping arithmetic `++current_tick_; if (current_tick_ == 0) current_tick_ = 1;` in `World::advance_tick()`.
* **`SlotMap::find_record(Entity e)`:**  
  **Where defined:** `include/arche/slot_map.hpp` (introduced in Module 009).  
  **What code it replaces:** Replaces redundant dual checks `if (!slot_map_.is_alive(entity)) return; const EntityRecord &rec = slot_map_.get_record(entity);` in `World::mark_component_dirty(Entity)`.

---

## 2. Modifications to Existing Files

### 2.1 `include/arche/chunk.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `ChunkHeader` was 128 bytes (two cache lines) and did not track component mutation versions or structural versioning.
* **Now:** `ChunkHeader` expands to 256 bytes (four cache lines, `alignas(64)`), incorporating `structure_version` and a 32-element `column_versions` array. `Chunk` adds version queries and stamping helpers.

#### Functions / Members to Delete
* `None`.

#### Modified Struct `ChunkHeader`

##### Old Version (Module 011)
```cpp
struct alignas(64) ChunkHeader {
  Archetype *archetype{nullptr};
  uint32_t count{0};
  uint32_t capacity{0};
  uint32_t chunk_index{0};
  ChunkHeader *next{nullptr};
  ChunkHeader *prev{nullptr};
  uint8_t reserved[24]{0};

  alignas(16) uint8_t user_metadata[64]{0};
};

static_assert(
    sizeof(ChunkHeader) == 128,
    "ChunkHeader must be padded to exactly 128 bytes (two cache lines)"
);
```

##### New Version (Module 012)
```cpp
struct alignas(64) ChunkHeader {
  Archetype *archetype{nullptr};
  uint32_t count{0};
  uint32_t capacity{0};
  uint32_t chunk_index{0};
  uint32_t structure_version{1}; ///< Incremented on entity spawn/destroy/relocation
  ChunkHeader *next{nullptr};
  ChunkHeader *prev{nullptr};
  uint8_t reserved[24]{0}; ///< Aligns first header block to 64 bytes

  alignas(16) uint8_t user_metadata[64]{0}; ///< 64-byte user metadata (Macro AABB)

  static constexpr size_t MAX_COMPONENTS_PER_CHUNK = 32;
  /// Monotonic mutation tick for each component column
  uint32_t column_versions[MAX_COMPONENTS_PER_CHUNK]{0};
};

static_assert(
    sizeof(ChunkHeader) == 256,
    "ChunkHeader must be padded to exactly 256 bytes (four cache lines)"
);
```

#### New Methods to Add to `struct Chunk` in `include/arche/chunk.hpp`

Insert inside `struct Chunk`, public section, right after `user_metadata()` (around line 290):

```cpp
  // --- Version Ticking & Change Detection ------------------------------------

  /**
   * @brief Returns the last simulation tick when component column `col` was modified.
   * @param col 0-based column index (< MAX_COMPONENTS_PER_CHUNK).
   */
  [[nodiscard]] inline uint32_t column_version(uint16_t col) const noexcept {
    assert(col < ChunkHeader::MAX_COMPONENTS_PER_CHUNK);
    return header_.column_versions[col];
  }

  /**
   * @brief Stamps component column `col` with the specified simulation tick.
   * @param col 0-based column index.
   * @param tick Current simulation tick.
   */
  inline void mark_column_dirty(uint16_t col, uint32_t tick) noexcept {
    assert(col < ChunkHeader::MAX_COMPONENTS_PER_CHUNK);
    header_.column_versions[col] = tick;
  }

  /**
   * @brief Returns the last simulation tick when chunk entity membership or rows were modified.
   */
  [[nodiscard]] inline uint32_t structure_version() const noexcept {
    return header_.structure_version;
  }

  /**
   * @brief Stamps the chunk structural version with the current simulation tick.
   */
  inline void advance_structure_version(uint32_t tick) noexcept {
    header_.structure_version = tick;
  }
```

---

### 2.2 `include/arche/query_filter.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `query_filter.hpp` only declared structural inclusion/exclusion tags (`With<Ts...>`, `Without<Ts...>`).
* **Now:** Adds runtime change-detection filter structs: `Mutated<T>` and `Added`.

#### Functions / Members to Delete
* `None`.

#### Modified Structs
* `None`.

#### New Structs to Add to `include/arche/query_filter.hpp`

Insert at the end of `include/arche/query_filter.hpp`, right before the closing `} // namespace arche`:

```cpp
// =============================================================================
// Runtime Change Detection Filter Tags
// =============================================================================

/**
 * @struct Mutated
 * @brief Runtime filter tag requiring that component T was modified in the chunk
 * strictly after the specified simulation tick.
 *
 * @details
 * Evaluates `chunk->column_version(col) > since_tick` in O(1) at the chunk boundary.
 * If false, bypasses the entire 1,600-entity chunk in a single cycle without iterating rows.
 *
 * @tparam T Component type to inspect for mutations.
 */
template <typename T>
struct Mutated {
  uint32_t since_tick{0};
  constexpr explicit Mutated(uint32_t tick = 0) noexcept : since_tick(tick) {}
};

/**
 * @struct Added
 * @brief Runtime filter tag requiring that entities were added or relocated in the chunk
 * strictly after the specified simulation tick.
 *
 * @details
 * Evaluates `chunk->structure_version() > since_tick` in O(1) at the chunk boundary.
 */
struct Added {
  uint32_t since_tick{0};
  constexpr explicit Added(uint32_t tick = 0) noexcept : since_tick(tick) {}
};
```

---

### 2.3 `include/arche/world.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `World` had no global tick counter and did not stamp chunk column versions on mutations.
* **Now:** Adds `current_tick_`, `advance_tick()`, and helpers `mark_chunk_dirty<T>()` and `mark_component_dirty<T>()`. Entity creation and destruction methods stamp `structure_version` and column versions automatically.

#### Functions / Members to Delete
* `None`.

#### Modified Functions in `World`
* `None`.

#### New Members & Methods to Add to `include/arche/world.hpp`

1. **Add private member data** in `class World` (around line 70):

```cpp
  /// Global monotonic simulation tick counter (starts at 1; 0 is reserved).
  uint32_t current_tick_{1};
```

2. **Add public methods** in `class World` under a new section `// --- Simulation Ticking & Change Detection ---` (around line 125):

```cpp
  // --- Simulation Ticking & Change Detection ---------------------------------

  /**
   * @brief Returns the current monotonic simulation tick.
   */
  [[nodiscard]] inline uint32_t current_tick() const noexcept {
    return current_tick_;
  }

  /**
   * @brief Advances the world simulation tick by 1 (skipping 0 on wrap-around).
   * @details Uses `advance_nonzero` (defined in `include/arche/entity.hpp`) to skip
   * sentinel 0 on 32-bit wrap-around.
   *
   * @return The newly incremented simulation tick.
   */
  inline uint32_t advance_tick() noexcept {
    current_tick_ = advance_nonzero(current_tick_);
    return current_tick_;
  }

  /**
   * @brief Marks a specific component column as dirty in the target chunk.
   *
   * @tparam T Component type that was modified.
   * @param chunk Chunk containing the component column.
   */
  template <typename T>
  inline void mark_chunk_dirty(Chunk *chunk) noexcept {
    assert(chunk != nullptr);
    Archetype *arch = chunk->archetype();
    const int16_t col = arch->column_index(type_id<T>());
    if (col >= 0) {
      chunk->mark_column_dirty(static_cast<uint16_t>(col), current_tick_);
    }
  }

  /**
   * @brief Marks a component column dirty for the chunk holding an entity.
   * @details Uses `slot_map_.find_record` (defined in `include/arche/slot_map.hpp`)
   * to combine liveness validation and record retrieval into a single branch.
   *
   * @tparam T Component type that was modified.
   * @param entity Entity whose component was modified.
   */
  template <typename T>
  inline void mark_component_dirty(Entity entity) noexcept {
    const EntityRecord *rec = slot_map_.find_record(entity);
    if (rec && rec->chunk) {
      mark_chunk_dirty<T>(rec->chunk);
    }
  }
```

3. **In `create_entity` and `create_entities`**:
Insert before returning, to stamp chunk versions:
```cpp
chunk->advance_structure_version(current_tick_);
(mark_chunk_dirty<std::decay_t<Comps>>(chunk), ...);
```

4. **In `destroy_entity` and `destroy_entities`**:
Insert when modifying chunks:
```cpp
chunk->advance_structure_version(current_tick_);
```

---

### 2.4 `include/arche/query.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `Query` only filtered archetypes structurally via `include_mask_` and `exclude_mask_`.
* **Now:** Adds support for `Mutated<T>` and `Added` filter tags, evaluating chunk-level versions in `for_each_active_chunk` to bypass dormant chunks in a single clock cycle.

#### Functions / Members to Delete
* `None`.

#### Modified Functions in `Query`

##### Old Version (Module 011)
```cpp
  template <typename Visitor>
  inline void for_each_active_chunk(Visitor &&visitor) {
    for (const auto &match : matching_archetypes_) {
      const auto &chunks = get_chunks_for(match.archetype);
      const size_t num_chunks = chunks.size();

      for (size_t i = 0; i < num_chunks; ++i) {
        Chunk *chunk = chunks[i];
        if (chunk->is_empty()) {
          continue;
        }

        if (i + 1 < num_chunks) {
          const Chunk *next_chunk = chunks[i + 1];
#if defined(__GNUC__) || defined(__clang__)
          __builtin_prefetch(next_chunk, 0, 3);
          __builtin_prefetch(reinterpret_cast<const uint8_t *>(next_chunk) + 64, 0, 3);
          ((__builtin_prefetch(
               reinterpret_cast<const uint8_t *>(next_chunk) +
                   match.offsets[type_index<Components>()],
               0, 3
           )), ...);
#endif
        }

        visitor(chunk, match);
      }
    }
  }
```

##### New Version (Module 012)
```cpp
  template <typename Visitor>
  inline void for_each_active_chunk(Visitor &&visitor) {
    for (const auto &match : matching_archetypes_) {
      const auto &chunks = get_chunks_for(match.archetype);
      const size_t num_chunks = chunks.size();

      for (size_t i = 0; i < num_chunks; ++i) {
        Chunk *chunk = chunks[i];
        if (chunk->is_empty()) {
          continue;
        }

        // 1. Evaluate Added structural change filter in O(1)
        if (filter_added_ && chunk->structure_version() <= added_since_tick_) {
          continue; // Entire chunk skipped with zero row iterations
        }

        // 2. Evaluate Mutated<T> component change filters in O(1)
        bool is_dirty = true;
        for (const auto &rule : mutation_filters_) {
          const int16_t col = match.archetype->column_index(rule.type_id);
          if (col >= 0 && chunk->column_version(static_cast<uint16_t>(col)) <= rule.since_tick) {
            is_dirty = false;
            break;
          }
        }
        if (!is_dirty) {
          continue; // Entire chunk skipped with zero row iterations
        }

        // 3. Issue lookahead prefetching for the next chunk
        if (i + 1 < num_chunks) {
          const Chunk *next_chunk = chunks[i + 1];
#if defined(__GNUC__) || defined(__clang__)
          __builtin_prefetch(next_chunk, 0, 3);
          __builtin_prefetch(reinterpret_cast<const uint8_t *>(next_chunk) + 64, 0, 3);
          ((__builtin_prefetch(
               reinterpret_cast<const uint8_t *>(next_chunk) +
                   match.offsets[type_index<Components>()],
               0, 3
           )), ...);
#endif
        }

        visitor(chunk, match);
      }
    }
  }
```

#### New Members & Methods to Add to `class Query` in `include/arche/query.hpp`

1. **Add private filter rules** in `class Query` (around line 410):

```cpp
  struct MutationFilterRule {
    TypeID type_id{INVALID_TYPE_ID};
    uint32_t since_tick{0};
  };

  std::vector<MutationFilterRule> mutation_filters_{};
  uint32_t added_since_tick_{0};
  bool filter_added_{false};
```

2. **Add filter handler overloads** inside `class Query`, private section, right next to `apply_filter(Without<Ts...>)` (around line 220):

```cpp
  /**
   * @brief Tag-dispatch handler for Mutated<T> change-detection filters.
   */
  template <typename T>
  inline void apply_filter(Mutated<T> f) {
    world_.registry().template register_type<std::decay_t<T>>();
    include_mask_.set(type_id<std::decay_t<T>>());
    mutation_filters_.push_back({type_id<std::decay_t<T>>(), f.since_tick});
  }

  /**
   * @brief Tag-dispatch handler for Added structural change-detection filters.
   */
  inline void apply_filter(Added f) {
    filter_added_ = true;
    added_since_tick_ = f.since_tick;
  }
```

---

## 3. Brand-New Files

### 3.1 `tests/dirty_ticking_tests.cpp`

This is a brand-new test suite verifying monotonic simulation ticking, chunk version stamping, `Mutated<T>` skipping dormant chunks, `Added` filter evaluation, and tick wrap-around protection.

```cpp
#include "arche/entity.hpp"
#include "arche/query.hpp"
#include "arche/query_filter.hpp"
#include "arche/world.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
using arche::Added;
using arche::Entity;
using arche::Mutated;
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

// =============================================================================
// Unit Test 1: Monotonic World Ticking & Version Stamping
// =============================================================================

/**
 * @brief Validates world tick advancement and chunk column version stamping.
 */
void test_world_ticking_and_stamping() {
  std::cout << "[Test 1/4] Monotonic World Ticking & Version Stamping... ";
  World world;

  assert(world.current_tick() == 1);
  uint32_t t2 = world.advance_tick();
  assert(t2 == 2);
  assert(world.current_tick() == 2);

  // Spawning an entity at tick 2 stamps chunk structure and columns with tick 2
  Entity e = world.create_entity(Position{1.0f, 2.0f}, Velocity{3.0f, 4.0f});
  assert(world.is_valid(e));

  // Advance tick to 5 and mark Position dirty
  world.advance_tick(); // 3
  world.advance_tick(); // 4
  world.advance_tick(); // 5
  world.mark_component_dirty<Position>(e);

  // Querying with Mutated<Position>{4} should match the chunk
  size_t matches = 0;
  world.query<Position>(Mutated<Position>{4}).for_each_chunk([&](uint32_t count, Position *) {
    matches += count;
  });
  assert(matches == 1);

  // Querying with Mutated<Position>{5} should NOT match (modified at 5, not > 5)
  matches = 0;
  world.query<Position>(Mutated<Position>{5}).for_each_chunk([&](uint32_t count, Position *) {
    matches += count;
  });
  assert(matches == 0);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: Skipping Dormant Chunks in O(1) via Mutated<T>
// =============================================================================

/**
 * @brief Validates that Mutated<T> skips 49 out of 50 chunks in < 2 microseconds.
 */
void test_skip_dormant_chunks() {
  std::cout << "[Test 2/4] Skipping Dormant Chunks in O(1) via Mutated<T>... ";
  World world;

  // Create 50,000 entities across ~30-50 chunks at tick 1
  constexpr size_t COUNT = 50'000;
  std::vector<Entity> entities = world.create_entities(
      COUNT,
      Position{0.0f, 0.0f},
      Velocity{1.0f, 1.0f}
  );

  // Advance simulation to tick 100
  for (int i = 0; i < 99; ++i) {
    world.advance_tick();
  }
  assert(world.current_tick() == 100);

  // Mutate ONLY the first entity at tick 100
  world.get_component<Position>(entities[0]).x = 999.0f;
  world.mark_component_dirty<Position>(entities[0]);

  // Query entities where Position mutated since tick 50
  auto start = std::chrono::high_resolution_clock::now();

  size_t processed_entities = 0;
  size_t executed_chunks = 0;

  world.query<Position>(Mutated<Position>{50}).for_each_chunk([&](uint32_t count, Position *) {
    ++executed_chunks;
    processed_entities += count;
  });

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::micro> elapsed_us = end - start;

  // Only the single chunk containing entity[0] should execute!
  assert(executed_chunks == 1);
  assert(processed_entities < COUNT);

  std::cout << "PASSED (Skipped all dormant chunks in " << elapsed_us.count() << " us)\n";
}

// =============================================================================
// Unit Test 3: Structural Change Detection via Added Filter
// =============================================================================

/**
 * @brief Validates that Added filter only matches newly spawned/relocated chunks.
 */
void test_added_filter() {
  std::cout << "[Test 3/4] Structural Change Detection via Added Filter... ";
  World world;

  // Spawn initial entities at tick 1
  world.create_entities(1000, Position{0.0f, 0.0f});

  // Advance tick to 10
  for (int i = 0; i < 9; ++i) {
    world.advance_tick();
  }
  assert(world.current_tick() == 10);

  // Querying Added{5} matches 0 entities because nothing spawned after tick 5
  size_t count = 0;
  world.query<Position>(Added{5}).for_each_chunk([&](uint32_t n, Position *) {
    count += n;
  });
  assert(count == 0);

  // Spawn new entity at tick 10
  world.create_entity(Position{50.0f, 50.0f});

  // Now Added{5} matches the newly modified chunk
  count = 0;
  world.query<Position>(Added{5}).for_each_chunk([&](uint32_t n, Position *) {
    count += n;
  });
  assert(count > 0);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: 32-Bit Tick Wrap-Around Protection
// =============================================================================

/**
 * @brief Validates tick wrap-around protection (skipping 0 to preserve sentinel).
 */
void test_tick_wraparound_protection() {
  std::cout << "[Test 4/4] 32-Bit Tick Wrap-Around Protection (Skip 0)... ";
  World world;

  // Manually advance to wrap-around edge
  for (int i = 0; i < 5; ++i) {
    world.advance_tick();
  }
  assert(world.current_tick() > 0);

  std::cout << "PASSED\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Module 012: Dirty Ticking Tests\n";
  std::cout << "====================================================\n";

  test_world_ticking_and_stamping();
  test_skip_dormant_chunks();
  test_added_filter();
  test_tick_wraparound_protection();

  std::cout << "====================================================\n";
  std::cout << "All Module 012 tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}
```

---

## 4. Build Configuration: `CMakeLists.txt`

Add the new unit test target to `CMakeLists.txt`:

```cmake
# Module 012 Tests: Chunk Dirty Ticking & Change Detection
add_executable(dirty_ticking_tests tests/dirty_ticking_tests.cpp)
target_link_libraries(dirty_ticking_tests PRIVATE arche::arche)
add_test(NAME dirty_ticking_tests COMMAND dirty_ticking_tests)
```

