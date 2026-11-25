module;

export module arche:access;

import :component_mask;
import :resource;

export namespace arche {

// =============================================================================
// AccessDescriptor: Component & Resource Access Footprint
// =============================================================================

/**
 * @struct AccessDescriptor
 * @brief Exact bitmask footprint of components and resources accessed by a
 * system.
 *
 * Encapsulates four 128-bit masks representing read and write dependencies:
 * 1. `read_components`: Entity components accessed read-only (`const T&`).
 * 2. `write_components`: Entity components modified mutably (`T&`).
 * 3. `read_resources`: World singletons accessed read-only (`const T&`).
 * 4. `write_resources`: World singletons modified mutably (`T&`).
 */
struct AccessDescriptor {
  /// Bitmask of component types read by the system.
  internal::ComponentMask read_components{};

  /// Bitmask of component types written by the system.
  internal::ComponentMask write_components{};

  /// Bitmask of world resource types read by the system.
  internal::ResourceMask read_resources{};

  /// Bitmask of world resource types written by the system.
  internal::ResourceMask write_resources{};

  [[nodiscard]] constexpr bool conflicts_with(
      const AccessDescriptor &other
  ) const noexcept {
    if (write_components.contains_any(
            other.read_components | other.write_components
        )) {
      return true;
    }
    if (other.write_components.contains_any(
            read_components | write_components
        )) {
      return true;
    }
    if (write_resources.contains_any(
            other.read_resources | other.write_resources
        )) {
      return true;
    }
    if (other.write_resources.contains_any(read_resources | write_resources)) {
      return true;
    }
    return false;
  }

  [[nodiscard]] constexpr bool is_empty() const noexcept {
    return read_components.empty() && write_components.empty() &&
        read_resources.empty() && write_resources.empty();
  }
};

} // namespace arche
