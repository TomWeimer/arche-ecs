# Module 012: Chunk Dirty Ticking & Change Detection — Overview

## 1. Architectural Mission
In simulations with 1,000,000 entities, iterating over all chunks every frame is extremely wasteful when 80–90% of entities are dormant, sleeping, or unmutated (e.g. resting particles, inactive hazards, static colliders).

The mission of Module 012 is to introduce **chunk-level monotonic version ticking**. Systems can verify if any component in a 64 KB chunk was modified since the system's last execution tick, allowing queries to skip entire 1,600-entity chunks in a single clock cycle ($O(1)$) before touching cache memory.

---

## 2. Key Capabilities & Interfaces

### 2.1 Monotonic World Ticking
- `World` maintains a global `uint32_t current_tick{1}` incremented at the start of each simulation frame:
  ```cpp
  uint32_t tick = world.advance_tick();
  ```

### 2.2 Chunk Header Version Tracking
- Extend the 64-byte `Chunk` header with a dense array of version stamps (one `uint32_t` per column):
  ```cpp
  struct Chunk {
      // ... existing header fields ...
      uint32_t column_version[MAX_COMPONENTS_PER_CHUNK];
      uint32_t chunk_structure_version; // Updated on Swap-and-Pop / row relocations
  };
  ```

### 2.3 Change Detection Query Filters (`Mutated<T>`, `Added<T>`)
```cpp
using arche::Mutated;
using arche::Added;

// Only iterates chunks where 'Position' has been modified since last_system_tick
auto moving_query = world.query<Position, Velocity>(
    Mutated<Position>{last_system_tick}
);

query.for_each_chunk([&](uint32_t count, Position* p, Velocity* v) {
    // Executes ONLY on dirty chunks; skips dormant chunks in O(1)
});
```

---

## 3. Subsystem Modifications
- **`Chunk` (`chunk.hpp`):** Add per-column mutation timestamps and a helper `mark_column_dirty(uint16_t col, uint32_t tick)`.
- **`Query` / `QueryFilter` (`query_filter.hpp`):** Add `Mutated<T>` and `Added<T>` filter structs.
- **`QueryIterator` & `for_each_chunk` (`query.hpp`):** Evaluate `chunk->column_version[col] > filter_tick` at the chunk boundary. If false, bypass the entire 64 KB page with zero row iterations.

---

## 4. Invariants & Performance Target
- **Hard Invariants:** Zero memory footprint overhead in component columns; version stamping overhead limited to 1 store instruction when writing.
- **Performance Budget:** Skipping 500 dormant chunks (800,000 entities) in $< 0.002\text{ ms}$ ($< 2\ \mu\text{s}$).

---

## 5. Verification & Testing Strategy
- **`dirty_ticking_tests`:**
  - Verify tick stamping on component writes.
  - Test query filtering where only 1 out of 50 chunks is marked dirty.
  - Verify wrap-around handling for 32-bit tick counters.

