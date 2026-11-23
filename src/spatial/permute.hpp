#pragma once

#include "archetype/archetype.hpp"
#include "chunk/chunk.hpp"
#include "entity/slot_map.hpp"

#include <span>
#include <vector>

namespace arche {

void permute(
    Archetype *arch,
    std::span<Chunk *const> chunks,
    SlotMap &slot_map,
    uint32_t version,
    std::span<const uint32_t> target_indices
);

} // namespace arche
