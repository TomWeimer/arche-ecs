module;

#include <type_traits>

export module arche:access_builder;

import :access;
import :component_id;
import :resource;

export namespace arche {

// =============================================================================
// AccessBuilder (Fluent Component & Resource Access Descriptor Builder)
// =============================================================================

/**
 * @class AccessBuilder
 * @brief Fluent declarative builder for component and resource `AccessDescriptor` bitmasks.
 *
 * Used during system registration to declare component read/write sets and resource
 * dependencies for automatic dependency graph construction and concurrency scheduling.
 */
class AccessBuilder {
public:
  /**
   * @brief Registers component types as read-only access.
   *
   * @tparam Comps Component types to read.
   * @return Reference to this builder for chaining.
   */
  template <typename... Comps>
  AccessBuilder &reads() noexcept;

  /**
   * @brief Registers component types as read-write (mutable) access.
   *
   * @tparam Comps Component types to write.
   * @return Reference to this builder for chaining.
   */
  template <typename... Comps>
  AccessBuilder &writes() noexcept;

  /**
   * @brief Registers world resource types as read-only access.
   *
   * @tparam Res Resource types to read.
   * @return Reference to this builder for chaining.
   */
  template <typename... Res>
  AccessBuilder &reads_resource() noexcept;

  /**
   * @brief Registers world resource types as read-write (mutable) access.
   *
   * @tparam Res Resource types to write.
   * @return Reference to this builder for chaining.
   */
  template <typename... Res>
  AccessBuilder &writes_resource() noexcept;

  /**
   * @brief Finalizes and returns the compiled AccessDescriptor bitmasks.
   */
  [[nodiscard]] AccessDescriptor build() const noexcept;

private:
  internal::ComponentMask read_components_{};
  internal::ComponentMask write_components_{};
  internal::ResourceMask read_resources_{};
  internal::ResourceMask write_resources_{};
};

/// Ergonomic alias for AccessBuilder.
using Access = AccessBuilder;

} // namespace arche
