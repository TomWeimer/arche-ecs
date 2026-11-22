#pragma once

#include "arche/entity/entity_view.hpp"
#include "arche/world.hpp"

namespace arche {

// =============================================================================
// EntityRef (Read-Only) Implementations
// =============================================================================

inline bool EntityRef::is_alive() const noexcept {
  return world_ != nullptr && world_->entities.contains(entity_);
}

template <typename T>
inline bool EntityRef::has() const noexcept {
  assert(world_ != nullptr && "EntityRef operation called on null World!");
  return world_->has<T>(entity_);
}

template <typename T>
inline const T &EntityRef::get() const {
  assert(world_ != nullptr && "EntityRef operation called on null World!");
  return world_->get<T>(entity_);
}

// =============================================================================
// EntityMut (Mutable) Implementations
// =============================================================================

inline bool EntityMut::is_alive() const noexcept {
  return world_ != nullptr && world_->entities.contains(entity_);
}

template <typename T>
inline bool EntityMut::has() const noexcept {
  assert(world_ != nullptr && "EntityMut operation called on null World!");
  return world_->has<T>(entity_);
}

template <typename T>
inline const T &EntityMut::get() const {
  assert(world_ != nullptr && "EntityMut operation called on null World!");
  return world_->get<T>(entity_);
}

template <typename T>
inline T &EntityMut::get_mut() {
  assert(world_ != nullptr && "EntityMut operation called on null World!");
  return world_->get_mut<T>(entity_);
}

template <typename T>
inline void EntityMut::add(T &&component) {
  assert(world_ != nullptr && "EntityMut operation called on null World!");
  world_->add<T>(entity_, std::forward<T>(component));
}

template <typename T>
inline void EntityMut::remove() {
  assert(world_ != nullptr && "EntityMut operation called on null World!");
  world_->remove<T>(entity_);
}

inline bool EntityMut::despawn() {
  if (world_ == nullptr) {
    return false;
  }
  return world_->despawn(entity_);
}

// =============================================================================
// World Overloaded Accessors
// =============================================================================

inline EntityMut World::entity(Entity entity) noexcept {
  return EntityMut(this, entity);
}

inline EntityRef World::entity(Entity entity) const noexcept {
  return EntityRef(this, entity);
}

} // namespace arche
