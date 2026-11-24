module;

#include <cassert>
#include <cstddef>
#include <cstdint>

export module arche:component.detail.component_access_impl;

import :component_access;
import :type_id;
import :slot_map;
import :archetype;
import :chunk;
import :archetype_graph;

export namespace arche {

// =============================================================================
// ComponentAccess Template & Static Implementations
// =============================================================================

template <typename T>
[[nodiscard]] inline bool ComponentAccess::has(
    const SlotMap &slot_map,
    Entity entity
) noexcept {
  const EntityRecord *record = slot_map.find_record(entity);
  if (!record || !record->archetype) {
    return false;
  }
  return record->archetype->has_component(type_id<T>());
}

template <typename T>
[[nodiscard]] inline const T &ComponentAccess::get(
    const SlotMap &slot_map,
    Entity entity
) {
  const EntityRecord &record = slot_map.get_record(entity);
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
[[nodiscard]] inline T &ComponentAccess::get_mut(
    const SlotMap &slot_map,
    Entity entity,
    uint32_t current_version
) {
  const EntityRecord &record = slot_map.get_record(entity);
  Archetype *arch = record.archetype;
  const TypeID tid = type_id<T>();
  const int16_t col = arch->column_index(tid);
  assert(col >= 0 && "Component not present on entity");

  Chunk *chunk = record.chunk;
  assert(chunk != nullptr);
  chunk->set_column_version(static_cast<uint16_t>(col), current_version);

  const auto &layout = arch->column_layout(static_cast<size_t>(col));
  void *ptr = chunk->component_element(layout, record.row);
  return *reinterpret_cast<T *>(ptr);
}

template <typename T>
inline void ComponentAccess::stamp_column_version(
    Archetype *arch,
    Chunk *chunk,
    uint32_t current_version
) noexcept {
  assert(chunk != nullptr && arch != nullptr);
  const int16_t col = arch->column_index(type_id<T>());
  if (col >= 0) {
    chunk->set_column_version(static_cast<uint16_t>(col), current_version);
  }
}

void ComponentAccess::stamp_component_version(
    const ArchetypeGraph &graph,
    TypeID tid,
    uint32_t current_version
) noexcept {
  for (const auto &arch_ptr : graph.archetypes()) {
    const int16_t col = arch_ptr->column_index(tid);
    if (col >= 0) {
      for (Chunk *chunk : arch_ptr->chunks()) {
        chunk->set_column_version(static_cast<uint16_t>(col), current_version);
      }
    }
  }
}

} // namespace arche
