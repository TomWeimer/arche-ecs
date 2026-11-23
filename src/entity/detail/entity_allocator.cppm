module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

export module arche:entity.detail.entity_allocator;

import :entity;
import :archetype;
import :chunk_pool;
import :slot_map;

export namespace arche {

class World;

struct EntityAllocator {
  template <typename... Comps>
  static Archetype *resolve_archetype(World &world);

  static void update_entity_locations(
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
  static std::vector<Entity> spawn(
      World &world,
      Batch batch,
      const Comps &...components
  );

  template <typename... Comps>
  static void spawn(
      World &world,
      Batch batch,
      std::span<Entity> out_entities,
      const Comps *...column_data
  );
};

} // namespace arche
