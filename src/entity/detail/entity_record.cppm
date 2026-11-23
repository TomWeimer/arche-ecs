module;

#include <cstdint>

export module arche:entity.detail.entity_record;

export namespace arche {

class Archetype;
struct Chunk;

/**
 * @struct EntityRecord
 * @brief Routing record mapping an Entity handle to its physical storage
 * location.
 */
struct EntityRecord {
  Archetype *archetype{nullptr};
  Chunk *chunk{nullptr};
  uint32_t row{0};
};

} // namespace arche
