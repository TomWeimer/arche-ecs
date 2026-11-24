module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

export module arche:slot_map;

import :entity;
import :entity.detail.entity_record;

export namespace arche::internal {

class Archetype;
struct Chunk;

/**
 * @brief Advances a 32-bit generation integer, skipping zero.
 *
 * Generation zero is reserved across arche-ecs as an unallocated or tombstone marker.
 */
[[nodiscard]] inline constexpr uint32_t advance_nonzero(uint32_t val) noexcept {
  const uint32_t next = val + 1;
  return (next == 0) ? 1 : next;
}

/**
 * @class SlotMap
 * @brief Generational 1-to-1 routing table mapping Entity handles to chunk memory locations.
 *
 * Provides O(1) allocation, generational recycling, constant-time handle validation,
 * and chunk coordinate lookup (archetype pointer, chunk pointer, row offset).
 * Memory is backed by a contiguous array of 32-byte Slot records.
 */
class SlotMap {
public:
  /// Sentinel value indicating the end of the embedded free list.
  static constexpr uint32_t INDEX_NONE_32 = 0xFFFFFFFFU;

  /// Theoretical upper limit of entities manageable by a 32-bit slot index.
  static constexpr uint64_t MAX_ENTITIES = 0xFFFFFFFFULL;

  /**
   * @struct Slot
   * @brief 32-byte generational slot record storing location or free-list linkage.
   */
  struct Slot {
    uint32_t generation{1};
    uint32_t is_alive_flag{0};

    union {
      EntityRecord record;
      uint32_t next_free{INDEX_NONE_32};
    };

    Slot() : generation(1), is_alive_flag(0), next_free(INDEX_NONE_32) {}
    ~Slot() = default;
  };

private:
  static_assert(sizeof(Slot) == 32, "Slot must remain exactly 32 bytes");

  std::vector<Slot> slots;
  uint32_t free_list_head{INDEX_NONE_32};
  size_t active_count{0};

  [[nodiscard]] uint32_t pop_free_slot() noexcept;
  [[nodiscard]] uint32_t append_new_slot();
  void push_free_slot(uint32_t index) noexcept;

  [[nodiscard]] static constexpr uint32_t advance_generation(
      uint32_t gen
  ) noexcept {
    return advance_nonzero(gen);
  }

public:
  SlotMap() = default;
  ~SlotMap() = default;

  SlotMap(const SlotMap &) = delete;
  SlotMap &operator=(const SlotMap &) = delete;

  SlotMap(SlotMap &&) noexcept = default;
  SlotMap &operator=(SlotMap &&) noexcept = default;

  // --- Entity Allocation & Deallocation --------------------------------------

  /**
   * @brief Allocates a new Entity handle, recycling from the free list or growing the table.
   * @return A valid 64-bit Entity handle containing slot index and current generation.
   */
  [[nodiscard]] Entity allocate();

  /**
   * @brief Frees an Entity handle, advancing its generation and prepending to the free list.
   * @param e Entity handle to deallocate.
   */
  void free(Entity e) noexcept;

  /**
   * @brief High-performance bulk allocation recycling free slots before appending new capacity.
   * @param count Number of handles to allocate.
   * @param out_handles Pre-allocated destination buffer receiving the handles.
   */
  void allocate_batch(size_t count, Entity *out_handles);

  /**
   * @brief Bulk deallocates a batch of entity handles.
   * @param handles Span of entities to recycle.
   */
  void free_batch(std::span<const Entity> handles) noexcept;

  // --- Validation & Resolution -----------------------------------------------

  /**
   * @brief Reconstructs a valid Entity handle from a 32-bit slot index, or std::nullopt if dead.
   */
  [[nodiscard]] std::optional<Entity> resolve(uint32_t index) const noexcept;

  /**
   * @brief Returns true if the entity handle matches its slot's active generation.
   */
  [[nodiscard]] bool is_alive(Entity e) const noexcept;

  /**
   * @brief Alias for is_alive. Returns true if the entity currently exists.
   */
  [[nodiscard]] bool contains(Entity e) const noexcept;

  // --- Record Accessors ------------------------------------------------------

  /**
   * @brief Retrieves the read-only chunk coordinate record for an alive entity.
   */
  [[nodiscard]] const EntityRecord &get(Entity e) const;

  /**
   * @brief Read-only accessor for entity coordinate record.
   */
  [[nodiscard]] const EntityRecord &get_record(Entity e) const;

  /**
   * @brief Mutable accessor for entity coordinate record.
   */
  [[nodiscard]] EntityRecord &get_record(Entity e);

  /**
   * @brief Safely finds an entity record, returning nullptr if dead or out of bounds.
   */
  [[nodiscard]] const EntityRecord *find_record(Entity e) const noexcept;

  /**
   * @brief Safely finds a mutable entity record, returning nullptr if dead or out of bounds.
   */
  [[nodiscard]] EntityRecord *find_record(Entity e) noexcept;

  /**
   * @brief Mutable accessor for entity coordinate record.
   */
  [[nodiscard]] EntityRecord &get_mut(Entity e);

  // --- Location Synchronization ----------------------------------------------

  /**
   * @brief Updates the full physical location of an entity following relocation or swap-and-pop.
   */
  void update_location(
      Entity e,
      Archetype *arch,
      Chunk *chunk,
      uint32_t row
  ) noexcept;

  /**
   * @brief Updates chunk pointer and local row within the same archetype.
   */
  void update_record(Entity e, Chunk *chunk, uint32_t row) noexcept;

  /**
   * @brief Updates archetype, chunk pointer, and local row.
   */
  void update_record(
      Entity e,
      Archetype *archetype,
      Chunk *chunk,
      uint32_t row
  ) noexcept;

  /**
   * @brief Fast update of row index within the existing chunk.
   */
  void update_row(Entity e, uint32_t newRow) noexcept;

  // --- Snapshot Serialization ------------------------------------------------

  /**
   * @brief Computes total raw byte capacity required to serialize the slot table.
   */
  [[nodiscard]] size_t snapshot_byte_size() const noexcept;

  /**
   * @brief Blits raw slot table memory directly into a destination snapshot buffer.
   */
  void write_snapshot_data(void *dest) const noexcept;

  /**
   * @brief Restores slot table memory from a raw snapshot buffer.
   */
  void read_snapshot_data(
      const void *src,
      size_t slot_count,
      uint32_t free_head,
      size_t count
  );

  /**
   * @brief Returns the index of the first free slot in the recycling list.
   */
  [[nodiscard]] uint32_t free_head_index() const noexcept;

  // --- Metrics & Maintenance -------------------------------------------------

  /**
   * @brief Returns the number of currently active entities.
   */
  [[nodiscard]] size_t size() const noexcept;

  /**
   * @brief Returns the total allocated slot capacity (active + recycled).
   */
  [[nodiscard]] size_t capacity() const noexcept;

  /**
   * @brief Returns true if zero entities are currently active.
   */
  [[nodiscard]] bool is_empty() const noexcept;

  /**
   * @brief Clears all slots and resets allocation state.
   */
  void clear() noexcept;
};

} // namespace arche::internal
