#pragma once

#include <cstddef>
#include <cstdint>

namespace arche {

// =============================================================================
// World State Snapshot Header
// =============================================================================

/**
 * @struct SnapshotHeader
 * @brief 64-byte cache-aligned header encapsulating world snapshot metadata.
 */
struct alignas(16) SnapshotHeader {
  uint32_t magic{0x48435241};    ///< Magic bytes ('ARCH' in Little-Endian)
  uint32_t version{1};           ///< Snapshot format version
  uint64_t current_version{0};   ///< World sequence version at snapshot time
  uint32_t slot_count{0};        ///< Total capacity of SlotMap slots array
  uint32_t free_list_head{0};    ///< Head index of intrusive free list
  uint32_t active_entities{0};   ///< Total living entities
  uint32_t chunk_count{0};       ///< Total active 64 KB chunk pages
  size_t total_payload_bytes{0}; ///< Total byte size including header
  uint32_t archetype_count{0};   ///< Total archetypes at snapshot time
  uint8_t reserved[20]{0};       ///< Reserved padding to align header to 64 bytes
};

static_assert(
    sizeof(SnapshotHeader) == 64,
    "SnapshotHeader must be exactly 64 bytes"
);

} // namespace arche
