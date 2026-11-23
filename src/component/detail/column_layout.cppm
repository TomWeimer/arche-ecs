module;

#include <cstdint>

export module arche:component.detail.column_layout;

import :component_mask;
import :type_id;

export namespace arche {

/**
 * @struct ComponentColumnLayout
 * @brief Column memory layout descriptor for a component type inside an
 * archetype chunk.
 */
struct ComponentColumnLayout {
  TypeID type_id{INVALID_TYPE_ID};
  uint32_t size{0};
  uint32_t alignment{0};
  uint32_t chunk_byte_offset{0};
  const ComponentDescriptor *descriptor{nullptr};
};

} // namespace arche
