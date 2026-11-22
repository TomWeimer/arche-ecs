#pragma once

#include "arche/archetype/component_migration.hpp"
#include "arche/component/component_access.hpp"
#include "arche/world.hpp"
#include "arche/world_access.hpp"

#include <cassert>
#include <type_traits>
#include <utility>

namespace arche {

template <typename T>
[[nodiscard]] inline bool ComponentAccess::has(const World &world, Entity entity) noexcept {
  const EntityRecord *record = internal::WorldAccess::slot_map(world).find_record(entity);
  if (!record || !record->archetype) {
    return false;
  }
  return record->archetype->has_component(type_id<T>());
}

template <typename T>
[[nodiscard]] inline const T &ComponentAccess::get(const World &world, Entity entity) {
  const EntityRecord &record = internal::WorldAccess::slot_map(world).get_record(entity);
  Archetype *arch = record.archetype;
  const TypeID tid = type_id<T>();
  const int16_t col = arch->column_index(tid);
  assert(col >= 0 && "Component not present on entity");

  const Chunk *chunk = record.chunk;
  assert(chunk != nullptr);
  const auto &layout = arch->column_layout(static_cast<size_t>(col));
  const void *ptr = chunk->component_element(layout, record.row);
  return *reinterpret_cast<const T *>(ptr);
}

template <typename T>
[[nodiscard]] inline T &ComponentAccess::get_mut(World &world, Entity entity) {
  const EntityRecord &record = internal::WorldAccess::slot_map(world).get_record(entity);
  Archetype *arch = record.archetype;
  const TypeID tid = type_id<T>();
  const int16_t col = arch->column_index(tid);
  assert(col >= 0 && "Component not present on entity");

  Chunk *chunk = record.chunk;
  assert(chunk != nullptr);
  chunk->set_column_version(static_cast<uint16_t>(col), internal::WorldAccess::version(world));

  const auto &layout = arch->column_layout(static_cast<size_t>(col));
  void *ptr = chunk->component_element(layout, record.row);
  return *reinterpret_cast<T *>(ptr);
}

template <typename T>
inline void ComponentAccess::add(World &world, Entity entity, T &&component) {
  internal::WorldAccess::registry(world).register_type<std::decay_t<T>>();
  const TypeID type = type_id<std::decay_t<T>>();
  const EntityRecord &record = internal::WorldAccess::slot_map(world).get_record(entity);
  Archetype *src_arch = record.archetype;
  assert(!src_arch->has_component(type) && "Entity already contains component");

  Archetype *dst_arch = internal::WorldAccess::graph(world).traverse_add(src_arch, type);
  ComponentMigration::migrate(world, entity, record, src_arch, dst_arch, type, &component);
}

template <typename T>
inline void ComponentAccess::remove(World &world, Entity entity) {
  const TypeID type = type_id<std::decay_t<T>>();
  const EntityRecord &record = internal::WorldAccess::slot_map(world).get_record(entity);
  Archetype *src_arch = record.archetype;
  assert(src_arch->has_component(type) && "Entity does not contain component");

  Archetype *dst_arch = internal::WorldAccess::graph(world).traverse_remove(src_arch, type);
  ComponentMigration::migrate(world, entity, record, src_arch, dst_arch, type, nullptr);
}

template <typename T, typename U>
inline void ComponentAccess::construct_component_in_row(
    Archetype *arch,
    Chunk *chunk,
    uint32_t row,
    U &&value
) {
  const TypeID tid = type_id<std::decay_t<T>>();
  const int16_t col = arch->column_index(tid);
  assert(col >= 0);
  const auto &layout = arch->column_layout(static_cast<size_t>(col));
  void *slot = chunk->component_element(layout, row);
  new (slot) T(std::forward<U>(value));
}

template <typename T>
inline void ComponentAccess::stamp_column_version(World &world, Chunk *chunk) noexcept {
  assert(chunk != nullptr);
  Archetype *arch = chunk->archetype();
  const int16_t col = arch->column_index(type_id<T>());
  if (col >= 0) {
    chunk->set_column_version(static_cast<uint16_t>(col), internal::WorldAccess::version(world));
  }
}

inline void ComponentAccess::stamp_component_version(World &world, TypeID tid) noexcept {
  for (const auto &arch_ptr : internal::WorldAccess::graph(world).archetypes()) {
    const int16_t col = arch_ptr->column_index(tid);
    if (col >= 0) {
      if (arch_ptr->id() < internal::WorldAccess::chunks(world).size()) {
        for (Chunk *chunk : internal::WorldAccess::chunks(world)[arch_ptr->id()]) {
          chunk->set_column_version(static_cast<uint16_t>(col), internal::WorldAccess::version(world));
        }
      }
    }
  }
}

template <typename T>
inline bool World::has(Entity entity) const noexcept {
  return ComponentAccess::has<T>(*this, entity);
}

template <typename T>
inline const T &World::get(Entity entity) const {
  return ComponentAccess::get<T>(*this, entity);
}

template <typename T>
inline T &World::get_mut(Entity entity) {
  return ComponentAccess::get_mut<T>(*this, entity);
}

template <typename T>
inline void World::add(Entity entity, T &&component) {
  ComponentAccess::add<T>(*this, entity, std::forward<T>(component));
}

template <typename T>
inline void World::remove(Entity entity) {
  ComponentAccess::remove<T>(*this, entity);
}

} // namespace arche
