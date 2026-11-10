#pragma once

#include "arche/entity.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace arche::Internal {

struct EntityRecord {
  Archetype *archetype{nullptr};
  Chunk *chunk{nullptr};
  uint32_t row{0};
};

class SlotMap {
public:
  static constexpr uint32_t INDEX_NONE = Entity::INDEX_NONE;
  static constexpr uint32_t MAX_ENTITIES = Entity::INDEX_MASK;

  SlotMap() = default;

  [[nodiscard]] Entity allocate() {
    uint32_t index = 0;
    uint8_t generation = 1;

    if (free_list_head != INDEX_NONE) {
      index = free_list_head;
      free_list_head = slots[index].next_free;
      generation = slots[index].generation;
    } else {
      if (slots.size() >= MAX_ENTITIES) {
        throw std::runtime_error(
            "arche::SlotMap: capacity limit reached (16.7M entities)");
      }
      index = static_cast<uint32_t>(slots.size());
      slots.emplace_back();
      slots[index].generation = 1;
      generation = 1;
    }

    slots[index].record = EntityRecord{};
    ++active_count;
    return Entity::from_parts(index, generation);
  }

  void free(Entity e) noexcept {
    const uint32_t index = e.index();

    uint8_t next_gen = slots[index].generation + 1;
    if (next_gen == 0) {
      next_gen = 1; // Preserve generation 0 for null_entity
    }
    slots[index].generation = next_gen;

    // Link into intrusive free list
    slots[index].next_free = free_list_head;
    free_list_head = index;

    --active_count;
  }

  [[nodiscard]] bool is_alive(Entity e) const noexcept {
    const uint32_t index = e.index();
    if (index >= slots.size() || e == null_entity) {
      return false;
    }
    return slots[index].generation == e.generation();
  }

  [[nodiscard]] EntityRecord &get_record(Entity e) {
    return slots[e.index()].record;
  }

  [[nodiscard]] const EntityRecord &get_record(Entity e) const {
    return slots[e.index()].record;
  }

  void update_location(Entity e, Archetype *arch, Chunk *chunk,
                       uint32_t row) noexcept {
    slots[e.index()].record = EntityRecord{arch, chunk, row};
  }

  void update_row(Entity e, uint32_t newRow) noexcept {
    slots[e.index()].record.row = newRow;
  }

  [[nodiscard]] size_t size() const noexcept { return active_count; }

  [[nodiscard]] size_t capacity() const noexcept { return slots.size(); }

  void clear() noexcept {
    slots.clear();
    free_list_head = INDEX_NONE;
    active_count = 0;
  }

private:
  struct Slot {
    uint8_t generation{1};
    union {
      EntityRecord record;
      uint32_t next_free{INDEX_NONE};
    };

    Slot() : generation(1), next_free(INDEX_NONE) {}
    ~Slot() {} // Trivial cleanup
  };

  std::vector<Slot> slots;
  uint32_t free_list_head{INDEX_NONE};
  size_t active_count{0};
};

} // namespace arche::Internal