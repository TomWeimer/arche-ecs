#pragma once

#include "arche/types/resource_id.hpp"

namespace arche {

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
 *
 * Used by the scheduler to compute Directed Acyclic Graphs (DAGs) and partition
 * systems into conflict-free parallel stages.
 *
 * ```cpp
 * // Example:
 * AccessDescriptor desc = AccessBuilder{}
 *     .reads<Position>()
 *     .writes<Velocity>()
 *     .reads_resource<Time>()
 *     .build();
 * ```
 */
struct AccessDescriptor {
  /// Bitmask of component types read by the system.
  ComponentMask read_components{};

  /// Bitmask of component types written by the system.
  ComponentMask write_components{};

  /// Bitmask of world resource types read by the system.
  ResourceMask read_resources{};

  /// Bitmask of world resource types written by the system.
  ResourceMask write_resources{};

  /// Checks if this access footprint conflicts with another (read-write or
  /// write-write race).
  /// Checks if this access footprint conflicts with another (read-write or
  /// write-write race).
  [[nodiscard]] inline bool conflicts_with(
      const AccessDescriptor &other
  ) const noexcept;

  /// Checks if the descriptor specifies zero component and resource access.
  [[nodiscard]] inline bool is_empty() const noexcept;
};

// =============================================================================
// AccessBuilder: Fluent Builder for AccessDescriptor
// =============================================================================

/**
 * @class AccessBuilder
 * @brief Fluent builder interface for constructing an AccessDescriptor.
 *
 * Provides a type-safe, compile-time mechanism for declaring component and
 * resource dependencies without manual TypeID tracking.
 *
 * ```cpp
 * // Example:
 * auto access = Access{}
 *     .reads<Position, Mass>()
 *     .writes<Velocity>()
 *     .writes_resource<PhysicsConfig>();
 * ```
 */
class AccessBuilder {
public:
  /// Declares read-only access (`const Comps&...`) to entity components.
  template <typename... Comps> AccessBuilder &reads() noexcept;

  /// Declares mutable write access (`Comps&...`) to entity components.
  template <typename... Comps> AccessBuilder &writes() noexcept;

  /// Declares read-only access (`const Res&...`) to World resources.
  template <typename... Res> AccessBuilder &reads_resource() noexcept;

  /// Declares mutable write access (`Res&...`) to World resources.
  template <typename... Res> AccessBuilder &writes_resource() noexcept;

  /// Constructs and returns the final immutable AccessDescriptor.
  [[nodiscard]] AccessDescriptor build() const noexcept;

private:
  ComponentMask read_components_{};
  ComponentMask write_components_{};
  ResourceMask read_resources_{};
  ResourceMask write_resources_{};
};

/// Type alias for concise syntax matching engine conventions.
using Access = AccessBuilder;

} // namespace arche

#include "system/access.inl"
