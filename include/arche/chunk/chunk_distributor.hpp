#pragma once

#include "arche/archetype/archetype.hpp"
#include "arche/chunk/chunk.hpp"
#include "arche/entity/entity.hpp"

#include <cstdint>
#include <span>

namespace arche {

class World;

struct ChunkDistributor {
  static Chunk *acquire_chunk_for(World &world, Archetype *arch);
  static std::pair<Chunk *, uint32_t> allocate_slot(World &world, Archetype *arch, Entity entity);
  static void recycle_chunk(World &world, Archetype *arch, Chunk *chunk);
  static std::vector<Chunk *> &get_archetype_chunks(World &world, Archetype *arch) noexcept;
  static std::span<Chunk *const> get_archetype_chunks(const World &world, const Archetype *arch) noexcept;

  template <typename Comp>
  static inline void broadcast_component(
      Archetype *arch,
      Chunk *chunk,
      uint32_t start_row,
      uint32_t count,
      const Comp &component
  );

  template <typename Comp>
  static inline void stream_component(
      Archetype *arch,
      Chunk *chunk,
      uint32_t start_row,
      const Comp *src_data,
      size_t placed,
      uint32_t to_add
  );

  static void update_entity_locations(
      World &world,
      std::span<const Entity> entities,
      Archetype *arch,
      Chunk *chunk,
      uint32_t start_row
  ) noexcept;

  template <typename ColumnWriterFn>
  static void populate_chunks_batch(
      World &world,
      Archetype *arch,
      std::span<const Entity> entities,
      ColumnWriterFn &&write_columns
  );
};

} // namespace arche
