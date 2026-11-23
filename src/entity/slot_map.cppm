module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

export module arche:slot_map;

import :entity;
import :entity.detail.entity_record;

export namespace arche {

class Archetype;
struct Chunk;

namespace Internal {

[[nodiscard]] inline constexpr uint32_t advance_nonzero(uint32_t val) noexcept {
  const uint32_t next = val + 1;
  return (next == 0) ? 1 : next;
}

} // namespace Internal

/**
 * @class SlotMap
 * @brief 1-to-1 routing table mapping Entity handles to their chunk memory
 * locations.
 */
class SlotMap {
public:
  static constexpr uint32_t INDEX_NONE_32 = 0xFFFFFFFFU;
  static constexpr uint64_t MAX_ENTITIES = 0xFFFFFFFFULL;

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

  [[nodiscard]] inline uint32_t pop_free_slot() noexcept {
    const uint32_t index = free_list_head;
    free_list_head = slots[index].next_free;
    return index;
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

  [[nodiscard]] static constexpr uint32_t advance_generation(
      uint32_t gen
  ) noexcept {
    return Internal::advance_nonzero(gen);
  }

  inline void push_free_slot(uint32_t index) noexcept {
    slots[index].generation = advance_generation(slots[index].generation);
    slots[index].is_alive_flag = 0;
    slots[index].next_free = free_list_head;
    free_list_head = index;
  }

public:
  SlotMap() = default;
  ~SlotMap() = default;

  SlotMap(const SlotMap &) = delete;
  SlotMap &operator=(const SlotMap &) = delete;

  SlotMap(SlotMap &&) noexcept = default;
  SlotMap &operator=(SlotMap &&) noexcept = default;

  [[nodiscard]] Entity allocate() {
    const uint32_t index = (free_list_head != INDEX_NONE_32)
        ? pop_free_slot()
        : append_new_slot();

    slots[index].record = EntityRecord{};
    slots[index].is_alive_flag = 1;
    ++active_count;
    return Entity::from_parts(index, slots[index].generation);
  }

  void free(Entity e) noexcept {
    push_free_slot(e.index());
    --active_count;
  }

  void allocate_batch(size_t count, Entity *out_handles) {
    assert(out_handles != nullptr && "Output handles pointer cannot be null");
    if (count == 0) {
      return;
    }

    size_t allocated = 0;

    while (free_list_head != INDEX_NONE_32 && allocated < count) {
      const uint32_t idx = pop_free_slot();
      slots[idx].record = EntityRecord{};
      slots[idx].is_alive_flag = 1;
      out_handles[allocated++] = Entity::from_parts(idx, slots[idx].generation);
    }

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
        slots[idx].is_alive_flag = 1;
        slots[idx].record = EntityRecord{};
        out_handles[allocated++] = Entity::from_parts(idx, 1);
      }
    }

    active_count += count;
  }

  void free_batch(std::span<const Entity> handles) noexcept {
    for (Entity e : handles) {
      if (is_alive(e)) {
        push_free_slot(e.index());
        --active_count;
      }
    }
  }

  [[nodiscard]] inline std::optional<Entity> resolve(
      uint32_t index
  ) const noexcept {
    if (index >= slots.size() || !slots[index].is_alive_flag) {
      return std::nullopt;
    }
    return Entity::from_parts(index, slots[index].generation);
  }

  [[nodiscard]] inline bool is_alive(Entity e) const noexcept {
    const uint32_t index = e.index();
    if (index >= slots.size())
      return false;
    return slots[index].is_alive_flag == 1 &&
        slots[index].generation == e.generation();
  }

  [[nodiscard]] inline const EntityRecord &get(Entity e) const {
    assert(
        is_alive(e) &&
        "arche::SlotMap: attempt to access dead or unallocated entity"
    );
    return slots[e.index()].record;
  }

  [[nodiscard]] inline const EntityRecord &get_record(Entity e) const {
    return get(e);
  }

  [[nodiscard]] inline EntityRecord &get_record(Entity e) {
    assert(
        is_alive(e) &&
        "arche::SlotMap: attempt to access dead or unallocated entity"
    );
    return slots[e.index()].record;
  }

  [[nodiscard]] inline const EntityRecord *find_record(
      Entity e
  ) const noexcept {
    const uint32_t idx = e.index();
    if (idx < slots.size() && slots[idx].generation == e.generation() &&
        slots[idx].is_alive_flag == 1) {
      return &slots[idx].record;
    }
    return nullptr;
  }

  [[nodiscard]] inline EntityRecord *find_record(Entity e) noexcept {
    const uint32_t idx = e.index();
    if (idx < slots.size() && slots[idx].generation == e.generation() &&
        slots[idx].is_alive_flag == 1) {
      return &slots[idx].record;
    }
    return nullptr;
  }

  [[nodiscard]] inline EntityRecord &get_mut(Entity e) {
    assert(
        is_alive(e) &&
        "arche::SlotMap: attempt to access dead or unallocated entity"
    );
    return slots[e.index()].record;
  }

  [[nodiscard]] inline size_t snapshot_byte_size() const noexcept {
    return slots.size() * sizeof(Slot);
  }

  inline void write_snapshot_data(void *dest) const noexcept {
    assert(dest != nullptr && "Destination buffer cannot be null");
    std::memcpy(dest, slots.data(), snapshot_byte_size());
  }

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

  [[nodiscard]] inline uint32_t free_head_index() const noexcept {
    return free_list_head;
  }

  inline void update_location(
      Entity e,
      Archetype *arch,
      Chunk *chunk,
      uint32_t row
  ) noexcept {
    slots[e.index()].record = EntityRecord{arch, chunk, row};
  }

  inline void update_record(Entity e, Chunk *chunk, uint32_t row) noexcept {
    slots[e.index()].record.chunk = chunk;
    slots[e.index()].record.row = row;
  }

  inline void update_record(
      Entity e,
      Archetype *archetype,
      Chunk *chunk,
      uint32_t row
  ) noexcept {
    slots[e.index()].record.archetype = archetype;
    slots[e.index()].record.chunk = chunk;
    slots[e.index()].record.row = row;
  }

  inline void update_row(Entity e, uint32_t newRow) noexcept {
    slots[e.index()].record.row = newRow;
  }

  [[nodiscard]] size_t size() const noexcept { return active_count; }
  [[nodiscard]] size_t capacity() const noexcept { return slots.size(); }

  void clear() noexcept {
    slots.clear();
    free_list_head = INDEX_NONE_32;
    active_count = 0;
  }
};

namespace Internal {
using SlotMap = arche::SlotMap;
}

} // namespace arche
