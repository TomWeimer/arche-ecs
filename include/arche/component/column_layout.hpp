#pragma once

#include "arche/component/type_id.hpp"
#include <cstdint>

namespace arche {

/**
 * @struct ComponentColumnLayout
 * @brief Column memory layout descriptor for a component type inside an
 * archetype chunk.
 *
 * @details
 * - Stores precomputed byte offsets and component dimensions for zero-cost
 * column pointer resolution without runtime lookup.
 */
struct ComponentColumnLayout {
  /// Unique TypeID of this component.
  TypeID type_id{INVALID_TYPE_ID};
  /// Byte size of a single component instance.
  uint32_t size{0};
  /// Required memory alignment in bytes.
  uint32_t alignment{0};
  /// Byte offset from the start of the chunk buffer.
  uint32_t chunk_byte_offset{0};
  /// Pointer to type lifecycle reflection metadata.
  const ComponentDescriptor *descriptor{nullptr};
};

} // namespace arche
