module;

#include <cassert>
#include <cstddef>
#include <utility>

export module arche:domain.resource_impl;

import :domain.resource;
import :resource;

export namespace arche {

using namespace internal;

// =============================================================================
// ResourceFacade Method Implementations
// =============================================================================

bool ResourceFacade::has(ResourceID id) const noexcept {
  assert(resources_ != nullptr);
  return resources_->has(id);
}

const ResourceContainer &ResourceFacade::container() const noexcept {
  assert(resources_ != nullptr);
  return *resources_;
}

ResourceContainer &ResourceFacade::container() noexcept {
  assert(resources_ != nullptr);
  return *resources_;
}

template <typename T, typename... Args>
inline T &ResourceFacade::insert(Args &&...args) {
  assert(resources_ != nullptr && "Cannot insert resource to null container!");
  return resources_->insert<T>(std::forward<Args>(args)...);
}

template <typename T>
inline T &ResourceFacade::get() {
  assert(resources_ != nullptr && "Cannot get resource from null container!");
  return resources_->get<T>();
}

template <typename T>
inline const T &ResourceFacade::get() const {
  assert(resources_ != nullptr && "Cannot get resource from null container!");
  return resources_->get<T>();
}

template <typename T>
inline bool ResourceFacade::has() const noexcept {
  assert(resources_ != nullptr && "Cannot check resource in null container!");
  return resources_->has<T>();
}

template <typename T>
inline bool ResourceFacade::remove() {
  assert(resources_ != nullptr && "Cannot remove resource from null container!");
  return resources_->remove<T>();
}

} // namespace arche
