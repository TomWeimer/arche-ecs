# Module 010: Bulk Entity Lifecycle & Batch Allocations — Overview

## 1. Architectural Mission
In simulations exceeding 500,000 to 1,000,000 entities, spawning or destroying entities one-by-one introduces significant overhead:
- Repeated free-list traversal and slot checks in `SlotMap`.
- Repetitive chunk capacity checks and row increment instructions in `Archetype`.
- Instruction cache misses from looping through scalar constructor functions.

The mission of Module 010 is to provide **high-throughput batch allocation and deallocation APIs** that populate contiguous chunk rows directly in single vectorized sweeps, achieving $> 50\text{M}$ entity operations per second.

---

## 2. Key Capabilities & Interfaces

### 2.1 Bulk Entity Creation (`world.create_entities`)
```cpp
// 1. Prototype-broadcast: creates N entities initialized with component prototypes
template <typename... Components>
std::vector<Entity> create_entities(size_t count, const Components&... prototypes);

// 2. Direct SoA streaming: writes pre-computed contiguous component arrays
template <typename... Components>
void create_entities(size_t count, std::span<Entity> out_entities, const Components*... column_data);
```
- **Contiguous Slot Reservation:** Allocates $N$ slots in `SlotMap` in a single vector resize or bulk free-list pop.
- **Bulk Chunk Page Allocation:** Calculates `chunks_needed = ceil(count / chunk_capacity)` and acquires pre-aligned pages from `ChunkPool` in one batch.
- **Direct SoA Streaming:** Uses `std::memcpy` or vectorized broadcast to fill component columns across chunks without row-by-row dispatch.

### 2.2 Bulk Entity Destruction (`world.destroy_entities`)
```cpp
void destroy_entities(std::span<const Entity> entities);
```
- **Batched Swap-and-Pop:** Groups deletions by chunk, sorts them by descending row index, and compacts each chunk in a single pass to eliminate redundant intermediate moves.
- **Bulk SlotMap Invalidation:** Recycles slots back to `SlotMap`'s free-list in bulk with 32-bit generation increments.

---

## 3. Subsystem Modifications
- **`SlotMap` (`slot_map.hpp`):** Add `allocate_batch(size_t count, Entity* out_handles)` and `free_batch(std::span<const Entity> handles)`.
- **`Archetype` (`archetype.hpp`):** Add `reserve_rows(size_t count)` and `append_rows(size_t count, const void* const* component_sources)`.
- **`World` (`world.hpp`):** Expose `create_entities` and `destroy_entities`.

---

## 4. Invariants & Performance Target
- **Hard Invariants:** Zero heap allocations during steady-state chunk acquisition; POD structs only.
- **Performance Budget:** Spawning 1,000,000 entities with `[Position, Velocity]` in $< 25\text{ ms}$ ($> 40\text{M ops/sec}$).

---

## 5. Verification & Testing Strategy
- **`batch_lifecycle_tests`:**
  - Verify handle uniqueness and generation validity for 100,000 batch allocations.
  - Verify column data continuity across multi-chunk boundaries.
  - Stress-test batched destruction with interleaved row deletions.

