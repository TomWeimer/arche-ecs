
#include "chunk.hpp"

#include <cassert>
#include <cstring>
#include <type_traits>

namespace arche {

// =============================================================================
// Internal Swap-and-Pop Helper Functions
// =============================================================================

namespace {

/**
 * @brief Relocates a single component value from src to dst.
 *
 * @details
 * Uses `relocate` from the component descriptor (memcpy for trivial types,
 * move_assign for non-trivial types), then destructs the source if needed.
 *
 * @param layout Column layout and type descriptor.
 * @param dst Destination memory address in the chunk.
 * @param src Source memory address (last entity in the chunk).
 */
inline void relocate_component(
    const ComponentColumnLayout &layout,
    void *dst,
    void *src
) noexcept {
  layout.descriptor->relocate(dst, src);
  layout.descriptor->destruct(src);
}

/**
 * @brief Destructs non-trivial component instances at a specific row within a
 * chunk.
 *
 * @param chunk Pointer to the chunk containing the components.
 * @param row Dense row index of the entity whose components are to be
 * destroyed.
 * @param layouts Span of component column layouts defining strides and
 * descriptors.
 */
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

/**
 * @brief Relocates component data from source row to destination row across all
 * columns.
 *
 * @param chunk Pointer to the chunk being compacted.
 * @param dst_row Target vacated row index receiving the relocated component
 * data.
 * @param src_row Source row index (last active entity in the chunk).
 * @param layouts Span of component column layouts defining offsets, sizes, and
 * descriptors.
 */
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

/**
 * @brief Transfers the entity handle from source row to destination row.
 *
 * @param chunk Pointer to the chunk being compacted.
 * @param dst_row Vacated destination row receiving the relocated entity handle.
 * @param src_row Source row index where the relocated entity previously
 * resided.
 * @return Entity The entity handle that was relocated.
 */
inline Entity relocate_entity_handle(
    Chunk *chunk,
    uint32_t dst_row,
    uint32_t src_row
) noexcept {
  Entity *entities = chunk->entity_slice();
  Entity moved_entity = entities[src_row];

  // Move Entity handle into destination slot
  entities[dst_row] = moved_entity;
  return moved_entity;
}

/**
 * @brief Relocates both component data and entity handle from source row to
 * destination row.
 *
 * @param chunk Pointer to the chunk being compacted.
 * @param dst_row Vacated destination row receiving the relocated entity.
 * @param src_row Source row index where the relocated entity previously
 * resided.
 * @param layouts Span of component column layouts.
 * @return Entity The entity handle that was relocated.
 */
inline Entity relocate_entity(
    Chunk *chunk,
    uint32_t dst_row,
    uint32_t src_row,
    std::span<const ComponentColumnLayout> layouts
) noexcept {
  relocate_row_components(chunk, dst_row, src_row, layouts);
  return relocate_entity_handle(chunk, dst_row, src_row);
}

/**
 * @brief Destructs non-trivial component instances across a batch of row
 * indices.
 *
 * @param chunk Pointer to the chunk containing the components.
 * @param rows Span of dense row indices marked for destruction.
 * @param layouts Span of component column layouts defining strides and
 * descriptors.
 */
inline void destruct_rows(
    Chunk *chunk,
    std::span<const uint32_t> rows,
    std::span<const ComponentColumnLayout> layouts
) noexcept {
  for (uint32_t row : rows) {
    destruct_row_components(chunk, row, layouts);
  }
}

/**
 * @brief Checks if a specific row index is included in the deletion batch.
 *
 * @param row Row index to search for.
 * @param rows_to_delete Span of row indices marked for deletion.
 * @return true if the row is marked for deletion, false otherwise.
 */
[[nodiscard]] inline bool is_row_deleted(
    uint32_t row,
    std::span<const uint32_t> rows_to_delete
) noexcept {
  for (uint32_t deleted_row : rows_to_delete) {
    if (deleted_row == row) {
      return true;
    }
  }
  return false;
}

/**
 * @brief Decrements the tail cursor until it points to a valid surviving entity
 * that is not marked for deletion.
 *
 * @param tail_cursor Reference to the tail row index moving downward.
 * @param final_count Threshold boundary; surviving tail entities must reside at
 * or above this row.
 * @param rows_to_delete Span of row indices marked for deletion.
 */
inline void advance_to_surviving_tail(
    uint32_t &tail_cursor,
    uint32_t final_count,
    std::span<const uint32_t> rows_to_delete
) noexcept {
  while (tail_cursor >= final_count &&
         is_row_deleted(tail_cursor, rows_to_delete)) {
    --tail_cursor;
  }
}

/**
 * @brief Relocates surviving entities from the tail region into vacated holes.
 *
 * @param chunk Pointer to the chunk being compacted.
 * @param rows_to_delete Span of row indices marked for deletion.
 * @param final_count Final entity count after compaction (partition boundary).
 * @param layouts Span of component column layouts.
 * @param out_relocations Span to receive entity relocation records.
 * @return uint32_t Number of entities relocated.
 */
inline uint32_t relocate_tail_entities(
    Chunk *chunk,
    std::span<const uint32_t> rows_to_delete,
    uint32_t final_count,
    std::span<const ComponentColumnLayout> layouts,
    std::span<EntityRelocation> out_relocations
) noexcept {
  uint32_t tail_cursor = chunk->count() - 1;
  uint32_t reloc_count = 0;

  for (uint32_t target_row : rows_to_delete) {
    // Only vacated rows in the surviving active range (< final_count) need to
    // be filled
    if (target_row >= final_count) {
      continue;
    }

    advance_to_surviving_tail(tail_cursor, final_count, rows_to_delete);
    assert(
        tail_cursor >= final_count && "Tail cursor underflow during compaction"
    );

    Entity moved = relocate_entity(chunk, target_row, tail_cursor, layouts);
    if (reloc_count < out_relocations.size()) {
      out_relocations[reloc_count] = EntityRelocation{moved, target_row};
      ++reloc_count;
    }
    --tail_cursor;
  }
  return reloc_count;
}

} // namespace

// =============================================================================
// Swap-and-Pop Compaction Routine
// =============================================================================

/**
 * @brief Removes an entity from the chunk using the Swap-and-Pop technique.
 *
 * @param row The row index of the entity being removed.
 * @param layouts The component column layouts defining offsets, sizes, and
 *                type descriptors.
 * @return ChunkRelocation Record containing the entity moved into `row`,
 * or an invalid entity if no relocation occurred.
 */
ChunkRelocation Chunk::swap_and_pop(
    uint32_t row,
    std::span<const ComponentColumnLayout> layouts
) {
  assert(row < header_.count && "Row index out of bounds during swap_and_pop");

  const uint32_t last_row = header_.count - 1;

  // Edge Case: Deleting the last entity in the chunk requires only destruction
  if (row == last_row) {
    destruct_row_components(this, row, layouts);
    --header_.count;
    return ChunkRelocation{};
  }

  // General Case: Relocate last entity components and handle into vacated row
  Entity moved = relocate_entity(this, row, last_row, layouts);

  // Decrement active entity count
  --header_.count;
  return ChunkRelocation{moved};
}

// =============================================================================
// Batched Swap-and-Pop Compaction Routine
// =============================================================================

/**
 * @brief Compacts multiple vacated rows in a single pass without redundant
 * moves.
 *
 * @param rows_to_delete Span of row indices marked for deletion.
 * @param layouts Component column layouts defining offsets and descriptors.
 * @param out_relocations Span to receive entity relocation records.
 * @return uint32_t Number of entities relocated.
 */
uint32_t Chunk::batch_swap_and_pop(
    std::span<const uint32_t> rows_to_delete,
    std::span<const ComponentColumnLayout> layouts,
    std::span<EntityRelocation> out_relocations
) {
  if (rows_to_delete.empty()) {
    return 0;
  }

  const uint32_t delete_count = static_cast<uint32_t>(rows_to_delete.size());
  assert(
      delete_count <= header_.count &&
      "Cannot delete more entities than chunk contains"
  );

  const uint32_t final_count = header_.count - delete_count;

  // 1. Destruct components for all rows marked for deletion
  destruct_rows(this, rows_to_delete, layouts);

  // 2. Relocate surviving tail entities into vacated rows < final_count
  const uint32_t count = relocate_tail_entities(
      this, rows_to_delete, final_count, layouts, out_relocations
  );

  // 3. Update chunk entity count to compacted total
  header_.count = final_count;
  return count;
}

} // namespace arche
