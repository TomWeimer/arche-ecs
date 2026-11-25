module;

#include <cstddef>
#include <utility>

export module arche:domain.resource;

import :resource;

export namespace arche {

// =============================================================================
// Resources Domain Sub-Facade
// =============================================================================

/**
 * @class ResourceFacade
 * @brief Domain sub-facade for managing global singleton resources independent of entities.
 *
 * Provides type-safe insertion, retrieval, and lifecycle management of ambient
 * world singletons stored directly in the ResourceContainer.
 *
 * Accessible via `world.resources`.
 */
class ResourceFacade {
public:
  constexpr ResourceFacade() noexcept = default;
  explicit constexpr ResourceFacade(internal::ResourceContainer &resources) noexcept
      : resources_(&resources) {}

  /// Inserts or replaces a global singleton resource of type T.
  template <typename T, typename... Args>
  T &insert(Args &&...args);

  /// Retrieves a mutable reference to the singleton resource of type T.
  template <typename T>
  [[nodiscard]] T &get();

  /// Retrieves a read-only reference to the singleton resource of type T.
  template <typename T>
  [[nodiscard]] const T &get() const;

  /// Checks whether a singleton resource of type T is registered.
  template <typename T>
  [[nodiscard]] bool has() const noexcept;

  /// Checks whether a resource with the specified runtime ResourceID is registered.
  [[nodiscard]] bool has(ResourceID id) const noexcept;

  /// Removes and destructs the singleton resource of type T.
  template <typename T>
  bool remove();

  /// Direct immutable access to the underlying ResourceContainer.
  [[nodiscard]] const internal::ResourceContainer &container() const noexcept;

  /// Direct mutable access to the underlying ResourceContainer.
  [[nodiscard]] internal::ResourceContainer &container() noexcept;

private:
  internal::ResourceContainer *resources_{nullptr};
};

} // namespace arche
