#pragma once

#include "arche/command/command_buffer.hpp"
#include "arche/component/type_id.hpp"
#include "arche/entity/entity.hpp"

namespace arche {

class World;
class Archetype;
struct EntityRecord;

struct CommandPlayback {
  static void playback(World &world, CommandBuffer &cmd_buffer);
  static void playback(World &world, ParallelCommandBuffer &cmd_buffers);

  static void apply_create_entity(
      World &world,
      void (*spawn_fn)(World &, const uint8_t *),
      const uint8_t *payload
  );
  static void apply_add_component(World &world, Entity entity, TypeID tid, const void *data);
  static void apply_remove_component(World &world, Entity entity, TypeID tid);
  static void migrate_entity(
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
