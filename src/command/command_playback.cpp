#include "command/command_playback.hpp"
#include "archetype/component_migration.hpp"

#include <cassert>

namespace arche {

void CommandPlayback::apply_create_entity(
    World &world,
    void (*spawn_fn)(World &, const uint8_t *),
    const uint8_t *payload
) {
  assert(spawn_fn != nullptr);
  spawn_fn(world, payload);
}

void CommandPlayback::migrate_entity(
    ChunkPool &chunk_pool,
    SlotMap &slot_map,
    Entity entity,
    const EntityRecord &old_record,
    Archetype *src_arch,
    Archetype *dst_arch,
    TypeID mutated_type,
    const void *added_component_data
) {
  ComponentMigration::migrate(
      chunk_pool,
      slot_map,
      entity,
      old_record,
      src_arch,
      dst_arch,
      mutated_type,
      added_component_data
  );
}

void CommandPlayback::apply_add_component(
    ChunkPool &chunk_pool,
    SlotMap &slot_map,
    ArchetypeGraph &graph,
    Entity entity,
    TypeID tid,
    const void *data
) {
  const EntityRecord *rec = slot_map.find_record(entity);
  if (!rec || !rec->archetype || rec->archetype->has_component(tid)) {
    return;
  }

  Archetype *dst = graph.traverse_add(rec->archetype, tid);
  migrate_entity(chunk_pool, slot_map, entity, *rec, rec->archetype, dst, tid, data);
}

void CommandPlayback::apply_remove_component(
    ChunkPool &chunk_pool,
    SlotMap &slot_map,
    ArchetypeGraph &graph,
    Entity entity,
    TypeID tid
) {
  const EntityRecord *rec = slot_map.find_record(entity);
  if (!rec || !rec->archetype || !rec->archetype->has_component(tid)) {
    return;
  }

  Archetype *dst = graph.traverse_remove(rec->archetype, tid);
  migrate_entity(chunk_pool, slot_map, entity, *rec, rec->archetype, dst, tid, nullptr);
}

} // namespace arche
