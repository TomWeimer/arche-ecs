# Architectural Coupling Audit: Delta Decomposition vs. Side-Effect Invariants

## Executive Summary
This document analyzes instances of **horizontal structural coupling**, **cross-ADT side-effects**, and **upward architectural violations** across the `arche-ecs` codebase. 

In pure Computer Science and Systems Architecture theory:
* An **Abstract Data Type (ADT)** or storage primitive must remain closed under its own representation.
* An operation that transforms state must output a **relocation delta / result record** rather than executing side-effects against an external index or sibling subsystem.
* Coordination across multiple independent primitives belongs to the composition layer above them.

---

## 1. Concrete Solution 1 Implementation: `Chunk::swap_and_pop`

### The Current Coupled State (Anti-Pattern)
`Chunk` (Layer 1 storage page) directly includes and mutates `SlotMap` (Layer 1 secondary index):
```cpp
// src/chunk/chunk.hpp
void swap_and_pop(
    uint32_t row,
    SlotMap &slot_map,                                // <-- Structural Coupling
    std::span<const ComponentColumnLayout> layouts
);
```
Inside `chunk.cpp`, `relocate_entity_handle` calls `slot_map.update_row(moved_entity, dst_row)` directly in the middle of byte-moving operations.

### The Solution 1 Algebraic Implementation

#### 1. Single Entity `swap_and_pop`
`Chunk` only moves memory and returns the relocated `Entity` (or an invalid entity if no relocation occurred):

```cpp
// in src/chunk/chunk.hpp (Zero SlotMap dependency!)
struct ChunkRelocation {
    Entity moved_entity{INDEX_NONE, 0}; // 64-bit POD
};

[[nodiscard]] ChunkRelocation swap_and_pop(
    uint32_t row,
    std::span<const ComponentColumnLayout> layouts
) noexcept;
```

#### Assembly / Hardware Mechanics:
1. `ChunkRelocation` is 64 bits. Under the System V AMD64 and ARM64 AAPCS calling conventions, it returns directly in register `rax` (x86-64) or `x0` (ARM64).
2. It requires **0 stack allocations** and **0 extra clock cycles**.
3. All column byte-copies finish sequentially in L1D cache before the CPU ever touches `SlotMap`.

#### Caller Execution (`src/world/world.cpp` or `Archetype`):
```cpp
// Caller coordinates storage page + secondary index
const auto reloc = chunk->swap_and_pop(record->row, arch->column_layouts());
if (reloc.moved_entity.is_valid()) {
    storage_->slot_map.update_row(reloc.moved_entity, record->row);
}
arch->decrement_entity_count();
storage_->slot_map.free(entity);
```

#### 2. Batched `batch_swap_and_pop`
Instead of mutating `SlotMap` while compacting, the chunk outputs a dense stream of relocations into a caller-provided span:

```cpp
// in src/chunk/chunk.hpp
struct EntityRelocation {
    Entity entity;
    uint32_t new_row;
};

// Returns number of relocations written to out_relocations
uint32_t batch_swap_and_pop(
    std::span<const uint32_t> rows_to_delete,
    std::span<const ComponentColumnLayout> layouts,
    std::span<EntityRelocation> out_relocations
) noexcept;
```

#### Caller Execution:
```cpp
// Caller preallocates small stack/vector buffer
std::vector<EntityRelocation> relocations(rows.size());
const uint32_t count = chunk->batch_swap_and_pop(rows, layouts, relocations);

// Sequential index synchronization (no L1D thrashing during chunk compaction)
for (uint32_t i = 0; i < count; ++i) {
    storage_->slot_map.update_row(relocations[i].entity, relocations[i].new_row);
}
```

---

## 2. Exhaustive Audit: Similar Structural Violations Across the Codebase

### Finding 1: `SpatialSort` Permutation Interleaved with `SlotMap` Mutation
* **Location:** `src/spatial/sort.cpp:102-140` (`permute_entities_and_sync_slots`)
* **Problem:** 
  During in-place cyclic permutation of chunks, for every step in the cycle, the algorithm calls `sync_entity_slot(...)` which invokes `slot_map.update_location(...)`.
* **Hardware Impact:** 
  Interleaves linear entity/column permutations with random-access cache lines to `SlotMap`. Causes store buffer stalling and breaks CPU prefetchers.
* **Solution 1 Remediation:**
  1. `permute(...)` executes pure cyclic memory permutations on all columns and entity handles across chunks.
  2. Once chunk memory is permuted, a single linear pass over the chunks reads `chunk->entity_slice()` and batch-updates `SlotMap`, or the cycle loop emits `(Entity, Chunk*, uint32_t)` deltas to be applied in a dedicated second phase.

---

### Finding 2: `ChunkHeader::archetype` Upward Inverted Dependency
* **Location:** `src/chunk/chunk.hpp:40, 164, 226`
* **Problem:**
  `Chunk` (Layer 1) stores a raw pointer `Archetype *archetype` in its `ChunkHeader`, forward-declaring `class Archetype` (Layer 2).
* **Architectural Impact:**
  Direct violation of the Layered Architecture ($L_1 \to L_2$). A low-level 64KB memory page must not hold a pointer to the higher-level table that manages it.
* **Audit Finding:**
  The getter `chunk->archetype()` is used in only **one single place** across the entire project (`src/component/component_access.inl:96`). In all other contexts, `Archetype` and `Chunk` are already paired together by `EntityRecord` in `SlotMap`.
* **Remediation:**
  Remove `Archetype *archetype` from `ChunkHeader`. If an operation has a `Chunk*`, the caller must pass the `Archetype*` contextually rather than reading upward through the chunk header.

---

### Finding 3: `ComponentMigration` Reaching Upward to `WorldAccess`
* **Location:** `src/archetype/component_migration.cpp:54-78`
* **Problem:**
  `ComponentMigration::migrate(...)` resides in Layer 2 (`archetype`). To move an entity between archetypes, it takes `World &world` (Layer 5) and invokes:
  * `internal::WorldAccess::chunk_pool(world)`
  * `internal::WorldAccess::slot_map(world)`
  * `src_chunk->swap_and_pop(..., slot_map, ...)`
  * `internal::WorldAccess::slot_map(world).update_location(...)`
* **Architectural Impact:**
  Layer 2 reaches up to Layer 5 (`WorldAccess`) solely to pull out Layer 1 primitives (`SlotMap`, `ChunkPool`).
* **Solution 1 Remediation:**
  `ComponentMigration` should not take `World &world`. It should accept:
  ```cpp
  void ComponentMigration::migrate(
      ChunkPool &pool,
      SlotMap &slot_map,
      Entity entity,
      const EntityRecord &old_record,
      Archetype *src_arch,
      Archetype *dst_arch,
      TypeID mutated_type,
      const void *added_component_data
  );
  ```
  `src_chunk->swap_and_pop` returns `ChunkRelocation`, and `ComponentMigration` updates `slot_map`.

---

### Finding 4: `EntityAllocator` Monolithic Coupling via `WorldAccess`
* **Location:** `src/entity/entity_allocator.inl:57-145`
* **Problem:**
  `EntityAllocator` (Layer 3) takes `World &world` (Layer 5) in inline template methods (`create`, `create_batch`) and invokes:
  * `WorldAccess::registry(world).register_type<T>()`
  * `WorldAccess::graph(world).get_or_create(types)`
  * `WorldAccess::slot_map(world).allocate()`
  * `WorldAccess::chunk_pool(world)`
  * `WorldAccess::version(world)`
* **Architectural Impact:**
  `EntityAllocator` is an inverted umbrella that binds every subsystem together through the `World` facade instead of composing clean sub-facades or receiving explicit dependencies.
* **Remediation:**
  Decompose `EntityAllocator` into:
  1. An Archetype slot locator (`graph.get_or_create`).
  2. Memory appender (`chunk->append_entities`).
  3. Index synchronizer (`slot_map.update_location`).
  Do not route through `WorldAccess` inside template inlines.

---

### Finding 5: Runtime Side-Effects in `TypeRegistry` During Reads / Iteration
* **Location:** 
  * `src/spatial/sort.hpp:25`
  * `src/component/component_access.inl:57`
  * `src/command/command_buffer.inl:29`
* **Problem:**
  Ad-hoc calls to `internal::WorldAccess::registry(world).register_type<T>()` occur lazily inside data access, sorting, and command buffering.
* **Architectural Impact:**
  Mutating the type registry during spatial sorting or component access turns read/sort operations into write operations on global state. This prevents const-correctness, breaks thread-safety, and invalidates pure query pipelines.
* **Remediation:**
  Type registration must occur exclusively at system initialization or query construction. Data-movement algorithms (sorting, chunk compaction) must treat types and layouts as immutable inputs (`std::span<const ComponentColumnLayout>`).

---

## 3. Structural Comparison Matrix

| Subsystem / Operation | Current Architecture (Side-Effect Coupling) | Pure Systems / Solution 1 Form (Algebraic Delta) | Benefits |
| :--- | :--- | :--- | :--- |
| **`Chunk::swap_and_pop`** | Takes `SlotMap&`; mutates external index during memory copy. | Returns `ChunkRelocation` directly in register `rax`/`x0`. | Eliminates `Chunk` $\to$ `SlotMap` dependency; maximum L1D locality. |
| **`Chunk::batch_swap_and_pop`** | Takes `SlotMap&`; updates each slot during compaction. | Writes `std::span<EntityRelocation>` output stream. | Auto-vectorizable; zero cache-thrashing between chunk and sparse table. |
| **`SpatialSort::permute`** | Interleaves component/entity permutation with `SlotMap` updates. | Permutes chunks in Phase 1; scans permuted entity slice to sync `SlotMap` in Phase 2. | Store buffer does not stall; prefetchers stay saturated. |
| **`ChunkHeader::archetype`** | Stores `Archetype*` in 64KB page header ($L_1 \to L_2$). | Removed from header; `Archetype*` passed by coordinator when required. | Enforces strict DAG dependency ($L_2 \to L_1$ only). |
| **`ComponentMigration`** | Layer 2 reaches into `WorldAccess` (Layer 5) to grab `SlotMap` & `ChunkPool`. | Accepts `(ChunkPool&, SlotMap&)`; coordinates using return deltas. | Eliminates upward dependency cycle $L_2 \to L_5$. |
