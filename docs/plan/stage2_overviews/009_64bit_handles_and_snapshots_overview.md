# Module 009: 64-Bit Generational Handles & Instant Snapshots — Overview

## 1. Architectural Mission
In simulations exceeding 500,000 to 1,000,000 entities:
- **The 8-Bit ABA Hazard:** The Stage 1 Entity handle used 24 bits for the index and 8 bits for the generation. When fast particle systems spawn and destroy thousands of entities per second, an 8-bit generation counter wraps around in minutes ($256$ generations). Stale handles held by systems or renderers accidentally validate against new entities occupying the same slot.
- **Save/Restore Overhead:** Serializing massive simulations entity-by-entity wastes seconds of CPU time and fragments memory.

The mission of Module 009 is:
- Transition to **64-bit scalar Entity handles** (32-bit index + 32-bit generation), eliminating the ABA hazard permanently.
- Provide **instant raw memory snapshots & rollback** (`create_snapshot` / `restore_snapshot`) that dump and restore all active chunk memory and `SlotMap` state in $< 1\text{ ms}$ via raw DMA/`memcpy`.

---

## 2. Key Capabilities & Interfaces

### 2.1 64-Bit Generational Entity Handle
```cpp
class Entity {
    uint64_t id_{INDEX_NONE};
public:
    static constexpr uint64_t INDEX_NONE = 0xFFFFFFFFFFFFFFFFULL;
    static constexpr uint64_t INDEX_MASK = 0x00000000FFFFFFFFULL;
    static constexpr uint64_t GENERATION_MASK = 0xFFFFFFFF00000000ULL;

    [[nodiscard]] inline uint32_t index() const noexcept {
        return static_cast<uint32_t>(id_ & INDEX_MASK);
    }
    [[nodiscard]] inline uint32_t generation() const noexcept {
        return static_cast<uint32_t>((id_ & GENERATION_MASK) >> 32);
    }
    [[nodiscard]] inline bool is_null() const noexcept {
        return id_ == INDEX_NONE;
    }
};
```
- Fits in a single 64-bit general-purpose CPU register.
- Allows up to $4.29\text{ billion}$ entities and $4.29\text{ billion}$ recycling cycles per slot.

### 2.2 Instant World Snapshotting & Rollback (Pure Memory Dumps)
Because all entity data in `arche-ecs` is stored in contiguous, page-aligned 64 KB memory chunks without internal pointers:
```cpp
// Serializes entire simulation state in < 1 ms via raw DMA / memcpy:
size_t create_snapshot(void* destination_buffer, size_t max_bytes);
void restore_snapshot(const void* snapshot_buffer, size_t bytes);
```
- Completely bypasses entity-by-entity serialization.
- Copies raw chunk memory blocks and the 64-bit `SlotMap` array directly into a contiguous snapshot buffer (ideal for replays, state rewinds, and network save-states).

---

## 3. Subsystem Modifications
- **`Entity` (`entity.hpp`):** Expand internal scalar to `uint64_t` (32b index / 32b generation).
- **`SlotMap` (`slot_map.hpp`):** Update `Slot` struct to store `uint32_t generation`.
- **`World` (`world.hpp`):** Expose `create_snapshot` and `restore_snapshot`.

---

## 4. Invariants & Performance Target
- **Hard Invariants:** Zero ABA hazard risk (32-bit generations); POD structs only; handles fit in single registers.
- **Performance Budget:** Instant snapshot of 500,000 entities in $< 1\text{ ms}$.

---

## 5. Verification & Testing Strategy
- **`handles_and_snapshots_tests`:**
  - Verify 64-bit handle bit-packing, generation extraction, and 32-bit wrap safety.
  - Verify stale handle rejection across millions of recycling cycles.
  - Verify exact bit-for-bit restoration after `create_snapshot` and `restore_snapshot`.

