module;

#include <cstddef>
#include <cstdint>

export module arche:snapshot.detail.header;

export namespace arche {

// =============================================================================
// SnapshotHeader: 64-Byte Cache-Aligned Binary State Header
// =============================================================================

/**
 * @struct SnapshotHeader
 * @brief 64-byte cache-aligned header encapsulating world snapshot metadata.
 */
struct alignas(16) SnapshotHeader {
  static constexpr uint32_t MAGIC = 0x48435241; ///< 'ARCH'
  static constexpr uint32_t FORMAT_VERSION = 1;

  uint32_t magic{MAGIC};
  uint32_t version{FORMAT_VERSION};
  uint64_t current_version{0};
  uint32_t slot_count{0};
  uint32_t free_list_head{0};
  uint32_t active_entities{0};
  uint32_t chunk_count{0};
  size_t total_payload_bytes{0};
  uint32_t archetype_count{0};
  uint8_t reserved[20]{0};
};

static_assert(
    sizeof(SnapshotHeader) == 64,
    "SnapshotHeader must be exactly 64 bytes"
);

} // namespace arche
