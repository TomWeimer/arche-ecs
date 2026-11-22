#pragma once

#include <cstdint>

namespace arche {

class Archetype;
struct Chunk;

/**
 * @struct EntityRecord
 * @brief Routing record mapping an Entity handle to its physical storage
 * location.
 *
 * @details
 * - EntityRecord provides constant-time O(1) indirection from an Entity handle
 *   to its underlying component data in cache-aligned chunk pages.
 * - Memory footprint: 24 bytes on 64-bit platforms (8B pointer + 8B pointer +
 *   4B row + 4B pad).
 */
struct EntityRecord {
  /// Pointer to current Archetype DAG node.
  Archetype *archetype{nullptr};

  /// Pointer to current 64 KB memory chunk page.
  Chunk *chunk{nullptr};

  /// Dense row index within the chunk's SoA slices.
  uint32_t row{0};
};

} // namespace arche
