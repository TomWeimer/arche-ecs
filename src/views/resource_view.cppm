module;

#include <cassert>
#include <cstddef>
#include <utility>

export module arche:resource_view;

import :resource;

export namespace arche {

// =============================================================================
// Resources Domain View
// =============================================================================

/**
 * @class ResourceView
 * @brief Domain view for managing global singleton resources independent of entities.
 *
 * Provides type-safe insertion, retrieval, and lifecycle management of ambient
 * world singletons stored directly in the ResourceContainer.
 *
 * Accessible via `world.resources`.
 */
class ResourceView {
public:
  constexpr ResourceView() noexcept = default;
  explicit constexpr ResourceView(internal::ResourceContainer &resources) noexcept
      : resources_(&resources) {}

  /// Inserts or replaces a global singleton resource of type T.
  template <typename T, typename... Args>
  T &insert(Args &&...args) {
    assert(resources_ != nullptr && "Cannot insert resource to null container!");
    return resources_->insert<T>(std::forward<Args>(args)...);
  }

  /// Retrieves a mutable reference to the singleton resource of type T.
  template <typename T>
  [[nodiscard]] T &get() {
    assert(resources_ != nullptr && "Cannot get resource from null container!");
    return resources_->get<T>();
  }

  /// Retrieves a read-only reference to the singleton resource of type T.
  template <typename T>
  [[nodiscard]] const T &get() const {
    assert(resources_ != nullptr && "Cannot get resource from null container!");
    return resources_->get<T>();
  }

  /// Checks whether a singleton resource of type T is registered.
  template <typename T>
  [[nodiscard]] bool has() const noexcept {
    assert(resources_ != nullptr && "Cannot check resource in null container!");
    return resources_->has<T>();
  }

  /// Checks whether a resource with the specified runtime ResourceID is registered.
  [[nodiscard]] bool has(ResourceID id) const noexcept {
    assert(resources_ != nullptr);
    return resources_->has(id);
  }

  /// Removes and destructs the singleton resource of type T.
  template <typename T>
  bool remove() {
    assert(resources_ != nullptr && "Cannot remove resource from null container!");
    return resources_->remove<T>();
  }

private:
  internal::ResourceContainer *resources_{nullptr};
};

using ResourceFacade = ResourceView;

} // namespace arche
