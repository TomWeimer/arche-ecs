module;

#include <cstdint>

export module arche:types.detail.resource_id;

import :component_mask;

export namespace arche {

// =============================================================================
// Resource ID Definitions
// =============================================================================

/// Dense 32-bit integer identifier for world singleton resource types.
using ResourceID = uint32_t;

/// Sentinel value indicating an invalid or unallocated ResourceID.
inline constexpr ResourceID INVALID_RESOURCE_ID = 0xFFFFFFFF;

namespace internal {

/// 128-bit bitmask tracking resource dependencies in system access descriptors.
using ResourceMask = ComponentMask;

} // namespace internal

} // namespace arche
