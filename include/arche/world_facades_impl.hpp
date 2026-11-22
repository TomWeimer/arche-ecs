#pragma once

#include "arche/chunk/chunk.hpp"
#include "arche/world.hpp"
#include "arche/world_access.hpp"
#include "arche/world_facades.hpp"

#include <cassert>
#include <cstring>
#include <utility>
#include <vector>

namespace arche {

// =============================================================================
// EntityFacade Implementations
// =============================================================================

inline bool EntityFacade::contains(Entity entity) const noexcept {
  assert(world_ != nullptr);
  return internal::WorldAccess::slot_map(*world_).is_alive(entity);
}

inline size_t EntityFacade::count() const noexcept {
  assert(world_ != nullptr);
  return internal::WorldAccess::slot_map(*world_).size();
}

inline size_t EntityFacade::capacity() const noexcept {
  assert(world_ != nullptr);
  return internal::WorldAccess::slot_map(*world_).capacity();
}

inline bool EntityFacade::is_empty() const noexcept {
  assert(world_ != nullptr);
  return internal::WorldAccess::slot_map(*world_).size() == 0;
}

inline std::optional<Entity> EntityFacade::resolve(uint32_t index) const noexcept {
  assert(world_ != nullptr);
  return internal::WorldAccess::slot_map(*world_).resolve(index);
}

// =============================================================================
// ResourceFacade Template Implementations
// =============================================================================

template <typename T, typename... Args>
inline T &ResourceFacade::insert(Args &&...args) {
  assert(world_ != nullptr);
  return internal::WorldAccess::resources(*world_).insert<T>(std::forward<Args>(args)...);
}

template <typename T>
inline T &ResourceFacade::get() {
  assert(world_ != nullptr);
  return internal::WorldAccess::resources(*world_).get<T>();
}

template <typename T>
inline const T &ResourceFacade::get() const {
  assert(world_ != nullptr);
  return internal::WorldAccess::resources(*world_).get<T>();
}

template <typename T>
inline bool ResourceFacade::has() const noexcept {
  assert(world_ != nullptr);
  return internal::WorldAccess::resources(*world_).has<T>();
}

inline bool ResourceFacade::has(ResourceID id) const noexcept {
  assert(world_ != nullptr);
  return internal::WorldAccess::resources(*world_).has(id);
}

template <typename T>
inline bool ResourceFacade::remove() {
  assert(world_ != nullptr);
  return internal::WorldAccess::resources(*world_).remove<T>();
}

// =============================================================================
// ArchetypeFacade Template Implementations
// =============================================================================

template <typename T>
inline uint32_t ArchetypeFacade::count_with() const noexcept {
  assert(world_ != nullptr);
  const TypeID tid = type_id<T>();
  uint32_t total = 0;
  for (const auto &arch_ptr : internal::WorldAccess::graph(*world_).archetypes()) {
    if (arch_ptr->has_component(tid)) {
      total += arch_ptr->entity_count();
    }
  }
  return total;
}

// =============================================================================
// SystemFacade Template Implementations
// =============================================================================

template <typename P>
inline void SystemFacade::register_plugin_name() {
  assert(world_ != nullptr);
  const std::string_view p_name = P::name();
  auto &installed = internal::WorldAccess::installed_plugins(*world_);
  for (const auto &name : installed) {
    if (name == p_name) {
      throw std::runtime_error(
          "arche::World: Plugin '" + std::string(p_name) +
          "' is already installed."
      );
    }
  }
  installed.emplace_back(p_name);
}

template <Plugin P>
inline World &SystemFacade::add_plugin() {
  assert(world_ != nullptr);
  register_plugin_name<P>();
  P plugin{};
  plugin.build(*world_, internal::WorldAccess::scheduler(*world_));
  return *world_;
}

template <Plugin P>
inline World &SystemFacade::add_plugin(P &&plugin) {
  assert(world_ != nullptr);
  register_plugin_name<std::decay_t<P>>();
  plugin.build(*world_, internal::WorldAccess::scheduler(*world_));
  return *world_;
}

template <Plugin P>
inline World &SystemFacade::add_plugin(Scheduler &ext_scheduler) {
  assert(world_ != nullptr);
  register_plugin_name<P>();
  P plugin{};
  plugin.build(*world_, ext_scheduler);
  return *world_;
}

template <Plugin P>
inline World &SystemFacade::add_plugin(P &&plugin, Scheduler &ext_scheduler) {
  assert(world_ != nullptr);
  register_plugin_name<std::decay_t<P>>();
  plugin.build(*world_, ext_scheduler);
  return *world_;
}

// =============================================================================
// GpuFacade Template Implementations
// =============================================================================

template <typename T>
inline std::vector<ComponentSpan<T>> GpuFacade::view_chunks() {
  assert(world_ != nullptr);
  const TypeID tid = type_id<T>();
  std::vector<ComponentSpan<T>> result;

  const auto &chunks_map = internal::WorldAccess::chunks(*world_);
  for (const auto &arch_ptr : internal::WorldAccess::graph(*world_).archetypes()) {
    const int16_t col = arch_ptr->column_index(tid);
    if (col < 0) {
      continue;
    }

    if (arch_ptr->id() >= chunks_map.size()) {
      continue;
    }

    const auto &layout = arch_ptr->column_layout(static_cast<size_t>(col));
    for (Chunk *chunk : chunks_map[arch_ptr->id()]) {
      if (chunk->count() > 0) {
        T *ptr = reinterpret_cast<T *>(chunk->component_element(layout, 0));
        result.emplace_back(ptr, chunk->count());
      }
    }
  }

  return result;
}

template <typename T>
inline size_t GpuFacade::stream_to(void *destination_buffer) {
  assert(world_ != nullptr);
  assert(destination_buffer != nullptr);

  const TypeID tid = type_id<T>();
  uint8_t *dst = reinterpret_cast<uint8_t *>(destination_buffer);
  size_t total_bytes = 0;

  const auto &chunks_map = internal::WorldAccess::chunks(*world_);
  for (const auto &arch_ptr : internal::WorldAccess::graph(*world_).archetypes()) {
    const int16_t col = arch_ptr->column_index(tid);
    if (col < 0) {
      continue;
    }

    if (arch_ptr->id() >= chunks_map.size()) {
      continue;
    }

    const auto &layout = arch_ptr->column_layout(static_cast<size_t>(col));
    for (Chunk *chunk : chunks_map[arch_ptr->id()]) {
      const uint32_t count = chunk->count();
      if (count > 0) {
        const size_t bytes = count * sizeof(T);
        const void *src = chunk->component_element(layout, 0);
        std::memcpy(dst, src, bytes);
        dst += bytes;
        total_bytes += bytes;
      }
    }
  }

  return total_bytes;
}

} // namespace arche
