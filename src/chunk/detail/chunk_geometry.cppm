module;

#include <cstddef>

export module arche:chunk.detail.chunk_geometry;

import :chunk.detail.chunk_header;

export namespace arche {

/**
 * @struct ChunkGeometry
 * @brief Standardized chunk header and payload layout constants.
 */
struct ChunkGeometry {
  static constexpr size_t HEADER_BYTES = sizeof(ChunkHeader);
  static constexpr size_t PAYLOAD_BYTES = 65536 - HEADER_BYTES;
};

} // namespace arche
