module;

#include <cstdint>
#include <span>

export module arche:sort.detail.permute;

import :archetype;
import :chunk;
import :slot_map;

export namespace arche::internal {

// =============================================================================
// Low-Level Permutation Engine Interface
// =============================================================================

/**
 * @brief Performs in-place permutation of all component columns and entity IDs within an archetype.
 *
 * Cycles through disjoint permutation orbits using a minimal single-element scratch buffer,
 * updates SlotMap location mappings for every moved entity, and advances the structural
 * version stamp across all affected memory chunks.
 *
 * @param arch Pointer to the target archetype being rearranged.
 * @param chunks The contiguous chunk memory pages comprising the archetype's storage.
 * @param slot_map The global entity generational slot map to synchronize.
 * @param version The new simulation version sequence to write to chunk headers.
 * @param target_indices Precomputed permutation mapping where `target_indices[i]` specifies
 *                       the new index for the entity previously at index `i`.
 */
void permute(
    Archetype *arch,
    std::span<Chunk *const> chunks,
    SlotMap &slot_map,
    uint32_t version,
    std::span<const uint32_t> target_indices
);

} // namespace arche::internal
