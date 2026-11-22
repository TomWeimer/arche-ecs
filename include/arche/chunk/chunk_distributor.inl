#pragma once

#include "arche/chunk/chunk_distributor.hpp"
#include "arche/world.hpp"
#include "arche/world_access.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace arche {

inline Chunk *ChunkDistributor::acquire_chunk_for(World &world, Archetype *arch) {
  auto &chunks = internal::WorldAccess::chunks(world);
  if (arch->id() >= chunks.size()) {
    chunks.resize(arch->id() + 1);
  }
  auto &arch_chunks = chunks[arch->id()];
  if (arch_chunks.empty() || arch_chunks.back()->is_full()) {
    void *raw = internal::WorldAccess::chunk_pool(world).acquire_buffer();
    auto *chunk = Chunk::create(
        raw, arch, arch->chunk_capacity(), internal::WorldAccess::chunk_size(world)
    );
    chunk->set_chunk_index(static_cast<uint32_t>(arch_chunks.size()));
    arch_chunks.push_back(chunk);
  }
  return arch_chunks.back();
}

inline std::pair<Chunk *, uint32_t> ChunkDistributor::allocate_slot(
    World &world, Archetype *arch, Entity entity
) {
  Chunk *active = acquire_chunk_for(world, arch);
  const uint32_t row = active->append_entity(entity);
  return {active, row};
}

inline std::vector<Chunk *> &ChunkDistributor::get_archetype_chunks(
    World &world, Archetype *arch
) noexcept {
  assert(arch != nullptr);
  auto &chunks = internal::WorldAccess::chunks(world);
  const size_t id = arch->id();
  if (id >= chunks.size()) {
    chunks.resize(id + 1);
  }
  return chunks[id];
}

inline std::span<Chunk *const> ChunkDistributor::get_archetype_chunks(
    const World &world, const Archetype *arch
) noexcept {
  assert(arch != nullptr);
  const auto &chunks = internal::WorldAccess::chunks(world);
  const size_t id = arch->id();
  if (id >= chunks.size()) {
    return {};
  }
  return std::span<Chunk *const>(chunks[id].data(), chunks[id].size());
}

inline void ChunkDistributor::recycle_chunk(
    World &world, Archetype *arch, Chunk *chunk
) {
  auto &chunks = get_archetype_chunks(world, arch);
  auto it = std::find(chunks.begin(), chunks.end(), chunk);
  if (it == chunks.end()) {
    return;
  }
  chunks.erase(it);
  internal::WorldAccess::chunk_pool(world).release_buffer(chunk);
}

template <typename Comp>
inline void ChunkDistributor::broadcast_component(
    Archetype *arch,
    Chunk *chunk,
    uint32_t start_row,
    uint32_t count,
    const Comp &component
) {
  const auto *layout = arch->column_layout_for(type_id<Comp>());
  assert(layout != nullptr);
  Comp *col = static_cast<Comp *>(chunk->component_element(*layout, start_row));
  for (uint32_t i = 0; i < count; ++i) {
    new (&col[i]) Comp(component);
  }
}

template <typename Comp>
inline void ChunkDistributor::stream_component(
    Archetype *arch,
    Chunk *chunk,
    uint32_t start_row,
    const Comp *src_data,
    size_t placed,
    uint32_t to_add
) {
  const auto *layout = arch->column_layout_for(type_id<Comp>());
  assert(layout != nullptr);
  void *dst = chunk->component_element(*layout, start_row);
  std::memcpy(dst, &src_data[placed], to_add * sizeof(Comp));
}

inline void ChunkDistributor::update_entity_locations(
    World &world,
    std::span<const Entity> entities,
    Archetype *arch,
    Chunk *chunk,
    uint32_t start_row
) noexcept {
  for (uint32_t i = 0; i < entities.size(); ++i) {
    internal::WorldAccess::slot_map(world).update_location(entities[i], arch, chunk, start_row + i);
  }
}

template <typename ColumnWriterFn>
inline void ChunkDistributor::populate_chunks_batch(
    World &world,
    Archetype *arch,
    std::span<const Entity> entities,
    ColumnWriterFn &&write_columns
) {
  size_t offset = 0;
  while (offset < entities.size()) {
    Chunk *chunk = acquire_chunk_for(world, arch);
    const uint32_t space = chunk->capacity() - chunk->count();
    const uint32_t remaining = static_cast<uint32_t>(entities.size() - offset);
    const uint32_t batch_size = std::min(space, remaining);

    auto batch = entities.subspan(offset, batch_size);
    const uint32_t start_row = chunk->append_entities(batch);

    write_columns(chunk, start_row, offset, batch_size);
    update_entity_locations(world, batch, arch, chunk, start_row);

    offset += batch_size;
  }
  arch->increment_entity_count(entities.size());
}

} // namespace arche
