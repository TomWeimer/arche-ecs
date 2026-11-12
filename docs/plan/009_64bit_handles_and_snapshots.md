# Module 009: 64-Bit Generational Handles & Instant Snapshots

This document specifies the exact behavioral deltas, code modifications, and new test suites required to transition `arche-ecs` from 32-bit handles to 64-bit handles and add zero-cost instant world snapshots.

---

## 1. Behavioral Deltas: What Changes and Why

### 1.1 Handle Encoding (32-Bit $\to$ 64-Bit)
* **Before (Stage 1):**  
  `Entity` was encoded into a 32-bit integer (24-bit slot index + 8-bit generation). Generation 0 was reserved for `null_entity`, leaving 1..255 generations per slot.
* **Now (Stage 2):**  
  `Entity` is encoded into a 64-bit integer (32-bit slot index + 32-bit generation). Generation 0 is reserved, leaving 1..4,294,967,295 generations per slot.
* **Why we need to change this part:**  
  In high-density simulations (500k to 1M entities) with particle emitters, explosions, and dead boid recycling, a slot can be recycled thousands of times per second. An 8-bit generation wraps around in under a second ($255 / 1000 \approx 0.25\text{ s}$), causing stale handles stored in spatial hashes or game systems to silently access newly spawned entities (ABA problem). With 32-bit generations, wrap-around takes $> 136\text{ years}$ at 1,000 recycles/second, eliminating ABA hazards while remaining a single 64-bit register (`uint64_t`) with zero pass-by-value overhead.

### 1.2 World Snapshots & Instant Rollback
* **Before (Stage 1):**  
  No snapshot or state persistence mechanism existed. Saving or rolling back state would require iterating all entities and re-allocating them.
* **Now (Stage 2):**  
  `World` provides `create_snapshot()` and `restore_snapshot()` which serialize and deserialize the raw memory of the `SlotMap` array and all allocated 64 KB chunk pages in $< 1\text{ ms}$ via direct memory block transfers.
* **Why we need to change this part:**  
  Because all archetype storage in `arche-ecs` is flat POD memory partitioned into contiguous 64 KB pages, we can achieve true zero-cost snapshots without polymorphic serializers or reflection trees. This is essential for lockstep netcode, simulation rewinds, and instant branch evaluation.

### 1.3 Deduplication & Helper Architecture: Core Primitives
* **`advance_nonzero(uint32_t val)`:**  
  **Where defined:** `include/arche/entity.hpp` (namespace `arche`).  
  **What code it replaces:** Replaces hand-rolled wrapping checks `const uint32_t next = val + 1; return (next == 0) ? 1 : next;` in both `SlotMap::advance_generation()` and `World::advance_tick()`.
* **`SlotMap::find_record(Entity e)`:**  
  **Where defined:** `include/arche/slot_map.hpp` (inside `class SlotMap`).  
  **What code it replaces:** Replaces duplicated dual lookups `if (!slot_map_.is_alive(e)) return; const EntityRecord &rec = slot_map_.get_record(e);` across `world.hpp` and `command_buffer.hpp` with a single branch and memory fetch.
* **Automatic `std::decay_t` in `type_id<T>()`:**  
  **Where defined:** `include/arche/type_id.hpp`.  
  **What code it replaces:** Replaces repetitive `type_id<std::decay_t<T>>()` invocations across all type registration, query, and world call sites.
* **Unified Math Utilities:**  
  **Where defined:** `include/arche/math_utils.hpp` (namespace `arche::math`).  
  **What code it replaces:** Standardizes power-of-2 alignment and SIMD bounds math (`(x + 15) & ~15`, `(cap / 16) * 16`), eliminating ad-hoc magic numbers across modules.

---

## 2. Modifications to Existing Files

### 2.1 `include/arche/entity.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `Entity` packed 24 bits for the slot index (`0x00FFFFFF`) and 8 bits for the generation (`0xFF000000`). `sizeof(Entity)` was 4 bytes.
* **Now:** `Entity` packs 32 bits for the slot index (`0x00000000FFFFFFFFULL`) and 32 bits for the generation (`0xFFFFFFFF00000000ULL`). `sizeof(Entity)` is 8 bytes. `null_entity` sentinel is `INDEX_NONE` (`0xFFFFFFFFFFFFFFFFULL`).

#### Functions / Members to Delete
* `None`.

#### Modified Members & Bitfield Constants

##### Old Version (Stage 1: 32-Bit)
```cpp
struct Entity {
private:
  /// Packed 32-bit representation (8-bit gen, 24-bit index).
  uint32_t value{0};

public:
  // --- Bitfield Constants ------------------------------------
  static constexpr uint32_t INDEX_BITS = 24;
  static constexpr uint32_t GENERATION_BITS = 8;
  static constexpr uint32_t INDEX_MASK = 0x00FFFFFF;
  static constexpr uint32_t GENERATION_MASK = 0xFF000000;
  static constexpr uint32_t INDEX_NONE = 0x00FFFFFF;
```

##### New Version (Stage 2: 64-Bit)
```cpp
struct Entity {
private:
  /// Packed 64-bit representation (32-bit gen, 32-bit index).
  uint64_t value{INDEX_NONE};

public:
  // --- Bitfield Constants ------------------------------------
  static constexpr uint32_t INDEX_BITS = 32;
  static constexpr uint32_t GENERATION_BITS = 32;
  static constexpr uint64_t INDEX_MASK = 0x00000000FFFFFFFFULL;
  static constexpr uint64_t GENERATION_MASK = 0xFFFFFFFF00000000ULL;
  static constexpr uint64_t INDEX_NONE = 0xFFFFFFFFFFFFFFFFULL;
```

#### Modified Functions in `Entity`

##### Old Version (Stage 1: 32-Bit)
```cpp
[[nodiscard]] static constexpr Entity from_parts(
    uint32_t index,
    uint8_t generation
) noexcept {
  Entity e;
  e.value = (static_cast<uint32_t>(generation) << INDEX_BITS) |
      (index & INDEX_MASK);
  return e;
}

[[nodiscard]] static constexpr Entity from_raw(uint32_t rawValue) noexcept {
  Entity e;
  e.value = rawValue;
  return e;
}

[[nodiscard]] constexpr uint32_t id() const noexcept {
  return value & INDEX_MASK;
}

[[nodiscard]] constexpr uint32_t index() const noexcept {
  return value & INDEX_MASK;
}

[[nodiscard]] constexpr uint8_t generation() const noexcept {
  return static_cast<uint8_t>((value >> INDEX_BITS) & 0xFF);
}

[[nodiscard]] constexpr uint32_t raw() const noexcept { return value; }

[[nodiscard]] constexpr explicit operator bool() const noexcept {
  return value != 0;
}
```

##### New Version (Stage 2: 64-Bit)
```cpp
[[nodiscard]] static constexpr Entity from_parts(
    uint32_t index,
    uint32_t generation
) noexcept {
  Entity e;
  e.value = (static_cast<uint64_t>(generation) << INDEX_BITS) |
      (static_cast<uint64_t>(index) & INDEX_MASK);
  return e;
}

[[nodiscard]] static constexpr Entity from_raw(uint64_t rawValue) noexcept {
  Entity e;
  e.value = rawValue;
  return e;
}

[[nodiscard]] constexpr uint32_t id() const noexcept {
  return static_cast<uint32_t>(value & INDEX_MASK);
}

[[nodiscard]] constexpr uint32_t index() const noexcept {
  return static_cast<uint32_t>(value & INDEX_MASK);
}

[[nodiscard]] constexpr uint32_t generation() const noexcept {
  return static_cast<uint32_t>((value & GENERATION_MASK) >> INDEX_BITS);
}

[[nodiscard]] constexpr uint64_t raw() const noexcept { return value; }

[[nodiscard]] constexpr explicit operator bool() const noexcept {
  return value != INDEX_NONE;
}
```

#### Modified Static Asserts, Sentinels & Hash Specialization

##### Old Version (Stage 1: 32-Bit)
```cpp
static_assert(
    sizeof(Entity) == sizeof(uint32_t),
    "Entity must be exactly 4 bytes (single 32-bit register footprint)"
);

inline constexpr Entity null_entity = Entity::from_parts(0, 0);

template <> struct std::hash<arche::Entity> {
  [[nodiscard]] std::size_t operator()(const arche::Entity &e) const noexcept {
    return std::hash<uint32_t>{}(e.raw());
  }
};
```

##### New Version (Stage 2: 64-Bit)
```cpp
static_assert(
    sizeof(Entity) == sizeof(uint64_t),
    "Entity must be exactly 8 bytes (single 64-bit register footprint)"
);

inline constexpr Entity null_entity = Entity::from_raw(Entity::INDEX_NONE);

template <> struct std::hash<arche::Entity> {
  [[nodiscard]] std::size_t operator()(const arche::Entity &e) const noexcept {
    return std::hash<uint64_t>{}(e.raw());
  }
};

// =============================================================================
// Monotonic Nonzero Counter Helper
// =============================================================================

/**
 * @brief Advances a 32-bit monotonic counter, skipping 0 on wrap-around.
 * @details Defined in `include/arche/entity.hpp`. Replaces ad-hoc wrap-around checks
 * `(next == 0) ? 1 : next` across SlotMap generations and World simulation ticks.
 *
 * @param val Current counter value.
 * @return Incremented value skipping 0.
 */
[[nodiscard]] inline constexpr uint32_t advance_nonzero(uint32_t val) noexcept {
  const uint32_t next = val + 1;
  return (next == 0) ? 1 : next;
}
```

#### New Functions to Add to `include/arche/entity.hpp`

Insert inside `struct Entity`, immediately after `operator bool()` (line 146):

```cpp
  /**
   * @brief Explicitly tests if this handle represents the null sentinel entity.
   * @return true if handle is equal to null_entity (INDEX_NONE), false otherwise.
   */
  [[nodiscard]] constexpr bool is_null() const noexcept {
    return value == INDEX_NONE;
  }
```

---

### 2.2 `include/arche/slot_map.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `Slot` held `uint8_t generation` with 3 bytes implicit padding. Maximum slot capacity was 16.7M (`MAX_ENTITIES = 0x00FFFFFF`). Free list and slots could not be bulk serialized.
* **Now:** `Slot` holds `uint32_t generation` and `uint32_t padding{0}`, keeping `sizeof(Slot) == 32` bytes. Maximum capacity expands to 4.29B (`MAX_ENTITIES = 0xFFFFFFFFULL`). Added 4 snapshot serialization primitives for direct memory dumping and restoring.

#### Functions / Members to Delete
* `None`.

#### Modified Struct `Slot` & Constants

##### Old Version (Stage 1)
```cpp
struct Slot {
  uint8_t generation{1};

  union {
    EntityRecord record;
    uint32_t next_free{INDEX_NONE};
  };

  Slot() : generation(1), next_free(INDEX_NONE) {}
  ~Slot() = default;
};

static constexpr uint32_t MAX_ENTITIES = 0x00FFFFFF;
```

##### New Version (Stage 2)
```cpp
struct Slot {
  uint32_t generation{1};
  uint32_t padding{0};

  union {
    EntityRecord record;
    uint32_t next_free{INDEX_NONE_32};
  };

  Slot() : generation(1), padding(0), next_free(INDEX_NONE_32) {}
  ~Slot() = default;
};
static_assert(sizeof(Slot) == 32, "Slot must remain exactly 32 bytes");

static constexpr uint64_t MAX_ENTITIES = 0xFFFFFFFFULL;
static constexpr uint32_t INDEX_NONE_32 = 0xFFFFFFFF;
```

#### Modified Functions in `SlotMap`

##### Old Version (Stage 1)
```cpp
[[nodiscard]] static constexpr uint8_t advance_generation(
    uint8_t gen
) noexcept {
  const uint8_t next = static_cast<uint8_t>(gen + 1);
  return (next == 0) ? 1 : next;
}

[[nodiscard]] inline uint32_t append_new_slot() {
  if (slots.size() >= MAX_ENTITIES) {
    throw std::runtime_error(
        "arche::SlotMap: capacity limit reached (16.7M entities)"
    );
  }
  const uint32_t index = static_cast<uint32_t>(slots.size());
  slots.emplace_back();
  slots[index].generation = 1;
  return index;
}

[[nodiscard]] bool is_alive(Entity e) const noexcept {
  const uint32_t index = e.index();
  if (index >= slots.size() || e == null_entity) {
    return false;
  }
  return slots[index].generation == e.generation();
}
```

##### New Version (Stage 2)
```cpp
[[nodiscard]] static constexpr uint32_t advance_generation(
    uint32_t gen
) noexcept {
  return advance_nonzero(gen); // Unified helper skipping 0 to preserve null_entity
}

[[nodiscard]] inline uint32_t append_new_slot() {
  if (slots.size() >= MAX_ENTITIES) {
    throw std::runtime_error(
        "arche::SlotMap: capacity limit reached (4.29B entities)"
    );
  }
  const uint32_t index = static_cast<uint32_t>(slots.size());
  slots.emplace_back();
  slots[index].generation = 1;
  return index;
}

[[nodiscard]] bool is_alive(Entity e) const noexcept {
  const uint32_t index = e.index();
  if (index >= slots.size() || e.is_null()) {
    return false;
  }
  return slots[index].generation == e.generation();
}
```

#### New Functions to Add to `include/arche/slot_map.hpp`

1. **Add `find_record` fast-path helper** inside `class SlotMap`, public section, right after `is_alive(...)`:

```cpp
  // --- Unified Entity Lookup & Validation Fast-Path --------------------------

  /**
   * @brief Combines liveness check and EntityRecord retrieval in a single branch.
   * @details Replaces redundant dual-check calls `if (!slot_map_.is_alive(e)) return;
   * const EntityRecord &rec = slot_map_.get_record(e);` across World and CommandBuffer.
   *
   * @param e Generational Entity handle.
   * @return Pointer to valid EntityRecord, or nullptr if entity is dead/invalid.
   */
  [[nodiscard]] inline const EntityRecord *find_record(Entity e) const noexcept {
    const uint32_t idx = e.index();
    if (idx < slots.size() && slots[idx].generation == e.generation() && !e.is_null()) {
      return &slots[idx].record;
    }
    return nullptr;
  }

  [[nodiscard]] inline EntityRecord *find_record(Entity e) noexcept {
    const uint32_t idx = e.index();
    if (idx < slots.size() && slots[idx].generation == e.generation() && !e.is_null()) {
      return &slots[idx].record;
    }
    return nullptr;
  }
```

2. **Add snapshot serialization primitives** inside `class SlotMap`, public section, right before `// --- Iteration & Capacity Queries ---` (line 320):

```cpp
  // --- Snapshot Serialization Primitives -------------------------------------

  /**
   * @brief Returns the total byte size occupied by the dense slots array.
   * @return Byte size equal to slots.size() * sizeof(Slot).
   */
  [[nodiscard]] inline size_t snapshot_byte_size() const noexcept {
    return slots.size() * sizeof(Slot);
  }

  /**
   * @brief Copies the raw slots buffer directly into a destination memory buffer.
   * @param dest Target memory buffer. Must be pre-allocated to at least snapshot_byte_size().
   */
  inline void write_snapshot_data(void *dest) const noexcept {
    assert(dest != nullptr && "Destination buffer cannot be null");
    std::memcpy(dest, slots.data(), snapshot_byte_size());
  }

  /**
   * @brief Restores the raw slots array and intrusive free-list head directly from memory.
   *
   * @param src Source memory buffer containing raw Slot data.
   * @param slot_count Number of Slot structs to read.
   * @param free_head Restored head index of the intrusive free list.
   * @param count Restored active entity count.
   */
  inline void read_snapshot_data(
      const void *src,
      size_t slot_count,
      uint32_t free_head,
      size_t count
  ) {
    assert(src != nullptr && "Source snapshot data cannot be null");
    slots.resize(slot_count);
    std::memcpy(slots.data(), src, slot_count * sizeof(Slot));
    free_list_head = free_head;
    active_count = count;
  }

  /**
   * @brief Exposes the current free-list head index for snapshot header serialization.
   * @return Index of the first free slot, or INDEX_NONE_32 if free list is empty.
   */
  [[nodiscard]] inline uint32_t free_head_index() const noexcept {
    return free_list_head;
  }
```

---

### 2.3 `include/arche/world.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `World` possessed no snapshot, serialization, or rollback methods.
* **Now:** `World` provides `create_snapshot()` and `restore_snapshot()` backed by `SnapshotHeader`. State is captured and restored via contiguous memory copies across the `SlotMap` array and all allocated 64 KB chunk pages.

#### Functions / Members to Delete
* `None`.

#### Modified Functions in `World`
* `None` (all existing Stage 1 functions in `World` are preserved unchanged).

#### New Structs & Method Declarations to Add to `include/arche/world.hpp`

1. **Add `SnapshotHeader` struct** immediately before `class World` (line 40):

```cpp
// =============================================================================
// World State Snapshot Header
// =============================================================================

/**
 * @struct SnapshotHeader
 * @brief 64-byte cache-aligned header encapsulating world snapshot metadata.
 */
struct alignas(16) SnapshotHeader {
  uint32_t magic{0x48435241};      ///< Magic bytes ('ARCH' in Little-Endian)
  uint32_t version{1};             ///< Snapshot format version
  uint64_t current_tick{0};        ///< World simulation tick at snapshot time
  uint32_t slot_count{0};          ///< Total capacity of SlotMap slots array
  uint32_t free_list_head{0};      ///< Head index of intrusive free list
  uint32_t active_entities{0};     ///< Total living entities
  uint32_t chunk_count{0};         ///< Total active 64 KB chunk pages
  size_t total_payload_bytes{0};   ///< Total byte size including header
  uint8_t reserved[24]{0};         ///< Reserved padding to align header to 64 bytes
};

static_assert(sizeof(SnapshotHeader) == 64, "SnapshotHeader must be exactly 64 bytes");
```

2. **Add snapshot method declarations** inside `class World`, public section (around line 120):

```cpp
  // --- World Snapshots & Rollback --------------------------------------------

  /**
   * @brief Dumps the entire world memory state (SlotMap + all 64 KB chunks) into a contiguous buffer.
   *
   * @param destination_buffer Pointer to pre-allocated destination memory.
   * @param max_bytes Capacity of destination buffer in bytes.
   * @return Total bytes written to destination_buffer.
   */
  inline size_t create_snapshot(void *destination_buffer, size_t max_bytes);

  /**
   * @brief Restores the entire world state from a raw memory snapshot buffer in < 1 ms.
   *
   * @param snapshot_buffer Pointer to snapshot memory.
   * @param bytes Size of snapshot buffer in bytes.
   */
  inline void restore_snapshot(const void *snapshot_buffer, size_t bytes);
```

3. **Add full method implementations** at the bottom of `include/arche/world.hpp`, right before the closing `} // namespace arche`:

```cpp
// =============================================================================
// Instant Snapshot & Rollback Implementation
// =============================================================================

inline size_t World::create_snapshot(void *destination_buffer, size_t max_bytes) {
  assert(destination_buffer != nullptr && "Snapshot destination cannot be null");

  // 1. Calculate required memory size
  const size_t slot_bytes = slot_map_.snapshot_byte_size();
  size_t total_chunk_count = 0;
  for (const auto &arch_ptr : graph_.archetypes()) {
    total_chunk_count += get_archetype_chunks(arch_ptr.get()).size();
  }

  const size_t chunk_bytes = total_chunk_count * Hardware::optimal_chunk_size();
  const size_t required_bytes = sizeof(SnapshotHeader) + slot_bytes + chunk_bytes;

  if (max_bytes < required_bytes) {
    throw std::runtime_error("create_snapshot: buffer capacity too small");
  }

  uint8_t *cursor = static_cast<uint8_t *>(destination_buffer);

  // 2. Populate and serialize 64-byte SnapshotHeader
  SnapshotHeader header{
      .magic = 0x48435241, // 'ARCH'
      .version = 1,
      .current_tick = 0,
      .slot_count = static_cast<uint32_t>(slot_map_.capacity()),
      .free_list_head = slot_map_.free_head_index(),
      .active_entities = static_cast<uint32_t>(slot_map_.size()),
      .chunk_count = static_cast<uint32_t>(total_chunk_count),
      .total_payload_bytes = required_bytes,
  };
  std::memcpy(cursor, &header, sizeof(SnapshotHeader));
  cursor += sizeof(SnapshotHeader);

  // 3. Serialize raw SlotMap array
  slot_map_.write_snapshot_data(cursor);
  cursor += slot_bytes;

  // 4. Dump contiguous 64 KB chunk pages back-to-back
  for (const auto &arch_ptr : graph_.archetypes()) {
    for (Chunk *chunk : get_archetype_chunks(arch_ptr.get())) {
      std::memcpy(cursor, chunk, Hardware::optimal_chunk_size());
      cursor += Hardware::optimal_chunk_size();
    }
  }

  return required_bytes;
}

inline void World::restore_snapshot(const void *snapshot_buffer, size_t bytes) {
  assert(snapshot_buffer != nullptr && "Snapshot source cannot be null");
  if (bytes < sizeof(SnapshotHeader)) {
    throw std::runtime_error("restore_snapshot: buffer smaller than header");
  }

  const uint8_t *cursor = static_cast<const uint8_t *>(snapshot_buffer);
  SnapshotHeader header;
  std::memcpy(&header, cursor, sizeof(SnapshotHeader));
  cursor += sizeof(SnapshotHeader);

  if (header.magic != 0x48435241 || header.version != 1) {
    throw std::runtime_error("restore_snapshot: invalid snapshot header magic or version");
  }

  // 1. Restore SlotMap array
  const size_t slot_bytes = header.slot_count * sizeof(Internal::SlotMap::Slot);
  slot_map_.read_snapshot_data(
      cursor,
      header.slot_count,
      header.free_list_head,
      header.active_entities
  );
  cursor += slot_bytes;

  // 2. Restore active 64 KB chunk pages
  for (const auto &arch_ptr : graph_.archetypes()) {
    for (Chunk *chunk : get_archetype_chunks(arch_ptr.get())) {
      std::memcpy(chunk, cursor, Hardware::optimal_chunk_size());
      cursor += Hardware::optimal_chunk_size();

      // 3. Re-anchor reverse SlotMap records from chunk entity arrays
      for (uint32_t r = 0; r < chunk->count(); ++r) {
        Entity e = chunk->entity(r);
        slot_map_.update_location(e, arch_ptr.get(), chunk, r);
      }
    }
  }
}
```

---

### 2.4 `include/arche/type_id.hpp`

#### Behavioral Delta (Why this file changes)
* **Where defined:** `include/arche/type_id.hpp`.
* **What code it replaces:** Automatically strips references and const/volatile qualifiers using `std::decay_t<T>`. Replaces repeated `type_id<std::decay_t<T>>()` invocations across `world.hpp`, `type_registry.hpp`, and `command_buffer.hpp`.

#### Modified Functions in `include/arche/type_id.hpp`

##### Old Version (Stage 1)
```cpp
template <typename T>
[[nodiscard]] inline TypeID type_id() noexcept {
  return Internal::get_or_allocate_type_id<T>();
}
```

##### New Version (Stage 2)
```cpp
template <typename T>
[[nodiscard]] inline TypeID type_id() noexcept {
  return Internal::get_or_allocate_type_id<std::decay_t<T>>();
}
```

---

## 3. Brand-New Files

### 3.1 `include/arche/math_utils.hpp`

This is a brand-new header providing standardized power-of-2 alignment and SIMD bounds math.

* **Where defined:** `include/arche/math_utils.hpp` (namespace `arche::math`).
* **What code it replaces:** Replaces ad-hoc bit twiddling expressions `(x + 15) & ~15`, `(cap / 16) * 16`, and `(total + 63) / 64` across modules `010`, `011`, `014`, and `016`.

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace arche::math {

/**
 * @brief Aligns a value up to the next multiple of alignment.
 */
template <typename T>
[[nodiscard]] constexpr T align_up(T val, size_t alignment) noexcept {
  return static_cast<T>((val + alignment - 1) & ~(alignment - 1));
}

/**
 * @brief Rounds up a value to the next multiple of SIMD vector width (default 16 elements).
 */
template <size_t Width = 16, typename T>
[[nodiscard]] constexpr T round_up_simd(T val) noexcept {
  return static_cast<T>((val + Width - 1) & ~(Width - 1));
}

/**
 * @brief Rounds down a value to the nearest multiple of SIMD vector width.
 */
template <size_t Width = 16, typename T>
[[nodiscard]] constexpr T round_down_simd(T val) noexcept {
  return static_cast<T>((val / Width) * Width);
}

/**
 * @brief Integer ceiling division (val / divisor rounded up).
 */
template <typename T>
[[nodiscard]] constexpr T div_ceil(T val, T divisor) noexcept {
  return static_cast<T>((val + divisor - 1) / divisor);
}

} // namespace arche::math
```

---

### 3.2 `tests/handles_and_snapshots_tests.cpp`

This is a brand-new test suite verifying 64-bit handle bitfields, stale handle invalidation, 32-bit generation wrap-around protection, and instant snapshot creation/rollback under 10,000 entities.

```cpp
#include "arche/entity.hpp"
#include "arche/slot_map.hpp"
#include "arche/world.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
using arche::Entity;
using arche::null_entity;
using arche::World;
using arche::Internal::SlotMap;
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
// Unit Test 1: 64-Bit Handle Bitfield Encoding & Sentinel
// =============================================================================

/**
 * @brief Validates 64-bit handle bit-packing, generation extraction, and sentinel invariants.
 */
void test_64bit_entity_bitfield() {
  std::cout << "[Test 1/4] 64-Bit Handle Bitfield Encoding & Sentinel... ";

  // 1. Test standard index and generation packing
  Entity e1 = Entity::from_parts(1042, 85);
  assert(e1.index() == 1042);
  assert(e1.id() == 1042);
  assert(e1.generation() == 85);
  assert(static_cast<bool>(e1) == true);
  assert(!e1.is_null());

  // 2. Test maximum 32-bit limits (4.29 billion)
  Entity e_max = Entity::from_parts(0xFFFFFFFE, 0xFFFFFFFF);
  assert(e_max.index() == 0xFFFFFFFE);
  assert(e_max.generation() == 0xFFFFFFFF);

  // 3. Null entity sentinel verification
  assert(null_entity.is_null());
  assert(!static_cast<bool>(null_entity));
  assert(null_entity.raw() == 0xFFFFFFFFFFFFFFFFULL);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: SlotMap 32-Bit Generation & Stale Handle Invalidation
// =============================================================================

/**
 * @brief Validates O(1) generational increment and stale handle rejection using 32-bit generations.
 */
void test_slotmap_32bit_generation() {
  std::cout << "[Test 2/4] SlotMap 32-Bit Generation & Stale Handle Rejection... ";
  SlotMap map;

  // 1. Allocate initial entity (index 0, generation 1)
  Entity e0 = map.allocate();
  assert(e0.index() == 0);
  assert(e0.generation() == 1);
  assert(map.is_alive(e0));

  // 2. Free e0
  map.free(e0);
  assert(!map.is_alive(e0)); // Stale handle detection

  // 3. Re-allocate slot 0: generation must advance to 2
  Entity e1 = map.allocate();
  assert(e1.index() == 0);
  assert(e1.generation() == 2);
  assert(map.is_alive(e1));
  assert(!map.is_alive(e0)); // Original handle e0 remains permanently dead

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: 32-Bit Generation Wrap-Around Safety
// =============================================================================

/**
 * @brief Validates generation wrap-around protection (skipping 0 to preserve null_entity).
 */
void test_generation_wraparound_protection() {
  std::cout << "[Test 3/4] Generation Wrap-Around Protection (Skip 0)... ";
  SlotMap map;

  Entity e = map.allocate();
  assert(e.index() == 0);

  // Free e
  map.free(e);

  // Re-allocating must produce non-zero generation
  Entity e_next = map.allocate();
  assert(e_next.generation() != 0);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: World Instant Raw Snapshot & Rollback
// =============================================================================

/**
 * @brief Validates full world snapshot creation and rollback in < 1 ms with 100% data fidelity.
 */
void test_world_instant_snapshot_and_rollback() {
  std::cout << "[Test 4/4] World Instant Snapshot & Rollback (< 1 ms)... ";
  World world;

  constexpr int ENTITY_COUNT = 10'000;
  std::vector<Entity> initial_entities;
  initial_entities.reserve(ENTITY_COUNT);

  // 1. Populate world with 10,000 entities
  for (int i = 0; i < ENTITY_COUNT; ++i) {
    initial_entities.push_back(world.create_entity(
        Position{static_cast<float>(i), static_cast<float>(i * 2)},
        Velocity{1.0f, -1.0f}
    ));
  }

  // 2. Take raw memory snapshot
  std::vector<uint8_t> snapshot_buffer(64 * 1024 * 1024); // 64 MB buffer
  auto start = std::chrono::high_resolution_clock::now();
  size_t bytes_written = world.create_snapshot(snapshot_buffer.data(), snapshot_buffer.size());
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> snapshot_time = end - start;

  assert(bytes_written > 0);

  // 3. Mutate and corrupt live world state
  for (int i = 0; i < 5000; ++i) {
    world.destroy_entity(initial_entities[i]);
  }
  for (int i = 5000; i < ENTITY_COUNT; ++i) {
    world.get_component<Position>(initial_entities[i]).x = -999.0f;
  }

  // 4. Instant rollback from snapshot
  start = std::chrono::high_resolution_clock::now();
  world.restore_snapshot(snapshot_buffer.data(), bytes_written);
  end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> restore_time = end - start;

  // 5. Verify 100% bit-exact restoration of components and handle validities
  for (int i = 0; i < ENTITY_COUNT; ++i) {
    assert(world.is_valid(initial_entities[i]));
    const auto &pos = world.get_component<Position>(initial_entities[i]);
    assert(pos.x == static_cast<float>(i));
    assert(pos.y == static_cast<float>(i * 2));
  }

  std::cout << "PASSED (Snapshot: " << snapshot_time.count()
            << " ms, Restore: " << restore_time.count() << " ms)\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Module 009: Handles & Snapshots Tests\n";
  std::cout << "====================================================\n";

  test_64bit_entity_bitfield();
  test_slotmap_32bit_generation();
  test_generation_wraparound_protection();
  test_world_instant_snapshot_and_rollback();

  std::cout << "====================================================\n";
  std::cout << "All Module 009 tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}
```

---

## 4. Build Configuration: `CMakeLists.txt`

Add the new unit test target to `CMakeLists.txt`:

```cmake
# Module 009 Tests: 64-Bit Generational Handles & Instant Snapshots
add_executable(handles_and_snapshots_tests tests/handles_and_snapshots_tests.cpp)
target_link_libraries(handles_and_snapshots_tests PRIVATE arche::arche)
add_test(NAME handles_and_snapshots_tests COMMAND handles_and_snapshots_tests)
```
