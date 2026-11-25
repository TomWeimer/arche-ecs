module;

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

export module arche:world.detail.impl;

import :world;
import :domain.archetype;
import :domain.archetype_impl;
import :domain.entity;
import :domain.entity_impl;
import :domain.resource;
import :domain.resource_impl;
import :domain.snapshot;
import :domain.snapshot_impl;
import :domain.system;
import :domain.system_impl;
import :mutation;
import :command;
import :component.detail.component_id_impl;
import :concurrency.detail.thread_pool_impl;
import :system.detail.scheduler_impl;
import :system.detail.system_context_impl;
import :system.detail.access_builder_impl;
import :system.detail.plugin_impl;
import :entity_view;
import :system_context;
import :query;
import :query.detail.impl;
import :resource.detail.impl;
import :sort;
import :component_id;
import :chunk;
import :archetype;
import :slot_map;

export namespace arche {

using namespace internal;

// =============================================================================
// EntityRef (Read-Only) Implementations
// =============================================================================

inline bool EntityRef::is_alive() const noexcept {
  return world_ != nullptr && world_->entities.contains(entity_);
}

template <typename T> inline bool EntityRef::has() const noexcept {
  assert(world_ != nullptr && "EntityRef operation called on null World!");
  return world_->has<T>(entity_);
}

template <typename T> inline const T &EntityRef::get() const {
  assert(world_ != nullptr && "EntityRef operation called on null World!");
  return world_->get<T>(entity_);
}

// =============================================================================
// EntityMut (Mutable) Implementations
// =============================================================================

inline bool EntityMut::is_alive() const noexcept {
  return world_ != nullptr && world_->entities.contains(entity_);
}

template <typename T> inline bool EntityMut::has() const noexcept {
  assert(world_ != nullptr && "EntityMut operation called on null World!");
  return world_->has<T>(entity_);
}

template <typename T> inline const T &EntityMut::get() const {
  assert(world_ != nullptr && "EntityMut operation called on null World!");
  return world_->get<T>(entity_);
}

template <typename T> inline T &EntityMut::get_mut() {
  assert(world_ != nullptr && "EntityMut operation called on null World!");
  return world_->get_mut<T>(entity_);
}

template <typename T> inline void EntityMut::add(T &&component) {
  assert(world_ != nullptr && "EntityMut operation called on null World!");
  world_->add<T>(entity_, std::forward<T>(component));
}

template <typename T> inline void EntityMut::remove() {
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

inline MutationContext World::mutation_context() noexcept {
  return MutationContext{
      .slot_map = storage_->slot_map,
      .chunk_pool = storage_->chunk_pool,
      .graph = storage_->graph,
      .registry = storage_->registry,
      .current_version = storage_->current_version,
  };
}

inline World::operator MutationContext() noexcept {
  return mutation_context();
}

inline SystemExecutionTarget World::system_execution_target() noexcept {
  return SystemExecutionTarget{
      .resources = storage_->resources,
      .graph = storage_->graph,
      .current_version = storage_->current_version,
      .previous_version = storage_->previous_version,
      .cache_ctx = this,
      .get_cache_fn =
          [](void *ctx,
             ComponentMask inc,
             ComponentMask exc,
             ComponentMask any) -> QueryCache * {
        return static_cast<World *>(ctx)->query_cache(inc, exc, any);
      },
      .commit_parallel_cmd =
          [](ParallelCommandBuffer &p_cmd, void *world_ptr) {
        p_cmd.commit(*static_cast<World *>(world_ptr));
      },
      .advance_version_fn =
          [](void *world_ptr) {
        static_cast<World *>(world_ptr)->advance_version();
      },
      .world_ptr = this,
  };
}

inline World::operator SystemExecutionTarget() noexcept {
  return system_execution_target();
}

inline SystemContext World::create_system_context(
    CommandBuffer &cmd,
    ParallelCommandBuffer *parallel_cmd
) {
  return SystemContext{
      cmd,
      storage_->resources,
      this,
      [](void *ctx,
         ComponentMask inc,
         ComponentMask exc,
         ComponentMask any) -> QueryCache * {
        return static_cast<World *>(ctx)->query_cache(inc, exc, any);
      },
      version(),
      previous_version(),
      storage_->thread_pool.get(),
      parallel_cmd != nullptr ? parallel_cmd : storage_->parallel_cmd.get()
  };
}

template <Component T> inline bool World::has(Entity entity) const noexcept {
  const EntityRecord *record = storage_->slot_map.find_record(entity);
  if (!record || !record->archetype) {
    return false;
  }
  return record->archetype->has_component(component_id<T>());
}

template <Component T> inline const T &World::get(Entity entity) const {
  const EntityRecord &record = storage_->slot_map.get_record(entity);
  Archetype *arch = record.archetype;
  const int16_t col = arch->column_index(component_id<T>());
  assert(col >= 0 && "Component not present on entity");
  const auto &layout = arch->column_layout(static_cast<size_t>(col));
  return *reinterpret_cast<const T *>(record.chunk->component_element(layout, record.row));
}

template <Component T> inline T &World::get_mut(Entity entity) {
  const EntityRecord &record = storage_->slot_map.get_record(entity);
  Archetype *arch = record.archetype;
  const int16_t col = arch->column_index(component_id<T>());
  assert(col >= 0 && "Component not present on entity");
  record.chunk->set_column_version(static_cast<uint16_t>(col), storage_->current_version);
  const auto &layout = arch->column_layout(static_cast<size_t>(col));
  return *reinterpret_cast<T *>(record.chunk->component_element(layout, record.row));
}

template <Component T> inline void World::add(Entity entity, T &&component) {
  auto ctx = mutation_context();
  Mutation::add_component(ctx, entity, std::forward<T>(component));
}

template <Component T> inline void World::remove(Entity entity) {
  auto ctx = mutation_context();
  Mutation::remove_component<T>(ctx, entity);
}

template <Component... Comps>
  requires(!(std::is_same_v<std::decay_t<Comps>, Batch> || ...))
inline Entity World::spawn(Comps &&...components) {
  auto ctx = mutation_context();
  return Mutation::spawn(ctx, std::forward<Comps>(components)...);
}

template <Component... Comps>
  requires(!(std::is_same_v<std::decay_t<Comps>, std::span<Entity>> || ...))
inline std::vector<Entity> World::spawn(
    Batch batch,
    const Comps &...components
) {
  auto ctx = mutation_context();
  return Mutation::spawn_batch(ctx, batch, components...);
}

template <Component... Comps>
inline void World::spawn(
    Batch batch,
    std::span<Entity> out_entities,
    const Comps *...column_data
) {
  auto ctx = mutation_context();
  Mutation::spawn_batch(ctx, batch, out_entities, column_data...);
}

// =============================================================================
// SystemFacade::add_plugin Implementations
// =============================================================================

// =============================================================================
// World::add_plugin Implementations
// =============================================================================

template <Plugin P> inline World &World::add_plugin() {
  assert(storage_ != nullptr);
  storage_->installed_plugins.register_plugin(P::name());
  P plugin{};
  plugin.build(*this);
  return *this;
}

template <Plugin P> inline World &World::add_plugin(P &&plugin) {
  assert(storage_ != nullptr);
  storage_->installed_plugins.register_plugin(std::decay_t<P>::name());
  plugin.build(*this);
  return *this;
}

// =============================================================================
// World::query Implementation
// =============================================================================

template <QueryTerm... Components, QueryFilter... FilterArgs>
inline Query<Components...> World::query(FilterArgs &&...filters) {
  auto [include_mask, exclude_mask, any_mask] =
      arche::detail::query::compute_query_masks<Components...>(filters...);

  QueryCache *cache = query_cache(include_mask, exclude_mask, any_mask);
  assert(cache != nullptr);

  return Query<Components...>(
      *cache,
      QueryExecutionContext{
          storage_->thread_pool.get(),
          storage_->parallel_cmd.get(),
          version(),
          previous_version()
      },
      std::forward<FilterArgs>(filters)...
  );
}

// =============================================================================
// Spatial Permutation & Sort Wrappers
// =============================================================================

inline void permute(
    World &world,
    Archetype *arch,
    std::span<const uint32_t> target_indices
) {
  world.advance_version();
  Sort::permute(
      SortTarget{
          .arch = arch,
          .chunks = arch->chunks(),
          .slot_map = world.storage()->slot_map,
          .version = world.version(),
      },
      target_indices
  );
}

template <typename... Components>
inline void permute(World &world, std::span<const uint32_t> target_indices) {
  (world.storage()->registry.register_component<std::decay_t<Components>>(), ...);
  std::vector<ComponentID> types = {component_id<std::decay_t<Components>>()...};
  Archetype *arch = world.storage()->graph.get_or_create(types);
  permute(world, arch, target_indices);
}

inline void WorldTestAccessor::commit_commands(World &w) {
  assert(w.storage() != nullptr && w.storage()->parallel_cmd != nullptr);
  w.storage()->parallel_cmd->commit(w);
}

inline void WorldTestAccessor::permute(
    World &w,
    internal::Archetype *arch,
    std::span<const uint32_t> indices
) {
  arche::permute(w, arch, indices);
}

template <typename Component, typename Compare>
inline void sort(World &world, Archetype *arch, Compare &&comp) {
  world.advance_version();
  Sort::by_component<Component>(
      SortTarget{
          .arch = arch,
          .chunks = arch->chunks(),
          .slot_map = world.storage()->slot_map,
          .version = world.version(),
      },
      std::forward<Compare>(comp)
  );
}

template <typename Component, typename... Filters, typename Compare>
inline void sort(World &world, Compare &&comp) {
  ComponentMask include_mask;
  ComponentMask exclude_mask;
  include_mask.set(component_id<Component>());
  if constexpr (sizeof...(Filters) > 0) {
    (
        [&]<typename Filter>() {
          if constexpr (is_with_v<Filter>) {
            apply_filter_to_mask<Filter>(
                include_mask, world.storage()->registry
            );
          } else if constexpr (is_without_v<Filter>) {
            apply_filter_to_mask<Filter>(
                exclude_mask, world.storage()->registry
            );
          }
        }.template operator()<Filters>(),
        ...);
  }

  QueryCache *cache = world.query_cache(include_mask, exclude_mask);
  for (Archetype *arch : cache->matching_archetypes) {
    sort<Component>(world, arch, comp);
  }
}

template <typename Component, typename... Filters, typename KeyExtractor>
inline void sort_by_key(World &world, KeyExtractor &&key_fn) {
  ComponentMask include_mask;
  ComponentMask exclude_mask;
  include_mask.set(component_id<Component>());
  if constexpr (sizeof...(Filters) > 0) {
    (
        [&]<typename Filter>() {
          if constexpr (is_with_v<Filter>) {
            apply_filter_to_mask<Filter>(
                include_mask, world.storage()->registry
            );
          } else if constexpr (is_without_v<Filter>) {
            apply_filter_to_mask<Filter>(
                exclude_mask, world.storage()->registry
            );
          }
        }.template operator()<Filters>(),
        ...);
  }

  QueryCache *cache = world.query_cache(include_mask, exclude_mask);
  for (Archetype *arch : cache->matching_archetypes) {
    world.advance_version();
    Sort::by_key<Component>(
        SortTarget{
            .arch = arch,
            .chunks = arch->chunks(),
            .slot_map = world.storage()->slot_map,
            .version = world.version(),
        },
        key_fn
    );
  }
}

} // namespace arche
