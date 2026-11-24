module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

export module arche:entity.detail.slot_map_impl;

import :slot_map;

export namespace arche {

// =============================================================================
// SlotMap Lifecycle & Allocation
// =============================================================================

uint32_t SlotMap::pop_free_slot() noexcept {
  const uint32_t index = free_list_head;
  free_list_head = slots[index].next_free;
  return index;
}

uint32_t SlotMap::append_new_slot() {
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

void SlotMap::push_free_slot(uint32_t index) noexcept {
  slots[index].generation = advance_generation(slots[index].generation);
  slots[index].is_alive_flag = 0;
  slots[index].next_free = free_list_head;
  free_list_head = index;
}

Entity SlotMap::allocate() {
  const uint32_t index = (free_list_head != INDEX_NONE_32)
      ? pop_free_slot()
      : append_new_slot();

  slots[index].record = EntityRecord{};
  slots[index].is_alive_flag = 1;
  ++active_count;
  return Entity::from_parts(index, slots[index].generation);
}

void SlotMap::free(Entity e) noexcept {
  push_free_slot(e.index());
  --active_count;
}

void SlotMap::allocate_batch(size_t count, Entity *out_handles) {
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

void SlotMap::free_batch(std::span<const Entity> handles) noexcept {
  for (Entity e : handles) {
    if (is_alive(e)) {
      push_free_slot(e.index());
      --active_count;
    }
  }
}

// =============================================================================
// Slot Resolution & Queries
// =============================================================================

std::optional<Entity> SlotMap::resolve(uint32_t index) const noexcept {
  if (index >= slots.size() || !slots[index].is_alive_flag) {
    return std::nullopt;
  }
  return Entity::from_parts(index, slots[index].generation);
}

bool SlotMap::is_alive(Entity e) const noexcept {
  const uint32_t index = e.index();
  if (index >= slots.size())
    return false;
  return slots[index].is_alive_flag == 1 &&
      slots[index].generation == e.generation();
}

bool SlotMap::contains(Entity e) const noexcept {
  return is_alive(e);
}

const EntityRecord &SlotMap::get(Entity e) const {
  assert(
      is_alive(e) &&
      "arche::SlotMap: attempt to access dead or unallocated entity"
  );
  return slots[e.index()].record;
}

const EntityRecord &SlotMap::get_record(Entity e) const {
  return get(e);
}

EntityRecord &SlotMap::get_record(Entity e) {
  assert(
      is_alive(e) &&
      "arche::SlotMap: attempt to access dead or unallocated entity"
  );
  return slots[e.index()].record;
}

const EntityRecord *SlotMap::find_record(Entity e) const noexcept {
  const uint32_t idx = e.index();
  if (idx < slots.size() && slots[idx].generation == e.generation() &&
      slots[idx].is_alive_flag == 1) {
    return &slots[idx].record;
  }
  return nullptr;
}

EntityRecord *SlotMap::find_record(Entity e) noexcept {
  const uint32_t idx = e.index();
  if (idx < slots.size() && slots[idx].generation == e.generation() &&
      slots[idx].is_alive_flag == 1) {
    return &slots[idx].record;
  }
  return nullptr;
}

EntityRecord &SlotMap::get_mut(Entity e) {
  assert(
      is_alive(e) &&
      "arche::SlotMap: attempt to access dead or unallocated entity"
  );
  return slots[e.index()].record;
}

// =============================================================================
// Location Updating & Mutators
// =============================================================================

void SlotMap::update_location(
    Entity e,
    Archetype *arch,
    Chunk *chunk,
    uint32_t row
) noexcept {
  slots[e.index()].record = EntityRecord{arch, chunk, row};
}

void SlotMap::update_record(Entity e, Chunk *chunk, uint32_t row) noexcept {
  slots[e.index()].record.chunk = chunk;
  slots[e.index()].record.row = row;
}

void SlotMap::update_record(
    Entity e,
    Archetype *archetype,
    Chunk *chunk,
    uint32_t row
) noexcept {
  slots[e.index()].record.archetype = archetype;
  slots[e.index()].record.chunk = chunk;
  slots[e.index()].record.row = row;
}

void SlotMap::update_row(Entity e, uint32_t newRow) noexcept {
  slots[e.index()].record.row = newRow;
}

// =============================================================================
// Snapshot Serialization
// =============================================================================

size_t SlotMap::snapshot_byte_size() const noexcept {
  return slots.size() * sizeof(Slot);
}

void SlotMap::write_snapshot_data(void *dest) const noexcept {
  assert(dest != nullptr && "Destination buffer cannot be null");
  std::memcpy(dest, slots.data(), snapshot_byte_size());
}

void SlotMap::read_snapshot_data(
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

uint32_t SlotMap::free_head_index() const noexcept {
  return free_list_head;
}

size_t SlotMap::size() const noexcept {
  return active_count;
}

size_t SlotMap::capacity() const noexcept {
  return slots.size();
}

bool SlotMap::is_empty() const noexcept {
  return active_count == 0;
}

void SlotMap::clear() noexcept {
  slots.clear();
  free_list_head = INDEX_NONE_32;
  active_count = 0;
}

} // namespace arche
