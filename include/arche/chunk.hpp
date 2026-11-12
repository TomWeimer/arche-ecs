#pragma once

#include "arche/entity.hpp"
#include "arche/hardware.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

namespace arche {

class Archetype;

namespace Internal {
class SlotMap;
}
using SlotMap = Internal::SlotMap;

struct ComponentColumnLayout;

// =============================================================================
// Cache-Aligned Physical Chunk Header
// =============================================================================

/**
 * @struct ChunkHeader
 * @brief Cache-aligned metadata header located at byte offset 0 of every Chunk
 * buffer.
 *
 * @details
 * Guaranteed to occupy exactly 64 bytes (`alignas(64)`), matching host L1
 * cache-line boundaries to prevent false sharing and split-line penalties
 * across concurrent systems.
 */
struct alignas(64) ChunkHeader {

  /// Owning Archetype describing column layouts.
  Archetype *archetype{nullptr};

  /// Number of currently active entities in this chunk.
  uint32_t count{0};

  /// Maximum entity storage capacity (K) for this chunk.
  uint32_t capacity{0};

  /// Dense index of this chunk within the archetype list.
  uint32_t chunk_index{0};

  /// Intrusive link pointer for ChunkPool recycling.
  ChunkHeader *next{nullptr};

  /// Intrusive reverse link pointer.
  ChunkHeader *prev{nullptr};
};

static_assert(
    std::is_standard_layout_v<ChunkHeader>,
    "ChunkHeader must be standard layout for predictable memory offsets"
);
static_assert(
    sizeof(ChunkHeader) == 64,
    "ChunkHeader must be padded to exactly 64 bytes"
);

// =============================================================================
// Hardware-Adaptive Chunk Storage (Structure of Arrays)
// =============================================================================

/**
 * @struct Chunk
 * @brief Fixed-size memory block that stores entities and their components.
 *
 * @details
 * ### How Chunk Memory Works
 * A Chunk is not a regular struct that holds a pointer to memory. Instead, it
 * is the memory block itself (typically 64 KB).
 *
 * When a chunk is created:
 * 1. A single 64 KB block of memory is allocated.
 * 2. The `Chunk` header is placed at the very start of this block (bytes 0 to
 * 63).
 * 3. The rest of the 64 KB block stores the entity IDs and component arrays:
 *    - Bytes 0 to 63: Chunk header (entity count, capacity, and links).
 *    - Bytes 64 to (64 + 4*K): Array of entity IDs.
 *    - Remaining space: Component arrays, where each component type has its own
 *      continuous section (Structure of Arrays).
 *
 * ### Memory Layout:
 * ```text
 * +-----------------------------------------------------------------------------+
 * | Chunk Header (Bytes 0 to 63) |
 * +-----------------------------------------------------------------------------+
 * | Entity IDs: Entity[0], Entity[1], ... |
 * +-----------------------------------------------------------------------------+
 * | Component Column 0: Position[0], Position[1], ... |
 * +-----------------------------------------------------------------------------+
 * | Component Column 1: Velocity[0], Velocity[1], ... |
 * +-----------------------------------------------------------------------------+
 * | ... other component columns ... |
 * +-----------------------------------------------------------------------------+
 * | Unused remaining space |
 * +-----------------------------------------------------------------------------+
 * ```
 *
 * ### Key Concepts:
 * 1. **No Extra Pointers:** Because the header and data share the same block, a
 *    `Chunk*` pointer points directly to the memory. Getting component data
 * only requires adding a byte offset to `this`.
 * 2. **Fast Memory Access:** Component arrays are lined up with CPU cache
 * boundaries so the processor can read them efficiently without slowdowns.
 * 3. **Swap-and-Pop Removal:** When an entity is removed, the last entity in
 * the chunk moves into its place. This keeps the data packed together without
 * holes.
 */
struct Chunk {

private:
  // --- Member Data -----------------------------------------------------------

  /// Header at byte offset 0.
  /// The rest of the 64 KB block holds the entity IDs and component arrays.
  ChunkHeader header_;

  // --- Constructors & Move Semantics  ----------------------------------------
private:
  /**
   * @brief Private constructor called via placement-new from `allocate()`.
   */
  inline Chunk(
      Archetype *archetype,
      uint32_t capacity,
      uint32_t /*chunk_size*/ = 0
  ) noexcept {
    header_.archetype = archetype;
    header_.capacity = capacity;
    header_.count = 0;
    header_.chunk_index = 0;
    header_.next = nullptr;
    header_.prev = nullptr;
  }

  ~Chunk() = default;

public:
  // Non-copyable, non-movable: represents a fixed physical memory address
  Chunk(const Chunk &) = delete;
  Chunk &operator=(const Chunk &) = delete;
  Chunk(Chunk &&) = delete;
  Chunk &operator=(Chunk &&) = delete;

  // --- Factories -------------------------------------------------------------

  /**
   * @brief Allocates a memory block and sets up the chunk header at the start.
   *
   * @param archetype Archetype that defines the component columns for this
   * chunk.
   * @param capacity Maximum number of entities this chunk can hold.
   * @param chunk_size Size of the memory block in bytes (defaults to 64 KB).
   * @return Pointer to the new Chunk (which is the start of the memory block).
   */
  [[nodiscard]] inline static Chunk *allocate(
      Archetype *archetype,
      uint32_t capacity,
      uint32_t chunk_size
  ) {
    const size_t alignment = std::max(
        Hardware::cache_line_size(), static_cast<size_t>(64)
    );
    void *raw_mem = Hardware::allocate_aligned(chunk_size, alignment);
    assert(raw_mem != nullptr && "Virtual memory allocation for Chunk failed!");

    return new (raw_mem) Chunk(archetype, capacity, chunk_size);
  }

  /**
   * @brief Destructs and frees the aligned chunk buffer back to the host OS.
   *
   * @param chunk Pointer to the Chunk to destroy and deallocate.
   */
  inline static void destroy(Chunk *chunk) noexcept {
    if (!chunk) {
      return;
    }
    chunk->~Chunk();
    Hardware::free_aligned(chunk);
  }

  // --- Header State & Capacity Queries ---------------------------------------

  /**
   * @brief Returns the owning archetype governing this chunk's layout.
   */
  [[nodiscard]] inline Archetype *archetype() const noexcept {
    return header_.archetype;
  }

  /**
   * @brief Returns the number of currently active entities in this chunk.
   */
  [[nodiscard]] inline uint32_t count() const noexcept { return header_.count; }

  /**
   * @brief Returns the maximum number of entities (K) this chunk can store.
   */
  [[nodiscard]] inline uint32_t capacity() const noexcept {
    return header_.capacity;
  }

  /**
   * @brief Returns the index of this chunk within the parent archetype's chunk
   * list.
   */
  [[nodiscard]] inline uint32_t chunk_index() const noexcept {
    return header_.chunk_index;
  }

  /**
   * @brief Tests whether this chunk has reached maximum capacity (`count ==
   * capacity`).
   */
  [[nodiscard]] inline bool is_full() const noexcept {
    return header_.count >= header_.capacity;
  }

  /**
   * @brief Tests whether this chunk contains zero active entities (`count ==
   * 0`).
   */
  [[nodiscard]] inline bool is_empty() const noexcept {
    return header_.count == 0;
  }

  /**
   * @brief Retrieves the next chunk link pointer for intrusive pool free-lists.
   */
  [[nodiscard]] inline ChunkHeader *next() const noexcept {
    return header_.next;
  }

  /**
   * @brief Updates the chunk's index within the parent archetype's chunk array.
   */
  inline void set_chunk_index(uint32_t idx) noexcept {
    header_.chunk_index = idx;
  }

  /**
   * @brief Sets the next intrusive link pointer for pool free-lists.
   */
  inline void set_next(ChunkHeader *n) noexcept { header_.next = n; }

  // --- SoA Physical Slice Resolution -----------------------------------------

  /**
   * @brief Resolves raw pointer to the Entity array slice: `Entity[K]`.
   *
   * @details
   * Located immediately following `ChunkHeader` at byte offset 64.
   *
   * @return Mutable pointer to the base of the Entity handle array.
   */
  [[nodiscard]] inline Entity *entity_slice() noexcept {
    constexpr size_t HEADER_RESERVE = sizeof(ChunkHeader);
    return reinterpret_cast<Entity *>(
        reinterpret_cast<uint8_t *>(this) + HEADER_RESERVE
    );
  }

  /**
   * @brief Resolves read-only pointer to the Entity array slice: `const
   * Entity[K]`.
   */
  [[nodiscard]] inline const Entity *entity_slice() const noexcept {
    constexpr size_t HEADER_RESERVE = sizeof(ChunkHeader);
    return reinterpret_cast<const Entity *>(
        reinterpret_cast<const uint8_t *>(this) + HEADER_RESERVE
    );
  }

  /**
   * @brief Resolves raw pointer to a component column slice by precomputed byte
   * offset.
   *
   * @param chunk_byte_offset Offset in bytes calculated during archetype layout
   * construction.
   * @return Mutable pointer to component column array base.
   */
  [[nodiscard]] inline void *component_slice(
      uint32_t chunk_byte_offset
  ) noexcept {
    return reinterpret_cast<uint8_t *>(this) + chunk_byte_offset;
  }

  /**
   * @brief Resolves read-only pointer to a component column slice by byte
   * offset.
   */
  [[nodiscard]] inline const void *component_slice(
      uint32_t chunk_byte_offset
  ) const noexcept {
    return reinterpret_cast<const uint8_t *>(this) + chunk_byte_offset;
  }

  // --- Entity Insertion & Compaction -----------------------------------------

  /**
   * @brief Appends an entity handle into the chunk's dense entity slice.
   *
   * @param entity Generational Entity handle being allocated into this chunk.
   * @return Row index where the entity was stored.
   */
  inline uint32_t append_entity(Entity entity) noexcept {
    assert(header_.count < header_.capacity);
    const uint32_t row = header_.count++;
    entity_slice()[row] = entity;
    return row;
  }

  /**
   * @brief Removes an entity from the chunk using the Swap-and-Pop technique.
   *
   * @details
   * Instead of shifting all elements down (O(N) slow) or leaving an empty gap
   * (fragmentation), Swap-and-Pop moves the very last entity in the chunk into
   * the vacated row in O(1) time and shrinks the active entity count.
   *
   * @param row Dense row index being vacated.
   * @param slot_map Central SlotMap to update for the relocated entity.
   * @param layouts Precomputed column layouts defining sizes, offsets, and type
   *                descriptors.
   */
  void swap_and_pop(
      uint32_t row,
      SlotMap &slot_map,
      std::span<const ComponentColumnLayout> layouts
  );
};

} // namespace arche
