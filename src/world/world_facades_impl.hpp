#pragma once

#include "chunk/chunk.hpp"
#include "world/world_access.hpp"
#include "world/world_facades.hpp"
#include "spatial/sort.hpp"

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

  for (const auto &arch_ptr : internal::WorldAccess::graph(*world_).archetypes()) {
    const int16_t col = arch_ptr->column_index(tid);
    if (col < 0) {
      continue;
    }

    const auto &layout = arch_ptr->column_layout(static_cast<size_t>(col));
    for (Chunk *chunk : arch_ptr->chunks()) {
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

  for (const auto &arch_ptr : internal::WorldAccess::graph(*world_).archetypes()) {
    const int16_t col = arch_ptr->column_index(tid);
    if (col < 0) {
      continue;
    }

    const auto &layout = arch_ptr->column_layout(static_cast<size_t>(col));
    for (Chunk *chunk : arch_ptr->chunks()) {
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

// =============================================================================
// World::query Implementation
// =============================================================================

template <typename... Components, typename... FilterArgs>
inline Query<Components...> World::query(FilterArgs &&...filters) {
  auto [include_mask, exclude_mask] =
      Query<Components...>::compute_query_masks(filters...);
  QueryCache *cache =
      internal::WorldAccess::query_cache(*this, include_mask, exclude_mask);
  assert(cache != nullptr);
  return Query<Components...>(*cache, std::forward<FilterArgs>(filters)...);
}

// =============================================================================
// Spatial Permutation & Sort Wrappers
// =============================================================================

inline void permute(World &world, Archetype *arch, std::span<const uint32_t> target_indices) {
  internal::WorldAccess::advance_version(world);
  permute(
      arch,
      world.archetypes.chunks(arch),
      internal::WorldAccess::slot_map(world),
      world.version(),
      target_indices
  );
}

template <typename... Components>
inline void permute(World &world, std::span<const uint32_t> target_indices) {
  (internal::WorldAccess::registry(world).register_type<std::decay_t<Components>>(), ...);
  std::vector<TypeID> types = {type_id<std::decay_t<Components>>()...};
  Archetype *arch = internal::WorldAccess::graph(world).get_or_create(types);
  permute(world, arch, target_indices);
}

template <typename Component, typename Compare>
inline void sort(World &world, Archetype *arch, Compare &&comp) {
  internal::WorldAccess::advance_version(world);
  sort_archetype<Component>(
      arch,
      world.archetypes.chunks(arch),
      internal::WorldAccess::slot_map(world),
      world.version(),
      std::forward<Compare>(comp)
  );
}

template <typename Component, typename... Filters, typename Compare>
inline void sort(World &world, Compare &&comp) {
  ComponentMask include_mask;
  ComponentMask exclude_mask;
  include_mask.set(type_id<Component>());
  if constexpr (sizeof...(Filters) > 0) {
    ([&]<typename Filter>() {
      if constexpr (is_with_v<Filter>) {
        apply_filter_to_mask<Filter>(include_mask, internal::WorldAccess::registry(world));
      } else if constexpr (is_without_v<Filter>) {
        apply_filter_to_mask<Filter>(exclude_mask, internal::WorldAccess::registry(world));
      }
    }.template operator()<Filters>(), ...);
  }

  QueryCache *cache = internal::WorldAccess::query_cache(world, include_mask, exclude_mask);
  for (Archetype *arch : cache->matching_archetypes) {
    sort<Component>(world, arch, comp);
  }
}

template <typename Component, typename... Filters, typename KeyExtractor>
inline void sort_by_key(World &world, KeyExtractor &&key_fn) {
  ComponentMask include_mask;
  ComponentMask exclude_mask;
  include_mask.set(type_id<Component>());
  if constexpr (sizeof...(Filters) > 0) {
    ([&]<typename Filter>() {
      if constexpr (is_with_v<Filter>) {
        apply_filter_to_mask<Filter>(include_mask, internal::WorldAccess::registry(world));
      } else if constexpr (is_without_v<Filter>) {
        apply_filter_to_mask<Filter>(exclude_mask, internal::WorldAccess::registry(world));
      }
    }.template operator()<Filters>(), ...);
  }

  QueryCache *cache = internal::WorldAccess::query_cache(world, include_mask, exclude_mask);
  for (Archetype *arch : cache->matching_archetypes) {
    internal::WorldAccess::advance_version(world);
    sort_archetype_by_key<Component>(
        arch,
        world.archetypes.chunks(arch),
        internal::WorldAccess::slot_map(world),
        world.version(),
        key_fn
    );
  }
}

} // namespace arche
