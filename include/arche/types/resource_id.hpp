#pragma once

#include <cstdint>

#include "arche/types/component.hpp"

namespace arche {

// =============================================================================
// Resource ID Definitions
// =============================================================================

/// Dense 32-bit integer identifier for world singleton resource types.
using ResourceID = uint32_t;

/// 128-bit bitmask tracking resource dependencies in system access descriptors.
using ResourceMask = ComponentMask;

/// Sentinel value indicating an invalid or unallocated ResourceID.
inline constexpr ResourceID INVALID_RESOURCE_ID = 0xFFFFFFFF;

} // namespace arche
