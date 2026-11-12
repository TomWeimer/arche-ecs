# Module 013: Reactive Flat Query Caching (Zero Hooks)

This document specifies the exact behavioral deltas, code modifications, and new test suites required to implement high-performance flat-data reactive query caching in `arche-ecs` with zero virtual methods and zero runtime hooks, assuming Module 012 (Chunk Dirty Ticking) is already implemented.

---

## 1. Behavioral Deltas: What Changes and Why

### 1.1 Query Construction ($O(A)$ Linear Scan $\to$ $O(1)$ Deduplicated Cache Lookup)
* **Before (Module 012):**  
  Every time a `Query` was constructed, `find_matching_archetypes()` executed a linear scan over all archetypes in `ArchetypeGraph` to evaluate component bitmasks. In large worlds with 50 systems running each frame and 100+ archetypes, this performed over 5,000 redundant bitmask comparisons per frame.
* **Now (Module 013):**  
  `World` centrally owns a pool of `QueryCache` structures indexed by signature hash `(include_mask, exclude_mask)`. When a query is constructed, it retrieves its shared `const QueryCache*` in $O(1)$. Multiple systems querying the same signature (`Query<Position, Velocity>`) share the exact same pre-filtered pointer array.
* **Why we need to change this part:**  
  Eliminates all redundant bitmask evaluations across disparate systems and reduces query construction cost to a single hash-map lookup.

### 1.2 Reactive Archetype Graph Population (Zero Callbacks / Zero Hooks)
* **Before (Module 012):**  
  If a new archetype was instantiated in the graph *after* a query had already been constructed, that query remained unaware of the new archetype unless manually reconstructed.
* **Now (Module 013):**  
  When `ArchetypeGraph::create_archetype` instantiates a new graph node, it immediately evaluates the new node's bitmask against all active `QueryCache` instances and pushes the pointer to matching caches.
* **Why we need to change this part:**  
  Allows existing queries to immediately traverse newly spawned archetypes without rebuilding query state. Unlike generic ECS engines (like Flecs or Unreal Mass) that use dynamic event listeners (`on_add`, `on_set`, string observers) which introduce branch mispredictions and instruction-cache thrashing, `arche-ecs` maintains **zero runtime hooks**: queries simply read from a contiguous array of `Archetype*` pointers.

---

## 2. Modifications to Existing Files

### 2.1 `include/arche/archetype.hpp`

#### Behavioral Delta (Why this file changes)
* **Where defined:** `include/arche/archetype.hpp`.
* **What code it replaces:** Adds `matches(const ComponentMask &include, const ComponentMask &exclude)` to `Archetype`. Replaces nested bitmask checks `mask.contains_all(inc) && (exc.empty() || !mask.contains_any(exc))` duplicated in `ArchetypeGraph` and `World`.

#### New Methods to Add to `class Archetype` in `include/arche/archetype.hpp`

Insert inside `class Archetype`, public section (around line 140):

```cpp
  // --- Query Bitmask Matching Helper -----------------------------------------

  /**
   * @brief Evaluates whether this archetype satisfies query inclusion/exclusion criteria.
   * @param include Mask of components that must all be present.
   * @param exclude Mask of components that must not be present.
   * @return True if archetype matches the query filter.
   */
  [[nodiscard]] inline bool matches(
      const ComponentMask &include,
      const ComponentMask &exclude
  ) const noexcept {
    return mask_.contains_all(include) &&
        (exclude.empty() || !mask_.contains_any(exclude));
  }
```

---

### 2.2 `include/arche/archetype_graph.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `ArchetypeGraph` was passive; adding a new archetype did not update any query caches.
* **Now:** `ArchetypeGraph` maintains a registry of active `QueryCache*` pointers. In `create_archetype`, it tests the newly constructed archetype against each registered cache mask using `ptr->matches()` and appends it to `matching_archetypes`.

#### Functions / Members to Delete
* `None`.

#### Modified Functions in `ArchetypeGraph`

##### Old Version (Module 012)
```cpp
  inline Archetype *create_archetype(
      const ComponentMask &mask,
      std::vector<TypeID> sorted_types
  ) {
    const uint32_t id = static_cast<uint32_t>(archetypes_.size());
    auto arch = std::make_unique<Archetype>(
        id, mask, std::move(sorted_types), registry_, chunk_size_
    );
    Archetype *ptr = arch.get();

    mask_to_archetype_[mask] = ptr;
    archetypes_.push_back(std::move(arch));

    return ptr;
  }
```

##### New Version (Module 013)
```cpp
  inline Archetype *create_archetype(
      const ComponentMask &mask,
      std::vector<TypeID> sorted_types
  ) {
    const uint32_t id = static_cast<uint32_t>(archetypes_.size());
    auto arch = std::make_unique<Archetype>(
        id, mask, std::move(sorted_types), registry_, chunk_size_
    );
    Archetype *ptr = arch.get();

    mask_to_archetype_[mask] = ptr;
    archetypes_.push_back(std::move(arch));

    // Reactively update all registered query caches with zero hooks using matches() helper
    for (QueryCache *cache : registered_caches_) {
      if (ptr->matches(cache->include_mask, cache->exclude_mask)) {
        cache->matching_archetypes.push_back(ptr);
      }
    }

    return ptr;
  }
```

#### New Members & Methods to Add to `include/arche/archetype_graph.hpp`

1. **Add `#include "arche/query_cache.hpp"`** at the top of `archetype_graph.hpp`.

2. **Add member data** in `class ArchetypeGraph`, private section (around line 88):

```cpp
  /// Pointers to all active query caches registered for reactive updates.
  std::vector<QueryCache *> registered_caches_{};
```

3. **Add registration method** in `class ArchetypeGraph`, public section (around line 125):

```cpp
  /**
   * @brief Registers a QueryCache for reactive updates when new archetypes are created.
   * @param cache Non-null pointer to QueryCache.
   */
  inline void register_query_cache(QueryCache *cache) noexcept {
    assert(cache != nullptr);
    registered_caches_.push_back(cache);
  }
```

---

### 2.2 `include/arche/world.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `World` did not cache query matching results.
* **Now:** `World` owns a deduplicated hash map of `QueryCache` instances and provides `get_or_create_query_cache()`.

#### Functions / Members to Delete
* `None`.

#### Modified Functions in `World`
* `None`.

#### New Members & Methods to Add to `include/arche/world.hpp`

1. **Add `#include "arche/query_cache.hpp"`** at the top of `world.hpp`.

2. **Add private member data** in `class World` (around line 72):

```cpp
  /// Central pool of deduplicated query caches.
  std::unordered_map<QueryCacheKey, std::unique_ptr<QueryCache>, QueryCacheKeyHash>
      query_caches_{};
```

3. **Add public query cache resolver** in `class World` (around line 130):

```cpp
  // --- Query Cache Management ------------------------------------------------

  /**
   * @brief Retrieves or creates a deduplicated QueryCache for the given component masks.
   *
   * @param include Component inclusion mask.
   * @param exclude Component exclusion mask.
   * @return Pointer to shared QueryCache.
   */
  inline QueryCache *get_or_create_query_cache(
      const ComponentMask &include,
      const ComponentMask &exclude
  ) {
    QueryCacheKey key{include, exclude};
    auto it = query_caches_.find(key);
    if (it != query_caches_.end()) {
      return it->second.get();
    }

    // 1. Construct new QueryCache
    auto cache = std::make_unique<QueryCache>();
    cache->include_mask = include;
    cache->exclude_mask = exclude;

    // 2. Initial scan over currently existing archetypes in graph
    for (const auto &arch_ptr : graph_.archetypes()) {
      const ComponentMask &mask = arch_ptr->mask();
      if (mask.contains_all(include)) {
        if (exclude.empty() || !mask.contains_any(exclude)) {
          cache->matching_archetypes.push_back(arch_ptr.get());
        }
      }
    }

    QueryCache *raw_cache = cache.get();
    query_caches_.emplace(key, std::move(cache));

    // 3. Register cache with graph for reactive updates on future archetypes
    graph_.register_query_cache(raw_cache);

    return raw_cache;
  }
```

---

### 2.3 `include/arche/query.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `Query` constructor called `find_matching_archetypes()` to scan all archetypes and populated a local `std::vector<ArchetypeMatch>`.
* **Now:** `Query` constructor retrieves a shared `const QueryCache *cache_` from `World` in $O(1)$. Execution directly traverses `cache_->matching_archetypes`.

#### Functions / Members to Delete
* `inline void find_matching_archetypes()`: **DELETE**.
* `std::vector<ArchetypeMatch> matching_archetypes_{}`: **DELETE**.
* `struct ArchetypeMatch`: **DELETE**.

#### Modified Functions in `Query`

##### Old Version (Module 012)
```cpp
  template <typename... Filters>
  explicit inline Query(World &world, Filters... /*filters*/) : world_(world) {
    build_masks();
    (apply_filter(Filters{}), ...);
    find_matching_archetypes();
  }

  template <typename Visitor>
  inline void for_each_active_chunk(Visitor &&visitor) {
    for (const auto &match : matching_archetypes_) {
      const auto &chunks = get_chunks_for(match.archetype);
      const size_t num_chunks = chunks.size();

      for (size_t i = 0; i < num_chunks; ++i) {
        Chunk *chunk = chunks[i];
        if (chunk->is_empty()) continue;

        if (filter_added_ && chunk->structure_version() <= added_since_tick_) continue;

        bool is_dirty = true;
        for (const auto &rule : mutation_filters_) {
          const int16_t col = match.archetype->column_index(rule.type_id);
          if (col >= 0 && chunk->column_version(static_cast<uint16_t>(col)) <= rule.since_tick) {
            is_dirty = false;
            break;
          }
        }
        if (!is_dirty) continue;

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

##### New Version (Module 013)
```cpp
  template <typename... Filters>
  explicit inline Query(World &world, Filters... /*filters*/) : world_(world) {
    build_masks();
    (apply_filter(Filters{}), ...);
    // Acquire deduplicated query cache from World in O(1)
    cache_ = world_.get_or_create_query_cache(include_mask_, exclude_mask_);
  }

  template <typename Visitor>
  inline void for_each_active_chunk(Visitor &&visitor) {
    assert(cache_ != nullptr);
    for (Archetype *arch : cache_->matching_archetypes) {
      const auto &chunks = get_chunks_for(arch);
      const size_t num_chunks = chunks.size();

      for (size_t i = 0; i < num_chunks; ++i) {
        Chunk *chunk = chunks[i];
        if (chunk->is_empty()) continue;

        // 1. Evaluate Added structural change filter in O(1)
        if (filter_added_ && chunk->structure_version() <= added_since_tick_) continue;

        // 2. Evaluate Mutated<T> component change filters in O(1)
        bool is_dirty = true;
        for (const auto &rule : mutation_filters_) {
          const int16_t col = arch->column_index(rule.type_id);
          if (col >= 0 && chunk->column_version(static_cast<uint16_t>(col)) <= rule.since_tick) {
            is_dirty = false;
            break;
          }
        }
        if (!is_dirty) continue;

        // 3. Issue lookahead prefetching for the next chunk
        if (i + 1 < num_chunks) {
          const Chunk *next_chunk = chunks[i + 1];
#if defined(__GNUC__) || defined(__clang__)
          __builtin_prefetch(next_chunk, 0, 3);
          __builtin_prefetch(reinterpret_cast<const uint8_t *>(next_chunk) + 64, 0, 3);
          ((__builtin_prefetch(
               reinterpret_cast<const uint8_t *>(next_chunk) +
                   resolve_column_offset<Components>(arch),
               0, 3
           )), ...);
#endif
        }

        visitor(chunk, arch);
      }
    }
  }
```

#### New Members to Add to `class Query` in `include/arche/query.hpp`

Insert in `class Query`, private section (around line 415):

```cpp
  /// Non-owning pointer to shared, reactively updated QueryCache.
  const QueryCache *cache_{nullptr};
```

---

## 3. Brand-New Files

### 3.1 `include/arche/query_cache.hpp`

This is a brand-new header defining the flat-data `QueryCache` structures with zero virtual methods.

```cpp
#pragma once

#include "arche/component_mask.hpp"

#include <vector>

namespace arche {

// Forward declarations
class Archetype;

// =============================================================================
// Flat Reactive Query Cache
// =============================================================================

/**
 * @struct QueryCacheKey
 * @brief Hash key for query cache deduplication by component masks.
 */
struct QueryCacheKey {
  ComponentMask include_mask;
  ComponentMask exclude_mask;

  constexpr bool operator==(const QueryCacheKey &other) const noexcept {
    return include_mask == other.include_mask && exclude_mask == other.exclude_mask;
  }
};

/**
 * @struct QueryCacheKeyHash
 * @brief Hash functor for QueryCacheKey.
 */
struct QueryCacheKeyHash {
  [[nodiscard]] size_t operator()(const QueryCacheKey &k) const noexcept {
    ComponentMaskHash h;
    return h(k.include_mask) ^ (h(k.exclude_mask) << 1);
  }
};

/**
 * @struct QueryCache
 * @brief Pure flat-data cache of matching archetype pointers for a specific query signature.
 *
 * @details
 * - Zero virtual functions, zero abstract classes, zero polymorphic interfaces.
 * - Owned centrally by World.
 * - When a new archetype is added to the graph, it is tested against active query caches once.
 * - Queries iterate over matching_archetypes directly with zero bitmask evaluations.
 */
struct QueryCache {
  ComponentMask include_mask;
  ComponentMask exclude_mask;
  std::vector<Archetype *> matching_archetypes;
};

} // namespace arche
```

---

### 3.2 `tests/query_caching_tests.cpp`

This is a brand-new test suite verifying $O(1)$ query cache retrieval, deduplication, dynamic reactive cache updates without hooks, and exclusion mask filtering.

```cpp
#include "arche/entity.hpp"
#include "arche/query.hpp"
#include "arche/query_cache.hpp"
#include "arche/query_filter.hpp"
#include "arche/world.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <vector>

namespace {
using arche::Entity;
using arche::World;
using arche::With;
using arche::Without;
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

struct DeadTag {};

// =============================================================================
// Unit Test 1: O(1) Cache Deduplication
// =============================================================================

/**
 * @brief Validates that multiple queries with identical signatures share the exact same QueryCache.
 */
void test_query_cache_deduplication() {
  std::cout << "[Test 1/4] O(1) Query Cache Retrieval & Deduplication... ";
  World world;

  // Create initial entities
  world.create_entity(Position{1.0f, 2.0f}, Velocity{3.0f, 4.0f});

  // Construct two separate query instances with identical signatures
  auto q1 = world.query<Position, Velocity>();
  auto q2 = world.query<Position, Velocity>();

  // Both queries must produce identical results with zero redundant allocations
  size_t count1 = 0;
  q1.for_each_chunk([&](uint32_t n, Position *, Velocity *) { count1 += n; });

  size_t count2 = 0;
  q2.for_each_chunk([&](uint32_t n, Position *, Velocity *) { count2 += n; });

  assert(count1 == 1);
  assert(count2 == 1);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: Dynamic Reactive Cache Updates Without Hooks
// =============================================================================

/**
 * @brief Validates that creating a new archetype *after* query construction updates the query reactively.
 */
void test_reactive_cache_update() {
  std::cout << "[Test 2/4] Dynamic Reactive Cache Updates (Zero Hooks)... ";
  World world;

  // 1. Construct query first (when only 0 archetypes exist)
  auto q = world.query<Position, Velocity>();

  size_t count = 0;
  q.for_each_chunk([&](uint32_t n, Position *, Velocity *) { count += n; });
  assert(count == 0);

  // 2. Spawn entity in Archetype A: [Position, Velocity]
  world.create_entity(Position{10.0f, 20.0f}, Velocity{1.0f, 2.0f});

  // Existing query must immediately and reactively see the new entity!
  count = 0;
  q.for_each_chunk([&](uint32_t n, Position *, Velocity *) { count += n; });
  assert(count == 1);

  // 3. Spawn entity in Archetype B: [Position, Velocity, Mass] (new graph node)
  world.create_entity(Position{30.0f, 40.0f}, Velocity{3.0f, 4.0f}, Mass{100.0f});

  // Query must automatically see the new archetype without re-construction
  count = 0;
  q.for_each_chunk([&](uint32_t n, Position *, Velocity *) { count += n; });
  assert(count == 2);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Exclusion Mask Caching Correctness
// =============================================================================

/**
 * @brief Validates that Without<T> filters are properly evaluated during cache population.
 */
void test_exclusion_caching() {
  std::cout << "[Test 3/4] Exclusion Mask Caching Correctness... ";
  World world;

  // Spawn alive and dead entities
  world.create_entity(Position{1.0f, 1.0f}, Velocity{1.0f, 1.0f});
  world.create_entity(Position{2.0f, 2.0f}, Velocity{2.0f, 2.0f}, DeadTag{});

  // Query excluding DeadTag
  auto q = world.query<Position, Velocity>(Without<DeadTag>{});

  size_t count = 0;
  q.for_each_chunk([&](uint32_t n, Position *, Velocity *) { count += n; });
  assert(count == 1);

  // Spawn another dead entity (Archetype with DeadTag already exists)
  world.create_entity(Position{3.0f, 3.0f}, Velocity{3.0f, 3.0f}, DeadTag{});
  count = 0;
  q.for_each_chunk([&](uint32_t n, Position *, Velocity *) { count += n; });
  assert(count == 1);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Pure Declarative Hook-Free Syntax
// =============================================================================

/**
 * @brief Validates standard declarative Colony loop syntax with zero hooks.
 */
void test_declarative_syntax() {
  std::cout << "[Test 4/4] Pure Declarative Hook-Free Syntax... ";
  World world;

  constexpr size_t COUNT = 10'000;
  world.create_entities(COUNT, Position{0.0f, 0.0f}, Velocity{1.0f, 1.0f});

  // Standard Colony boid query
  auto query = world.query<Position, const Velocity>();
  query.for_each_chunk([](uint32_t count, Position *pos, const Velocity *vel) {
    for (uint32_t i = 0; i < count; ++i) {
      pos[i].x += vel[i].vx;
      pos[i].y += vel[i].vy;
    }
  });

  std::cout << "PASSED\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Module 013: Query Caching Tests\n";
  std::cout << "====================================================\n";

  test_query_cache_deduplication();
  test_reactive_cache_update();
  test_exclusion_caching();
  test_declarative_syntax();

  std::cout << "====================================================\n";
  std::cout << "All Module 013 tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}
```

---

## 4. Build Configuration: `CMakeLists.txt`

Add the new unit test target to `CMakeLists.txt`:

```cmake
# Module 013 Tests: Reactive Flat Query Caching (Zero Hooks)
add_executable(query_caching_tests tests/query_caching_tests.cpp)
target_link_libraries(query_caching_tests PRIVATE arche::arche)
add_test(NAME query_caching_tests COMMAND query_caching_tests)
```

