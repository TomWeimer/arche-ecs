#pragma once

#include "archetype/archetype.hpp"
#include "arche/types/entity.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace arche {

class World;
class ChunkPool;
namespace Internal {
class SlotMap;
}
using SlotMap = Internal::SlotMap;

struct EntityAllocator {
  template <typename... Comps>
  static Archetype *resolve_archetype(World &world);

  static inline void update_entity_locations(
      SlotMap &slot_map,
      std::span<const Entity> entities,
      Archetype *arch,
      Chunk *chunk,
      uint32_t start_row
  ) noexcept;

  template <typename ColumnWriterFn>
  static void populate_chunks_batch(
      Archetype *arch,
      ChunkPool &pool,
      SlotMap &slot_map,
      std::span<const Entity> entities,
      ColumnWriterFn &&write_columns
  );

  template <typename... Comps>
    requires(!(std::is_same_v<std::decay_t<Comps>, Batch> || ...))
  static Entity spawn(World &world, Comps &&...components);

  template <typename... Comps>
    requires(!(std::is_same_v<std::decay_t<Comps>, std::span<Entity>> || ...))
  static std::vector<Entity> spawn(World &world, Batch batch, const Comps &...components);

  template <typename... Comps>
  static void spawn(World &world, Batch batch, std::span<Entity> out_entities, const Comps *...column_data);
};

} // namespace arche
