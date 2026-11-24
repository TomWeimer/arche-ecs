module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>

export module arche:facade.entity_impl;

import :facade.entity;
import :slot_map;

export namespace arche {

// =============================================================================
// EntityFacade Method Implementations
// =============================================================================

bool EntityFacade::contains(Entity entity) const noexcept {
  assert(slot_map_ != nullptr);
  return slot_map_->is_alive(entity);
}

size_t EntityFacade::count() const noexcept {
  assert(slot_map_ != nullptr);
  return slot_map_->size();
}

size_t EntityFacade::capacity() const noexcept {
  assert(slot_map_ != nullptr);
  return slot_map_->capacity();
}

bool EntityFacade::is_empty() const noexcept {
  assert(slot_map_ != nullptr);
  return slot_map_->size() == 0;
}

std::optional<Entity> EntityFacade::resolve(uint32_t index) const noexcept {
  assert(slot_map_ != nullptr);
  return slot_map_->resolve(index);
}

const SlotMap &EntityFacade::slot_map() const noexcept {
  assert(slot_map_ != nullptr);
  return *slot_map_;
}

SlotMap &EntityFacade::slot_map() noexcept {
  assert(slot_map_ != nullptr);
  return *slot_map_;
}

} // namespace arche
