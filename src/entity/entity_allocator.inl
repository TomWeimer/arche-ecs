#pragma once

#include "component/component_access.hpp"
#include "entity/entity_allocator.hpp"
#include "world/world_access.hpp"

#include <cassert>

namespace arche {

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
[[nodiscard]] inline Archetype *EntityAllocator::resolve_archetype(World &world) {
  (internal::WorldAccess::registry(world).register_type<std::decay_t<Comps>>(), ...);
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
  internal::WorldAccess::slot_map(world).update_location(entity, arch, chunk, row);

  chunk->set_structure_version(internal::WorldAccess::version(world));
  (ComponentAccess::stamp_column_version<std::decay_t<Comps>>(world, arch, chunk), ...);

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
             start_row, to_add, components
         ),
         ...);
        chunk->set_structure_version(internal::WorldAccess::version(world));
        (ComponentAccess::stamp_column_version<std::decay_t<Comps>>(world, arch, chunk), ...);
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
  assert(out_entities.size() >= count && "Output entities span smaller than count");
  if (count == 0) {
    return;
  }
  Archetype *arch = resolve_archetype<Comps...>(world);
  internal::WorldAccess::slot_map(world).allocate_batch(count, out_entities.data());

  populate_chunks_batch(
      arch,
      internal::WorldAccess::chunk_pool(world),
      internal::WorldAccess::slot_map(world),
      std::span<const Entity>(out_entities.data(), count),
      [&](Chunk *chunk, uint32_t start_row, size_t placed, uint32_t to_add) {
        (chunk->copy_column<std::decay_t<Comps>>(
             *arch->column_layout_for(type_id<std::decay_t<Comps>>()),
             start_row, &column_data[placed], to_add
         ),
         ...);
        chunk->set_structure_version(internal::WorldAccess::version(world));
        (ComponentAccess::stamp_column_version<std::decay_t<Comps>>(world, arch, chunk), ...);
      }
  );
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

} // namespace arche
