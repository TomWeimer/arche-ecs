#include "arche/command/command_playback.hpp"
#include "arche/archetype/component_migration.hpp"
#include "arche/command/command_buffer.hpp"
#include "arche/world.hpp"
#include "arche/world_access.hpp"

#include <cassert>

namespace arche {

void CommandPlayback::playback(World &world, CommandBuffer &cmd_buffer) {
  cmd_buffer.commit(world);
}

void CommandPlayback::playback(World &world, ParallelCommandBuffer &cmd_buffers) {
  cmd_buffers.commit(world);
}

void CommandPlayback::apply_create_entity(
    World &world,
    void (*spawn_fn)(World &, const uint8_t *),
    const uint8_t *payload
) {
  assert(spawn_fn != nullptr);
  spawn_fn(world, payload);
}

void CommandPlayback::migrate_entity(
    World &world,
    Entity entity,
    const EntityRecord &old_record,
    Archetype *src_arch,
    Archetype *dst_arch,
    TypeID mutated_type,
    const void *added_component_data
) {
  ComponentMigration::migrate(
      world, entity, old_record, src_arch, dst_arch, mutated_type, added_component_data
  );
}

void CommandPlayback::apply_add_component(
    World &world,
    Entity entity,
    TypeID tid,
    const void *data
) {
  const EntityRecord *rec = internal::WorldAccess::slot_map(world).find_record(entity);
  if (!rec || !rec->archetype || rec->archetype->has_component(tid)) {
    return;
  }

  Archetype *dst = internal::WorldAccess::graph(world).traverse_add(rec->archetype, tid);
  migrate_entity(world, entity, *rec, rec->archetype, dst, tid, data);
}

void CommandPlayback::apply_remove_component(World &world, Entity entity, TypeID tid) {
  const EntityRecord *rec = internal::WorldAccess::slot_map(world).find_record(entity);
  if (!rec || !rec->archetype || !rec->archetype->has_component(tid)) {
    return;
  }

  Archetype *dst = internal::WorldAccess::graph(world).traverse_remove(rec->archetype, tid);
  migrate_entity(world, entity, *rec, rec->archetype, dst, tid, nullptr);
}

} // namespace arche
