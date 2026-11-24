module;

#include <cstddef>
#include <cstdint>
#include <type_traits>

export module arche:chunk.detail.chunk_header;

export namespace arche {

// =============================================================================
// Cache-Aligned Physical Chunk Header
// =============================================================================

/**
 * @struct ChunkHeader
 * @brief Cache-aligned metadata header located at byte offset 0 of every Chunk
 * buffer.
 *
 * Guaranteed to occupy exactly 256 bytes (four 64-byte cache lines), matching
 * host cache-line boundaries to prevent false sharing and split-line penalties
 * across concurrent systems.
 */
struct alignas(64) ChunkHeader {
  /// Number of currently active entities in this chunk.
  uint32_t count{0};

  /// Maximum entity storage capacity (K) for this chunk.
  uint32_t capacity{0};

  /// Dense index of this chunk within the archetype list.
  uint32_t chunk_index{0};

  /// Incremented on entity spawn/destroy/relocation.
  uint32_t structure_version{1};

  /// Intrusive link pointer for ChunkPool recycling.
  ChunkHeader *next{nullptr};

  /// Intrusive reverse link pointer.
  ChunkHeader *prev{nullptr};

  /// Internal padding to align to 64 bytes.
  uint8_t reserved[32]{0};

  /// 64-byte user metadata slot (e.g. for Macro AABBs or Spatial Hashes).
  alignas(16) uint8_t user_metadata[64]{0};

  static constexpr size_t MAX_COMPONENTS_PER_CHUNK = 32;

  /// Monotonic mutation tick for each component column.
  uint32_t column_versions[MAX_COMPONENTS_PER_CHUNK]{0};
};

static_assert(
    std::is_standard_layout_v<ChunkHeader>,
    "ChunkHeader must be standard layout for predictable memory offsets"
);

static_assert(
    sizeof(ChunkHeader) == 256,
    "ChunkHeader must be padded to exactly 256 bytes (four cache lines)"
);

// =============================================================================
// Standardized Chunk Geometry & Sizing Constants
// =============================================================================

/**
 * @struct ChunkGeometry
 * @brief Standardized chunk header and payload layout constants.
 */
struct ChunkGeometry {
  static constexpr size_t HEADER_BYTES = sizeof(ChunkHeader);
  static constexpr size_t DEFAULT_CHUNK_SIZE = 65536;
  static constexpr size_t PAYLOAD_BYTES = DEFAULT_CHUNK_SIZE - HEADER_BYTES;

  [[nodiscard]] static constexpr size_t payload_bytes(
      size_t chunk_size = DEFAULT_CHUNK_SIZE
  ) noexcept {
    return (chunk_size > HEADER_BYTES) ? (chunk_size - HEADER_BYTES) : 0;
  }
};

} // namespace arche
