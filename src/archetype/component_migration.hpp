#pragma once

#include "archetype/archetype.hpp"
#include "chunk/chunk_pool.hpp"
#include "component/type_id.hpp"
#include "arche/types/entity.hpp"
#include "entity/entity_record.hpp"
#include "entity/slot_map.hpp"

namespace arche {

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
