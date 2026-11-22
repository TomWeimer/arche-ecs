#pragma once

#include "arche/chunk/chunk_distributor.hpp"
#include "arche/component/component_access.hpp"
#include "arche/entity/entity_allocator.hpp"
#include "arche/world.hpp"
#include "arche/world_access.hpp"

#include <cassert>

namespace arche {

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
  auto [chunk, row] = ChunkDistributor::allocate_slot(world, arch, entity);

  (ComponentAccess::construct_component_in_row<std::decay_t<Comps>>(
       arch, chunk, row, std::forward<Comps>(components)
   ),
   ...);

  arch->increment_entity_count();
  internal::WorldAccess::slot_map(world).update_location(entity, arch, chunk, row);

  chunk->set_structure_version(internal::WorldAccess::version(world));
  (ComponentAccess::stamp_column_version<std::decay_t<Comps>>(world, chunk), ...);

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

  ChunkDistributor::populate_chunks_batch(
      world,
      arch,
      entities,
      [&](Chunk *chunk, uint32_t start_row, size_t, uint32_t to_add) {
        (ChunkDistributor::broadcast_component(
             arch, chunk, start_row, to_add, components
         ),
         ...);
        chunk->set_structure_version(internal::WorldAccess::version(world));
        (ComponentAccess::stamp_column_version<std::decay_t<Comps>>(world, chunk), ...);
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

  ChunkDistributor::populate_chunks_batch(
      world,
      arch,
      std::span<const Entity>(out_entities.data(), count),
      [&](Chunk *chunk, uint32_t start_row, size_t placed, uint32_t to_add) {
        (ChunkDistributor::stream_component(
             arch, chunk, start_row, column_data, placed, to_add
         ),
         ...);
        chunk->set_structure_version(internal::WorldAccess::version(world));
        (ComponentAccess::stamp_column_version<std::decay_t<Comps>>(world, chunk), ...);
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
