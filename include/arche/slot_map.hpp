#pragma once

#include "arche/entity.hpp"

#include <cassert>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace arche {
class Archetype;
struct Chunk;
} // namespace arche

namespace arche::Internal {

using arche::Archetype;
using arche::Chunk;

// =============================================================================
// Entity Record
// =============================================================================

/**
 * @struct EntityRecord
 * @brief Tracks the exact physical location of an active entity in archetype
 * chunk memory.
 *
 * @details
 * In an archetype ECS:
 * - When an entity is spawned, mutated (components added/removed), or moved via
 *   Swap-and-Pop, its physical location (Archetype, Chunk, and Row) changes
 *   dynamically.
 * - EntityRecord provides constant-time O(1) indirection from an Entity handle
 *   to its underlying component data in cache-aligned chunk pages.
 * - Memory footprint: 24 bytes on 64-bit platforms (8B pointer + 8B pointer +
 *   4B row + 4B pad). 50,000 active entities occupy ~1.2 MB, fitting
 *   comfortably inside CPU L3 cache.
 */
struct EntityRecord {
  /// Pointer to current Archetype DAG node.
  Archetype *archetype{nullptr};

  /// Pointer to current 64 KB memory chunk page.
  Chunk *chunk{nullptr};

  /// Dense row index within the chunk's SoA slices.
  uint32_t row{0};
};

// =============================================================================
// Generational Slot Map
// =============================================================================

/**
 * @class SlotMap
 * @brief 1-to-1 routing table mapping Entity handles to their chunk memory
 * locations.
 *
 * @details
 * ### What is a SlotMap?
 * In arche-ecs, an `Entity` handle is just a 32-bit ID (24-bit index + 8-bit
 * generation). The `SlotMap` is the central table that resolves an entity's
 * index into its actual physical component data:
 *
 * ```text
 *   Entity Handle (Index: 3)
 *            │
 *            ▼
 *     SlotMap Array
 *   ┌─────────────┐
 *   │  slots[0]   │ ──► [ALIVE] Record: Archetype A, Chunk 0, Row 12
 *   │  slots[1]   │ ──► [DEAD]  next_free = 4
 *   │  slots[2]   │ ──► [ALIVE] Record: Archetype B, Chunk 1, Row 5
 *   │  slots[3]   │ ──► [ALIVE] Record: Archetype A, Chunk 0, Row 13
 *   │  slots[4]   │ ──► [DEAD]  next_free = INDEX_NONE (End of chain)
 *   └─────────────┘
 * ```
 *
 * ### Key Concepts:
 *
 * 1. **Strict 1-to-1 Mapping:**
 *    Each slot holds at most **one single entity** at any time. It is never a
 *    bucket for multiple entities.
 *
 * 2. **Dual-State Slot (`union`):**
 *    A slot is in one of two mutually exclusive states:
 *    - **ALIVE:** Holds an `EntityRecord` (pointing to the entity's current
 *      chunk and row).
 *    - **DEAD:** The entity was destroyed. Its memory is repurposed to store
 *      `next_free` (an array index pointing to the *next available dead slot*).
 *      This forms an intrusive linked list of free slots inside the existing
 *      array, requiring **zero extra heap allocations** to track free slots.
 *
 * 3. **Generational Safety (Use-After-Free Prevention):**
 *    Each slot has a `generation` counter (1..255).
 *    - When an entity at Slot 3 is destroyed, Slot 3's generation increments.
 *    - If an old system tries to access Slot 3 using the obsolete handle:
 *      `slot.generation != handle.generation`
 *    - The check fails instantly in 1 CPU cycle, preventing memory corruption.
 *
 * 4. **O(1) Guarantees:**
 *    - `allocate()`: Reuses the head dead slot from the free list (~4 CPU ops).
 *    - `free()`: Increments generation, pushes slot to free list (~5 CPU ops).
 *    - `is_alive()`: Compares generation equality in 1 CPU clock cycle.
 *    - `update_row()`: Updates row during Swap-and-Pop in 1 store instruction.
 */
class SlotMap {

public:
  // --- Constants -------------------------------------------------------------

  /// Sentinel constant representing an invalid slot index or empty free list
  /// (~0U).
  static constexpr uint32_t INDEX_NONE_32 = 0xFFFFFFFFU;

  /// Maximum theoretical entities supported by the 32-bit index bitfield
  /// (~4.29B).
  static constexpr uint64_t MAX_ENTITIES = 0xFFFFFFFFULL;

  // --- Slot Definition -------------------------------------------------------

  /**
   * @struct Slot
   * @brief Storage cell multiplexing between a live EntityRecord and a dead
   * free-list link.
   *
   * @note Total size is exactly 32 bytes (power-of-two): fits 2 slots per
   * 64-byte x86 cache line, or 4 slots per 128-byte Apple Silicon cache line.
   */
  struct Slot {
    /// Incremented every time this slot is recycled (1..255).
    uint32_t generation{1};
    uint32_t padding{0};

    union {
      /// Valid when ALIVE: Physical location of the entity's components.
      EntityRecord record;

      /// Valid when DEAD: Array index of the next dead slot in the free-list
      /// chain.
      uint32_t next_free{INDEX_NONE_32};
    };

    Slot() : generation(1), padding(0), next_free(INDEX_NONE_32) {}
    ~Slot() = default;
  };

private:
  static_assert(sizeof(Slot) == 32, "Slot must remain exactly 32 bytes");

  // --- Member Data -----------------------------------------------------------

  /// Flat, dense slot array in memory.
  std::vector<Slot> slots;

  /// Head index of intrusive free list.
  uint32_t free_list_head{INDEX_NONE_32};

  /// Number of currently living entities.
  size_t active_count{0};

  // --- Internal Free-List & Allocation Helpers -------------------------------

  /**
   * @brief Pops an existing slot index from the head of the intrusive free
   * list.
   * @return Reusable slot index whose generation has already been incremented.
   */
  [[nodiscard]] inline uint32_t pop_free_slot() noexcept {
    const uint32_t index = free_list_head;
    free_list_head = slots[index].next_free;
    return index;
  }

  /**
   * @brief Appends a new uninitialized slot to the back of the dense array.
   * @return Newly allocated slot index with generation initialized to 1.
   * @throws std::runtime_error If maximum slot capacity is reached.
   */
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

  /**
   * @brief Monotonically advances a slot's generation counter.
   * @details Uses advance_nonzero to skip 0 on wrap-around, preserving 0 as
   * a sentinel and preventing null_entity collision.
   *
   * @param gen Current generation value (1 .. 4,294,967,295).
   * @return Next valid generation value (1 .. 4,294,967,295).
   */
  [[nodiscard]] static constexpr uint32_t advance_generation(
      uint32_t gen
  ) noexcept {
    return advance_nonzero(gen);
  }

  /**
   * @brief Pushes a vacated slot index onto the head of the intrusive free
   * list.
   * @param index Slot index to recycle.
   */
  inline void push_free_slot(uint32_t index) noexcept {
    slots[index].generation = advance_generation(slots[index].generation);
    slots[index].next_free = free_list_head;
    free_list_head = index;
  }

public:
  // --- Constructors & Destructors --------------------------------------------

  SlotMap() = default;
  ~SlotMap() = default;

  SlotMap(const SlotMap &) = delete;
  SlotMap &operator=(const SlotMap &) = delete;

  SlotMap(SlotMap &&) noexcept = default;
  SlotMap &operator=(SlotMap &&) noexcept = default;

  // --- Allocation & Deallocation ---------------------------------------------

  /**
   * @brief Allocates an entity handle in strict O(1) time.
   *
   * @details
   * Checks the intrusive free list:
   * - If non-empty, pops the head slot index and retains its current
   *   incremented generation.
   * - If empty, appends a new slot (generation initialized to 1).
   *
   * @return Valid generational Entity handle.
   * @throws std::runtime_error If maximum slot capacity (16,777,215 entities)
   * is exceeded.
   */
  [[nodiscard]] Entity allocate() {
    const uint32_t index = (free_list_head != INDEX_NONE_32)
        ? pop_free_slot()
        : append_new_slot();

    slots[index].record = EntityRecord{};
    ++active_count;
    return Entity::from_parts(index, slots[index].generation);
  }

  /**
   * @brief Frees an active entity in strict O(1) time.
   *
   * @details
   * Increments the slot's generation counter (skipping 0 to preserve
   * null_entity), and links the slot into the head of the intrusive free list.
   * Any subsequent checks using stale handles will fail immediately.
   *
   * @param e Entity handle to destroy.
   */
  void free(Entity e) noexcept {
    push_free_slot(e.index());
    --active_count;
  }

  // --- Bulk Allocation & Deallocation ----------------------------------------

  /**
   * @brief Allocates N entity handles in a single contiguous batch operation.
   *
   * @details
   * 1. Pops all available slots from the intrusive free list first.
   * 2. If additional slots are required, resizes the underlying slots array
   * once and initializes new slots with generation 1.
   *
   * @param count Number of entity handles to allocate.
   * @param out_handles Output array of at least `count` Entity handles.
   * @throws std::runtime_error If maximum slot capacity is exceeded.
   */
  void allocate_batch(size_t count, Entity *out_handles) {
    assert(out_handles != nullptr && "Output handles pointer cannot be null");
    if (count == 0) {
      return;
    }

    size_t allocated = 0;

    // 1. Drain available slots from the intrusive free list
    while (free_list_head != INDEX_NONE_32 && allocated < count) {
      const uint32_t idx = pop_free_slot();
      slots[idx].record = EntityRecord{};
      out_handles[allocated++] = Entity::from_parts(idx, slots[idx].generation);
    }

    // 2. Bulk-expand the dense slots vector for the remaining count
    if (allocated < count) {
      const size_t remaining = count - allocated;
      const size_t current_size = slots.size();
      if (current_size + remaining > MAX_ENTITIES) {
        throw std::runtime_error(
            "arche::SlotMap: capacity limit reached (4.29B entities)"
        );
      }

      slots.resize(current_size + remaining);
      for (size_t i = 0; i < remaining; ++i) {
        const uint32_t idx = static_cast<uint32_t>(current_size + i);
        slots[idx].generation = 1;
        slots[idx].padding = 0;
        slots[idx].record = EntityRecord{};
        out_handles[allocated++] = Entity::from_parts(idx, 1);
      }
    }

    active_count += count;
  }

  /**
   * @brief Frees a batch of entity handles in a single cache-friendly pass.
   *
   * @param handles Span of Entity handles to invalidate and recycle.
   */
  void free_batch(std::span<const Entity> handles) noexcept {
    for (Entity e : handles) {
      if (is_alive(e)) {
        push_free_slot(e.index());
        --active_count;
      }
    }
  }

  // --- Liveness & Record Inspection ------------------------------------------

  /**
   * @brief Validates if an entity handle is currently active and alive in O(1).
   *
   * @param e Entity handle to inspect.
   * @return true if entity is alive and generation matches; false if stale or
   * null.
   */
  [[nodiscard]] bool is_alive(Entity e) const noexcept {
    const uint32_t index = e.index();
    if (index >= slots.size() || e.is_null()) {
      return false;
    }
    return slots[index].generation == e.generation();
  }

  // --- Unified Entity Lookup & Validation Fast-Path --------------------------

  /**
   * @brief Combines liveness check and EntityRecord retrieval in a single
   * branch.
   * @details Replaces redundant dual-check calls `if (!slot_map_.is_alive(e))
   * return; const EntityRecord &rec = slot_map_.get_record(e);` across World
   * and CommandBuffer.
   *
   * @param e Generational Entity handle.
   * @return Pointer to valid EntityRecord, or nullptr if entity is
   * dead/invalid.
   */
  [[nodiscard]] inline const EntityRecord *find_record(
      Entity e
  ) const noexcept {
    const uint32_t idx = e.index();
    if (idx < slots.size() && slots[idx].generation == e.generation() &&
        !e.is_null()) {
      return &slots[idx].record;
    }
    return nullptr;
  }

  /**
   * @brief Combines liveness check and mutable EntityRecord retrieval in a
   * single branch.
   *
   * @param e Entity handle to look up.
   * @return Pointer to valid mutable EntityRecord, or nullptr if entity is
   * dead/invalid.
   */
  [[nodiscard]] inline EntityRecord *find_record(Entity e) noexcept {
    const uint32_t idx = e.index();
    if (idx < slots.size() && slots[idx].generation == e.generation() &&
        !e.is_null()) {
      return &slots[idx].record;
    }
    return nullptr;
  }

  /**
   * @brief Retrieves a mutable reference to an entity's physical location
   * record.
   * @param e Entity handle.
   * @return Mutable reference to EntityRecord.
   */
  [[nodiscard]] EntityRecord &get_record(Entity e) {
    return slots[e.index()].record;
  }

  /**
   * @brief Retrieves a const reference to an entity's physical location record.
   * @param e Entity handle.
   * @return Const reference to EntityRecord.
   */
  [[nodiscard]] const EntityRecord &get_record(Entity e) const {
    return slots[e.index()].record;
  }

  // --- Snapshot Serialization Primitives -------------------------------------

  /**
   * @brief Returns the total byte size occupied by the dense slots array.
   * @return Byte size equal to slots.size() * sizeof(Slot).
   */
  [[nodiscard]] inline size_t snapshot_byte_size() const noexcept {
    return slots.size() * sizeof(Slot);
  }

  /**
   * @brief Copies the raw slots buffer directly into a destination memory
   * buffer.
   * @param dest Target memory buffer. Must be pre-allocated to at least
   * snapshot_byte_size().
   */
  inline void write_snapshot_data(void *dest) const noexcept {
    assert(dest != nullptr && "Destination buffer cannot be null");
    std::memcpy(dest, slots.data(), snapshot_byte_size());
  }

  /**
   * @brief Restores the raw slots array and intrusive free-list head directly
   * from memory.
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
   * @brief Exposes the current free-list head index for snapshot header
   * serialization.
   * @return Index of the first free slot, or INDEX_NONE_32 if free list is
   * empty.
   */
  [[nodiscard]] inline uint32_t free_head_index() const noexcept {
    return free_list_head;
  }

  // --- Physical Location Updates (Swap-and-Pop Protocol) ---------------------

  /**
   * @brief Updates an entity's complete physical memory coordinates in O(1).
   *
   * @param e Target entity.
   * @param arch Pointer to new archetype node.
   * @param chunk Pointer to new 64 KB memory chunk.
   * @param row New row index within the chunk.
   */
  void update_location(
      Entity e,
      Archetype *arch,
      Chunk *chunk,
      uint32_t row
  ) noexcept {
    slots[e.index()].record = EntityRecord{arch, chunk, row};
  }

  /**
   * @brief Fast-path update of physical chunk and row during intra-archetype
   * permutation.
   *
   * @param e Generational Entity handle being relocated.
   * @param chunk Pointer to the new physical 64 KB chunk.
   * @param row New row index within the target chunk.
   */
  inline void update_record(Entity e, Chunk *chunk, uint32_t row) noexcept {
    slots[e.index()].record.chunk = chunk;
    slots[e.index()].record.row = row;
  }

  /**
   * @brief Fast-path update of an entity's row index during chunk Swap-and-Pop.
   *
   * @details
   * Executed when a chunk compacts memory by swapping its last row into a
   * vacated row. Updates the relocated entity's row coordinate in a single
   * store instruction.
   *
   * @param e Relocated entity handle.
   * @param newRow New row index in the same chunk.
   */
  void update_row(Entity e, uint32_t newRow) noexcept {
    slots[e.index()].record.row = newRow;
  }

  // --- Size & Capacity Queries -----------------------------------------------

  /**
   * @brief Returns the total count of currently active live entities.
   */
  [[nodiscard]] size_t size() const noexcept { return active_count; }

  /**
   * @brief Returns the total allocated slot capacity in the underlying array.
   */
  [[nodiscard]] size_t capacity() const noexcept { return slots.size(); }

  /**
   * @brief Clears all slots and resets the free list.
   */
  void clear() noexcept {
    slots.clear();
    free_list_head = INDEX_NONE_32;
    active_count = 0;
  }
};

} // namespace arche::Internal

namespace arche {
using Internal::EntityRecord;
using Internal::SlotMap;
} // namespace arche
