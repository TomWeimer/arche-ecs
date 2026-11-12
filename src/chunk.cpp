#include "arche/chunk.hpp"
#include "arche/archetype.hpp"
#include "arche/slot_map.hpp"

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
  relocate_row_components(this, row, last_row, layouts); // The data
  relocate_entity_handle(this, row, last_row, slot_map); // The entity id

  // Decrement active entity count
  --header_.count;
}

} // namespace arche
