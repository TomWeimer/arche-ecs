export module arche:archetype.detail.component_migration;

import :archetype;
import :chunk_pool;
import :type_id;
import :entity;
import :slot_map;

export namespace arche {

struct ComponentMigration {
  static void migrate(
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
