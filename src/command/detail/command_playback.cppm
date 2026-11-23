module;

#include <cstdint>

export module arche:command.detail.command_playback;

import :archetype;
import :archetype_graph;
import :chunk_pool;
import :type_id;
import :entity;
import :slot_map;

export namespace arche {

class World;
struct EntityRecord;

struct CommandPlayback {
  static void apply_create_entity(
      World &world,
      void (*spawn_fn)(World &, const uint8_t *),
      const uint8_t *payload
  );

  static void apply_add_component(
      ChunkPool &chunk_pool,
      SlotMap &slot_map,
      ArchetypeGraph &graph,
      Entity entity,
      TypeID tid,
      const void *data
  );

  static void apply_remove_component(
      ChunkPool &chunk_pool,
      SlotMap &slot_map,
      ArchetypeGraph &graph,
      Entity entity,
      TypeID tid
  );

  static void migrate_entity(
      ChunkPool &chunk_pool,
      SlotMap &slot_map,
      Entity entity,
      const EntityRecord &old_record,
      Archetype *src_arch,
      Archetype *dst_arch,
      TypeID mutated_type,
      const void *added_component_data
  );
};

} // namespace arche
