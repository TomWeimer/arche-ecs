module;

#include <cstdint>
#include <span>

export module arche:spatial.detail.permute;

import :archetype;
import :chunk;
import :slot_map;

export namespace arche {

void permute(
    Archetype *arch,
    std::span<Chunk *const> chunks,
    SlotMap &slot_map,
    uint32_t version,
    std::span<const uint32_t> target_indices
);

} // namespace arche
