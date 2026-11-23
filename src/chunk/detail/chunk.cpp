module;

#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>

module arche;

namespace arche {

namespace {

inline void relocate_component(
    const ComponentColumnLayout &layout,
    void *dst,
    void *src
) noexcept {
  layout.descriptor->relocate(dst, src);
  layout.descriptor->destruct(src);
}

inline void destruct_row_components(
    Chunk *chunk,
    uint32_t row,
    std::span<const ComponentColumnLayout> layouts
) noexcept {
  for (const auto &layout : layouts) {
    void *ptr = chunk->component_element(layout, row);
    layout.descriptor->destruct(ptr);
  }
}

inline void relocate_row_components(
    Chunk *chunk,
    uint32_t dst_row,
    uint32_t src_row,
    std::span<const ComponentColumnLayout> layouts
) noexcept {
  for (const auto &layout : layouts) {
    void *dst = chunk->component_element(layout, dst_row);
    void *src = chunk->component_element(layout, src_row);
    relocate_component(layout, dst, src);
  }
}

inline Entity relocate_entity_handle(
    Chunk *chunk,
    uint32_t dst_row,
    uint32_t src_row
) noexcept {
  Entity *entities = chunk->entity_slice();
  Entity moved_entity = entities[src_row];
  entities[dst_row] = moved_entity;
  return moved_entity;
}

} // namespace

ChunkRelocation Chunk::swap_and_pop(
    uint32_t row,
    std::span<const ComponentColumnLayout> layouts
) {
  assert(row < header_.count && "Row out of bounds in swap_and_pop!");

  const uint32_t last_row = header_.count - 1;
  destruct_row_components(this, row, layouts);

  ChunkRelocation result{};
  if (row != last_row) {
    relocate_row_components(this, row, last_row, layouts);
    result.moved_entity = relocate_entity_handle(this, row, last_row);
  }

  header_.count--;
  return result;
}

uint32_t Chunk::batch_swap_and_pop(
    std::span<const uint32_t> rows_to_delete,
    std::span<const ComponentColumnLayout> layouts,
    std::span<EntityRelocation> out_relocations
) {
  if (rows_to_delete.empty()) {
    return 0;
  }

  uint32_t write_idx = 0;
  int32_t current_tail = static_cast<int32_t>(header_.count) - 1;

  for (uint32_t row : rows_to_delete) {
    assert(row < header_.count && "Row out of bounds in batch_swap_and_pop!");
    destruct_row_components(this, row, layouts);

    while (current_tail >= 0) {
      bool is_also_deleted = false;
      for (uint32_t r : rows_to_delete) {
        if (static_cast<uint32_t>(current_tail) == r) {
          is_also_deleted = true;
          break;
        }
      }
      if (!is_also_deleted) {
        break;
      }
      current_tail--;
    }

    if (static_cast<int32_t>(row) < current_tail) {
      relocate_row_components(
          this, row, static_cast<uint32_t>(current_tail), layouts
      );
      Entity moved = relocate_entity_handle(
          this, row, static_cast<uint32_t>(current_tail)
      );

      if (write_idx < out_relocations.size()) {
        out_relocations[write_idx++] = EntityRelocation{
            .entity = moved,
            .new_row = row,
        };
      }
      current_tail--;
    }
  }

  header_.count -= static_cast<uint32_t>(rows_to_delete.size());
  return write_idx;
}

} // namespace arche
