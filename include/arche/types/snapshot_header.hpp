#pragma once

#include <cstddef>
#include <cstdint>

namespace arche {

// =============================================================================
// SnapshotHeader: 64-Byte Cache-Aligned Binary State Header
// =============================================================================

/**
 * @struct SnapshotHeader
 * @brief 64-byte cache-aligned header encapsulating world snapshot metadata.
 *
 * Prefixes all serialized world checkpoint streams. Validates binary format
 * compatibility, schema versions, and memory layout dimensions before restoring
 * chunk data.
 *
 * ```cpp
 * // Example:
 * SnapshotHeader header;
 * std::memcpy(&header, buffer.data(), sizeof(SnapshotHeader));
 * if (header.magic == SnapshotHeader::MAGIC && header.version ==
 * SnapshotHeader::FORMAT_VERSION) {
 *     // Valid snapshot
 * }
 * ```
 */
struct alignas(16) SnapshotHeader {
  static constexpr uint32_t
      MAGIC = 0x48435241; ///< Magic bytes ('ARCH' in Little-Endian)
  static constexpr uint32_t
      FORMAT_VERSION = 1; ///< Current snapshot format version

  uint32_t magic{MAGIC};            ///< Magic bytes identifier
  uint32_t version{FORMAT_VERSION}; ///< Snapshot schema format version
  uint64_t current_version{0};      ///< World sequence tick at snapshot time
  uint32_t slot_count{0};           ///< Total capacity of SlotMap slots array
  uint32_t free_list_head{0};       ///< Head index of intrusive free list
  uint32_t active_entities{0};      ///< Total active living entities
  uint32_t chunk_count{0};          ///< Total active 64 KB chunk pages
  size_t total_payload_bytes{0};    ///< Total byte size including header
  uint32_t archetype_count{0};      ///< Total archetypes at snapshot time
  uint8_t reserved[20]{0}; ///< Reserved padding aligning struct to 64 bytes
};

static_assert(
    sizeof(SnapshotHeader) == 64,
    "SnapshotHeader must be exactly 64 bytes"
);

} // namespace arche
