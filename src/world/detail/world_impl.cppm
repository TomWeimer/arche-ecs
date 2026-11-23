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
import :world.detail.world_access;
import :world.detail.world_facades;
import :command;
import :component_access;
import :entity_view;
import :entity.detail.entity_allocator;
import :system_context;
import :query;
import :query.detail.impl;
import :sort;
import :type_id;
import :chunk;
import :archetype;
import :slot_map;

export namespace arche {

template <typename T>
[[nodiscard]] inline bool ComponentAccess::has(
    const World &world,
    Entity entity
) noexcept {
  const EntityRecord *record = internal::WorldAccess::slot_map(world)
                                   .find_record(entity);
  if (!record || !record->archetype) {
    return false;
  }
  return record->archetype->has_component(type_id<T>());
}

template <typename T>
[[nodiscard]] inline const T &ComponentAccess::get(
    const World &world,
    Entity entity
) {
  const EntityRecord &record = internal::WorldAccess::slot_map(world)
                                   .get_record(entity);
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
  const EntityRecord &record = internal::WorldAccess::slot_map(world)
                                   .get_record(entity);
  Archetype *arch = record.archetype;
  const TypeID tid = type_id<T>();
  const int16_t col = arch->column_index(tid);
  assert(col >= 0 && "Component not present on entity");

  Chunk *chunk = record.chunk;
  assert(chunk != nullptr);
  chunk->set_column_version(
      static_cast<uint16_t>(col), internal::WorldAccess::version(world)
  );

  const auto &layout = arch->column_layout(static_cast<size_t>(col));
  void *ptr = chunk->component_element(layout, record.row);
  return *reinterpret_cast<T *>(ptr);
}

template <typename T>
inline void ComponentAccess::add(World &world, Entity entity, T &&component) {
  internal::WorldAccess::registry(world).register_type<std::decay_t<T>>();
  const TypeID type = type_id<std::decay_t<T>>();
  const EntityRecord &record = internal::WorldAccess::slot_map(world)
                                   .get_record(entity);
  Archetype *src_arch = record.archetype;
  assert(!src_arch->has_component(type) && "Entity already contains component");

  Archetype *dst_arch = internal::WorldAccess::graph(world).traverse_add(
      src_arch, type
  );
  ComponentMigration::migrate(
      internal::WorldAccess::chunk_pool(world),
      internal::WorldAccess::slot_map(world),
      entity,
      record,
      src_arch,
      dst_arch,
      type,
      &component
  );
}

template <typename T>
inline void ComponentAccess::remove(World &world, Entity entity) {
  const TypeID type = type_id<std::decay_t<T>>();
  const EntityRecord &record = internal::WorldAccess::slot_map(world)
                                   .get_record(entity);
  Archetype *src_arch = record.archetype;
  assert(src_arch->has_component(type) && "Entity does not contain component");

  Archetype *dst_arch = internal::WorldAccess::graph(world).traverse_remove(
      src_arch, type
  );
  ComponentMigration::migrate(
      internal::WorldAccess::chunk_pool(world),
      internal::WorldAccess::slot_map(world),
      entity,
      record,
      src_arch,
      dst_arch,
      type,
      nullptr
  );
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
inline void ComponentAccess::stamp_column_version(
    World &world,
    Archetype *arch,
    Chunk *chunk
) noexcept {
  assert(chunk != nullptr && arch != nullptr);
  const int16_t col = arch->column_index(type_id<T>());
  if (col >= 0) {
    chunk->set_column_version(
        static_cast<uint16_t>(col), internal::WorldAccess::version(world)
    );
  }
}

inline void ComponentAccess::stamp_component_version(
    World &world,
    TypeID tid
) noexcept {
  for (const auto &arch_ptr :
       internal::WorldAccess::graph(world).archetypes()) {
    const int16_t col = arch_ptr->column_index(tid);
    if (col >= 0) {
      for (Chunk *chunk : arch_ptr->chunks()) {
        chunk->set_column_version(
            static_cast<uint16_t>(col), internal::WorldAccess::version(world)
        );
      }
    }
  }
}

inline void EntityAllocator::update_entity_locations(
    SlotMap &slot_map,
    std::span<const Entity> entities,
    Archetype *arch,
    Chunk *chunk,
    uint32_t start_row
) noexcept {
  for (size_t i = 0; i < entities.size(); ++i) {
    slot_map.update_location(
        entities[i], arch, chunk, start_row + static_cast<uint32_t>(i)
    );
  }
}

template <typename ColumnWriterFn>
inline void EntityAllocator::populate_chunks_batch(
    Archetype *arch,
    ChunkPool &pool,
    SlotMap &slot_map,
    std::span<const Entity> entities,
    ColumnWriterFn &&write_columns
) {
  const size_t total_count = entities.size();
  size_t placed = 0;

  while (placed < total_count) {
    Chunk *chunk = arch->ensure_writable_chunk(pool);
    const uint32_t start_row = chunk->count();
    const uint32_t capacity = chunk->capacity();
    const uint32_t available = capacity - start_row;
    const uint32_t to_add = static_cast<uint32_t>(
        std::min<size_t>(available, total_count - placed)
    );

    auto slice = entities.subspan(placed, to_add);
    chunk->append_entities(slice);
    write_columns(chunk, start_row, placed, to_add);
    update_entity_locations(slot_map, slice, arch, chunk, start_row);

    placed += to_add;
  }
  arch->increment_entity_count(total_count);
}

template <typename... Comps>
[[nodiscard]] inline Archetype *EntityAllocator::resolve_archetype(
    World &world
) {
  (internal::WorldAccess::registry(world).register_type<std::decay_t<Comps>>(),
   ...);
  std::vector<TypeID> types = {type_id<std::decay_t<Comps>>()...};
  return internal::WorldAccess::graph(world).get_or_create(types);
}

template <typename... Comps>
  requires(!(std::is_same_v<std::decay_t<Comps>, Batch> || ...))
inline Entity EntityAllocator::spawn(World &world, Comps &&...components) {
  Archetype *arch = resolve_archetype<Comps...>(world);
  Entity entity = internal::WorldAccess::slot_map(world).allocate();
  auto [chunk, row] = arch->allocate_slot(
      internal::WorldAccess::chunk_pool(world), entity
  );

  (ComponentAccess::construct_component_in_row<std::decay_t<Comps>>(
       arch, chunk, row, std::forward<Comps>(components)
   ),
   ...);

  arch->increment_entity_count();
  internal::WorldAccess::slot_map(world).update_location(
      entity, arch, chunk, row
  );

  chunk->set_structure_version(internal::WorldAccess::version(world));
  (ComponentAccess::stamp_column_version<std::decay_t<Comps>>(
       world, arch, chunk
   ),
   ...);

  return entity;
}

template <typename... Comps>
  requires(!(std::is_same_v<std::decay_t<Comps>, std::span<Entity>> || ...))
inline std::vector<Entity> EntityAllocator::spawn(
    World &world,
    Batch batch,
    const Comps &...components
) {
  const size_t count = batch.count;
  if (count == 0) {
    return {};
  }
  Archetype *arch = resolve_archetype<Comps...>(world);
  std::vector<Entity> entities(count);
  internal::WorldAccess::slot_map(world).allocate_batch(count, entities.data());

  populate_chunks_batch(
      arch,
      internal::WorldAccess::chunk_pool(world),
      internal::WorldAccess::slot_map(world),
      entities,
      [&](Chunk *chunk, uint32_t start_row, size_t, uint32_t to_add) {
        (chunk->fill_column<std::decay_t<Comps>>(
             *arch->column_layout_for(type_id<std::decay_t<Comps>>()),
             start_row,
             to_add,
             components
         ),
         ...);
        chunk->set_structure_version(internal::WorldAccess::version(world));
        (ComponentAccess::stamp_column_version<std::decay_t<Comps>>(
             world, arch, chunk
         ),
         ...);
      }
  );
  return entities;
}

template <typename... Comps>
inline void EntityAllocator::spawn(
    World &world,
    Batch batch,
    std::span<Entity> out_entities,
    const Comps *...column_data
) {
  const size_t count = batch.count;
  assert(
      out_entities.size() >= count && "Output entities span smaller than count"
  );
  if (count == 0) {
    return;
  }
  Archetype *arch = resolve_archetype<Comps...>(world);
  internal::WorldAccess::slot_map(world).allocate_batch(
      count, out_entities.data()
  );

  populate_chunks_batch(
      arch,
      internal::WorldAccess::chunk_pool(world),
      internal::WorldAccess::slot_map(world),
      std::span<const Entity>(out_entities.data(), count),
      [&](Chunk *chunk, uint32_t start_row, size_t placed, uint32_t to_add) {
        (chunk->copy_column<std::decay_t<Comps>>(
             *arch->column_layout_for(type_id<std::decay_t<Comps>>()),
             start_row,
             &column_data[placed],
             to_add
         ),
         ...);
        chunk->set_structure_version(internal::WorldAccess::version(world));
        (ComponentAccess::stamp_column_version<std::decay_t<Comps>>(
             world, arch, chunk
         ),
         ...);
      }
  );
}

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

template <typename... Comps>
inline void CommandBuffer::spawn_entity_helper(
    World &world,
    const uint8_t *payload
) {
  using TupleType = std::tuple<std::decay_t<Comps>...>;
  TupleType tuple{};
  if constexpr (sizeof...(Comps) > 0) {
    std::memcpy(static_cast<void *>(&tuple), payload, sizeof(TupleType));
  }
  std::apply(
      [&world](auto &&...items) {
        world.spawn(std::forward<decltype(items)>(items)...);
      },
      tuple
  );
}

template <typename T>
inline void CommandBuffer::register_type_helper(World &world) {
  internal::WorldAccess::registry(world).register_type<T>();
}

template <typename... Components, typename Func>
inline void SystemContext::query_parallel(Func &&func) {
  auto q = world.query<Components...>();
  if (pool != nullptr && parallel_cmd != nullptr) {
    q.each_parallel(*pool, *parallel_cmd, std::forward<Func>(func));
  } else {
    // Single-threaded fallback
    q.for_each_chunk([&](uint32_t count, auto &&...ptrs) {
      func(count, this->cmd, ptrs...);
    });
  }
}

inline uint32_t SystemContext::version() const noexcept {
  return world.version();
}

inline uint32_t SystemContext::tick() const noexcept { return version(); }

inline uint32_t SystemContext::current_tick() const noexcept {
  return version();
}

template <typename T> inline bool World::has(Entity entity) const noexcept {
  return ComponentAccess::has<T>(*this, entity);
}

template <typename T> inline const T &World::get(Entity entity) const {
  return ComponentAccess::get<T>(*this, entity);
}

template <typename T> inline T &World::get_mut(Entity entity) {
  return ComponentAccess::get_mut<T>(*this, entity);
}

template <typename T> inline void World::add(Entity entity, T &&component) {
  ComponentAccess::add<T>(*this, entity, std::forward<T>(component));
}

template <typename T> inline void World::remove(Entity entity) {
  ComponentAccess::remove<T>(*this, entity);
}

template <typename... Comps>
  requires(!(std::is_same_v<std::decay_t<Comps>, Batch> || ...))
inline Entity World::spawn(Comps &&...components) {
  return EntityAllocator::spawn(*this, std::forward<Comps>(components)...);
}

template <typename... Comps>
  requires(!(std::is_same_v<std::decay_t<Comps>, std::span<Entity>> || ...))
inline std::vector<Entity> World::spawn(
    Batch batch,
    const Comps &...components
) {
  return EntityAllocator::spawn(*this, batch, components...);
}

template <typename... Comps>
inline void World::spawn(
    Batch batch,
    std::span<Entity> out_entities,
    const Comps *...column_data
) {
  EntityAllocator::spawn(*this, batch, out_entities, column_data...);
}

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

inline std::optional<Entity> EntityFacade::resolve(
    uint32_t index
) const noexcept {
  assert(world_ != nullptr);
  return internal::WorldAccess::slot_map(*world_).resolve(index);
}

// =============================================================================
// ResourceFacade Template Implementations
// =============================================================================

template <typename T, typename... Args>
inline T &ResourceFacade::insert(Args &&...args) {
  assert(world_ != nullptr);
  return internal::WorldAccess::resources(*world_).insert<T>(
      std::forward<Args>(args)...
  );
}

template <typename T> inline T &ResourceFacade::get() {
  assert(world_ != nullptr);
  return internal::WorldAccess::resources(*world_).get<T>();
}

template <typename T> inline const T &ResourceFacade::get() const {
  assert(world_ != nullptr);
  return internal::WorldAccess::resources(*world_).get<T>();
}

template <typename T> inline bool ResourceFacade::has() const noexcept {
  assert(world_ != nullptr);
  return internal::WorldAccess::resources(*world_).has<T>();
}

inline bool ResourceFacade::has(ResourceID id) const noexcept {
  assert(world_ != nullptr);
  return internal::WorldAccess::resources(*world_).has(id);
}

template <typename T> inline bool ResourceFacade::remove() {
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
  for (const auto &arch_ptr :
       internal::WorldAccess::graph(*world_).archetypes()) {
    if (arch_ptr->has_component(tid)) {
      total += arch_ptr->entity_count();
    }
  }
  return total;
}

// =============================================================================
// SystemFacade Template Implementations
// =============================================================================

template <typename P> inline void SystemFacade::register_plugin_name() {
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

template <Plugin P> inline World &SystemFacade::add_plugin() {
  assert(world_ != nullptr);
  register_plugin_name<P>();
  P plugin{};
  plugin.build(*world_, internal::WorldAccess::scheduler(*world_));
  return *world_;
}

template <Plugin P> inline World &SystemFacade::add_plugin(P &&plugin) {
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

  for (const auto &arch_ptr :
       internal::WorldAccess::graph(*world_).archetypes()) {
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

  for (const auto &arch_ptr :
       internal::WorldAccess::graph(*world_).archetypes()) {
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
  auto [include_mask, exclude_mask] = Query<Components...>::compute_query_masks(
      filters...
  );
  QueryCache *cache = internal::WorldAccess::query_cache(
      *this, include_mask, exclude_mask
  );
  assert(cache != nullptr);
  return Query<Components...>(*cache, std::forward<FilterArgs>(filters)...);
}

// =============================================================================
// Spatial Permutation & Sort Wrappers
// =============================================================================

inline void permute(
    World &world,
    Archetype *arch,
    std::span<const uint32_t> target_indices
) {
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
  (internal::WorldAccess::registry(world)
       .register_type<std::decay_t<Components>>(),
   ...);
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
    (
        [&]<typename Filter>() {
          if constexpr (is_with_v<Filter>) {
            apply_filter_to_mask<Filter>(
                include_mask, internal::WorldAccess::registry(world)
            );
          } else if constexpr (is_without_v<Filter>) {
            apply_filter_to_mask<Filter>(
                exclude_mask, internal::WorldAccess::registry(world)
            );
          }
        }.template operator()<Filters>(),
        ...);
  }

  QueryCache *cache = internal::WorldAccess::query_cache(
      world, include_mask, exclude_mask
  );
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
    (
        [&]<typename Filter>() {
          if constexpr (is_with_v<Filter>) {
            apply_filter_to_mask<Filter>(
                include_mask, internal::WorldAccess::registry(world)
            );
          } else if constexpr (is_without_v<Filter>) {
            apply_filter_to_mask<Filter>(
                exclude_mask, internal::WorldAccess::registry(world)
            );
          }
        }.template operator()<Filters>(),
        ...);
  }

  QueryCache *cache = internal::WorldAccess::query_cache(
      world, include_mask, exclude_mask
  );
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
