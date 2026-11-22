#pragma once

#include "arche/archetype/archetype.hpp"
#include "arche/component/type_id.hpp"
#include "arche/entity/entity.hpp"
#include "arche/entity/entity_record.hpp"

namespace arche {

class World;

struct ComponentMigration {
  static void migrate(
      World &world,
      Entity entity,
      const EntityRecord &old_record,
      Archetype *src_arch,
      Archetype *dst_arch,
      TypeID mutated_type,
      const void *added_component_data
  );
};

} // namespace arche
