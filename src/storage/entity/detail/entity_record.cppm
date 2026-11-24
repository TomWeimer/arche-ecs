module;

#include <cstdint>

export module arche:entity.detail.entity_record;

export namespace arche {

class Archetype;
struct Chunk;

// =============================================================================
// Entity Storage Coordinate Record
// =============================================================================

/**
 * @struct EntityRecord
 * @brief Routing record mapping an Entity handle to its physical storage location.
 *
 * Stored inside each 32-byte Slot of the SlotMap. Encapsulates:
 * - `archetype`: Pointer to the archetype table holding the entity's component columns.
 * - `chunk`: Pointer to the specific 64KB memory chunk holding the entity's data slice.
 * - `row`: Local row offset (0 to capacity-1) within the chunk.
 */
struct EntityRecord {
  /// Pointer to the archetype table possessing the entity's schema.
  Archetype *archetype{nullptr};

  /// Pointer to the memory chunk containing the entity's component row.
  Chunk *chunk{nullptr};

  /// Local row index of the entity inside the chunk.
  uint32_t row{0};
};

} // namespace arche
