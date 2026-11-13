#include "arche/chunk.hpp"
#include "arche/detail/slot_map.hpp"

#include <cassert>
#include <cstring>

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
 * @brief Transfers the entity handle and synchronizes the SlotMap record.
 *
 * @details
 * CRITICAL INVARIANT: The relocated entity's dense `(chunk_index, row)`
 * coordinate inside the central `SlotMap` must be updated immediately to
 * prevent dangling handles or memory corruption on subsequent entity lookups.
 *
 * @param chunk Pointer to the chunk being compacted.
 * @param dst_row Vacated destination row receiving the relocated entity handle.
 * @param src_row Source row index where the relocated entity previously
 * resided.
 * @param slot_map Central generational SlotMap to update with new row index.
 */
inline void relocate_entity_handle(
    Chunk *chunk,
    uint32_t dst_row,
    uint32_t src_row,
    SlotMap &slot_map
) noexcept {
  Entity *entities = chunk->entity_slice();
  Entity moved_entity = entities[src_row];

  // Move Entity handle into destination slot
  entities[dst_row] = moved_entity;

  // Synchronize SlotMap record with the entity's new row position
  slot_map.update_row(moved_entity, dst_row);
}

/**
 * @brief Relocates both component data and entity handle from source row to
 * destination row, keeping the SlotMap synchronized.
 *
 * @param chunk Pointer to the chunk being compacted.
 * @param dst_row Vacated destination row receiving the relocated entity.
 * @param src_row Source row index where the relocated entity previously
 * resided.
 * @param slot_map Central SlotMap to synchronize with the new row index.
 * @param layouts Span of component column layouts.
 */
inline void relocate_entity(
    Chunk *chunk,
    uint32_t dst_row,
    uint32_t src_row,
    SlotMap &slot_map,
    std::span<const ComponentColumnLayout> layouts
) noexcept {
  relocate_row_components(chunk, dst_row, src_row, layouts);
  relocate_entity_handle(chunk, dst_row, src_row, slot_map);
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
 * @param slot_map Central SlotMap to synchronize.
 * @param layouts Span of component column layouts.
 */
inline void relocate_tail_entities(
    Chunk *chunk,
    std::span<const uint32_t> rows_to_delete,
    uint32_t final_count,
    SlotMap &slot_map,
    std::span<const ComponentColumnLayout> layouts
) noexcept {
  uint32_t tail_cursor = chunk->count() - 1;

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

    relocate_entity(chunk, target_row, tail_cursor, slot_map, layouts);
    --tail_cursor;
  }
}

} // namespace

// =============================================================================
// Swap-and-Pop Compaction Routine
// =============================================================================

/**
 * @brief Removes an entity from the chunk using the Swap-and-Pop technique.
 *
 * @details
 * ### Why Swap-and-Pop is Used:
 * In game simulations, entities are created and destroyed frequently. If an
 * entity is removed from the middle of a chunk, we have three choices:
 *
 * 1. **Shift all elements left (Slow):**
 *    Moving every entity after the removed one down by one slot takes O(N)
 * time. With thousands of entities, this causes heavy memory copying and slows
 * down the simulation.
 * 2. **Leave an empty gap (Fragmented):**
 *    Leaving holes in memory means our simulation loops would have to check if
 *    each slot is empty or alive. This wastes memory and hurts CPU cache
 *    performance.
 * 3. **Swap-and-Pop (Fast & Packed):**
 *    Instead of shifting everything, we take the very last entity in the chunk
 *    and move it directly into the empty slot, then decrease the entity count
 * by one.
 *    - Runs in instant O(1) constant time, no matter how many entities are in
 *      the chunk.
 *    - Keeps all entities packed together continuously with zero gaps.
 *
 * ### How it Works:
 * - **Edge Case (Last Entity):** If the entity being removed is already at the
 *   end of the chunk, we simply call its destructors and decrease the count.
 * - **General Case:** We move the last entity's components into `row`, update
 *   its Entity ID in the chunk, update the `SlotMap` so the moved entity's
 *   handle knows its new row position, and decrease the count.
 *
 * @param row The row index of the entity being removed.
 * @param slot_map The central SlotMap, updated so the relocated entity can
 *                 still be found.
 * @param layouts The component column layouts defining offsets, sizes, and
 *                type descriptors.
 */
void Chunk::swap_and_pop(
    uint32_t row,
    SlotMap &slot_map,
    std::span<const ComponentColumnLayout> layouts
) {
  assert(row < header_.count && "Row index out of bounds during swap_and_pop");

  const uint32_t last_row = header_.count - 1;

  // Edge Case: Deleting the last entity in the chunk requires only destruction
  if (row == last_row) {
    destruct_row_components(this, row, layouts);
    --header_.count;
    return;
  }

  // General Case: Relocate last entity components and handle into vacated row
  relocate_entity(this, row, last_row, slot_map, layouts);

  // Decrement active entity count
  --header_.count;
}

// =============================================================================
// Batched Swap-and-Pop Compaction Routine
// =============================================================================

/**
 * @brief Compacts multiple vacated rows in a single pass without redundant
 * moves.
 *
 * @details
 * ### Batched Compaction Algorithm:
 * When deleting multiple entities at once:
 * 1. **Batch Destruction:** Destructs components for all rows in
 * `rows_to_delete`.
 * 2. **Tail Relocation:** Fills holes left in the surviving range (`<
 * final_count`) by relocating valid, surviving entities from the tail region
 * (`>= final_count`).
 * 3. **Batch Count Update:** Decrements the chunk active entity count in a
 * single operation.
 *
 * @param rows_to_delete Span of row indices marked for deletion.
 * @param slot_map Central generational SlotMap to update for relocated entity
 * handles.
 * @param layouts Component column layouts defining offsets and descriptors.
 */
void Chunk::batch_swap_and_pop(
    std::span<const uint32_t> rows_to_delete,
    SlotMap &slot_map,
    std::span<const ComponentColumnLayout> layouts
) {
  if (rows_to_delete.empty()) {
    return;
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
  relocate_tail_entities(this, rows_to_delete, final_count, slot_map, layouts);

  // 3. Update chunk entity count to compacted total
  header_.count = final_count;
}

} // namespace arche
