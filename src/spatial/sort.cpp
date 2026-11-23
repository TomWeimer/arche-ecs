#include "spatial/permute.hpp"
#include "archetype/archetype.hpp"
#include "chunk/chunk.hpp"
#include "entity/slot_map.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <span>
#include <vector>

namespace arche {

namespace {

inline Entity *resolve_entity_ptr(
    const Archetype *arch,
    std::span<Chunk *const> chunks,
    uint32_t global_idx
) noexcept {
  const Archetype::ChunkCoord coord = arch->decompose_index(global_idx);
  return &chunks[coord.chunk_index]->entity_slice()[coord.local_row];
}

inline void *resolve_component_ptr(
    const Archetype *arch,
    std::span<Chunk *const> chunks,
    const ComponentColumnLayout &layout,
    uint32_t global_idx
) noexcept {
  const Archetype::ChunkCoord coord = arch->decompose_index(global_idx);
  return chunks[coord.chunk_index]->component_element(layout, coord.local_row);
}

void permute_single_column(
    const Archetype *arch,
    std::span<Chunk *const> chunks,
    const ComponentColumnLayout &layout,
    std::span<const uint32_t> target_indices,
    std::vector<bool> &visited
) {
  const size_t total = target_indices.size();
  const size_t elem_size = layout.size;
  std::fill(visited.begin(), visited.end(), false);

  std::vector<uint8_t> temp_storage(elem_size);
  void *temp_elem = temp_storage.data();

  for (uint32_t start = 0; start < total; ++start) {
    if (visited[start] || target_indices[start] == start) {
      continue;
    }

    uint32_t cur = start;
    void *cur_ptr = resolve_component_ptr(arch, chunks, layout, cur);
    std::memcpy(temp_elem, cur_ptr, elem_size);

    while (true) {
      visited[cur] = true;
      uint32_t next = target_indices[cur];
      if (next == start) {
        std::memcpy(cur_ptr, temp_elem, elem_size);
        break;
      }

      void *next_ptr = resolve_component_ptr(arch, chunks, layout, next);
      std::memcpy(cur_ptr, next_ptr, elem_size);

      cur = next;
      cur_ptr = resolve_component_ptr(arch, chunks, layout, cur);
    }
  }
}

void permute_all_component_columns(
    const Archetype *arch,
    std::span<Chunk *const> chunks,
    std::span<const ComponentColumnLayout> layouts,
    std::span<const uint32_t> target_indices,
    std::vector<bool> &visited
) {
  for (const auto &layout : layouts) {
    permute_single_column(arch, chunks, layout, target_indices, visited);
  }
}

void permute_entity_column(
    const Archetype *arch,
    std::span<Chunk *const> chunks,
    std::span<const uint32_t> target_indices,
    std::vector<bool> &visited
) {
  const size_t total = target_indices.size();
  std::fill(visited.begin(), visited.end(), false);
  Entity temp_entity;

  for (uint32_t start = 0; start < total; ++start) {
    if (visited[start] || target_indices[start] == start) {
      continue;
    }

    uint32_t cur = start;
    Entity *cur_ptr = resolve_entity_ptr(arch, chunks, cur);
    temp_entity = *cur_ptr;

    while (true) {
      visited[cur] = true;
      uint32_t next = target_indices[cur];
      if (next == start) {
        *cur_ptr = temp_entity;
        break;
      }

      Entity *next_ptr = resolve_entity_ptr(arch, chunks, next);
      Entity moved = *next_ptr;
      *cur_ptr = moved;

      cur = next;
      cur_ptr = resolve_entity_ptr(arch, chunks, cur);
    }
  }
}

inline void sync_all_entity_slots(
    const Archetype *arch,
    std::span<Chunk *const> chunks,
    SlotMap &slot_map
) noexcept {
  for (uint32_t c = 0; c < chunks.size(); ++c) {
    Chunk *chunk = chunks[c];
    const uint32_t count = chunk->count();
    const Entity *entities = chunk->entity_slice();
    for (uint32_t r = 0; r < count; ++r) {
      slot_map.update_location(
          entities[r], const_cast<Archetype *>(arch), chunk, r
      );
    }
  }
}

} // namespace

void permute(
    Archetype *arch,
    std::span<Chunk *const> chunks,
    SlotMap &slot_map,
    uint32_t version,
    std::span<const uint32_t> target_indices
) {
  assert(arch != nullptr);
  const size_t total = arch->entity_count();
  assert(target_indices.size() == total && "Permutation size must match entity count");

  if (total <= 1) {
    return;
  }

  const auto layouts = arch->column_layouts();
  std::vector<bool> visited(total, false);

  permute_all_component_columns(arch, chunks, layouts, target_indices, visited);
  permute_entity_column(arch, chunks, target_indices, visited);
  sync_all_entity_slots(arch, chunks, slot_map);

  for (Chunk *chunk : chunks) {
    chunk->set_structure_version(version);
  }
}

} // namespace arche
