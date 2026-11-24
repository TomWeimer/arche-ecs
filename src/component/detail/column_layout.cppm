module;

#include <cstdint>

export module arche:component.detail.column_layout;

import :component_mask;
import :type_id;

export namespace arche {

// =============================================================================
// Component Column Layout Descriptor
// =============================================================================

/**
 * @struct ComponentColumnLayout
 * @brief Memory layout and alignment descriptor for a component column within an archetype chunk.
 *
 * Defines contiguous array parameters (type ID, byte size, required alignment,
 * starting byte offset inside the 64KB chunk page, and lifecycle metadata pointer).
 */
struct ComponentColumnLayout {
  TypeID type_id{INVALID_TYPE_ID};               ///< Component type identifier.
  uint32_t size{0};                              ///< Byte size of each component instance.
  uint32_t alignment{0};                         ///< Byte alignment constraint.
  uint32_t chunk_byte_offset{0};                 ///< Starting byte offset within the chunk memory page.
  const ComponentDescriptor *descriptor{nullptr}; ///< Pointer to type-erased lifecycle vtable.
};

} // namespace arche
